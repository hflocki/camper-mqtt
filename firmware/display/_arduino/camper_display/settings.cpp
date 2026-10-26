#include "settings.h"
#include "config.h"
#include <Preferences.h>
#include <ArduinoJson.h>

const char* const DEFAULT_PAGE_NAMES[BP_COUNT + 1] = {"Home", "Truma", "MaxxFan", "Level", "Temperatur", "Batterie", "System"};

String pageTitle(int idx) {
  if (idx < 0 || idx > BP_COUNT) return "";
  return settings.pageName[idx].length() ? settings.pageName[idx] : String(DEFAULT_PAGE_NAMES[idx]);
}

Settings settings;

namespace {
Preferences prefs;
const char* NS = "display";

// Example page shown on first start so the extension mechanism is visible.
const char* DEFAULT_PAGES =
  "{\"pages\":[{\"name\":\"Zigbee\"}],\"tiles\":["
  "{\"name\":\"Beispiel Temperatur\",\"page\":0,\"type\":0,\"topic\":\"zigbee2mqtt/Sensor\",\"key\":\"temperature\",\"unit\":\"\xC2\xB0""C\",\"decimals\":1},"
  "{\"name\":\"Beispiel Licht\",\"page\":0,\"type\":1,\"topic\":\"zigbee2mqtt/Licht\",\"key\":\"state\","
  "\"payloadOn\":\"{\\\"state\\\":\\\"ON\\\"}\",\"payloadOff\":\"{\\\"state\\\":\\\"OFF\\\"}\",\"stateOn\":\"ON\"}"
  "]}";
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
  uint8_t bmask         = prefs.getUChar ("bpages", 0xFF);
  // Pages added in later versions start switched on (older masks do not have their bit)
  uint8_t bknown        = prefs.getUChar ("bpknown", BP_LEVEL + 1);
  for (int i = bknown; i < BP_COUNT; i++) bmask |= (1 << i);
  for (int i = 0; i < BP_COUNT; i++) settings.builtin[i] = bmask & (1 << i);
  for (int i = 0; i <= BP_COUNT; i++) settings.pageName[i] = prefs.getString((String("pgn") + i).c_str(), "");
  settings.ovTiles      = prefs.getString("ovt",    DEFAULT_OV_TILES);

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
  uint8_t bmask = 0;
  for (int i = 0; i < BP_COUNT; i++) if (settings.builtin[i]) bmask |= (1 << i);
  prefs.putUChar("bpages", bmask);
  for (int i = 0; i <= BP_COUNT; i++) prefs.putString((String("pgn") + i).c_str(), settings.pageName[i]);
  prefs.putString("ovt",    settings.ovTiles);
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
