#include "webserver.h"
#include "config.h"
#include "settings.h"
#include "fan_control.h"
#include "wifi_manager.h"
#include "mqtt_ha.h"
#include "system.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <ArduinoJson.h>

namespace {

AsyncWebServer server(80);
DNSServer dns;
bool dnsRunning = false;
bool updateAuthorized = false;
bool fsReady = false;

// Shown when the LittleFS image (data/index.html.gz) has not been uploaded yet.
// Keeps the firmware update working, so the device can always be recovered from the browser.
const char FALLBACK_PAGE[] = R"HTML(<!doctype html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Camper MaxxFan</title>
<style>body{font-family:system-ui,sans-serif;background:#0f1419;color:#e8f0f8;max-width:520px;margin:40px auto;padding:0 16px}
code{background:#1e2d3d;padding:2px 6px;border-radius:4px}button{padding:10px 16px;margin-top:10px}</style></head><body>
<h2>Camper MaxxFan</h2><p>Die Web-Oberfl&auml;che fehlt. Bitte das LittleFS-Image hochladen
(<code>data/index.html.gz</code>, Arduino IDE: <em>Upload LittleFS</em>).</p>
<p>Firmware-Update (.bin):</p><form method="POST" action="/api/update" enctype="multipart/form-data">
<input type="file" name="fw" accept=".bin"><br><button>Hochladen</button></form></body></html>)HTML";

bool auth(AsyncWebServerRequest* r) {
  if (settings.webUser.isEmpty()) return true;
  if (r->authenticate(settings.webUser.c_str(), settings.webPass.c_str())) return true;
  r->requestAuthentication();
  return false;
}

String param(AsyncWebServerRequest* r, const char* name, const String& def = "") {
  if (r->hasParam(name, true)) return r->getParam(name, true)->value();
  if (r->hasParam(name))       return r->getParam(name)->value();
  return def;
}

void sendJson(AsyncWebServerRequest* r, JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  r->send(code, "application/json", out);
}

void sendOk(AsyncWebServerRequest* r, bool ok = true) {
  r->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void handleState(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  JsonDocument d;
  d["fw"]       = FW_VERSION;
  d["expander"] = fan::expanderOk();
  d["sync"]     = fan::syncRunning();
  d["syncPort"] = settings.syncPort;
  JsonArray ports = d["ports"].to<JsonArray>();
  for (uint8_t p = 0; p < 2; p++) {
    JsonObject o = ports.add<JsonObject>();
    o["label"]   = PORT_LABELS[p];
    o["enabled"] = settings.portEnabled[p];
    o["hold"]    = fan::autoHold(p);
    o["led"]     = fan::led(p);
  }
  d["sta"]    = wifimgr::staConnected();
  d["ap"]     = wifimgr::apActive();
  d["ip"]     = wifimgr::ipString();
  d["rssi"]   = wifimgr::rssi();
  d["mqtt"]   = mqttha::connected();
  d["uptime"] = millis() / 1000;
  sendJson(r, d);
}

void handleGetConfig(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  JsonDocument d;
  d["hostname"] = settings.hostname;
  d["ssid"]     = settings.wifiSsid;
  d["static_ip"] = settings.staticIp;
  d["ip"]       = settings.ip;
  d["gw"]       = settings.gateway;
  d["sn"]       = settings.subnet;
  d["dns"]      = settings.dns;
  d["mhost"]    = settings.mqttHost;
  d["mport"]    = settings.mqttPort;
  d["muser"]    = settings.mqttUser;
  d["prefix"]   = settings.mqttPrefix;
  d["disc"]     = settings.discovery;
  d["wuser"]    = settings.webUser;
  d["p0"]       = settings.portEnabled[0];
  d["p1"]       = settings.portEnabled[1];
  d["pulse"]    = settings.pulseMs;
  d["syncport"] = settings.syncPort;
  d["apssid"]   = wifimgr::apSsid();
  // Passwords are never sent back; empty field in the form = keep
  sendJson(r, d);
}

void handleSetConfig(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  auto b = [&](const char* n) { return param(r, n) == "1" || param(r, n) == "on" || param(r, n) == "true"; };
  auto keepIfEmpty = [&](const char* n, String& target) { String v = param(r, n); if (v.length()) target = v; };

  String host = param(r, "hostname"); host.trim();
  if (host.length()) settings.hostname = host;

  settings.wifiSsid = param(r, "ssid"); settings.wifiSsid.trim();
  keepIfEmpty("wpass", settings.wifiPass);
  settings.staticIp = b("static_ip");
  settings.ip      = param(r, "ip");
  settings.gateway = param(r, "gw");
  settings.subnet  = param(r, "sn");
  settings.dns     = param(r, "dns");

  String ap = param(r, "appass");
  if (ap.length() >= 8) settings.apPass = ap;

  settings.mqttHost = param(r, "mhost"); settings.mqttHost.trim();
  settings.mqttPort = param(r, "mport", String(DEFAULT_MQTT_PORT)).toInt();
  if (settings.mqttPort == 0) settings.mqttPort = DEFAULT_MQTT_PORT;
  settings.mqttUser = param(r, "muser");
  keepIfEmpty("mpass", settings.mqttPass);
  if (settings.mqttUser.isEmpty()) settings.mqttPass = "";
  String prefix = param(r, "prefix"); prefix.trim();
  if (prefix.length()) settings.mqttPrefix = prefix;
  settings.discovery = b("disc");

  settings.webUser = param(r, "wuser"); settings.webUser.trim();
  keepIfEmpty("wpw", settings.webPass);
  if (settings.webUser.isEmpty()) settings.webPass = "";

  settings.portEnabled[0] = b("p0");
  settings.portEnabled[1] = b("p1");
  int pulse = param(r, "pulse", String(DEFAULT_PULSE_MS)).toInt();
  settings.pulseMs = constrain(pulse, 50, 2000);
  settings.syncPort = param(r, "syncport", "1").toInt() == 0 ? 0 : 1;

  settingsSave();
  sendOk(r);
  requestReboot(1000);
}

void handlePress(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  int port = param(r, "port", "-1").toInt();
  FanButton btn = fan::buttonFromKey(param(r, "btn"));
  sendOk(r, port >= 0 && port <= 1 && btn != BTN_COUNT && fan::requestPress(port, btn));
}

void handleSpeed(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  int port = param(r, "port", "-1").toInt();
  sendOk(r, port >= 0 && port <= 1 && fan::requestSpeed(port, param(r, "level", "0").toInt()));
}

void handleLed(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  int port = param(r, "port", "-1").toInt();
  sendOk(r, port >= 0 && port <= 1 && fan::requestLed(port, param(r, "on") == "1"));
}

void handleUpdateDone(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  bool ok = updateAuthorized && !Update.hasError();
  r->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Update failed");
  if (ok) requestReboot(1000);
}

void handleUpdateChunk(AsyncWebServerRequest* r, const String& filename, size_t index,
                       uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    updateAuthorized = settings.webUser.isEmpty() ||
                       r->authenticate(settings.webUser.c_str(), settings.webPass.c_str());
    if (!updateAuthorized) return;
    Serial.printf("[ota] web update: %s\n", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
  }
  if (!updateAuthorized) return;
  if (len && Update.write(data, len) != len) Update.printError(Serial);
  if (final) {
    if (Update.end(true)) Serial.printf("[ota] success, %u bytes\n", (unsigned)(index + len));
    else Update.printError(Serial);
  }
}

} // namespace

namespace web {

void begin() {
  fsReady = LittleFS.begin(true);   // formats automatically if the mount fails
  if (!fsReady) Serial.println("[web] LittleFS mount failed");

  // Serve ONLY the web page from the filesystem (stored gzip compressed)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    if (fsReady && LittleFS.exists("/index.html.gz")) {
      AsyncWebServerResponse* resp = r->beginResponse(LittleFS, "/index.html.gz", "text/html");
      resp->addHeader("Content-Encoding", "gzip");
      resp->addHeader("Cache-Control", "no-cache");
      r->send(resp);
    } else if (fsReady && LittleFS.exists("/index.html")) {
      r->send(LittleFS, "/index.html", "text/html");
    } else {
      r->send(200, "text/html", FALLBACK_PAGE);
    }
  });

  server.on("/api/state",  HTTP_GET,  handleState);
  server.on("/api/config", HTTP_GET,  handleGetConfig);
  server.on("/api/config", HTTP_POST, handleSetConfig);
  server.on("/api/press",  HTTP_POST, handlePress);
  server.on("/api/led",    HTTP_POST, handleLed);
  server.on("/api/speed",  HTTP_POST, handleSpeed);
  server.on("/api/sync",   HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    sendOk(r, fan::requestSyncOff());
  });
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    sendOk(r);
    requestReboot();
  });
  server.on("/api/factory", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    settingsFactoryReset();
    sendOk(r);
    requestReboot();
  });
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateChunk);

  // Captive portal: redirect everything to the UI while the setup AP is up
  server.onNotFound([](AsyncWebServerRequest* r) {
    if (wifimgr::apActive()) r->redirect("http://" + WiFi.softAPIP().toString() + "/");
    else r->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("[web] server started");
}

void loop() {
  if (wifimgr::apActive() && !dnsRunning) {
    dns.start(53, "*", WiFi.softAPIP());
    dnsRunning = true;
  } else if (!wifimgr::apActive() && dnsRunning) {
    dns.stop();
    dnsRunning = false;
  }
  if (dnsRunning) dns.processNextRequest();
}

} // namespace web
