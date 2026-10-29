#include "sensors.h"
#include "icons.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <map>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace sensors {
namespace {

const size_t MAX_DEVICES = 32;
const size_t MAX_UNKNOWN = 24;
const uint32_t STALE_MS = 2UL * 60 * 60 * 1000;   // no message for 2 h: value shown grey
const char* const NS = "sensors";

String zbBase = "camper/zigbee/data";
String bleBase = "camper/OMG/BTtoMQTT";
float tempDiv = 10;     // raw 0402/0000 / tempDiv = °C (ZCL standard would be 100)
float humDiv = 1;       // raw 0405/0000 / humDiv = %

std::vector<Device> devs;
std::vector<Value> vals;
std::map<String, uint32_t> unknown;   // "z:<id>" / "b:<mac>" -> millis() last seen
volatile bool dirty = true;

SemaphoreHandle_t mtx = nullptr;
struct Lock {
  Lock() { if (!mtx) mtx = xSemaphoreCreateRecursiveMutex(); xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(mtx); }
};

// "0x00158D000AF152C9", "00158d000af152c9", "9C68", "0x9c68" -> "00158d000af152c9" / "9c68"
String normZb(String a) {
  a.trim();
  a.toLowerCase();
  if (a.startsWith("0x")) a = a.substring(2);
  return a;
}
// "A4:C1:38:AE:F5:6D" / "a4c138aef56d" -> "A4C138AEF56D"
String normMac(String a) {
  a.trim();
  a.replace(":", "");
  a.replace("-", "");
  a.toUpperCase();
  return a;
}

int findZb(const String& id) {
  String n = normZb(id);
  for (size_t i = 0; i < devs.size(); i++)
    if (devs[i].bus == B_ZIGBEE && (normZb(devs[i].addr) == n || (devs[i].alt.length() && normZb(devs[i].alt) == n))) return i;
  return -1;
}
int findBle(const String& mac) {
  String n = normMac(mac);
  for (size_t i = 0; i < devs.size(); i++)
    if (devs[i].bus == B_BLE && normMac(devs[i].addr) == n) return i;
  return -1;
}

void seen(const String& key) {
  if (unknown.count(key) || unknown.size() < MAX_UNKNOWN) unknown[key] = millis();
}

// first number in a payload: "215", "21.5", {"value":215} ...
bool number(const char* p, unsigned len, float& out) {
  String s;
  s.reserve(len);
  for (unsigned i = 0; i < len; i++) s += p[i];
  JsonDocument d;
  if (!deserializeJson(d, s)) {
    if (d.is<float>()) { out = d.as<float>(); return true; }
    if (d.is<const char*>()) s = d.as<const char*>();
    else if (d.is<JsonObject>()) {
      for (JsonPair kv : d.as<JsonObject>())
        if (kv.value().is<float>()) { out = kv.value().as<float>(); return true; }
      return false;
    }
  }
  char* end;
  s.trim();
  out = strtof(s.c_str(), &end);
  return end != s.c_str();
}

void loadJson(const String& json) {
  String err;
  if (json.length() && !fromJson(json, err, true)) Serial.printf("[sens] stored list invalid: %s\n", err.c_str());
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
  loadJson(json);
  Serial.printf("[sens] %u devices\n", (unsigned)devs.size());
}

void save() {
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

std::vector<String> topics() {
  Lock lock;
  std::vector<String> t;
  if (zbBase.length()) t.push_back(zbBase + "/#");
  if (bleBase.length()) t.push_back(bleBase + "/+");
  return t;
}

bool onMessage(const char* topic, const char* payload, unsigned int len) {
  Lock lock;
  String t = topic;
  // Zigbee: <zbBase>/<id>/<endpoint>/<cluster>/<attribute>
  if (zbBase.length() && t.startsWith(zbBase + "/")) {
    String rest = t.substring(zbBase.length() + 1);
    int a = rest.indexOf('/');
    if (a <= 0) return true;
    String id = rest.substring(0, a);
    int b = rest.indexOf('/', a + 1), c = b < 0 ? -1 : rest.indexOf('/', b + 1);
    if (b < 0 || c < 0) return true;
    String cluster = rest.substring(b + 1, c), attr = rest.substring(c + 1);
    cluster.toLowerCase();
    attr.toLowerCase();
    int i = findZb(id);
    if (i < 0) { seen("z:" + normZb(id)); return true; }
    float v;
    if (!number(payload, len, v)) return true;
    Value& x = vals[i];
    if (cluster == "0006" && attr == "0000") {
      bool open = v != 0;
      if (devs[i].invert) open = !open;
      x.open = open ? 1 : 0;
    } else if (cluster == "0001" && attr == "0021") x.batt = v / 2;              // ZCL: 0..200 = 0..100 %
    else if (cluster == "0402" && attr == "0000") x.temp = v / (tempDiv > 0 ? tempDiv : 1);
    else if (cluster == "0405" && attr == "0000") x.hum = v / (humDiv > 0 ? humDiv : 1);
    else if (cluster == "0403" && attr == "0000") x.pres = v;
    else return true;
    x.at = millis();
    if (!x.at) x.at = 1;
    dirty = true;
    return true;
  }
  // BLE (OpenMQTTGateway): <bleBase>/<MAC>
  if (bleBase.length() && t.startsWith(bleBase + "/")) {
    String mac = t.substring(bleBase.length() + 1);
    if (mac.indexOf('/') >= 0) return false;
    int i = findBle(mac);
    if (i < 0) {
      // only remember devices that send climate values, not every phone and beacon nearby
      if (memmem(payload, len, "tempc", 5) || memmem(payload, len, "\"hum\"", 5)) seen("b:" + normMac(mac));
      return true;
    }
    JsonDocument d;
    if (deserializeJson(d, payload, len)) return true;
    Value& x = vals[i];
    if (d["tempc"].is<float>()) x.temp = d["tempc"].as<float>();
    else if (d["temp"].is<float>()) x.temp = d["temp"].as<float>();
    if (d["hum"].is<float>()) x.hum = d["hum"].as<float>();
    if (d["pres"].is<float>()) x.pres = d["pres"].as<float>();
    if (d["batt"].is<float>()) x.batt = d["batt"].as<float>();
    x.at = millis();
    if (!x.at) x.at = 1;
    dirty = true;
    return true;
  }
  return false;
}

String toJson() {
  Lock lock;
  JsonDocument doc;
  doc["zbBase"] = zbBase;
  doc["bleBase"] = bleBase;
  doc["tempDiv"] = tempDiv;
  doc["humDiv"] = humDiv;
  JsonArray a = doc["devices"].to<JsonArray>();
  for (auto& d : devs) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = d.name;
    o["bus"] = d.bus;
    o["kind"] = d.kind;
    o["addr"] = d.addr;
    if (d.alt.length()) o["alt"] = d.alt;
    o["icon"] = d.icon;
    o["warn"] = d.warn;
    if (d.invert) o["invert"] = true;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool fromJson(const String& json, String& err, bool apply) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { err = e.c_str(); return false; }
  std::vector<Device> ds;
  for (JsonObjectConst o : doc["devices"].as<JsonArrayConst>()) {
    if (ds.size() >= MAX_DEVICES) { err = "Höchstens " + String(MAX_DEVICES) + " Sensoren"; return false; }
    Device d;
    d.name = o["name"] | "";
    d.bus = o["bus"] | 0;
    d.kind = o["kind"] | 1;
    d.addr = o["addr"] | "";
    d.alt = o["alt"] | "";
    d.icon = o["icon"] | "door";
    d.warn = o["warn"] | false;
    d.invert = o["invert"] | false;
    d.name.trim(); d.addr.trim(); d.alt.trim();
    if (d.bus > B_BLE) d.bus = B_ZIGBEE;
    if (d.kind > K_CLIMATE) d.kind = K_CLIMATE;
    if (d.bus == B_BLE) d.kind = K_CLIMATE;
    if (!icons::known(d.icon)) d.icon = "";
    if (d.addr.isEmpty()) { err = (d.name.length() ? d.name : String("Sensor")) + ": Adresse fehlt"; return false; }
    if (d.name.isEmpty()) d.name = d.addr;
    ds.push_back(d);
  }
  String zb = doc["zbBase"] | "camper/zigbee/data", ble = doc["bleBase"] | "camper/OMG/BTtoMQTT";
  zb.trim(); ble.trim();
  while (zb.endsWith("/")) zb.remove(zb.length() - 1);
  while (ble.endsWith("/")) ble.remove(ble.length() - 1);
  if (zb.indexOf('#') >= 0 || zb.indexOf('+') >= 0 || ble.indexOf('#') >= 0 || ble.indexOf('+') >= 0) {
    err = "Basis-Topics ohne # und +";
    return false;
  }
  if (apply) {
    Lock lock;
    // keep the values of devices that stay (same address)
    std::vector<Value> nv(ds.size());
    for (size_t i = 0; i < ds.size(); i++)
      for (size_t j = 0; j < devs.size(); j++)
        if (devs[j].bus == ds[i].bus && devs[j].addr == ds[i].addr) { nv[i] = vals[j]; break; }
    devs = ds;
    vals = nv;
    zbBase = zb;
    bleBase = ble;
    tempDiv = doc["tempDiv"] | 10.0f;
    humDiv = doc["humDiv"] | 1.0f;
    for (auto& d : devs) {
      if (d.bus == B_BLE) unknown.erase("b:" + normMac(d.addr));
      else { unknown.erase("z:" + normZb(d.addr)); if (d.alt.length()) unknown.erase("z:" + normZb(d.alt)); }
    }
    dirty = true;
  }
  return true;
}

String valuesJson() {
  Lock lock;
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  uint32_t now = millis();
  for (size_t i = 0; i < devs.size(); i++) {
    const Value& v = vals[i];
    JsonObject o = a.add<JsonObject>();
    o["n"] = devs[i].name;
    o["k"] = devs[i].kind;
    o["b"] = devs[i].bus;
    if (!isnan(v.temp)) o["t"] = roundf(v.temp * 10) / 10;
    if (!isnan(v.hum)) o["h"] = roundf(v.hum);
    if (!isnan(v.pres)) o["p"] = roundf(v.pres);
    if (!isnan(v.batt)) o["bat"] = roundf(v.batt);
    if (v.open >= 0) o["o"] = v.open;
    o["age"] = v.at ? (int32_t)((now - v.at) / 1000) : -1;
    o["i"] = devs[i].icon;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String unknownJson() {
  Lock lock;
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  uint32_t now = millis();
  for (auto& kv : unknown) {
    JsonObject o = a.add<JsonObject>();
    o["bus"] = kv.first.startsWith("b:") ? 1 : 0;
    o["addr"] = kv.first.substring(2);
    o["ago"] = (now - kv.second) / 1000;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

std::vector<TempOut> climate() {
  Lock lock;
  std::vector<TempOut> out;
  uint32_t now = millis();
  for (size_t i = 0; i < devs.size(); i++) {
    if (devs[i].kind != K_CLIMATE || isnan(vals[i].temp)) continue;
    out.push_back({devs[i].name, vals[i].temp, vals[i].hum, !vals[i].at || now - vals[i].at > STALE_MS});
  }
  return out;
}

std::vector<String> activeIcons() {
  Lock lock;
  std::vector<String> out;
  for (size_t i = 0; i < devs.size(); i++)
    if (devs[i].kind == K_CONTACT && vals[i].open == 1 && devs[i].icon.length()) out.push_back(devs[i].icon);
  return out;
}

String warnings() {
  Lock lock;
  String w;
  for (size_t i = 0; i < devs.size(); i++) {
    if (devs[i].kind != K_CONTACT || !devs[i].warn || vals[i].open != 1) continue;
    if (w.length()) w += "\n";
    w += devs[i].name + " offen";
  }
  return w;
}

bool changed() {
  bool d = dirty;
  dirty = false;
  return d;
}

}  // namespace sensors
