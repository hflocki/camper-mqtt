#include "wifi_manager.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include "mqtt_client.h"

// Same behaviour as the other camper projects:
//  - no WiFi configured  -> setup AP only
//  - WiFi lost > 1 min   -> setup AP opens, STA keeps retrying in the background
//  - WiFi back           -> setup AP closes again

namespace {
bool      ap = false;
bool      wasConnected = false;
bool      mdnsStarted = false;
bool      fallback = false;       // static IP gave no working connection, DHCP for this session
uint32_t  staticSince = 0;        // connected with static IP but MQTT not reachable since
uint32_t  lastConnected = 0;
String    suffix;
DNSServer dns;

void startAp() {
  WiFi.mode(settings.wifiSsid.isEmpty() ? WIFI_AP : WIFI_AP_STA);
  WiFi.softAP(wifimgr::apSsid().c_str(), settings.apPass.c_str());
  dns.start(53, "*", WiFi.softAPIP());   // captive portal: every name resolves to us
  ap = true;
  Serial.printf("[wifi] setup AP '%s' at %s\n", wifimgr::apSsid().c_str(),
                WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  dns.stop();
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

  configTzTime(settings.tz.c_str(), settings.ntpServer.c_str(), "pool.ntp.org");

  if (settings.wifiSsid.isEmpty()) {   // first start: AP only
    startAp();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                // keeps the web page and MQTT responsive
  WiFi.setAutoReconnect(true);
  if (settings.staticIp) {
    String err;
    if (wifimgr::staticIpValid(settings.ip, settings.gateway, settings.subnet, err)) {
      IPAddress ip, gw, sn, dnsIp;
      ip.fromString(settings.ip); gw.fromString(settings.gateway); sn.fromString(settings.subnet);
      if (!dnsIp.fromString(settings.dns)) dnsIp = gw;
      WiFi.config(ip, gw, sn, dnsIp);
    } else {
      Serial.printf("[wifi] static IP not used (%s), using DHCP\n", err.c_str());
      fallback = true;
    }
  }
  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
  lastConnected = millis();
  Serial.printf("[wifi] connecting to '%s'\n", settings.wifiSsid.c_str());
}

void loop() {
  if (ap) dns.processNextRequest();
  if (settings.wifiSsid.isEmpty()) return;

  bool conn = WiFi.status() == WL_CONNECTED;
  uint32_t now = millis();

  if (conn) {
    lastConnected = now;
    if (!wasConnected) {
      Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
      if (!mdnsStarted && MDNS.begin(settings.hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
      }
    }
    if (ap) stopAp();
    // Static IP, WiFi joined, but the MQTT broker stays unreachable (wrong address,
    // wrong network): switch to DHCP for this session so the device can be found again.
    if (settings.staticIp && !fallback && !settings.mqttHost.isEmpty()) {
      if (mqttc::connected()) staticSince = 0;
      else if (!staticSince) staticSince = now;
      else if (now - staticSince > STATIC_IP_FALLBACK_MS) {
        Serial.println("[wifi] no MQTT with the static IP, switching to DHCP until restart");
        fallback = true;
        WiFi.disconnect();
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
        wasConnected = false;
        return;
      }
    }
  } else if (!ap && now - lastConnected > WIFI_AP_FALLBACK_MS) {
    startAp();
  }
  wasConnected = conn;
}

bool   apActive()     { return ap; }
bool   staConnected() { return WiFi.status() == WL_CONNECTED; }
String apSsid()       { return String(AP_SSID_PREFIX) + suffix; }
String ipString()     { return staConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }
int    rssi()         { return staConnected() ? WiFi.RSSI() : 0; }
bool   timeValid()    { return time(nullptr) > 1700000000; }
bool   dhcpFallback() { return fallback; }

bool staticIpValid(const String& ipS, const String& gwS, const String& snS, String& err) {
  IPAddress ip, gw, sn;
  if (!ip.fromString(ipS))  { err = "IP-Adresse ung\xC3\xBCltig"; return false; }
  if (!gw.fromString(gwS))  { err = "Gateway ung\xC3\xBCltig"; return false; }
  if (!sn.fromString(snS))  { err = "Subnetz ung\xC3\xBCltig"; return false; }
  uint32_t i = (uint32_t)ip, g = (uint32_t)gw, m = (uint32_t)sn;
  uint32_t mh = __builtin_bswap32(m);   // host order: mask must be 1...10...0
  if (mh == 0 || (mh | (mh - 1)) != 0xFFFFFFFF) { err = "Subnetz ung\xC3\xBCltig (z. B. 255.255.255.0)"; return false; }
  if ((i & m) != (g & m)) { err = "Gateway liegt nicht im Netz der IP-Adresse"; return false; }
  if (i == g)             { err = "IP-Adresse ist gleich dem Gateway"; return false; }
  uint32_t host = __builtin_bswap32(i & ~m);
  if (host == 0 || host == __builtin_bswap32(~m)) { err = "IP-Adresse ist Netz- oder Broadcast-Adresse"; return false; }
  return true;
}

} // namespace wifimgr
