/*
 * Camper Emulator - test device for camper-mqtt
 * ==============================================
 * Hardware: ESP32-C3 / ESP32-S3 (Arduino-ESP32 3.3.x)
 *
 * Sends the MQTT topics of the devices built into the van (tanks, level sensor, heater,
 * roof fan, temperatures, batteries, router, Zigbee) with example values, so the display
 * and the Road Buddy can be tested at home. Every device can be switched on, off or
 * "offline" (connection dropped, last will), every value changed in the web UI, and
 * scenarios run on the device itself. Commands from the display change the values
 * like the real device would.
 *
 *   emu.cpp      field table, payload builders, commands, scenarios, /emu.json
 *   mqtt_ha.cpp  one broker connection per emulated device plus the own one
 *   webserver.cpp, wifi_manager.cpp  shared template parts
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#include "config.h"
#include "wifi_manager.h"
#include "safeguard.h"
#include "evlog.h"
#include "emu.h"
#include <time.h>
#include "mqtt_ha.h"
#include "webserver.h"

AppConfig config;
SensorData sensorData;

static unsigned long lastStatusPrint = 0;

static void setupOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(DEFAULT_OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        // Filesystem upload over the network replaces config.json too; unmount first
        if (ArduinoOTA.getCommand() != U_FLASH) LittleFS.end();
        Serial.printf("[OTA] Start updating %s\n", ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem");
    });
    ArduinoOTA.onEnd([]() {
        evlog::add("Update über den IDE-Netzwerkport ok, Neustart");
        if (ArduinoOTA.getCommand() == U_FLASH) guard::otaDone();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", total ? progress * 100U / total : 0);
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
    Serial.println("  " DEVICE_NAME " v" FW_VERSION);
    Serial.println("========================================");

    memset(&sensorData, 0, sizeof(SensorData));
    randomSeed(esp_random());

    webserverInit();      // 1. LittleFS + config
    emuInit();            // 2. emulated devices (/emu.json)
    startWiFi();    // 3. WiFi (or setup AP)
    webserverStart();     // 4. web UI
    setupOTA();           // 5. Arduino OTA
    mqttInit();           // 6. MQTT
    // Local time for the router JSON ("TIME"); without internet it stays at 1970
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");

    Serial.printf("[MAIN] Setup done! Heap: %u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
    wifiManagerLoop();
    guard::loop(isConnected() || config.wifi_ssid[0] == '\0');
    evlog::watch(isConnected(), mqttIsConnected());
    ArduinoOTA.handle();
    emuLoop();
    webserverLoop();
    mqttLoop();

    // Serial commands (115200 baud, line end "newline")
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "read") {
            emuPrint();
        } else if (cmd == "clear") {
            emuRequestClearAll();
            Serial.println("[CMD] Deleting all retained topics of the emulator");
        } else if (cmd == "noauth") {
            // Forgotten web login: switch it off over USB
            config.web_user[0] = '\0';
            config.web_pass[0] = '\0';
            saveConfig();
            Serial.println("[CMD] Web login removed");
        } else if (cmd == "help") {
            Serial.println("[CMD] Commands: read, clear, noauth, help");
        }
    }

    if (millis() - lastStatusPrint > 10000) {
        lastStatusPrint = millis();
        uint8_t up = 0;
        for (uint8_t d = 0; d < D_COUNT; d++) up += mqttConnState(d) && emuMode(d) == M_ON;
        Serial.printf("[STATUS] WiFi:%s MQTT:%s devices online:%u Heap:%u\n",
            getWiFiStateString().c_str(), mqttIsConnected() ? "OK" : "---", up, (unsigned)ESP.getFreeHeap());
    }

    delay(5);
}
