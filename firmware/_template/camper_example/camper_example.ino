/*
 * Camper Example - template for a new camper-mqtt device
 * =======================================================
 * Hardware: ESP32-C3 / ESP32-S3 (Arduino-ESP32 3.3.x)
 *
 * What the template already does (shared by all camper-mqtt devices):
 *   - WiFi with setup access point (first start / WiFi gone for 60 s), reconnect on its own
 *   - Web UI from LittleFS (data/index.html.gz) with live values, settings, WiFi/MQTT, login
 *   - Firmware (.bin) and web page (.gz) upload in the web UI, Arduino OTA
 *   - MQTT with last will and Home Assistant discovery, one entity per value
 *   - Settings in /config.json, factory defaults and passwords in config_local.h
 *
 * Your part: sensor.cpp (values list, hardware, measuring) - see ANLEITUNG.md
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#include "config.h"
#include "wifi_manager.h"
#include "sensor.h"
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
    ArduinoOTA.onEnd([]() { Serial.println("\n[OTA] Update complete!"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", total ? progress * 100U / total : 0);
    });
    ArduinoOTA.onError([](ota_error_t error) { Serial.printf("[OTA] Error[%u]\n", error); });
    ArduinoOTA.begin();
    Serial.println("[OTA] Ready");
}

void setup() {
    Serial.begin(115200);
    Serial.setTimeout(50);  // Do not block the loop while waiting for a line end
    delay(1000);
    Serial.println();
    Serial.println("========================================");
    Serial.println("  " DEVICE_NAME " v" FW_VERSION);
    Serial.println("========================================");

    memset(&sensorData, 0, sizeof(SensorData));

    webserverInit();      // 1. LittleFS + config
    sensorInit();         // 2. hardware
    wifiManagerInit();    // 3. WiFi (or setup AP)
    webserverStart();     // 4. web UI
    setupOTA();           // 5. Arduino OTA
    mqttInit();           // 6. MQTT

    Serial.printf("[MAIN] Setup done! Heap: %u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
    wifiManagerLoop();
    ArduinoOTA.handle();
    sensorLoop();
    webserverLoop();
    mqttLoop();

    // Serial commands (115200 baud, line end "newline")
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "read") {
            sensorPrint();
        } else if (cmd == "measure") {
            sensorRequestMeasure();
        } else if (cmd == "noauth") {
            // Forgotten web login: switch it off over USB
            config.web_user[0] = '\0';
            config.web_pass[0] = '\0';
            saveConfig();
            Serial.println("[CMD] Web login removed");
        } else if (cmd == "help") {
            Serial.println("[CMD] Commands: read, measure, noauth, help");
        }
        // TODO: own serial commands for testing
    }

    if (millis() - lastStatusPrint > 10000) {
        lastStatusPrint = millis();
        Serial.printf("[STATUS] Sensor:%s Reads:%u WiFi:%s MQTT:%s Heap:%u\n",
            sensorData.ok ? "OK" : "---", (unsigned)sensorData.readCount,
            getWiFiStateString().c_str(), mqttIsConnected() ? "OK" : "---", (unsigned)ESP.getFreeHeap());
    }

    delay(5);
}
