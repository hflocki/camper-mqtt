#pragma once
#include <Arduino.h>
#include <vector>

// Built-in pages. Their order here is the order on the display and web page.
// New pages are appended: the index is also the bit in the stored page mask.
enum BuiltinPage : uint8_t { BP_OVERVIEW, BP_HEATER, BP_FAN, BP_LEVEL, BP_TEMP, BP_COUNT };

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
  String apPass;
  String webUser, webPass;
  // wifi
  String wifiSsid, wifiPass;
  bool   staticIp = false;
  String ip, gateway, subnet, dns;
  // mqtt
  String mqttHost, mqttUser, mqttPass;
  uint16_t mqttPort = 1883;
  String prefixSelf, prefixTank, prefixLevel, prefixHeater, prefixFan, prefixTemp;
  uint8_t fanPort = 1;
  // time
  String ntpServer, tz;
  // display
  uint8_t  brightness, dimBrightness;
  uint16_t screenTimeout;
  bool     builtin[BP_COUNT];
  // user pages
  std::vector<Page> pages;
  std::vector<Tile> tiles;
};

extern Settings settings;

namespace cfg {
  void load();
  void save();                 // device / wifi / mqtt / display
  void savePages();            // pages + tiles
  void factoryReset();
  // pages + tiles <-> JSON (shared by web API and storage)
  String pagesToJson();
  bool   pagesFromJson(const String& json, String& err, bool apply = true);   // apply=false: validate only
}
