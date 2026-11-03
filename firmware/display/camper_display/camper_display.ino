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
#include "history.h"
#include "rules.h"
#include "sensors.h"
#include "weather.h"
#include "depart.h"
#include <ArduinoOTA.h>
#include "safeguard.h"
#include "evlog.h"

// A changed WiFi (saved in the web UI while connected) that does not connect within
// WIFI_TRIAL_MS: put the previous, working values back and restart (safeguard.h)
static void wifiTrialCheck() {
  if (!guard::wifiTrialPending()) return;
  if (wifimgr::staConnected()) { guard::wifiTrialOk(); return; }
  if (millis() < WIFI_TRIAL_MS) return;
  String ssid, pass, net;
  if (!guard::wifiTrialRestore(ssid, pass, net)) return;
  String f[5];   // "static|ip|gateway|subnet|dns"
  int from = 0;
  for (int i = 0; i < 5; i++) {
    int bar = net.indexOf('|', from);
    f[i] = bar < 0 ? net.substring(from) : net.substring(from, bar);
    from = bar < 0 ? net.length() : bar + 1;
  }
  settings.wifiSsid = ssid;
  settings.wifiPass = pass;
  settings.staticIp = f[0] == "1";
  settings.ip = f[1];
  settings.gateway = f[2];
  settings.subnet = f[3];
  settings.dns = f[4];
  cfg::save();
  Serial.println("[boot] new WiFi failed - previous WiFi restored, restarting");
  delay(200);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  evlog::begin();
  evlog::add("Start v%s, Grund: %s", FW_VERSION, evlog::resetReason());
  guard::begin();   // may boot the previous firmware (failed update)
  if (guard::status().length()) evlog::add("%s", guard::status().c_str());
  delay(200);
  Serial.printf("\n[boot] %s %s\n", FW_NAME, FW_VERSION);
  Serial.printf("[boot] PSRAM %u kB free\n", (unsigned)(ESP.getFreePsram() / 1024));

  cfg::load();
  store::begin();

  if (!disp::begin()) Serial.println("[boot] display init failed, web page still works");
  wifimgr::begin();
  wx::begin();      // before the UI: it shows weather data and warnings
  ui::begin();
  mqttc::begin();
  history::begin();
  rules::begin();
  sensors::begin();
  web::begin();
  notify::begin();
}

static bool otaStarted = false;

void loop() {
  wifimgr::loop();
  wifiTrialCheck();
  guard::loop(wifimgr::staConnected() || settings.wifiSsid.isEmpty());
  evlog::watch(wifimgr::staConnected(), mqttc::connected());
  mqttc::loop();
  web::loop();
  ui::loop();
  notify::loop();
  history::loop();
  rules::loop();
  depart::loop();
  disp::loop();
  if (!otaStarted && wifimgr::staConnected()) {   // network upload from the Arduino IDE
    ArduinoOTA.setHostname(settings.hostname.c_str());
    ArduinoOTA.setPassword(DEFAULT_OTA_PASSWORD);
    ArduinoOTA.setMdnsEnabled(false);              // mDNS is started by the WiFi manager
    ArduinoOTA.onEnd([]() {
      evlog::add("Update über den IDE-Netzwerkport ok, Neustart");
      if (ArduinoOTA.getCommand() == U_FLASH) guard::otaDone();
    });
    ArduinoOTA.onError([](ota_error_t e) { evlog::add("Update über den IDE-Netzwerkport fehlgeschlagen (Fehler %u)", (unsigned)e); });
    ArduinoOTA.begin();
    otaStarted = true;
  }
  if (otaStarted) ArduinoOTA.handle();
  delay(2);
}
