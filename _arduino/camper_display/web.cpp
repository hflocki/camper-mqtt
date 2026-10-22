#include "web.h"
#include "config.h"
#include "settings.h"
#include "datastore.h"
#include "mqtt_client.h"
#include "wifi_manager.h"
#include "display_hw.h"
#include "ui.h"
#include "web_ui.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <vector>

namespace {

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ---- cross-task queue (async web task -> Arduino loop) -------------------
struct Cmd {
  char c[8];       // heat / fan / lvl / tile / finder / hello
  char k[24];
  char v[160];
  int  i;
  uint32_t client;
};
QueueHandle_t queue = nullptr;

bool    reloadPages = false;   // pages/tiles changed via API
bool    reloadPrefix = false;  // prefixes / display settings changed
volatile uint32_t rebootAt = 0;
uint32_t lastPush = 0;
bool     cfgDirty = false;

const char* MASK = "********";

bool auth(AsyncWebServerRequest* r) {
  if (settings.webPass.isEmpty()) return true;
  if (r->authenticate(settings.webUser.c_str(), settings.webPass.c_str())) return true;
  r->requestAuthentication();
  return false;
}

void sendJson(AsyncWebServerRequest* r, JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  r->send(code, "application/json", out);
}

void sendResult(AsyncWebServerRequest* r, bool ok, const String& msg = "", bool reboot = false) {
  JsonDocument d;
  d["ok"] = ok;
  if (msg.length()) d["msg"] = msg;
  if (reboot) d["reboot"] = true;
  sendJson(r, d, ok ? 200 : 400);
}

void enqueue(const char* c, const char* k, const char* v, int i, uint32_t client) {
  Cmd cmd{};
  strlcpy(cmd.c, c, sizeof(cmd.c));
  strlcpy(cmd.k, k ? k : "", sizeof(cmd.k));
  strlcpy(cmd.v, v ? v : "", sizeof(cmd.v));
  cmd.i = i;
  cmd.client = client;
  xQueueSend(queue, &cmd, 0);
}

// ---- WebSocket -----------------------------------------------------------
void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    enqueue("hello", "", "", 0, client->id());
    return;
  }
  if (type != WS_EVT_DATA) return;
  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;

  JsonDocument doc;
  if (deserializeJson(doc, data, len)) return;
  const char* c = doc["c"] | "";
  if (!strcmp(c, "finder")) {
    enqueue("finder", (doc["on"] | false) ? "on" : "off", doc["f"] | "", 0, client->id());
    return;
  }
  String v = doc["v"].is<const char*>() ? String(doc["v"].as<const char*>())
           : doc["v"].isNull() ? String("") : doc["v"].as<String>();
  enqueue(c, doc["k"] | "", v.c_str(), doc["i"] | -1, client->id());
}

void buildCfg(JsonDocument& d) {
  d["t"] = "cfg";
  d["fw"] = FW_VERSION;
  d["host"] = settings.hostname;
  d["ap"] = wifimgr::apActive() && !wifimgr::staConnected();
  d["setup"] = settings.wifiSsid.isEmpty() || settings.mqttHost.isEmpty();
  JsonArray b = d["builtin"].to<JsonArray>();
  for (int i = 0; i < BP_COUNT; i++) b.add(settings.builtin[i]);
  JsonArray p = d["pages"].to<JsonArray>();
  for (auto& pg : settings.pages) p.add(pg.name);
  JsonArray ta = d["tiles"].to<JsonArray>();
  for (auto& t : settings.tiles) {
    JsonObject o = ta.add<JsonObject>();
    o["n"] = t.name;
    o["p"] = t.page;
    o["y"] = t.type;
    o["u"] = t.unit;
    if (t.type == TILE_SWITCH) o["son"] = t.stateOn;
    if (t.type == TILE_SLIDER) { o["min"] = t.min; o["max"] = t.max; o["step"] = t.step; }
  }
}

void buildSys(JsonObject s) {
  s["wifi"] = wifimgr::staConnected();
  s["rssi"] = wifimgr::rssi();
  s["mqtt"] = mqttc::connected();
  s["ip"] = wifimgr::ipString();
  s["bri"] = settings.brightness;
  s["finder"] = mqttc::finderActive();
}

void sendTo(uint32_t client, JsonDocument& d) {
  String out;
  serializeJson(d, out);
  if (client) ws.text(client, out);
  else        ws.textAll(out);
}

void sendFull(uint32_t client) {
  JsonDocument cfgDoc;
  buildCfg(cfgDoc);
  sendTo(client, cfgDoc);

  JsonDocument v;
  v["t"] = "v";
  store::fillJson(v["d"].to<JsonObject>(), false);
  buildSys(v["s"].to<JsonObject>());
  sendTo(client, v);

  if (mqttc::finderActive()) {
    JsonDocument f;
    f["t"] = "f";
    f["filter"] = mqttc::finderFilter();
    f["on"] = true;
    JsonArray a = f["l"].to<JsonArray>();
    for (auto& e : mqttc::finderAll()) { JsonArray x = a.add<JsonArray>(); x.add(e.topic); x.add(e.payload); }
    sendTo(client, f);
  }
}

void handleCmd(const Cmd& c) {
  String k = c.k, v = c.v;
  bool ok = true;
  if (!strcmp(c.c, "hello"))       { sendFull(c.client); return; }
  else if (!strcmp(c.c, "heat"))   ok = cmd::heater(k, v);
  else if (!strcmp(c.c, "fan"))    ok = cmd::fan(k, v);
  else if (!strcmp(c.c, "lvl"))    ok = cmd::level(k);
  else if (!strcmp(c.c, "tile"))   ok = c.i >= 0 && cmd::tile((size_t)c.i, v);
  else if (!strcmp(c.c, "bri")) {
    disp::setBrightness(v.toInt());
    disp::wake();
    cfg::save();
    mqttc::publishSelfState();
  } else if (!strcmp(c.c, "finder")) {
    if (k == "on") mqttc::finderStart(v); else mqttc::finderStop();
    JsonDocument f;
    f["t"] = "f";
    f["filter"] = mqttc::finderFilter();
    f["on"] = mqttc::finderActive();
    f["l"].to<JsonArray>();
    sendTo(0, f);
    return;
  } else ok = false;

  if (!ok) {
    JsonDocument e;
    e["t"] = "err";
    e["msg"] = mqttc::connected() ? "Befehl abgelehnt" : "Keine MQTT-Verbindung";
    sendTo(c.client, e);
  }
}

// ---- settings API ----------------------------------------------------------
void getSettings(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  JsonDocument d;
  d["hostname"] = settings.hostname;
  d["apPass"] = MASK;
  d["webUser"] = settings.webUser;
  d["webPass"] = settings.webPass.isEmpty() ? "" : MASK;
  d["wifiSsid"] = settings.wifiSsid;
  d["wifiPass"] = settings.wifiPass.isEmpty() ? "" : MASK;
  d["staticIp"] = settings.staticIp;
  d["ip"] = settings.ip;
  d["gateway"] = settings.gateway;
  d["subnet"] = settings.subnet;
  d["dns"] = settings.dns;
  d["mqttHost"] = settings.mqttHost;
  d["mqttPort"] = settings.mqttPort;
  d["mqttUser"] = settings.mqttUser;
  d["mqttPass"] = settings.mqttPass.isEmpty() ? "" : MASK;
  d["prefixSelf"] = settings.prefixSelf;
  d["prefixTank"] = settings.prefixTank;
  d["prefixLevel"] = settings.prefixLevel;
  d["prefixHeater"] = settings.prefixHeater;
  d["prefixFan"] = settings.prefixFan;
  d["fanPort"] = settings.fanPort;
  d["ntpServer"] = settings.ntpServer;
  d["tz"] = settings.tz;
  d["brightness"] = settings.brightness;
  d["dimBrightness"] = settings.dimBrightness;
  d["screenTimeout"] = settings.screenTimeout;
  JsonArray b = d["builtin"].to<JsonArray>();
  for (int i = 0; i < BP_COUNT; i++) b.add(settings.builtin[i]);
  d["fw"] = FW_VERSION;
  d["mac"] = WiFi.macAddress();
  sendJson(r, d);
}

String cleanPrefix(String p) {
  p.trim();
  while (p.endsWith("/")) p.remove(p.length() - 1);
  return p;
}

// Settings are applied in the Arduino loop (applyPending), never in the web
// server task, so the loop never sees a String being replaced under its feet.
// The same function runs once as a dry run to tell the browser about a reboot.
bool applySettings(JsonObject o, bool apply) {
  bool reboot = false;

  auto str = [&](const char* key, String& target, bool needsReboot, bool secret = false) {
    if (!o[key].is<const char*>()) return;
    String v = o[key].as<const char*>();
    v.trim();
    if (secret && v == MASK) return;          // unchanged
    if (v != target) { if (apply) target = v; if (needsReboot) reboot = true; }
  };

  str("hostname", settings.hostname, true);
  str("webUser", settings.webUser, false);
  str("webPass", settings.webPass, false, true);
  str("wifiSsid", settings.wifiSsid, true);
  str("wifiPass", settings.wifiPass, true, true);
  if (o["staticIp"].is<bool>() && o["staticIp"].as<bool>() != settings.staticIp) {
    if (apply) settings.staticIp = o["staticIp"];
    reboot = true;
  }
  str("ip", settings.ip, true);
  str("gateway", settings.gateway, true);
  str("subnet", settings.subnet, true);
  str("dns", settings.dns, true);
  str("mqttHost", settings.mqttHost, true);
  if (o["mqttPort"].is<int>() && o["mqttPort"].as<int>() != settings.mqttPort) {
    if (apply) settings.mqttPort = o["mqttPort"];
    reboot = true;
  }
  str("mqttUser", settings.mqttUser, true);
  str("mqttPass", settings.mqttPass, true, true);
  str("ntpServer", settings.ntpServer, true);
  str("tz", settings.tz, true);
  str("prefixSelf", settings.prefixSelf, true);
  if (!apply) return reboot;

  if (o["apPass"].is<const char*>()) {
    String ap = o["apPass"].as<const char*>();
    if (ap != MASK && ap.length() >= 8) settings.apPass = ap;
  }
  str("prefixTank", settings.prefixTank, false);
  str("prefixLevel", settings.prefixLevel, false);
  str("prefixHeater", settings.prefixHeater, false);
  str("prefixFan", settings.prefixFan, false);
  settings.prefixSelf = cleanPrefix(settings.prefixSelf);
  if (settings.prefixSelf.isEmpty()) settings.prefixSelf = DEFAULT_PREFIX_SELF;
  settings.prefixTank = cleanPrefix(settings.prefixTank);
  settings.prefixLevel = cleanPrefix(settings.prefixLevel);
  settings.prefixHeater = cleanPrefix(settings.prefixHeater);
  settings.prefixFan = cleanPrefix(settings.prefixFan);
  if (o["fanPort"].is<int>()) settings.fanPort = constrain(o["fanPort"].as<int>(), 0, 1);

  if (o["brightness"].is<int>())    settings.brightness = constrain(o["brightness"].as<int>(), 5, 100);
  if (o["dimBrightness"].is<int>()) settings.dimBrightness = constrain(o["dimBrightness"].as<int>(), 0, 100);
  if (o["screenTimeout"].is<int>()) settings.screenTimeout = constrain(o["screenTimeout"].as<int>(), 0, 3600);
  if (o["builtin"].is<JsonArray>()) {
    JsonArray b = o["builtin"];
    for (int i = 0; i < BP_COUNT && i < (int)b.size(); i++) settings.builtin[i] = b[i].as<bool>();
  }
  if (settings.hostname.isEmpty()) settings.hostname = DEFAULT_HOSTNAME;
  return reboot;
}

// hand-over buffers web task -> loop
portMUX_TYPE pendingMux = portMUX_INITIALIZER_UNLOCKED;
String pendingSettings, pendingPages;

void setPending(String& slot, const String& json) {
  portENTER_CRITICAL(&pendingMux);
  slot = json;
  portEXIT_CRITICAL(&pendingMux);
}

String takePending(String& slot) {
  String out;
  portENTER_CRITICAL(&pendingMux);
  out = slot;
  slot = "";
  portEXIT_CRITICAL(&pendingMux);
  return out;
}

void postSettings(AsyncWebServerRequest* r, JsonVariant& j) {
  if (!auth(r)) return;
  if (!j.is<JsonObject>()) { sendResult(r, false, "Ungültige Daten"); return; }
  bool reboot = applySettings(j.as<JsonObject>(), false);
  String json;
  serializeJson(j, json);
  setPending(pendingSettings, json);
  sendResult(r, true, reboot ? "Gespeichert, Neustart ..." : "Gespeichert", reboot);
}

void getPages(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  r->send(200, "application/json", cfg::pagesToJson());
}

void postPages(AsyncWebServerRequest* r, JsonVariant& j) {
  if (!auth(r)) return;
  String json;
  serializeJson(j, json);
  String err;
  if (!cfg::pagesFromJson(json, err, false)) { sendResult(r, false, err); return; }
  setPending(pendingPages, json);
  sendResult(r, true, "Seiten gespeichert");
}

void applyPending() {
  String s = takePending(pendingSettings);
  if (s.length()) {
    JsonDocument d;
    if (!deserializeJson(d, s)) {
      bool reboot = applySettings(d.as<JsonObject>(), true);
      cfg::save();
      reloadPrefix = true;
      if (reboot) rebootAt = millis() + 1500;
    }
  }
  String p = takePending(pendingPages);
  if (p.length()) {
    String err;
    if (cfg::pagesFromJson(p, err, true)) {
      cfg::savePages();
      reloadPages = true;
    }
  }
}

void getScan(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); n = WIFI_SCAN_RUNNING; }
  JsonDocument d;
  d["running"] = (n == WIFI_SCAN_RUNNING);
  JsonArray a = d["nets"].to<JsonArray>();
  if (n >= 0) {
    for (int i = 0; i < n; i++) {
      JsonObject x = a.add<JsonObject>();
      x["ssid"] = WiFi.SSID(i);
      x["rssi"] = WiFi.RSSI(i);
    }
    WiFi.scanDelete();
  }
  sendJson(r, d);
}

// ---- OTA upload ----------------------------------------------------------------
void otaUpload(AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (!settings.webPass.isEmpty() && !r->authenticate(settings.webUser.c_str(), settings.webPass.c_str())) return;
  if (index == 0) {
    Serial.printf("[ota] upload %s\n", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  }
  if (Update.isRunning() && Update.write(data, len) != len) Update.printError(Serial);
  if (final) {
    if (Update.end(true)) Serial.printf("[ota] done, %u bytes\n", (unsigned)(index + len));
    else Update.printError(Serial);
  }
}

void otaDone(AsyncWebServerRequest* r) {
  if (!auth(r)) return;
  bool ok = !Update.hasError();
  sendResult(r, ok, ok ? "Update ok, Neustart ..." : "Update fehlgeschlagen", ok);
  if (ok) rebootAt = millis() + 1500;
}

} // namespace

namespace web {

void begin() {
  queue = xQueueCreate(24, sizeof(Cmd));

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* res = r->beginResponse(200, "text/html", (const uint8_t*)WEB_UI, strlen_P(WEB_UI));
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
  });
  server.on("/api/settings", HTTP_GET, getSettings);
  server.on("/api/pages", HTTP_GET, getPages);
  server.on("/api/scan", HTTP_GET, getScan);
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    sendResult(r, true, "Neustart ...", true);
    rebootAt = millis() + 1000;
  });
  server.on("/api/factory", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    cfg::factoryReset();
    sendResult(r, true, "Werkseinstellungen, Neustart ...", true);
    rebootAt = millis() + 1000;
  });
  server.on("/update", HTTP_POST, otaDone, otaUpload);

  auto* hs = new AsyncCallbackJsonWebHandler("/api/settings", postSettings);
  hs->setMethod(HTTP_POST);
  hs->setMaxContentLength(8192);
  server.addHandler(hs);
  auto* hp = new AsyncCallbackJsonWebHandler("/api/pages", postPages);
  hp->setMethod(HTTP_POST);
  hp->setMaxContentLength(24576);
  server.addHandler(hp);

  // captive portal: unknown URLs lead to the page while the setup AP is open
  server.onNotFound([](AsyncWebServerRequest* r) {
    if (wifimgr::apActive()) r->redirect("http://" + WiFi.softAPIP().toString() + "/");
    else r->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("[web] server started");
}

void configChanged() { cfgDirty = true; }

void loop() {
  Cmd c;
  while (queue && xQueueReceive(queue, &c, 0) == pdTRUE) handleCmd(c);
  applyPending();

  if (reloadPrefix) {
    reloadPrefix = false;
    disp::setBrightness(settings.brightness);
    store::rebuildTopics();
    mqttc::resubscribe();
    ui::requestRebuild();
    cfgDirty = true;
  }
  if (reloadPages) {
    reloadPages = false;
    store::rebuildTopics();
    mqttc::resubscribe();
    ui::requestRebuild();
    cfgDirty = true;
  }
  if (rebootAt && (int32_t)(millis() - rebootAt) > 0) {
    ws.closeAll();
    delay(100);
    ESP.restart();
  }

  uint32_t now = millis();
  if (now - lastPush < 250) return;
  lastPush = now;
  ws.cleanupClients(6);

  if (ws.count() == 0) {
    store::clearDirty(store::C_WEB);
    mqttc::finderTake();
    cfgDirty = false;
    return;
  }
  if (cfgDirty) {
    cfgDirty = false;
    sendFull(0);
    store::clearDirty(store::C_WEB);
    return;
  }
  if (!ws.availableForWriteAll()) return;

  static uint32_t lastSys = 0;
  bool d, t;
  bool dirty = store::takeDirty(store::C_WEB, d, t);
  bool sysDue = now - lastSys > 2000;
  if (dirty || sysDue) {
    JsonDocument v;
    v["t"] = "v";
    store::fillJson(v["d"].to<JsonObject>(), true);
    if (sysDue) { buildSys(v["s"].to<JsonObject>()); lastSys = now; }
    sendTo(0, v);
    store::clearDirty(store::C_WEB);
  }

  auto found = mqttc::finderTake();
  if (!found.empty()) {
    JsonDocument f;
    f["t"] = "f";
    f["filter"] = mqttc::finderFilter();
    f["on"] = true;
    f["add"] = true;
    JsonArray a = f["l"].to<JsonArray>();
    for (auto& e : found) { JsonArray x = a.add<JsonArray>(); x.add(e.topic); x.add(e.payload); }
    sendTo(0, f);
  }
}

} // namespace web
