// =====================================================================
//  Camper Display
//  Guition ESP32-4848S040 (4" 480x480 touch, ESP32-S3, 16 MB flash, 8 MB PSRAM)
//
//  - Wall panel with LVGL touch UI
//  - Same pages in the browser (built-in web server + WebSocket)
//  - Values and commands via MQTT (e.g. broker on the LTE router)
//  - Setup access point like the other camper projects
//  - Everything lives in the internal flash / NVS: no SD card, safe on hard power cuts
//
//  Board settings (Arduino IDE): see README.md
// =====================================================================

#include "config.h"
#include "settings.h"
#include "datastore.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "display_hw.h"
#include "ui.h"
#include "web.h"
#include "notify.h"
#include "weather.h"
#include <ArduinoOTA.h>

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[boot] %s %s\n", FW_NAME, FW_VERSION);
  Serial.printf("[boot] PSRAM %u kB free\n", (unsigned)(ESP.getFreePsram() / 1024));

  cfg::load();
  store::begin();

  if (!disp::begin()) Serial.println("[boot] display init failed, web page still works");
  wifimgr::begin();
  ui::begin();
  mqttc::begin();
  web::begin();
  notify::begin();
  wx::begin();
}

static bool otaStarted = false;

void loop() {
  wifimgr::loop();
  mqttc::loop();
  web::loop();
  ui::loop();
  notify::loop();
  disp::loop();
  if (!otaStarted && wifimgr::staConnected()) {   // network upload from the Arduino IDE
    ArduinoOTA.setHostname(settings.hostname.c_str());
    ArduinoOTA.setPassword(DEFAULT_OTA_PASSWORD);
    ArduinoOTA.setMdnsEnabled(false);              // mDNS is started by the WiFi manager
    ArduinoOTA.begin();
    otaStarted = true;
  }
  if (otaStarted) ArduinoOTA.handle();
  delay(2);
}
