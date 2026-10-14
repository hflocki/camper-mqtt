/*
 * Camper Tanklevel - fresh water + gas level on one ESP32
 * =======================================================
 * Hardware: ESP32-C3 / ESP32
 *   - Fresh water: Votronic analog tank sender on ADC_PIN (ESP32-C3: GPIO0)
 *   - Gas: BLE gas level sensor, received passively via the built-in Bluetooth LE
 *          (default: Rotarex DIMES / SRG WAVE, see GAS SENSOR DEFINITION in config.h)
 *
 * Values are published via MQTT (Home Assistant discovery) and shown in the web UI.
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "config.h"
#include "wifi_manager.h"
#include "water_sensor.h"
#include "gas_sensor.h"
#include "mqtt_ha.h"
#include "webserver.h"

AppConfig config;
SensorData sensorData;

static unsigned long lastStatusPrint = 0;

void setupOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(DEFAULT_OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
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
    const WaterData& w = sensorData.water;
    const GasData& g = sensorData.gas;
    Serial.printf("[READ] Water: %.3f V | %.1f L | %.0f%% | Filter: %s\n",
        w.avg_voltage, w.liters, w.percent, config.filter_active ? "ON" : "OFF");
    Serial.printf("[READ] Gas:   %.1f%% (raw %.1f%%) | %.1f L | Battery: %.0f%% | BLE: %s\n",
        g.level, g.raw_level, g.content, g.battery, g.ble_state);
}

void setup() {
    Serial.begin(115200);
    Serial.setTimeout(50);  // Do not block the loop while waiting for a line end
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  Camper Tanklevel v" FW_VERSION " (Water + Gas)");
    Serial.println("========================================");
    Serial.println();

    memset(&sensorData, 0, sizeof(SensorData));

    // Phase 1: load config & mount LittleFS
    webserverInit();

    // Phase 2: fresh water ADC
    waterInit();

    // Phase 3: WiFi (blocking connect or AP fallback)
    wifiManagerInit();

    // Phase 4: Bluetooth LE scanner (after WiFi, both share the radio)
    gasInit();

    // Phase 5: start web server
    webserverStart();

    // Phase 6: OTA & MQTT
    setupOTA();
    mqttInit();

    Serial.printf("[MAIN] Setup done! Heap: %u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
    wifiManagerLoop();
    ArduinoOTA.handle();

    waterLoop();
    gasLoop();
    mqttLoop();
    webserverLoop();

    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "read") {
            printRead();
        } else if (cmd == "scan") {
            gasPrintSeenList();
        } else if (cmd == "filter") {
            config.filter_active = !config.filter_active;
            saveConfig();
            Serial.printf("[CMD] Water filter: %s\n", config.filter_active ? "ON" : "OFF");
        } else if (cmd == "help") {
            Serial.println("[CMD] Commands: read, scan, filter, help");
        }
    }

    if (millis() - lastStatusPrint > 10000) {
        lastStatusPrint = millis();
        Serial.printf("[STATUS] Water:%.1fL (%.0f%%) Gas:%.1f%% %s Adv:%lu WiFi:%s MQTT:%s Heap:%u\n",
            sensorData.water.liters, sensorData.water.percent,
            sensorData.gas.level, sensorData.gas.ble_state,
            (unsigned long)sensorData.gas.adv_count,
            getWiFiStateString().c_str(),
            mqttIsConnected() ? "OK" : "---",
            (unsigned)ESP.getFreeHeap());
    }

    delay(5);
    yield();
}
