#include "wifi_manager.h"
#include "config.h"
#include "settings.h"
#include "safeguard.h"
#include <WiFi.h>

namespace {
bool     ap = false;
uint32_t lastConnected = 0;
bool     wasConnected = false;
String   suffix;

void startAp() {
  WiFi.mode(settings.wifiSsid.isEmpty() ? WIFI_AP : WIFI_AP_STA);
  WiFi.softAP(wifimgr::apSsid().c_str(), settings.apPass.c_str());
  ap = true;
  Serial.printf("[wifi] setup AP '%s' at %s\n", wifimgr::apSsid().c_str(),
                WiFi.softAPIP().toString().c_str());
}

// A changed WiFi that does not connect: put the previous, working values back and restart
void restorePreviousWifi() {
  String ssid, pass, net;
  if (!guard::wifiTrialRestore(ssid, pass, net)) return;
  settings.wifiSsid = ssid;
  settings.wifiPass = pass;
  // net = "0" or "1|ip|gateway|subnet|dns" (wifimgr::netString)
  settings.staticIp = net.startsWith("1|");
  if (settings.staticIp) {
    String f[4];
    int from = 2;
    for (int i = 0; i < 4; i++) {
      int bar = net.indexOf('|', from);
      f[i] = bar < 0 ? net.substring(from) : net.substring(from, bar);
      from = bar < 0 ? net.length() : bar + 1;
    }
    settings.ip = f[0]; settings.gateway = f[1]; settings.subnet = f[2]; settings.dns = f[3];
  }
  settingsSave();
  Serial.println("[wifi] new WiFi failed - previous WiFi restored, restarting");
  delay(200);
  ESP.restart();
}

void stopAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap = false;
  Serial.println("[wifi] setup AP closed");
}
} // namespace

namespace wifimgr {

void begin() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[7];
  snprintf(buf, sizeof(buf), "%02X%02X%02X",
           (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  suffix = buf;

  WiFi.persistent(false);
  WiFi.setHostname(settings.hostname.c_str());

  if (settings.wifiSsid.isEmpty()) {   // first start: AP only
    startAp();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (settings.staticIp) {
    IPAddress ip, gw, sn, dns;
    if (ip.fromString(settings.ip) && gw.fromString(settings.gateway) && sn.fromString(settings.subnet)) {
      if (!dns.fromString(settings.dns)) dns = gw;
      WiFi.config(ip, gw, sn, dns);
    } else {
      Serial.println("[wifi] invalid static IP settings, using DHCP");
    }
  }
  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
  lastConnected = millis();
  Serial.printf("[wifi] connecting to '%s'\n", settings.wifiSsid.c_str());
}

void loop() {
  if (settings.wifiSsid.isEmpty()) return;
  bool conn = WiFi.status() == WL_CONNECTED;
  uint32_t now = millis();

  if (guard::wifiTrialPending()) {
    if (conn) guard::wifiTrialOk();
    else if (now > WIFI_TRIAL_MS) restorePreviousWifi();
  }

  if (conn) {
    lastConnected = now;
    if (!wasConnected) Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    if (ap) stopAp();
  } else if (!ap && now - lastConnected > WIFI_AP_FALLBACK_MS) {
    startAp();   // STA keeps retrying in AP_STA mode
  }
  wasConnected = conn;
}

bool   apActive()     { return ap; }
bool   staConnected() { return WiFi.status() == WL_CONNECTED; }
String apSsid()       { return String(AP_SSID_PREFIX) + suffix; }
String macSuffix()    { return suffix; }
String ipString()     { return staConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }
int    rssi()         { return staConnected() ? WiFi.RSSI() : 0; }

String netString() {
  if (!settings.staticIp) return "0";
  return "1|" + settings.ip + "|" + settings.gateway + "|" + settings.subnet + "|" + settings.dns;
}

} // namespace wifimgr
