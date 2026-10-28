#pragma once
#include <Arduino.h>
#include <vector>

// Built-in pages. Their order here is the order on the display and web page.
// New pages are appended: the index is also the bit in the stored page mask.
enum BuiltinPage : uint8_t { BP_OVERVIEW, BP_HEATER, BP_FAN, BP_LEVEL, BP_TEMP, BP_BATTERY, BP_WEATHER, BP_COUNT };

// User tile types
enum TileType : uint8_t { TILE_VALUE = 0, TILE_SWITCH = 1, TILE_BUTTON = 2, TILE_SLIDER = 3 };

struct Tile {
  String  name;
  uint8_t page = 0;         // index into Settings::pages
  uint8_t type = TILE_VALUE;
  String  topic;            // state topic (may be empty for pure buttons)
  String  key;              // JSON key, dotted path allowed ("a.b"), empty = raw payload
  String  unit;
  int8_t  decimals = -1;    // -1 = show as received
  String  cmdTopic;         // empty = <topic>/set
  String  payloadOn;        // switch ON / button payload / slider template with {v}
  String  payloadOff;       // switch OFF
  String  stateOn = "ON";   // value that means "on" for switches
  float   min = 0, max = 100, step = 1;
};

struct Page {
  String name;
};

struct Settings {
  // device
  String hostname;
  String displayName;
  String links;         // web page "Geräte": JSON [{"n":"Tanklevel","u":"http://..."}], max. MAX_LINKS   // shown centered in the header, e.g. what this panel is ("" = none)
  String apPass;
  String webUser, webPass;
  // wifi
  String wifiSsid, wifiPass;
  bool   staticIp = false;
  String ip, gateway, subnet, dns;
  // mqtt
  String mqttHost, mqttUser, mqttPass;
  uint16_t mqttPort = 1883;
  String prefixSelf, prefixTank, prefixLevel, prefixHeater, prefixFan, prefixTemp, prefixBattery;
  uint8_t fanPort = 1;
  // time
  String ntpServer, tz;
  // pushover
  bool   poOn = false;          // send warnings as push messages
  bool   poClear = true;        // also send a message when a warning is gone
  int8_t poPrio = 0;            // Pushover priority: 0 normal, 1 high (bypasses quiet hours)
  String poToken, poUser, poTitle;
  // weather (Open-Meteo)
  bool     wxOn = false;
  String   wxLat, wxLon;        // fixed position, decimal degrees
  String   wxName;              // shown on the weather page, e.g. the place
  String   wxTopic;             // MQTT topic with the position (GPS); empty = fixed position
  uint8_t  wxGust = 60;         // warn from these gusts in km/h, 0 = off
  uint8_t  wxEvery = 30;        // update interval in minutes
  // display
  uint8_t  brightness, dimBrightness;
  uint16_t screenTimeout;
  bool     builtin[BP_COUNT];
  // Titles of the fixed pages (index BP_*) and of the system page (index BP_COUNT), "" = default
  String   pageName[BP_COUNT + 1];
  // Overview tiles in order, comma separated (max. 9): water grey gas heater level fan temp bat
  // status bat1 bat2 bat3 clock none t<n> (custom tile n). "" = default layout
  String   ovTiles;
  // user pages
  std::vector<Page> pages;
  std::vector<Tile> tiles;
};

extern Settings settings;

// Default titles of the fixed pages (BP_* order, last = system page)
extern const char* const DEFAULT_PAGE_NAMES[BP_COUNT + 1];
String pageTitle(int idx);   // configured title or default

namespace cfg {
  void load();
  void save();                 // device / wifi / mqtt / display
  void savePages();            // pages + tiles
  void factoryReset();
  // pages + tiles <-> JSON (shared by web API and storage)
  String pagesToJson();
  bool   pagesFromJson(const String& json, String& err, bool apply = true);   // apply=false: validate only
}
