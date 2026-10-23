#include "wifi_manager.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include "config.h"
#include "logger.h"
#include "settings.h"

// WiFi handling modelled on camper-tanklevel, which runs stable on the ESP32-C3 Super Mini:
// - no SSID: access point only
// - SSID set: station; the access point is only a fallback after 60 s without WiFi (or always on, if set)
// - fallback: pure AP mode, a station attempt (10 s) only every 30 s and never while a phone is
//   connected to the AP, because station attempts make the radio leave the AP channel
// - an always-on AP runs on the router channel, so station and AP do not fight over the channel

namespace wifi {

static const char *const TAG = "wifi";

static const uint32_t AP_FALLBACK_MS = 60000;     // no WiFi for this long -> fallback AP
static const uint32_t RETRY_INTERVAL_MS = 30000;  // station attempt in fallback mode every 30 s
static const uint32_t ATTEMPT_MS = 10000;         // one station attempt

enum class State : uint8_t { AP_ONLY, CONNECTING, CONNECTED, FALLBACK_AP, FALLBACK_TRY };

static DNSServer g_dns;
static State g_state = State::AP_ONLY;
static bool g_ap = false;
static bool g_mdns = false;
static uint32_t g_since = 0;     // start of the current state
static uint32_t g_attempt = 0;   // start of the current station attempt
static uint32_t g_last_try = 0;  // last station attempt in fallback mode
static uint32_t g_drops = 0;     // losses of an established connection
static char g_reason[40] = "-";  // last disconnect reason

String ap_ssid() {
  if (!g_settings.ap_ssid.isEmpty()) return g_settings.ap_ssid;
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s-%02X%02X", DEFAULT_HOSTNAME, mac[4], mac[5]);
  return String(buf);
}

int8_t tx_power() {
  const int v = g_settings.wifi_tx_power;
  return (v >= 8 && v <= 84) ? v : WIFI_TX_POWER_QDBM;
}

static void apply_tx_power() { esp_wifi_set_max_tx_power(tx_power()); }

static void set_state(State s) {
  g_state = s;
  g_since = millis();
}

static void on_event(arduino_event_id_t event, arduino_event_info_t info) {
  if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED || g_state != State::CONNECTED) return;
  const uint8_t r = info.wifi_sta_disconnected.reason;
  const char *name = WiFi.disconnectReasonName((wifi_err_reason_t) r);
  g_drops++;
  snprintf(g_reason, sizeof(g_reason), "%s (%u)", name ? name : "?", r);
}

static void start_ap(int channel) {
  if (g_ap) return;
  if (channel < 1 || channel > 13) channel = 1;
  WiFi.softAP(ap_ssid().c_str(), g_settings.ap_pass.c_str(), channel, false, 4);
  delay(100);
  g_dns.start(53, "*", WiFi.softAPIP());
  g_ap = true;
  apply_tx_power();
  LOGI(TAG, "Access point '%s' on channel %d, open http://%s", ap_ssid().c_str(), channel,
       WiFi.softAPIP().toString().c_str());
}

static void stop_ap() {
  if (!g_ap) return;
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  g_ap = false;
  LOGI(TAG, "Access point off");
}

static void begin_sta() {
  IPAddress ip, gw, mask, dns;
  if (ip.fromString(g_settings.static_ip) && gw.fromString(g_settings.gateway)) {
    if (!mask.fromString(g_settings.subnet)) mask = IPAddress(255, 255, 255, 0);
    if (!dns.fromString(g_settings.dns)) dns = gw;
    WiFi.config(ip, gw, mask, dns);
  }
  WiFi.begin(g_settings.wifi_ssid.c_str(), g_settings.wifi_pass.c_str());
  apply_tx_power();
  g_attempt = millis();
}

void begin() {
  WiFi.persistent(false);
  WiFi.onEvent(on_event);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.setHostname(g_settings.hostname.c_str());
  WiFi.setAutoReconnect(false);  // reconnects are handled in loop()
  WiFi.setSleep(false);
  LOGI(TAG, "Last reset: %s, TX power %.1f dBm, AP %s", reset_reason(), tx_power() / 4.0f,
       g_settings.ap_always ? "always on" : "fallback only");

  if (g_settings.wifi_ssid.isEmpty()) {
    WiFi.mode(WIFI_AP);
    start_ap(1);
    set_state(State::AP_ONLY);
    LOGI(TAG, "No WiFi configured, access point only");
    return;
  }
  WiFi.mode(WIFI_STA);
  delay(100);
  begin_sta();
  set_state(State::CONNECTING);
  LOGI(TAG, "Connecting to '%s'", g_settings.wifi_ssid.c_str());
}

static void on_connected() {
  set_state(State::CONNECTED);
  LOGI(TAG, "Connected, IP %s, channel %d, RSSI %d dBm", WiFi.localIP().toString().c_str(), WiFi.channel(),
       WiFi.RSSI());
  apply_tx_power();
  if (!g_mdns && MDNS.begin(g_settings.hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    g_mdns = true;
  }
  if (g_settings.ap_always) {
    // Restart the AP on the router channel, otherwise station and AP keep switching channels.
    stop_ap();
    WiFi.mode(WIFI_AP_STA);
    start_ap(WiFi.channel());
  } else {
    stop_ap();
    WiFi.mode(WIFI_STA);
  }
}

static void enter_fallback() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  delay(100);
  start_ap(1);
  set_state(State::FALLBACK_AP);
  g_last_try = millis();
  LOGW(TAG, "No WiFi for %lu s, fallback access point active", (unsigned long) (AP_FALLBACK_MS / 1000));
}

void loop() {
  if (g_ap) g_dns.processNextRequest();
  const uint32_t now = millis();
  const bool up = WiFi.status() == WL_CONNECTED;
  const bool ap_in_use = g_ap && WiFi.softAPgetStationNum() > 0;

  switch (g_state) {
    case State::AP_ONLY:
      break;

    case State::CONNECTING:
      if (up) {
        on_connected();
      } else if (now - g_since > AP_FALLBACK_MS) {
        enter_fallback();
      } else if (now - g_attempt > ATTEMPT_MS) {
        // New attempt every 10 s, some routers ignore the first association.
        WiFi.disconnect();
        begin_sta();
      }
      break;

    case State::CONNECTED:
      if (!up) {
        LOGW(TAG, "Connection lost: %s", g_reason);
        WiFi.disconnect();
        begin_sta();
        set_state(State::CONNECTING);
      }
      break;

    case State::FALLBACK_AP:
      if (!ap_in_use && now - g_last_try > RETRY_INTERVAL_MS) {
        WiFi.mode(WIFI_AP_STA);
        begin_sta();
        set_state(State::FALLBACK_TRY);
      }
      break;

    case State::FALLBACK_TRY:
      if (up) {
        LOGI(TAG, "WiFi back");
        on_connected();
      } else if (ap_in_use || now - g_since > ATTEMPT_MS) {
        // Give up this attempt and go back to a quiet AP: a phone needs a stable AP to save settings.
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        set_state(State::FALLBACK_AP);
        g_last_try = now;
      }
      break;
  }
}

bool connected() { return WiFi.status() == WL_CONNECTED; }
bool ap_active() { return g_ap; }
String ip() { return connected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }
int rssi() { return connected() ? WiFi.RSSI() : 0; }
uint32_t drops() { return g_drops; }
const char *last_reason() { return g_reason; }

const char *state_text() {
  switch (g_state) {
    case State::AP_ONLY: return "Access Point (kein WLAN eingerichtet)";
    case State::CONNECTING: return "Verbinde …";
    case State::CONNECTED: return "Verbunden";
    case State::FALLBACK_AP: return "Access Point (WLAN nicht erreichbar)";
    case State::FALLBACK_TRY: return "Access Point, versuche WLAN …";
  }
  return "?";
}

const char *reset_reason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Einschalten";
    case ESP_RST_SW: return "Neustart (Software)";
    case ESP_RST_PANIC: return "Absturz";
    case ESP_RST_INT_WDT: return "Watchdog (Interrupt)";
    case ESP_RST_TASK_WDT: return "Watchdog (Task)";
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Brownout (Spannung zu niedrig)";
    case ESP_RST_DEEPSLEEP: return "Deep Sleep";
    case ESP_RST_EXT: return "Reset-Pin";
    case ESP_RST_USB: return "USB";
    default: return "Unbekannt";
  }
}

}  // namespace wifi
