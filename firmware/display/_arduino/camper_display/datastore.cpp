#include "datastore.h"
#include "settings.h"
#include <vector>

namespace {

struct DpDef {
  const char* key;   // short key for the web page
  Dev         dev;
  const char* sub;   // topic below the device prefix; '#' is replaced by the fan port
};

// Must match the order of DpId
const DpDef DEFS[DP_COUNT] = {
  {"tank_status", DEV_TANK, "status"},
  {"water_pct",   DEV_TANK, "water/percent"},
  {"water_l",     DEV_TANK, "water/liters"},
  {"grey_pct",    DEV_TANK, "grey/percent"},
  {"grey_l",      DEV_TANK, "grey/liters"},
  {"gas_pct",     DEV_TANK, "gas/level"},
  {"gas_l",       DEV_TANK, "gas/content"},
  {"gas_bat",     DEV_TANK, "gas/battery"},

  {"lvl_status",  DEV_LEVEL, "status"},
  {"lvl_pitch",   DEV_LEVEL, "pitch"},
  {"lvl_roll",    DEV_LEVEL, "roll"},
  {"lvl_ok",      DEV_LEVEL, "is_level"},
  {"lvl_tol",     DEV_LEVEL, "tolerance"},
  {"lvl_fl",      DEV_LEVEL, "wedge_fl"},
  {"lvl_fr",      DEV_LEVEL, "wedge_fr"},
  {"lvl_rl",      DEV_LEVEL, "wedge_rl"},
  {"lvl_rr",      DEV_LEVEL, "wedge_rr"},

  {"heat_status", DEV_HEATER, "status"},
  {"heat_alive",  DEV_HEATER, "control_status/alive"},
  {"heat_room",   DEV_HEATER, "control_status/current_temp_room"},
  {"heat_set",    DEV_HEATER, "control_status/room_setpoint"},
  {"heat_hvac",   DEV_HEATER, "control_status/hvac_mode"},
  {"heat_action", DEV_HEATER, "control_status/hvac_action"},
  {"heat_fan",    DEV_HEATER, "control_status/fan_mode"},
  {"heat_wmode",  DEV_HEATER, "control_status/water_mode"},
  {"heat_water",  DEV_HEATER, "control_status/current_temp_water"},
  {"heat_op",     DEV_HEATER, "control_status/operating_status_text"},
  {"heat_err",    DEV_HEATER, "control_status/error_code"},
  {"heat_errflag",DEV_HEATER, "control_status/heater_error"},
  {"heat_decoke", DEV_HEATER, "control_status/decoke"},
  {"heat_drem",   DEV_HEATER, "control_status/decoke_remaining"},

  {"fan_status",  DEV_FAN, "status"},
  {"fan_hold",    DEV_FAN, "p#/auto_hold"},
  {"fan_led",     DEV_FAN, "p#/led"},

  {"temp_status", DEV_TEMP, "status"},
  {"temp_list",   DEV_TEMP, "sensors"},
  {"temp_alarm",  DEV_TEMP, "alarm_text"},

  {"bat_status",  DEV_BATTERY, "status"},
  {"bat_list",    DEV_BATTERY, "batteries"},
};

// CampMatic box (ESPHome style climate topics below <heater prefix>), used instead of the
// "sub" column above for the heater when settings.heaterSrc == HEAT_SRC_CAMPMATIC.
// "" = not available. Hot water (40 / 60 °C / off) is put together from two topics, see cmWater*.
const char* const CM_WATER   = "climate/truma_wasser/";
const char* campmaticSub(int i) {
  switch (i) {
    case DP_HEAT_STATUS:   return "status";
    case DP_HEAT_ROOM:     return "climate/truma_heizung/current_temperature/state";
    case DP_HEAT_SETPOINT: return "climate/truma_heizung/target_temperature/state";
    case DP_HEAT_HVAC:     return "climate/truma_heizung/mode/state";
    case DP_HEAT_ACTION:   return "climate/truma_heizung/action/state";
    case DP_HEAT_FANMODE:  return "climate/truma_heizung/fan_mode/state";
    case DP_HEAT_WATER:    return "climate/truma_wasser/current_temperature/state";
    default:               return "";
  }
}
String cmWaterModeTopic, cmWaterSetTopic;   // full topics, empty unless CampMatic
String cmWaterMode, cmWaterSet;             // last values ("heat"/"off", "40"/"60"/"nan")
float lastHeatSet = NAN;                    // last real heater setpoint (CampMatic: "nan" while off)

String values[DP_COUNT];
String topics[DP_COUNT];
bool   dirty[2][DP_COUNT];

std::vector<String> tileVals;
std::vector<bool>   tileSeen;
std::vector<uint32_t> tileAt;             // millis() of the last message
std::vector<String> tileRawVals;          // as received, before valMap

// "close=Zu,open=Offen": shown text for a received value (case ignored), unchanged if not listed
String mapValue(const String& map, const String& v) {
  int pos = 0;
  while (pos < (int)map.length()) {
    int end = map.indexOf(',', pos);
    if (end < 0) end = map.length();
    String part = map.substring(pos, end);
    pos = end + 1;
    int eq = part.indexOf('=');
    if (eq <= 0) continue;
    String from = part.substring(0, eq), to = part.substring(eq + 1);
    from.trim();
    to.trim();
    if (from.equalsIgnoreCase(v)) return to;
  }
  return v;
}
std::vector<bool>   tileDirtyF[2];

const String EMPTY;

const String& prefixOf(Dev d) {
  switch (d) {
    case DEV_TANK:   return settings.prefixTank;
    case DEV_LEVEL:  return settings.prefixLevel;
    case DEV_HEATER: return settings.prefixHeater;
    case DEV_TEMP:   return settings.prefixTemp;
    case DEV_BATTERY: return settings.prefixBattery;
    default:         return settings.prefixFan;
  }
}

void setDp(int i, const String& v) {
  if (values[i] == v) return;
  values[i] = v;
  dirty[0][i] = dirty[1][i] = true;
}

// Resolve a dotted JSON path ("a.b.c") and return the value as text.
bool extractJson(const char* payload, unsigned int len, const String& key, int8_t decimals, String& out) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return false;
  JsonVariant v = doc.as<JsonVariant>();
  int start = 0;
  while (start <= (int)key.length()) {
    int dot = key.indexOf('.', start);
    String part = key.substring(start, dot < 0 ? key.length() : dot);
    if (v.is<JsonArray>()) v = v[part.toInt()];
    else                   v = v[part];
    if (v.isNull()) return false;
    if (dot < 0) break;
    start = dot + 1;
  }
  if (v.is<bool>())        out = v.as<bool>() ? "ON" : "OFF";
  else if (v.is<float>() && decimals >= 0) out = String(v.as<float>(), (unsigned)decimals);
  else if (v.is<const char*>()) out = v.as<const char*>();
  else { out = ""; serializeJson(v, out); }
  return true;
}

String formatRaw(const char* payload, unsigned int len, int8_t decimals) {
  String s;
  s.reserve(len);
  for (unsigned int i = 0; i < len; i++) s += payload[i];
  s.trim();
  if (decimals >= 0 && s.length()) {
    char* end;
    float f = strtof(s.c_str(), &end);
    if (end != s.c_str() && *end == 0) s = String(f, (unsigned)decimals);
  }
  return s;
}

} // namespace

namespace store {

void begin() {
  rebuildTopics();
}

void rebuildTopics() {
  bool cm = settings.heaterSrc == HEAT_SRC_CAMPMATIC;
  for (int i = 0; i < DP_COUNT; i++) {
    const String& pre = prefixOf(DEFS[i].dev);
    String sub = (cm && DEFS[i].dev == DEV_HEATER) ? campmaticSub(i) : DEFS[i].sub;
    String t;
    if (pre.length() && sub.length()) {
      sub.replace("#", String(settings.fanPort));
      t = pre + "/" + sub;
    }
    if (t != topics[i]) setDp(i, "");   // value of a topic that is no longer read
    topics[i] = t;
  }
  const String& hp = settings.prefixHeater;
  cmWaterModeTopic = cm && hp.length() ? hp + "/" + CM_WATER + "mode/state" : String("");
  cmWaterSetTopic  = cm && hp.length() ? hp + "/" + CM_WATER + "target_temperature/state" : String("");
  cmWaterMode = cmWaterSet = "";
  size_t n = settings.tiles.size();
  tileVals.assign(n, String());
  tileSeen.assign(n, false);
  tileAt.assign(n, millis());
  tileRawVals.assign(n, String());
  tileDirtyF[0].assign(n, true);
  tileDirtyF[1].assign(n, true);
}

bool onMessage(const char* topic, const char* payload, unsigned int len) {
  bool used = false;
  for (int i = 0; i < DP_COUNT; i++) {
    if (topics[i].length() && topics[i] == topic) {
      String v = formatRaw(payload, len, -1);
      // "nan" = no value (CampMatic sends it as setpoint while heating or water is off)
      if (v.equalsIgnoreCase("nan") || v.equalsIgnoreCase("inf") || v.equalsIgnoreCase("-inf")) v = "";
      setDp(i, v);
      if (i == DP_HEAT_SETPOINT && v.length() && v.toFloat() >= 5) lastHeatSet = v.toFloat();
      used = true;
    }
  }
  // CampMatic hot water: mode heat/off plus setpoint 40 / 60 -> water_mode off / eco / high
  bool wm = cmWaterModeTopic.length() && cmWaterModeTopic == topic;
  bool ws = cmWaterSetTopic.length() && cmWaterSetTopic == topic;
  if (wm || ws) {
    (wm ? cmWaterMode : cmWaterSet) = formatRaw(payload, len, -1);
    float set = cmWaterSet.toFloat();
    String mode = cmWaterMode.isEmpty() ? String("")
                : cmWaterMode != "heat" ? String("off")
                : set >= 50 ? String("high") : String("eco");
    setDp(DP_HEAT_WATERMODE, mode);
    used = true;
  }
  for (size_t i = 0; i < settings.tiles.size() && i < tileVals.size(); i++) {
    const Tile& t = settings.tiles[i];
    if (t.topic.isEmpty() || t.topic != topic) continue;
    String v;
    bool ok = t.key.length() ? extractJson(payload, len, t.key, t.decimals, v)
                             : (v = formatRaw(payload, len, t.decimals), true);
    if (!ok) continue;   // key not in this message (z2m sends partial updates too)
    tileAt[i] = millis();
    tileRawVals[i] = v;
    if (t.valMap.length()) v = mapValue(t.valMap, v);
    if (!tileSeen[i] || tileVals[i] != v) {
      tileVals[i] = v;
      tileSeen[i] = true;
      tileDirtyF[0][i] = tileDirtyF[1][i] = true;
    }
    used = true;
  }
  return used;
}

const String& get(DpId id)      { return values[id]; }
bool has(DpId id)               { return values[id].length() > 0; }
float lastHeaterSetpoint()      { return lastHeatSet; }
float getf(DpId id, float def) {
  if (!has(id)) return def;
  char* end;
  float f = strtof(values[id].c_str(), &end);
  return end == values[id].c_str() ? def : f;
}

bool devOnline(Dev d) {
  static const DpId st[DEV_COUNT] = {DP_TANK_STATUS, DP_LVL_STATUS, DP_HEAT_STATUS, DP_FAN_STATUS, DP_TEMP_STATUS,
                                         DP_BAT_STATUS};
  return values[st[d]] == "online";
}

size_t tileCount()              { return tileVals.size(); }
const String& tile(size_t i)    { return i < tileVals.size() ? tileVals[i] : EMPTY; }
bool tileHas(size_t i)          { return i < tileSeen.size() && tileSeen[i]; }
uint32_t tileAgeMs(size_t i)    { return i < tileAt.size() ? millis() - tileAt[i] : 0; }
const String& tileRaw(size_t i) { return i < tileRawVals.size() ? tileRawVals[i] : EMPTY; }
bool tileOn(size_t i) {
  if (i >= tileVals.size() || i >= settings.tiles.size()) return false;
  return tileVals[i].equalsIgnoreCase(settings.tiles[i].stateOn);
}

const char*   dpKey(DpId id)    { return DEFS[id].key; }
const String& dpTopic(DpId id)  { return topics[id]; }
const char*   devPrefix(Dev d)  { return prefixOf(d).c_str(); }

bool takeDirty(Consumer c, bool& anyDp, bool& anyTile) {
  anyDp = anyTile = false;
  for (int i = 0; i < DP_COUNT; i++) if (dirty[c][i]) { anyDp = true; break; }
  for (size_t i = 0; i < tileDirtyF[c].size(); i++) if (tileDirtyF[c][i]) { anyTile = true; break; }
  return anyDp || anyTile;
}

bool dpDirty(Consumer c, DpId id)     { return dirty[c][id]; }
bool tileDirty(Consumer c, size_t i)  { return i < tileDirtyF[c].size() && tileDirtyF[c][i]; }

void clearDirty(Consumer c) {
  for (int i = 0; i < DP_COUNT; i++) dirty[c][i] = false;
  for (size_t i = 0; i < tileDirtyF[c].size(); i++) tileDirtyF[c][i] = false;
}

void markAllDirty() {
  for (int c = 0; c < 2; c++) {
    for (int i = 0; i < DP_COUNT; i++) dirty[c][i] = true;
    for (size_t i = 0; i < tileDirtyF[c].size(); i++) tileDirtyF[c][i] = true;
  }
}

void fillJson(JsonObject vals, bool onlyDirty) {
  for (int i = 0; i < DP_COUNT; i++) {
    if (onlyDirty && !dirty[C_WEB][i]) continue;
    vals[DEFS[i].key] = values[i];
  }
  for (size_t i = 0; i < tileVals.size(); i++) {
    if (onlyDirty && !tileDirtyF[C_WEB][i]) continue;
    if (!tileSeen[i]) continue;
    vals["t" + String(i)] = tileVals[i];
  }
}

bool extract(const char* payload, unsigned int len, const String& key, String& out) {
  if (key.isEmpty()) { out = formatRaw(payload, len, -1); return true; }
  return extractJson(payload, len, key, -1, out);
}

} // namespace store
