/*
 * Camper Battery - BLE gateway for battery management systems (BMS)
 * ==================================================================
 * Hardware: ESP32-C3 (e.g. Super Mini) or any other ESP32 with Bluetooth LE
 *
 * Keeps a Bluetooth connection to up to MAX_BMS battery monitors / BMS, reads them every
 * few seconds and publishes the values via MQTT (Home Assistant discovery) and in the web UI.
 * Supported: Daly smart BMS, Ective BM X battery monitor (LiPower protocol).
 *
 * The protocols are ported from aiobmsble (https://github.com/patman15/aiobmsble, Apache-2.0),
 * the library behind the Home Assistant integration "BLE Battery Management".
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#include "config.h"
#include "wifi_manager.h"
#include "safeguard.h"
#include "evlog.h"
#include "bms_client.h"
#include "mqtt_ha.h"
#include "webserver.h"

AppConfig config;
SensorData sensorData;

static unsigned long lastStatusPrint = 0;

// ---- Status LED ----------------------------------------------------------
#if STATUS_LED_PIN >= 0
static void ledColor(uint8_t r, uint8_t g, uint8_t b) {
    const uint16_t k = STATUS_LED_BRIGHTNESS;
    rgbLedWrite(STATUS_LED_PIN, r * k / 255, g * k / 255, b * k / 255);
}

static void statusLedLoop() {
    static unsigned long last = 0;
    static bool phase = false;
    if (millis() - last < 500) return;
    last = millis();
    phase = !phase;

    if (!isConnected() || !mqttIsConnected()) {   // red, blinking
        if (phase) ledColor(255, 0, 0); else ledColor(0, 0, 0);
        return;
    }
    int used = 0, ok = 0;
    BmsLive l;
    for (int i = 0; i < MAX_BMS; i++) {
        if (!config.bms[i].used) continue;
        used++;
        bmsGetLive(i, l);
        if (l.valid) ok++;
    }
    if (bmsScanning() || used == 0) {             // blue (blinking while scanning)
        if (!bmsScanning() || phase) ledColor(0, 0, 255); else ledColor(0, 0, 0);
    } else if (ok == used) {
        ledColor(0, 255, 0);                      // green
    } else {
        ledColor(255, 160, 0);                    // yellow
    }
}
#else
static void statusLedLoop() {}
#endif

void setupOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(DEFAULT_OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        // Filesystem upload over the network replaces config.json too; unmount first
        if (ArduinoOTA.getCommand() != U_FLASH) LittleFS.end();
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("[OTA] Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        evlog::add("Update über den IDE-Netzwerkport ok, Neustart");
        if (ArduinoOTA.getCommand() == U_FLASH) guard::otaDone();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        unsigned int pct = (total > 0) ? (progress * 100U / total) : 0;
        Serial.printf("[OTA] Progress: %u%%\r", pct);
    });
    ArduinoOTA.onError([](ota_error_t error) { evlog::add("Update über den IDE-Netzwerkport fehlgeschlagen (Fehler %u)", (unsigned)error); });

    ArduinoOTA.begin();
    Serial.println("[OTA] Ready");
}

// WiFi start. A changed WiFi that does not connect is replaced by the previous, working
// one, followed by a restart so the wifi manager starts clean with it (safeguard.h).
static void startWiFi() {
    wifiManagerInit();
    if (!guard::wifiTrialPending()) return;
    if (isConnected()) { guard::wifiTrialOk(); return; }
    String ssid, pass, extra;
    if (!guard::wifiTrialRestore(ssid, pass, extra)) return;
    strlcpy(config.wifi_ssid, ssid.c_str(), sizeof(config.wifi_ssid));
    strlcpy(config.wifi_pass, pass.c_str(), sizeof(config.wifi_pass));
    saveConfig();
    Serial.println("[MAIN] New WiFi failed - previous WiFi restored, restarting");
    delay(200);
    ESP.restart();
}

void setup() {
    Serial.begin(115200);
    evlog::begin();
    evlog::add("Start v%s, Grund: %s", FW_VERSION, evlog::resetReason());
    guard::begin();   // may boot the previous firmware (failed update)
    if (guard::status().length()) evlog::add("%s", guard::status().c_str());
    Serial.setTimeout(50);  // Do not block the loop while waiting for a line end
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  Camper Battery v" FW_VERSION);
    Serial.println("========================================");
    Serial.println();

    memset(&sensorData, 0, sizeof(SensorData));

    // Phase 1: load config & mount LittleFS
    webserverInit();

    // Phase 2: WiFi (blocking connect or AP fallback)
    startWiFi();

    // Phase 3: Bluetooth (own task, after WiFi so both share the radio from the start)
    bmsInit();

    // Phase 4: web server, OTA & MQTT
    webserverStart();
    setupOTA();
    mqttInit();

    Serial.printf("[MAIN] Setup done! Heap: %u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
    wifiManagerLoop();
    guard::loop(isConnected() || config.wifi_ssid[0] == '\0');
    evlog::watch(isConnected(), mqttIsConnected());
    ArduinoOTA.handle();

    webserverLoop();
    mqttLoop();
    statusLedLoop();

    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "read") {
            bmsPrint();
        } else if (cmd == "scan") {
            bmsRequestScan();
            Serial.println("[CMD] Scan requested, list follows with 'read'");
        } else if (cmd == "noauth") {
            // Forgotten web login: switch it off over USB
            config.web_user[0] = '\0';
            config.web_pass[0] = '\0';
            saveConfig();
            Serial.println("[CMD] Web login removed");
        } else if (cmd == "help") {
            Serial.println("[CMD] Commands: read, scan, noauth, help");
        }
    }

    if (millis() - lastStatusPrint > 10000) {
        lastStatusPrint = millis();
        int ok = 0, used = 0;
        BmsLive l;
        for (int i = 0; i < MAX_BMS; i++) {
            if (!config.bms[i].used) continue;
            used++;
            bmsGetLive(i, l);
            if (l.valid) ok++;
        }
        Serial.printf("[STATUS] BMS:%d/%d ok WiFi:%s MQTT:%s Heap:%u\n", ok, used,
            getWiFiStateString().c_str(), mqttIsConnected() ? "OK" : "---", (unsigned)ESP.getFreeHeap());
    }

    delay(5);
}
