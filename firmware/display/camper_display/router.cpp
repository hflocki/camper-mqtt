#include "router.h"
#include "settings.h"
#include <ArduinoJson.h>

namespace rt {
namespace {

const uint32_t FRESH_MS = 5UL * 60UL * 1000UL;   // the router sends every 60 s by default

float rsrp = NAN, rssi = NAN;
String net;
uint32_t lastAt = 0;
bool seen = false;

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

// network name -> short label
String netLabel(String s) {
  s.toUpperCase();
  if (s.indexOf("5G") >= 0 || s.indexOf("NR") >= 0) return "5G";
  if (s.indexOf("LTE") >= 0 || s.indexOf("4G") >= 0) return "LTE";
  if (s.indexOf("WCDMA") >= 0 || s.indexOf("UMTS") >= 0 || s.indexOf("HSPA") >= 0 || s.indexOf("3G") >= 0) return "3G";
  if (s.indexOf("GSM") >= 0 || s.indexOf("EDGE") >= 0 || s.indexOf("GPRS") >= 0 || s.indexOf("2G") >= 0) return "2G";
  return "";
}

// searches nested objects (the RUT956 wraps values in objects and arrays)
void scan(JsonVariantConst v, int depth) {
  if (depth > 4) return;
  if (v.is<JsonArrayConst>()) {
    for (JsonVariantConst e : v.as<JsonArrayConst>()) scan(e, depth + 1);
    return;
  }
  if (!v.is<JsonObjectConst>()) return;
  for (JsonPairConst kv : v.as<JsonObjectConst>()) {
    String k = kv.key().c_str();
    k.toLowerCase();
    JsonVariantConst val = kv.value();
    if (val.is<JsonObjectConst>() || val.is<JsonArrayConst>()) { scan(val, depth + 1); continue; }
    if (k == "rsrp")                         { float f = numOf(val); if (!isnan(f)) rsrp = f; }
    else if (k == "rssi" || k == "signal")   { float f = numOf(val); if (!isnan(f) && f < 0) rssi = f; }
    else if (k == "conntype" || k == "network_type" || k == "nettype" || k == "net_type" ||
             k == "mode" || k == "type" || k == "service_mode") {
      if (val.is<const char*>()) { String l = netLabel(val.as<const char*>()); if (l.length()) net = l; }
    }
  }
}

}  // namespace

bool onMessage(const char* topic, const char* payload, unsigned int len) {
  if (settings.rtTopic.isEmpty() || settings.rtTopic != topic) return false;
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return true;
  float oldRsrp = rsrp, oldRssi = rssi;
  rsrp = rssi = NAN;
  scan(doc.as<JsonVariantConst>(), 0);
  if (isnan(rsrp) && isnan(rssi)) { rsrp = oldRsrp; rssi = oldRssi; return true; }   // no signal value in it
  lastAt = millis();
  if (!seen) Serial.printf("[rt] signal from %s: rsrp %.0f, rssi %.0f, %s\n", topic, rsrp, rssi, net.c_str());
  seen = true;
  return true;
}

bool fresh() { return seen && millis() - lastAt < FRESH_MS; }

int bars() {
  if (!fresh()) return 0;
  // RSRP describes LTE better than RSSI
  if (!isnan(rsrp)) return rsrp >= -80 ? 5 : rsrp >= -90 ? 4 : rsrp >= -100 ? 3 : rsrp >= -110 ? 2 : 1;
  if (!isnan(rssi)) return rssi >= -65 ? 5 : rssi >= -75 ? 4 : rssi >= -85 ? 3 : rssi >= -95 ? 2 : 1;
  return 0;
}

String type() {
  if (!fresh()) return "";
  if (net.length()) return net;
  return isnan(rsrp) ? String("") : String("LTE");   // RSRP only exists on LTE / 5G
}

}  // namespace rt
