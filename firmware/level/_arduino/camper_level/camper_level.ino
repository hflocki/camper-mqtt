/*
 * Camper Level - level sensor with wedge recommendation for motorhomes
 * =====================================================================
 * Hardware: ESP32 / ESP32-S3 / ESP32-C3 + Adafruit BNO085 (I2C, pins in config.h)
 *
 * Based on the caravan level sensor by klehnst:
 * https://github.com/klehnst/ESP32-Adrafruit-BNO085-HomeAssistant-Caravan-Levelsensor-MQTT
 * This version only supports motorhomes levelled with four wedges and follows the
 * conventions of the other camper-mqtt devices (topics, web UI, login, config_local.h).
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "config.h"
#include "wifi_manager.h"
#include "sensor.h"
#include "mqtt_ha.h"
#include "webserver.h"

AppConfig config;
SensorData sensorData;

static unsigned long lastStatusPrint = 0;
static unsigned long lastSensorTry = 0;
static bool sensorStarted = false;
static unsigned long fastSince = 0;

#define SENSOR_START_DELAY_MS  3000    // let WiFi settle before talking to the BNO085
#define SENSOR_RETRY_MS        30000   // retry if the sensor was not found

static void setupOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(DEFAULT_OTA_PASSWORD);
    ArduinoOTA.onStart([]() { Serial.println("[OTA] Start"); });
    ArduinoOTA.onEnd([]() { Serial.println("\n[OTA] Done"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] Error[%u]\n", e); });
    ArduinoOTA.begin();
}

void setup() {
    Serial.begin(115200);
    Serial.setTimeout(50);
    delay(1000);
    Serial.println();
    Serial.println("========================================");
    Serial.println("  Camper Level v" FW_VERSION);
    Serial.println("========================================");

    memset(&sensorData, 0, sizeof(SensorData));

    webserverInit();     // LittleFS + config
    wifiManagerInit();   // blocking connect or AP fallback
    webserverStart();
    setupOTA();
    mqttInit();
    Serial.printf("[MAIN] Setup done, heap %u\n", (unsigned)ESP.getFreeHeap());
}

static void handleSerial() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "tare") {
        sensorSetTare();
        mqttRequestSettings();
    } else if (cmd == "notare") {
        sensorResetTare();
        mqttRequestSettings();
    } else if (cmd == "read") {
        Serial.printf("[READ] pitch %.1f roll %.1f (raw %.2f / %.2f) level %s\n",
            sensorData.pitch, sensorData.roll, sensorData.raw_pitch, sensorData.raw_roll,
            sensorData.is_level ? "yes" : "no");
        Serial.printf("[READ] wedges cm: FL %.1f FR %.1f RL %.1f RR %.1f\n",
            sensorData.wedge_fl, sensorData.wedge_fr, sensorData.wedge_rl, sensorData.wedge_rr);
    } else if (cmd == "sensor") {
        sensorRestart();
    } else if (cmd == "noauth") {
        // Forgotten web login: switch it off over USB
        config.web_user[0] = '\0';
        config.web_pass[0] = '\0';
        saveConfig();
        Serial.println("[CMD] Web login removed");
    } else if (cmd == "help") {
        Serial.println("[CMD] Commands: read, tare, notare, sensor, noauth, help");
    }
}

void loop() {
    wifiManagerLoop();
    ArduinoOTA.handle();

    // The sensor is started a few seconds after boot and retried if it was not found
    if (!sensorData.sensor_ok && millis() > SENSOR_START_DELAY_MS &&
        (!sensorStarted || millis() - lastSensorTry > SENSOR_RETRY_MS)) {
        sensorStarted = true;
        lastSensorTry = millis();
        sensorInit();
    }
    sensorLoop();

    // Fast mode (5 Hz) is meant for levelling only: switch it off after a while, in case the
    // web UI or HA card that switched it on was closed
    if (!config.mqtt_fast) {
        fastSince = 0;
    } else if (fastSince == 0) {
        fastSince = millis();
    } else if (millis() - fastSince > MQTT_FAST_TIMEOUT_MS) {
        config.mqtt_fast = false;
        saveConfig();
        mqttRequestSettings();
        Serial.println("[MAIN] Fast mode switched off after timeout");
    }

    mqttLoop();
    webserverLoop();
    handleSerial();

    if (millis() - lastStatusPrint > 10000) {
        lastStatusPrint = millis();
        Serial.printf("[STATUS] P:%.1f R:%.1f Sensor:%s WiFi:%s MQTT:%s Heap:%u\n",
            sensorData.pitch, sensorData.roll, sensorData.sensor_ok ? "OK" : "---",
            getWiFiStateString().c_str(), mqttIsConnected() ? "OK" : "---",
            (unsigned)ESP.getFreeHeap());
    }
    delay(2);
}
