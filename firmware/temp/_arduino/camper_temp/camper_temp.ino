/*
 * Camper Temp - DS18B20 temperature sensors on one ESP8266 / ESP32
 * ==================================================================
 * Hardware: ESP8266 (ESP-12E / NodeMCU / Wemos D1 mini) or ESP32 / ESP32-C3
 *   - Up to MAX_SENSORS DS18B20 on one 1-Wire bus at ONEWIRE_PIN (default GPIO4 = D2)
 *   - 4.7 kOhm pull-up between DATA and 3.3 V
 *
 * Sensors are found automatically on the bus and named in the web UI.
 * Values are published via MQTT (Home Assistant discovery) and shown in the web UI.
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#include "config.h"
#include "wifi_manager.h"
#include "temp_sensor.h"
#include "mqtt_ha.h"
#include "webserver.h"

AppConfig config;
SensorData sensorData;

static unsigned long lastStatusPrint = 0;

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
        Serial.println("\n[OTA] Update complete!");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        unsigned int pct = (total > 0) ? (progress * 100U / total) : 0;
        Serial.printf("[OTA] Progress: %u%%\r", pct);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]\n", error);
    });

    ArduinoOTA.begin();
    Serial.println("[OTA] Ready");
}

static void printRead() {
    for (int i = 0; i < MAX_SENSORS; i++) {
        const TempSensorConfig& s = config.sensors[i];
        if (!s.used) continue;
        const TempData& t = sensorData.temps[i];
        if (t.ok) {
            Serial.printf("[READ] %-24s %6.2f C (min %.2f / max %.2f) -> %s/%s\n",
                s.name, t.temp, t.min, t.max, config.mqtt_prefix, s.key);
        } else {
            Serial.printf("[READ] %-24s  error (%s, %lu errors)\n",
                s.name, t.present ? "no valid reading" : "not found on bus", (unsigned long)t.errors);
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.setTimeout(50);  // Do not block the loop while waiting for a line end
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  Camper Temp v" FW_VERSION);
    Serial.println("========================================");
    Serial.println();

    memset(&sensorData, 0, sizeof(SensorData));

    // Phase 1: load config & mount LittleFS
    webserverInit();

    // Phase 2: 1-Wire bus
    tempInit();

    // Phase 3: WiFi (blocking connect or AP fallback)
    wifiManagerInit();

    // Phase 4: start web server
    webserverStart();

    // Phase 5: OTA & MQTT
    setupOTA();
    mqttInit();

    Serial.printf("[MAIN] Setup done! Heap: %u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
    wifiManagerLoop();
    ArduinoOTA.handle();

    tempLoop();
    webserverLoop();
    mqttLoop();

    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "read") {
            printRead();
        } else if (cmd == "scan") {
            tempRequestScan();
            Serial.println("[CMD] Bus scan requested, list follows with 'bus'");
        } else if (cmd == "bus") {
            tempPrintBus();
        } else if (cmd == "noauth") {
            // Forgotten web login: switch it off over USB
            config.web_user[0] = '\0';
            config.web_pass[0] = '\0';
            saveConfig();
            Serial.println("[CMD] Web login removed");
        } else if (cmd == "help") {
            Serial.println("[CMD] Commands: read, scan, bus, noauth, help");
        }
    }

    if (millis() - lastStatusPrint > 10000) {
        lastStatusPrint = millis();
        int ok = 0, used = 0;
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (!config.sensors[i].used) continue;
            used++;
            if (sensorData.temps[i].ok) ok++;
        }
        Serial.printf("[STATUS] Bus:%u Sensors:%d/%d ok WiFi:%s MQTT:%s Heap:%u\n",
            sensorData.busCount, ok, used,
            getWiFiStateString().c_str(),
            mqttIsConnected() ? "OK" : "---",
            (unsigned)ESP.getFreeHeap());
    }

    delay(5);
    yield();
}
