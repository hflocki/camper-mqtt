// Camper MaxxFan - VanTurtle board (TCA9555) on an ESP32-C3, controlled via MQTT.
// Arduino IDE sketch, no ESPHome. See README.md for setup.

#include "config.h"
#include "settings.h"
#include "fan_control.h"
#include "wifi_manager.h"
#include "mqtt_ha.h"
#include "webserver.h"
#include "system.h"
#include <WiFi.h>
#include <ArduinoOTA.h>

static volatile uint32_t rebootAt = 0;
static bool otaStarted = false;

void requestReboot(uint32_t delayMs) {
  rebootAt = millis() + delayMs;
  if (rebootAt == 0) rebootAt = 1;
}

static void startArduinoOta() {
  ArduinoOTA.setHostname(settings.hostname.c_str());
  ArduinoOTA.setPassword(DEFAULT_OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("[ota] start"); });
  ArduinoOTA.onEnd([]()   { Serial.println("[ota] done"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] error %u\n", e); });
  ArduinoOTA.begin();
  otaStarted = true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nCamper MaxxFan %s\n", FW_VERSION);

  settingsLoad();
  fan::begin();
  wifimgr::begin();
  web::begin();
  mqttha::begin();
}

void loop() {
  wifimgr::loop();
  if (!otaStarted && wifimgr::staConnected()) startArduinoOta();
  if (otaStarted) ArduinoOTA.handle();

  fan::loop();
  mqttha::loop();
  web::loop();

  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    Serial.println("Rebooting...");
    delay(100);
    ESP.restart();
  }
  delay(2);
}
