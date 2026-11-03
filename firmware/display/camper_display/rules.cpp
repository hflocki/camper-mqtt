#include "rules.h"
#include "datastore.h"
#include "icons.h"
#include "mqtt_client.h"
#include "weather.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace rules {
namespace {

const size_t MAX_RULES = 16;
const char* const NS = "rules";

struct State {
  bool     known = false;     // a first value was received (no action on it)
  bool     active = false;    // condition true
  bool     owned = false;     // the output was switched on by this rule
  bool     outKnown = false;  // state of the output seen on its state topic
  bool     outOn = false;
  uint32_t offAt = 0;         // millis() of the pending off action, 0 = none
  String   last;              // last action for the web page, e.g. "an 19:42"
};

std::vector<Rule> list;
std::vector<State> st;

// the web server (own task) reads and replaces the rules, MQTT and loop() run in the main loop
SemaphoreHandle_t mtx = nullptr;
struct Lock {
  Lock() { if (!mtx) mtx = xSemaphoreCreateRecursiveMutex(); xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(mtx); }
};

String clockText() {
  time_t now = time(nullptr);
  if (now < 1700000000) return "";
  struct tm t;
  localtime_r(&now, &t);
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
  return b;
}

int minutesOf(const String& hhmm) {
  int c = hhmm.indexOf(':');
  if (c < 0) return -1;
  int h = hhmm.substring(0, c).toInt(), m = hhmm.substring(c + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

bool allowedNow(const Rule& r) {
  if (r.when == W_DARK) return wx::isDark();
  if (r.when != W_TIME) return true;
  time_t now = time(nullptr);
  if (now < 1700000000) return true;   // no clock yet: rather act than not
  struct tm t;
  localtime_r(&now, &t);
  int m = t.tm_hour * 60 + t.tm_min, a = minutesOf(r.from), b = minutesOf(r.to);
  if (a < 0 || b < 0 || a == b) return true;
  return a < b ? (m >= a && m < b) : (m >= a || m < b);   // b < a: over midnight
}

bool test(const Rule& r, const String& v) {
  switch (r.cond) {
    case C_NE: return !v.equalsIgnoreCase(r.inVal);
    case C_LT: return v.toFloat() < r.inVal.toFloat();
    case C_GT: return v.toFloat() > r.inVal.toFloat();
    default:   return v.equalsIgnoreCase(r.inVal);
  }
}

// State topic and "on" value of the output, derived from the command:
// zigbee2mqtt/x/set + {"state":"ON"} -> zigbee2mqtt/x, key "state", "ON"; plain payloads compare as a whole
String outStateTopic(const Rule& r) {
  if (r.outTopic.endsWith("/set")) return r.outTopic.substring(0, r.outTopic.length() - 4);
  return "";
}
void outOnValue(const Rule& r, String& key, String& val) {
  JsonDocument d;
  if (!deserializeJson(d, r.onPayload) && d.is<JsonObject>() && d["state"].is<const char*>()) {
    key = "state";
    val = d["state"].as<const char*>();
  } else {
    key = "";
    val = r.onPayload;
  }
}

void send(size_t i, bool on) {
  const Rule& r = list[i];
  const String& p = on ? r.onPayload : r.offPayload;
  if (r.outTopic.isEmpty() || p.isEmpty()) return;
  bool ok = mqttc::publishTo(r.outTopic, p);
  st[i].outOn = on;   // assume it worked; the state topic corrects this if the device reports back
  st[i].last = String(on ? "an" : "aus") + (clockText().length() ? " " + clockText() : String("")) + (ok ? "" : " (MQTT getrennt)");
  Serial.printf("[rule] %s: %s\n", r.name.c_str(), on ? "on" : "off");
}

}  // namespace

void begin() {
  Preferences p;
  p.begin(NS, true);
  size_t n = p.getBytesLength("json");
  String json;
  if (n) {
    std::unique_ptr<char[]> buf(new char[n + 1]);
    p.getBytes("json", buf.get(), n);
    buf[n] = 0;
    json = buf.get();
  }
  p.end();
  String err;
  if (json.length() && !fromJson(json, err, true)) Serial.printf("[rule] stored rules invalid: %s\n", err.c_str());
  Serial.printf("[rule] %u rules\n", (unsigned)list.size());
}

void save() {
  Lock lock;
  String json = toJson();
  Preferences p;
  p.begin(NS, false);
  p.putBytes("json", json.c_str(), json.length());
  p.end();
}

void clear() {
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
}

void loop() {
  Lock lock;
  uint32_t now = millis();
  for (size_t i = 0; i < list.size(); i++) {
    State& s = st[i];
    if (!s.offAt || (int32_t)(now - s.offAt) < 0) continue;
    s.offAt = 0;
    if (s.active || !list[i].on) continue;   // became true again meanwhile, or switched off
    send(i, false);
    s.owned = false;
  }
}

bool onMessage(const char* topic, const char* payload, unsigned int len) {
  Lock lock;
  bool used = false;
  for (size_t i = 0; i < list.size(); i++) {
    const Rule& r = list[i];
    State& s = st[i];
    // state of the output (manual switching is noticed here)
    String ot = outStateTopic(r);
    if (ot.length() && ot == topic) {
      String key, onVal, v;
      outOnValue(r, key, onVal);
      if (store::extract(payload, len, key, v)) {
        s.outKnown = true;
        s.outOn = v.equalsIgnoreCase(onVal);
        if (!s.outOn) { s.owned = false; s.offAt = 0; }   // switched off by hand: nothing left to do
      }
      used = true;
    }
    if (r.inTopic != topic) continue;
    used = true;
    String v;
    if (!store::extract(payload, len, r.inKey, v)) continue;   // key not in this message
    bool t = test(r, v);
    if (!s.known) { s.known = true; s.active = t; continue; }  // first value (e.g. retained): no action
    if (t == s.active) continue;
    s.active = t;
    store::markAllDirty();   // display and web page redraw: status icon / warning changed
    if (!r.on) continue;
    if (t) {
      s.offAt = 0;
      if (!allowedNow(r)) { s.last = "nicht im Zeitraum"; continue; }
      if (r.onlyOwn && s.outKnown && s.outOn && !s.owned) { s.last = "war schon an"; continue; }
      send(i, true);
      s.owned = true;
    } else if (r.offPayload.length() && (!r.onlyOwn || s.owned)) {
      s.offAt = millis() + (uint32_t)r.offDelay * 1000UL;
      if (!s.offAt) s.offAt = 1;
    }
  }
  return used;
}

std::vector<String> topics() {
  Lock lock;
  std::vector<String> t;
  auto add = [&](const String& x) {
    if (x.isEmpty()) return;
    for (auto& y : t) if (y == x) return;
    t.push_back(x);
  };
  for (auto& r : list) {
    add(r.inTopic);
    add(outStateTopic(r));
  }
  return t;
}

String toJson() {
  Lock lock;
  JsonDocument doc;
  JsonArray a = doc["rules"].to<JsonArray>();
  for (auto& r : list) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = r.name;
    o["on"] = r.on;
    o["inTopic"] = r.inTopic;
    o["inKey"] = r.inKey;
    o["cond"] = r.cond;
    o["inVal"] = r.inVal;
    o["outTopic"] = r.outTopic;
    o["onPayload"] = r.onPayload;
    o["offPayload"] = r.offPayload;
    o["offDelay"] = r.offDelay;
    o["onlyOwn"] = r.onlyOwn;
    o["when"] = r.when;
    o["from"] = r.from;
    o["to"] = r.to;
    o["icon"] = r.icon;
    o["warn"] = r.warn;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool fromJson(const String& json, String& err, bool apply) {
  Lock lock;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { err = e.c_str(); return false; }
  std::vector<Rule> rs;
  for (JsonObjectConst o : doc["rules"].as<JsonArrayConst>()) {
    if (rs.size() >= MAX_RULES) { err = "Höchstens " + String(MAX_RULES) + " Regeln"; return false; }
    Rule r;
    r.name = o["name"] | "";
    r.on = o["on"] | true;
    r.inTopic = o["inTopic"] | "";
    r.inKey = o["inKey"] | "";
    r.cond = o["cond"] | 0;
    r.inVal = o["inVal"] | "";
    r.outTopic = o["outTopic"] | "";
    r.onPayload = o["onPayload"] | "";
    r.offPayload = o["offPayload"] | "";
    r.offDelay = constrain((int)(o["offDelay"] | 10), 0, 36000);
    r.onlyOwn = o["onlyOwn"] | true;
    r.when = o["when"] | 0;
    r.from = o["from"] | "19:00";
    r.to = o["to"] | "07:00";
    r.icon = o["icon"] | "";
    r.warn = o["warn"] | false;
    r.name.trim(); r.inTopic.trim(); r.inKey.trim(); r.inVal.trim(); r.outTopic.trim();
    if (r.name.isEmpty()) r.name = "Regel " + String(rs.size() + 1);
    if (r.cond > C_GT) r.cond = C_EQ;
    if (r.when > W_DARK) r.when = W_ALWAYS;
    if (!icons::known(r.icon)) r.icon = "";
    if (r.inTopic.isEmpty()) { err = r.name + ": Topic für „Wenn“ fehlt"; return false; }
    for (const String* t : {&r.inTopic, &r.outTopic})
      if (t->indexOf('#') >= 0 || t->indexOf('+') >= 0) { err = r.name + ": keine Platzhalter (#, +) in Topics"; return false; }
    if (r.when == W_TIME && (minutesOf(r.from) < 0 || minutesOf(r.to) < 0)) { err = r.name + ": Uhrzeit als HH:MM"; return false; }
    rs.push_back(r);
  }
  if (apply) {
    // keep the live state of rules whose trigger did not change, so saving does not lose "door is open"
    std::vector<State> ns(rs.size());
    for (size_t i = 0; i < rs.size(); i++)
      for (size_t j = 0; j < list.size(); j++)
        if (list[j].inTopic == rs[i].inTopic && list[j].inKey == rs[i].inKey && list[j].outTopic == rs[i].outTopic) {
          ns[i] = st[j];
          ns[i].active = ns[i].known ? ns[i].active : false;
          break;
        }
    list = rs;
    st = ns;
  }
  return true;
}

String statusJson() {
  Lock lock;
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  uint32_t now = millis();
  for (size_t i = 0; i < list.size(); i++) {
    const State& s = st[i];
    JsonObject o = a.add<JsonObject>();
    o["known"] = s.known;
    o["active"] = s.active;
    o["owned"] = s.owned;
    if (s.offAt) o["offIn"] = (int32_t)(s.offAt - now) > 0 ? (s.offAt - now + 999) / 1000 : 0;
    o["last"] = s.last;
    o["allowed"] = allowedNow(list[i]);
  }
  String out;
  serializeJson(doc, out);
  return out;
}

std::vector<String> activeIcons() {
  Lock lock;
  std::vector<String> out;
  for (size_t i = 0; i < list.size(); i++)
    if (list[i].on && st[i].active && list[i].icon.length()) out.push_back(list[i].icon);
  return out;
}

String warnings() {
  Lock lock;
  String w;
  for (size_t i = 0; i < list.size(); i++) {
    if (!list[i].on || !st[i].active || !list[i].warn) continue;
    if (w.length()) w += "\n";
    w += list[i].name;
  }
  return w;
}

}  // namespace rules
