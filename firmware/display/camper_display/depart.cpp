#include "depart.h"
#include "settings.h"
#include "sensors.h"
#include "alerts.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

const uint32_t GPS_STALE_MS = 3UL * 60UL * 1000UL;   // no GPS message for this long: not driving
const uint32_t STOP_MS      = 2UL * 60UL * 1000UL;   // standing this long ends a trip (traffic lights, jams)

SemaphoreHandle_t mtx = nullptr;
struct Lock {
  Lock() {
    if (!mtx) mtx = xSemaphoreCreateMutex();   // first use is in the loop task, before the web server asks
    xSemaphoreTake(mtx, portMAX_DELAY);
  }
  ~Lock() { xSemaphoreGive(mtx); }
};

float    spd = NAN;          // km/h, NAN = no fix
uint32_t gpsAt = 0;          // millis() of the last GPS message, 0 = never
uint32_t aboveSince = 0;     // start of the current run above the limit, 0 = not above
uint32_t belowSince = 0;     // start of the current run at or below the limit while driving
bool     drv = false;
int8_t   doorSt = -1;
bool     dirty = true;
String   lastIssues = "\x01";

float numOf(JsonVariantConst v) {
  if (v.is<float>() || v.is<int>()) return v.as<float>();
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    char* end;
    float f = strtof(s, &end);
    if (end != s) return f;
  }
  return NAN;
}

// speed and fix_status anywhere in the (nested) JSON; fix = -1 if the key is missing
void scanGps(JsonVariantConst v, float& speed, int& fix, int depth) {
  if (depth > 4 || !v.is<JsonObjectConst>()) return;
  for (JsonPairConst kv : v.as<JsonObjectConst>()) {
    String k = kv.key().c_str();
    k.toLowerCase();
    JsonVariantConst val = kv.value();
    if (val.is<JsonObjectConst>()) { scanGps(val, speed, fix, depth + 1); continue; }
    if (k == "speed" || k == "spd" || k == "speed_kmh") { float f = numOf(val); if (!isnan(f)) speed = f; }
    else if (k == "fix_status" || k == "fix" || k == "fixstatus") { float f = numOf(val); if (!isnan(f)) fix = (int)f; }
  }
}

// "open" / "closed" ... -> 1 / 0, anything else -1
int8_t stateOf(String s) {
  s.trim();
  s.toLowerCase();
  if (s == "open" || s == "opened" || s == "on" || s == "1" || s == "true" || s == "offen") return 1;
  if (s == "close" || s == "closed" || s == "off" || s == "0" || s == "false" || s == "zu") return 0;
  return -1;
}

// first "state" / "value" / "door" / "contact" string or number in the (nested) JSON
bool findState(JsonVariantConst v, String& out, int depth) {
  if (depth > 4 || !v.is<JsonObjectConst>()) return false;
  for (JsonPairConst kv : v.as<JsonObjectConst>()) {
    String k = kv.key().c_str();
    k.toLowerCase();
    JsonVariantConst val = kv.value();
    if (val.is<JsonObjectConst>()) { if (findState(val, out, depth + 1)) return true; continue; }
    if (k == "state" || k == "value" || k == "door" || k == "contact" || k == "val") {
      if (val.is<const char*>()) { out = val.as<const char*>(); return true; }
      if (val.is<bool>()) { out = val.as<bool>() ? "true" : "false"; return true; }
      if (val.is<int>()) { out = String(val.as<int>()); return true; }
    }
  }
  return false;
}

bool fresh(uint32_t now) { return gpsAt && now - gpsAt < GPS_STALE_MS; }

}  // namespace

namespace depart {

bool onMessage(const char* topic, const char* payload, unsigned int len) {
  if (!settings.dpOn) return false;
  bool isGps = settings.dpTopic.length() && settings.dpTopic == topic;
  bool isDoor = settings.dpDoor.length() && settings.dpDoor == topic;
  if (!isGps && !isDoor) return false;

  JsonDocument doc;
  bool json = !deserializeJson(doc, payload, len) && doc.is<JsonObject>();
  if (isDoor) {
    String s;
    if (json) findState(doc.as<JsonVariantConst>(), s, 0);
    else for (unsigned i = 0; i < len; i++) s += payload[i];
    int8_t st = stateOf(s);
    if (st >= 0 && settings.dpDoorInv) st = 1 - st;
    Lock lock;
    if (st != doorSt) {
      Serial.printf("[depart] door %s\n", st == 1 ? "open" : st == 0 ? "closed" : "unknown");
      doorSt = st;
      dirty = true;
    }
  }
  if (isGps && json) {
    float s = NAN;
    int fix = -1;
    scanGps(doc.as<JsonVariantConst>(), s, fix, 0);
    if (fix == 0) s = NAN;   // the router reports a speed of 0 without a fix, it means nothing
    Lock lock;
    bool first = !gpsAt;
    spd = s;
    gpsAt = millis();
    if (gpsAt == 0) gpsAt = 1;
    if (first) Serial.printf("[depart] GPS from %s: speed %.1f, fix %d\n", topic, s, fix);
  }
  // the GPS topic may also carry the position or the mobile signal: let the others see it too
  return isDoor && !isGps;
}

std::vector<String> topics() {
  std::vector<String> t;
  if (!settings.dpOn) return t;
  if (settings.dpTopic.length()) t.push_back(settings.dpTopic);
  if (settings.dpDoor.length() && settings.dpDoor != settings.dpTopic) t.push_back(settings.dpDoor);
  return t;
}

void loop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 500) return;
  last = now;
  Lock lock;
  bool was = drv;
  if (!settings.dpOn || !fresh(now)) {
    drv = false;
    aboveSince = belowSince = 0;
  } else {
    bool above = !isnan(spd) && spd > settings.dpSpeed;
    if (above) {
      belowSince = 0;
      if (!aboveSince) aboveSince = now;
      if (!drv && now - aboveSince >= (uint32_t)settings.dpSecs * 1000UL) drv = true;
    } else {
      aboveSince = 0;
      if (drv) {
        if (!belowSince) belowSince = now;
        if (now - belowSince >= STOP_MS) { drv = false; belowSince = 0; }
      }
    }
  }
  if (drv != was) {
    Serial.printf("[depart] %s (%.1f km/h)\n", drv ? "driving" : "stopped", spd);
    dirty = true;
  }
}

bool driving() { Lock lock; return drv; }

float speed() {
  Lock lock;
  return fresh(millis()) ? spd : NAN;
}

bool gpsFresh() { Lock lock; return fresh(millis()); }

int8_t door() {
  Lock lock;
  return settings.dpDoor.length() ? doorSt : -1;
}

std::vector<Item> items() {
  std::vector<Item> out;
  if (!settings.dpOn) return out;
  if (settings.dpDoor.length()) {
    int8_t d = door();
    out.push_back({"T\xC3\xBCr", d == 1 ? "offen" : d == 0 ? "zu" : "unbekannt", (int8_t)(d < 0 ? -1 : d == 0)});
  }
  int8_t a = alerts::awning();
  out.push_back({"Markise", a == 1 ? "ausgefahren" : a == 0 ? "eingefahren" : "unbekannt", (int8_t)(a < 0 ? -1 : a == 0)});
  if (settings.dpCont) {
    for (auto& c : sensors::contacts()) {
      if (c.name == settings.awnSrc) continue;   // already shown as the awning
      int8_t o = c.stale ? -1 : c.open;
      out.push_back({c.name, o == 1 ? "offen" : o == 0 ? "zu" : "unbekannt", (int8_t)(o < 0 ? -1 : o == 0)});
    }
  }
  return out;
}

String issues() {
  String s;
  for (auto& it : items()) {
    if (it.ok != 0) continue;
    if (s.length()) s += ", ";
    s += it.name + " " + it.state;
  }
  return s;
}

String warnings() {
  if (!driving()) return "";
  String w;
  // no ':' in the lines: each problem is its own warning for the push messages
  for (auto& it : items()) {
    if (it.ok != 0) continue;
    if (w.length()) w += "\n";
    w += it.name + " " + it.state + " w\xC3\xA4hrend der Fahrt";
  }
  return w;
}

bool changed() {
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck >= 1000) {   // the check list reads the sensors: not on every loop
    lastCheck = millis();
    String is = settings.dpOn ? issues() : String("");
    Lock lock;
    if (is != lastIssues) { lastIssues = is; dirty = true; }
  }
  Lock lock;
  bool d = dirty;
  dirty = false;
  return d;
}

}  // namespace depart
