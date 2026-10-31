#include "settings.h"
#include "rules.h"
#include "sensors.h"
#include "config.h"
#include <Preferences.h>
#include <ArduinoJson.h>

const char* const DEFAULT_PAGE_NAMES[BP_COUNT + 1] = {"Home", "Truma", "MaxxFan", "Level", "Temperatur", "Batterie", "Wetter",
                                                       "Aktoren", "Warnungen", "Licht", "System"};
const char* const PAGE_IDS[BP_COUNT] = {"overview", "heater", "fan", "level", "temp", "bat", "weather", "actors", "warn", "light"};

String pageTitle(int idx) {
  if (idx < 0 || idx > BP_COUNT) return "";
  return settings.pageName[idx].length() ? settings.pageName[idx] : String(DEFAULT_PAGE_NAMES[idx]);
}

Settings settings;

namespace {
Preferences prefs;
const char* NS = "display";

// Example page shown on first start so the extension mechanism is visible.
const char* DEFAULT_PAGES = "{\"pages\":[],\"tiles\":[]}";
}

namespace cfg {

void load() {
  prefs.begin(NS, true);
  settings.hostname     = prefs.getString("host",   DEFAULT_HOSTNAME);
  settings.displayName  = prefs.getString("dname",  DEFAULT_DISPLAY_NAME);
  settings.links        = prefs.getString("links",  "[]");
  settings.apPass       = prefs.getString("appass", DEFAULT_AP_PASSWORD);
  settings.webUser      = prefs.getString("wusr",   DEFAULT_WEB_USER);
  settings.webPass      = prefs.getString("wpw",    DEFAULT_WEB_PASSWORD);
  settings.wifiSsid     = prefs.getString("ssid",   DEFAULT_WIFI_SSID);
  settings.wifiPass     = prefs.getString("wpass",  DEFAULT_WIFI_PASSWORD);
  settings.ip           = prefs.getString("ip",     DEFAULT_STATIC_IP);
  settings.staticIp     = prefs.getBool  ("sip",    strlen(DEFAULT_STATIC_IP) > 0);
  settings.gateway      = prefs.getString("gw",     DEFAULT_GATEWAY);
  settings.subnet       = prefs.getString("sn",     DEFAULT_SUBNET);
  settings.dns          = prefs.getString("dns",    DEFAULT_DNS);
  settings.mqttHost     = prefs.getString("mhost",  DEFAULT_MQTT_SERVER);
  settings.mqttPort     = prefs.getUShort("mport",  DEFAULT_MQTT_PORT);
  settings.mqttUser     = prefs.getString("muser",  DEFAULT_MQTT_USER);
  settings.mqttPass     = prefs.getString("mpass",  DEFAULT_MQTT_PASSWORD);
  settings.prefixSelf   = prefs.getString("pself",  DEFAULT_PREFIX_SELF);
  settings.prefixTank   = prefs.getString("ptank",  DEFAULT_PREFIX_TANK);
  settings.prefixLevel  = prefs.getString("plvl",   DEFAULT_PREFIX_LEVEL);
  settings.prefixHeater = prefs.getString("pheat",  DEFAULT_PREFIX_HEATER);
  settings.prefixFan    = prefs.getString("pfan",   DEFAULT_PREFIX_FAN);
  settings.prefixTemp   = prefs.getString("ptemp",  DEFAULT_PREFIX_TEMP);
  settings.prefixBattery = prefs.getString("pbat",  DEFAULT_PREFIX_BATTERY);
  settings.fanPort      = prefs.getUChar ("fport",  DEFAULT_FAN_PORT);
  settings.ntpServer    = prefs.getString("ntp",    DEFAULT_NTP_SERVER);
  settings.tz           = prefs.getString("tz",     DEFAULT_TZ);
  settings.poOn         = prefs.getBool  ("pon",    false);
  settings.poClear      = prefs.getBool  ("pclr",   true);
  settings.poPrio       = prefs.getChar  ("pprio",  0);
  settings.poToken      = prefs.getString("ptok",   DEFAULT_PUSHOVER_TOKEN);
  settings.poUser       = prefs.getString("pusr",   DEFAULT_PUSHOVER_USER);
  settings.poTitle      = prefs.getString("ptitle", DEFAULT_PUSHOVER_TITLE);
  settings.brightness   = prefs.getUChar ("bri",    DEFAULT_BRIGHTNESS);
  settings.dimBrightness= prefs.getUChar ("dim",    DEFAULT_DIM_BRIGHTNESS);
  settings.screenTimeout= prefs.getUShort("sto",    DEFAULT_SCREEN_TIMEOUT);
  settings.warnWake     = prefs.getBool  ("wwake",  false);
  // 16 bit mask since 9 fixed pages; older versions stored 8 bits in "bpages"
  uint16_t bmask        = prefs.isKey("bpmask") ? prefs.getUShort("bpmask", 0xFFFF) : prefs.getUChar("bpages", 0xFF);
  // Pages added in later versions start switched on (older masks do not have their bit)
  uint8_t bknown        = prefs.getUChar ("bpknown", BP_LEVEL + 1);
  for (int i = bknown; i < BP_COUNT; i++) bmask |= (1u << i);
  for (int i = 0; i < BP_COUNT; i++) settings.builtin[i] = bmask & (1 << i);
  for (int i = 0; i <= BP_COUNT; i++) settings.pageName[i] = prefs.getString((String("pgn") + i).c_str(), "");
  // The system page title is stored after the fixed pages: move it when pages were added
  if (bknown < BP_COUNT) {
    settings.pageName[BP_COUNT] = prefs.getString((String("pgn") + bknown).c_str(), "");
    for (int i = bknown; i < BP_COUNT; i++) settings.pageName[i] = "";
  }
  settings.wxOn         = prefs.getBool  ("wxon",   DEFAULT_WX_LAT[0] != 0);
  settings.wxLat        = prefs.getString("wxlat",  DEFAULT_WX_LAT);
  settings.wxLon        = prefs.getString("wxlon",  DEFAULT_WX_LON);
  settings.wxName       = prefs.getString("wxname", DEFAULT_WX_NAME);
  settings.wxTopic      = prefs.getString("wxtop",  DEFAULT_WX_TOPIC);
  settings.wxGust       = prefs.getUChar ("wxgust", DEFAULT_WX_GUST);
  settings.wxEvery      = prefs.getUChar ("wxevery", 60);
  settings.ovTempSel    = prefs.getString("ovtsel", "");
  settings.ovBatSel     = prefs.getString("ovbsel", "");
  settings.ovTiles      = prefs.getString("ovt",    DEFAULT_OV_TILES);
  settings.navSel       = prefs.getString("navsel", "");
  settings.heaterSrc    = prefs.getUChar ("hsrc",   HEAT_SRC_OWN);

  String pagesJson;
  size_t len = prefs.getBytesLength("pages");
  if (len > 0) {
    std::vector<char> buf(len + 1, 0);
    prefs.getBytes("pages", buf.data(), len);
    pagesJson = buf.data();
  }
  prefs.end();

  if (settings.apPass.length() < 8) settings.apPass = DEFAULT_AP_PASSWORD;
  if (settings.brightness < 5) settings.brightness = 5;
  if (settings.fanPort > 1) settings.fanPort = 1;
  if (settings.poPrio < -1 || settings.poPrio > 1) settings.poPrio = 0;
  if (settings.poTitle.isEmpty()) settings.poTitle = DEFAULT_PUSHOVER_TITLE;
  if (settings.wxEvery < 10 || settings.wxEvery > 180) settings.wxEvery = 60;
  if (settings.heaterSrc > HEAT_SRC_CAMPMATIC) settings.heaterSrc = HEAT_SRC_OWN;

  String err;
  if (pagesJson.isEmpty() || !pagesFromJson(pagesJson, err)) {
    if (!pagesJson.isEmpty()) Serial.printf("[cfg] stored pages invalid (%s), using defaults\n", err.c_str());
    pagesFromJson(DEFAULT_PAGES, err);
  }
}

void save() {
  prefs.begin(NS, false);
  prefs.putString("host",   settings.hostname);
  prefs.putString("dname",  settings.displayName);
  prefs.putString("links",  settings.links);
  prefs.putString("appass", settings.apPass);
  prefs.putString("wusr",   settings.webUser);
  prefs.putString("wpw",    settings.webPass);
  prefs.putString("ssid",   settings.wifiSsid);
  prefs.putString("wpass",  settings.wifiPass);
  prefs.putBool  ("sip",    settings.staticIp);
  prefs.putString("ip",     settings.ip);
  prefs.putString("gw",     settings.gateway);
  prefs.putString("sn",     settings.subnet);
  prefs.putString("dns",    settings.dns);
  prefs.putString("mhost",  settings.mqttHost);
  prefs.putUShort("mport",  settings.mqttPort);
  prefs.putString("muser",  settings.mqttUser);
  prefs.putString("mpass",  settings.mqttPass);
  prefs.putString("pself",  settings.prefixSelf);
  prefs.putString("ptank",  settings.prefixTank);
  prefs.putString("plvl",   settings.prefixLevel);
  prefs.putString("pheat",  settings.prefixHeater);
  prefs.putString("pfan",   settings.prefixFan);
  prefs.putString("ptemp",  settings.prefixTemp);
  prefs.putString("pbat",   settings.prefixBattery);
  prefs.putUChar ("fport",  settings.fanPort);
  prefs.putString("ntp",    settings.ntpServer);
  prefs.putString("tz",     settings.tz);
  prefs.putBool  ("pon",    settings.poOn);
  prefs.putBool  ("pclr",   settings.poClear);
  prefs.putChar  ("pprio",  settings.poPrio);
  prefs.putString("ptok",   settings.poToken);
  prefs.putString("pusr",   settings.poUser);
  prefs.putString("ptitle", settings.poTitle);
  prefs.putUChar ("bri",    settings.brightness);
  prefs.putUChar ("dim",    settings.dimBrightness);
  prefs.putUShort("sto",    settings.screenTimeout);
  prefs.putBool  ("wwake",  settings.warnWake);
  uint16_t bmask = 0;
  for (int i = 0; i < BP_COUNT; i++) if (settings.builtin[i]) bmask |= (1u << i);
  prefs.putUShort("bpmask", bmask);
  for (int i = 0; i <= BP_COUNT; i++) prefs.putString((String("pgn") + i).c_str(), settings.pageName[i]);
  prefs.putString("ovt",    settings.ovTiles);
  prefs.putString("navsel", settings.navSel);
  prefs.putUChar ("hsrc",   settings.heaterSrc);
  prefs.putBool  ("wxon",   settings.wxOn);
  prefs.putString("wxlat",  settings.wxLat);
  prefs.putString("wxlon",  settings.wxLon);
  prefs.putString("wxname", settings.wxName);
  prefs.putString("wxtop",  settings.wxTopic);
  prefs.putUChar ("wxgust", settings.wxGust);
  prefs.putUChar ("wxevery", settings.wxEvery);
  prefs.putString("ovtsel", settings.ovTempSel);
  prefs.putString("ovbsel", settings.ovBatSel);
  prefs.putUChar("bpknown", BP_COUNT);
  prefs.end();
}

void savePages() {
  String json = pagesToJson();
  prefs.begin(NS, false);
  prefs.putBytes("pages", json.c_str(), json.length());
  prefs.end();
}

void factoryReset() {
  prefs.begin(NS, false);
  prefs.clear();
  prefs.end();
  rules::clear();
  sensors::clear();
}

String pagesToJson() {
  JsonDocument doc;
  JsonArray pa = doc["pages"].to<JsonArray>();
  for (auto& p : settings.pages) pa.add<JsonObject>()["name"] = p.name;
  JsonArray ta = doc["tiles"].to<JsonArray>();
  for (auto& t : settings.tiles) {
    JsonObject o = ta.add<JsonObject>();
    o["name"] = t.name;
    o["page"] = t.page;
    o["type"] = t.type;
    if (t.topic.length())      o["topic"] = t.topic;
    if (t.key.length())        o["key"] = t.key;
    if (t.unit.length())       o["unit"] = t.unit;
    if (t.decimals >= 0)       o["decimals"] = t.decimals;
    if (t.cmdTopic.length())   o["cmdTopic"] = t.cmdTopic;
    if (t.payloadOn.length())  o["payloadOn"] = t.payloadOn;
    if (t.payloadOff.length()) o["payloadOff"] = t.payloadOff;
    if (t.stateOn != "ON")     o["stateOn"] = t.stateOn;
    if (t.type == TILE_SLIDER) { o["min"] = t.min; o["max"] = t.max; o["step"] = t.step; }
    if (t.valMap.length()) o["valMap"] = t.valMap;
    if (t.warnMode) { o["warnMode"] = t.warnMode; o["warnVal"] = t.warnVal; }
    if (t.warnEq.length()) o["warnEq"] = t.warnEq;
    if (t.warnStale) o["warnStale"] = t.warnStale;
    if (t.warnText.length()) o["warnText"] = t.warnText;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool pagesFromJson(const String& json, String& err, bool apply) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { err = e.c_str(); return false; }

  std::vector<Page> pages;
  for (JsonObject p : doc["pages"].as<JsonArray>()) {
    if (pages.size() >= MAX_PAGES) break;
    Page pg;
    pg.name = p["name"] | "Seite";
    pg.name.trim();
    if (pg.name.isEmpty()) pg.name = "Seite " + String(pages.size() + 1);
    pages.push_back(pg);
  }

  std::vector<Tile> tiles;
  for (JsonObject o : doc["tiles"].as<JsonArray>()) {
    if (tiles.size() >= MAX_TILES) break;
    Tile t;
    t.name       = o["name"] | "";
    t.page       = o["page"] | 0;
    t.type       = o["type"] | 0;
    t.topic      = o["topic"] | "";
    t.key        = o["key"] | "";
    t.unit       = o["unit"] | "";
    t.decimals   = o["decimals"] | -1;
    t.cmdTopic   = o["cmdTopic"] | "";
    t.payloadOn  = o["payloadOn"] | "";
    t.payloadOff = o["payloadOff"] | "";
    t.stateOn    = o["stateOn"] | "ON";
    t.min        = o["min"] | 0.0f;
    t.max        = o["max"] | 100.0f;
    t.step       = o["step"] | 1.0f;
    t.warnMode   = o["warnMode"] | 0;
    t.warnVal    = o["warnVal"] | 0.0f;
    t.warnStale  = o["warnStale"] | 0;
    t.warnText   = o["warnText"] | "";
    t.warnEq     = o["warnEq"] | "";
    t.valMap     = o["valMap"] | "";
    t.warnText.trim(); t.warnEq.trim(); t.valMap.trim();
    if (t.warnMode > WARN_EQUAL) t.warnMode = WARN_NONE;
    if (t.valMap.length() > 120) t.valMap = t.valMap.substring(0, 120);
    if (t.warnEq.length() > 32) t.warnEq = t.warnEq.substring(0, 32);
    if (t.warnStale > 1440) t.warnStale = 1440;
    if (t.warnText.length() > 40) t.warnText = t.warnText.substring(0, 40);
    t.topic.trim(); t.cmdTopic.trim(); t.key.trim();
    if (t.type > TILE_SLIDER) t.type = TILE_VALUE;
    if (t.decimals > 4) t.decimals = 4;
    if (t.max <= t.min) t.max = t.min + 1;
    if (t.step <= 0) t.step = 1;
    if (t.topic.indexOf('#') >= 0 || t.topic.indexOf('+') >= 0) {
      err = "Wildcards sind in Kachel-Topics nicht erlaubt: " + t.topic;
      return false;
    }
    if (t.topic.isEmpty() && t.cmdTopic.isEmpty()) {
      err = "Kachel ohne Topic: " + t.name;
      return false;
    }
    if (pages.empty() || t.page >= pages.size()) {
      err = "Kachel auf nicht vorhandener Seite: " + t.name;
      return false;
    }
    if (t.name.isEmpty()) t.name = t.key.length() ? t.key : t.topic;
    tiles.push_back(t);
  }

  if (apply) {
    settings.pages = pages;
    settings.tiles = tiles;
  }
  return true;
}

} // namespace cfg
