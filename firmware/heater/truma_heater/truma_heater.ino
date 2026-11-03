// Truma heater control over MQTT via LIN bus (iNet Box emulation, CP Plus stays LIN master).
//
// Modules:
//   inetbox        LIN slave + Truma iNet Box protocol (own FreeRTOS task)
//   heater_control setpoint/fan memory, decoking, command mapping
//   mqtt_ha        MQTT topics (camper/heater, inetbox2mqtt layout) + Home Assistant discovery
//   webserver      web UI, settings, log, OTA
//   wifi_manager   station with static IP option, setup access point fallback

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "safeguard.h"
#include "evlog.h"
#include <Preferences.h>
#include "heater_control.h"
#include "inetbox.h"
#include "logger.h"
#include "mqtt_ha.h"
#include "settings.h"
#include "webserver.h"
#include "wifi_manager.h"

static const char *const TAG = "main";

static truma::InetBox g_inetbox;

static void led_write(int pin, bool on) {
  if (pin < 0) return;
  digitalWrite(pin, (on ^ LED_ACTIVE_LOW) ? HIGH : LOW);
}

static void leds_begin() {
  if (MQTT_LED_PIN >= 0) pinMode(MQTT_LED_PIN, OUTPUT);
  if (LIN_LED_PIN >= 0) pinMode(LIN_LED_PIN, OUTPUT);
  led_write(MQTT_LED_PIN, false);
  led_write(LIN_LED_PIN, false);
}

static void leds_loop() {
  static uint32_t last = 0;
  static uint32_t last_frames = 0;
  if (millis() - last < 100) return;
  last = millis();

  led_write(MQTT_LED_PIN, mqtt::connected());

  // LIN LED: steady on when the CP Plus polls us, short flicker on any LIN traffic before INIT.
  const auto &s = heater::state();
  bool flicker = s.frames_ok != last_frames;
  last_frames = s.frames_ok;
  led_write(LIN_LED_PIN, s.cpplus_connected || flicker);
}

static void time_begin() {
  // Gateway as second NTP server: camper routers often run one and work without internet.
  String gw = WiFi.gatewayIP().toString();
  configTzTime(g_settings.tz.c_str(), g_settings.ntp_server.c_str(), gw == "0.0.0.0" ? nullptr : gw.c_str());
}

// A changed WiFi (saved in the web UI while connected) that does not connect within
// WIFI_TRIAL_MS: put the previous, working values back and restart (safeguard.h)
static void wifiTrialCheck() {
  if (!guard::wifiTrialPending()) return;
  if (wifi::connected()) { guard::wifiTrialOk(); return; }
  if (millis() < WIFI_TRIAL_MS) return;
  String ssid, pass, net;
  if (!guard::wifiTrialRestore(ssid, pass, net)) return;
  String f[4];
  int from = 0;
  for (int i = 0; i < 4; i++) {
    int bar = net.indexOf('|', from);
    f[i] = bar < 0 ? net.substring(from) : net.substring(from, bar);
    from = bar < 0 ? net.length() : bar + 1;
  }
  Preferences p;
  p.begin("truma", false);
  p.putString("ssid", ssid);
  p.putString("wpass", pass);
  p.putString("ip", f[0]);
  p.putString("gw", f[1]);
  p.putString("mask", f[2]);
  p.putString("dns", f[3]);
  p.end();
  LOGW(TAG, "New WiFi failed - previous WiFi restored, restarting");
  delay(200);
  ESP.restart();
}

void setup() {
#if SERIAL_LOG_ENABLED
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  delay(200);
#else
  // Release UART0 pins (TX0 = GPIO1, RX0 = GPIO3) so nothing back-feeds the USB-serial chip.
  pinMode(1, INPUT);
  pinMode(3, INPUT);
#endif

  evlog::begin();
  evlog::add("Start v%s, Grund: %s", FW_VERSION, evlog::resetReason());
  guard::begin();   // may boot the previous firmware (failed update)
  if (guard::status().length()) evlog::add("%s", guard::status().c_str());
  settings_load();
  log_init((LogLevel) constrain(g_settings.log_level, 0, 3));
  LOGI(TAG, "%s %s on %s", FW_NAME, FW_VERSION, BOARD_NAME);

  leds_begin();

  // Start the LIN slave first: it does not depend on WiFi and the CP Plus should see us as early as possible.
  if (!g_inetbox.begin(LIN_UART_NUM, LIN_TX_PIN, LIN_RX_PIN, LIN_CS_PIN, LIN_FAULT_PIN, LIN_OBSERVER_MODE)) {
    LOGE(TAG, "LIN start failed");
  }
  heater::begin(&g_inetbox);

  wifi::begin();
  mqtt::begin();
  web::begin();
}

void loop() {
  static bool time_started = false;

  wifi::loop();
  wifiTrialCheck();
  guard::loop(wifi::connected() || g_settings.wifi_ssid.isEmpty());
  evlog::watch(wifi::connected(), mqtt::connected());
  if (!time_started && wifi::connected()) {
    time_started = true;
    time_begin();
  }

  heater::loop();
  mqtt::loop();
  web::loop();
  leds_loop();
  delay(2);
}
