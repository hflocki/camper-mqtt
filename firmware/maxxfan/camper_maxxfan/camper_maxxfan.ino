// MaxxFan Node - VanTurtle board (TCA9555) on an ESP32-C3, controlled via MQTT.
// Arduino IDE sketch, no ESPHome. See README.md for setup.

#include "config.h"
#include "settings.h"
#include "safeguard.h"
#include "evlog.h"
#include "fan_control.h"
#include "wifi_manager.h"
#include "mqtt_ha.h"
#include "webserver.h"
#include "system.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

static volatile uint32_t rebootAt = 0;
static bool otaStarted = false;

void requestReboot(uint32_t delayMs) {
  rebootAt = millis() + delayMs;
  if (rebootAt == 0) rebootAt = 1;
}

static void startArduinoOta() {
  ArduinoOTA.setHostname(settings.hostname.c_str());
  ArduinoOTA.setPassword(DEFAULT_OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    // Filesystem upload over the network (espota -s): unmount LittleFS first
    if (ArduinoOTA.getCommand() == U_SPIFFS) LittleFS.end();
    Serial.printf("[ota] start (%s)\n", ArduinoOTA.getCommand() == U_SPIFFS ? "filesystem" : "firmware");
  });
  ArduinoOTA.onEnd([]() {
    evlog::add("Update über den IDE-Netzwerkport ok, Neustart");
    if (ArduinoOTA.getCommand() == U_FLASH) guard::otaDone();
  });
  ArduinoOTA.onError([](ota_error_t e) { evlog::add("Update über den IDE-Netzwerkport fehlgeschlagen (Fehler %u)", (unsigned)e); });
  ArduinoOTA.begin();
  otaStarted = true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nCamper MaxxFan %s\n", FW_VERSION);

  evlog::begin();
  evlog::add("Start v%s, Grund: %s", FW_VERSION, evlog::resetReason());
  guard::begin();   // may boot the previous firmware (failed update)
  if (guard::status().length()) evlog::add("%s", guard::status().c_str());
  settingsLoad();
  fan::begin();
  wifimgr::begin();
  web::begin();
  mqttha::begin();
}

void loop() {
  wifimgr::loop();
  guard::loop(wifimgr::staConnected() || settings.wifiSsid.isEmpty());
  evlog::watch(wifimgr::staConnected(), mqttha::connected());
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
