#pragma once
#include <Arduino.h>
#include <vector>

// Built-in pages. Their order here is the order on the display and web page.
// New pages are appended: the index is also the bit in the stored page mask.
enum BuiltinPage : uint8_t { BP_OVERVIEW, BP_HEATER, BP_FAN, BP_LEVEL, BP_TEMP, BP_BATTERY, BP_WEATHER, BP_ACTORS, BP_WARN, BP_LIGHT,
                            BP_SWITCH, BP_EXTRA1, BP_EXTRA2, BP_HEKI, BP_COUNT };
// Short ids of the fixed pages (web page, nav selection), BP_* order
extern const char* const PAGE_IDS[BP_COUNT];

// Kinds of warnings, each with its own Pushover priority. New kinds are appended:
// the index is the position in the stored priority list (NVS "ppcat").
enum WarnCat : uint8_t { WC_TEMP, WC_HEATER, WC_BATTERY, WC_WATER, WC_GAS, WC_FROST, WC_WEATHER,
                         WC_DEPART, WC_TILE, WC_RULE, WC_CONTACT, WC_COUNT };
// Short ids of the warning kinds (web page, JSON), WC_* order
extern const char* const WARN_CAT_IDS[WC_COUNT];
// Special values of Settings::poCat besides the Pushover priorities -1 .. 2
const int8_t PRIO_DEFAULT = 3;    // use the general priority (poPrio)
const int8_t PRIO_OFF     = -3;   // shown on the display, but no push message

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
  String   valMap;          // shown text for received values: "close=Zu,open=Offen", "" = as received
  // warning (value and switch tiles): value below/above a limit, equal to a text, or no message for warnStale minutes
  uint8_t  warnMode = 0;    // WARN_NONE, WARN_BELOW, WARN_ABOVE, WARN_EQUAL
  float    warnVal = 0;
  String   warnEq;          // WARN_EQUAL: received or shown value, case ignored ("open")
  uint16_t warnStale = 0;   // minutes, 0 = off
  String   warnText;        // "" = "<name> zu niedrig" ...
};
enum TileWarn : uint8_t { WARN_NONE = 0, WARN_BELOW = 1, WARN_ABOVE = 2, WARN_EQUAL = 3 };

enum HeaterSrc : uint8_t { HEAT_SRC_OWN = 0, HEAT_SRC_CAMPMATIC = 1 };
const int NAV_FREE = 6;

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
  String prefixSelf, prefixTank, prefixLevel, prefixHeater, prefixFan, prefixTemp, prefixBattery, prefixToolbox;
  uint8_t fanPort = 1;
  // time
  String ntpServer, tz;
  // pushover
  bool   poOn = false;          // send warnings as push messages
  bool   poClear = true;        // also send a message when a warning is gone
  int8_t poPrio = 0;            // Pushover priority: -1 quiet, 0 normal, 1 high (bypasses quiet hours), 2 emergency
  int8_t poCat[WC_COUNT];       // priority per warning kind, PRIO_DEFAULT = poPrio, PRIO_OFF = not sent
  uint16_t poRetry = 300;       // emergency (2): repeat every this many seconds (min. 30) ...
  uint16_t poExpire = 3600;     // ... until confirmed in the app or this many seconds passed (max. 10800)
  String poToken, poUser, poTitle;
  // weather (Open-Meteo)
  bool     wxOn = false;
  String   wxLat, wxLon;        // fixed position, decimal degrees
  String   wxName;              // shown on the weather page, e.g. the place
  String   wxTopic;             // MQTT topic with the position (GPS); empty = fixed position
  String   rtTopic;             // MQTT topic with the mobile signal of the router; empty = off
  uint8_t  wxGust = 60;         // warn from these gusts in km/h, 0 = off
  uint8_t  wxEvery = 60;        // update interval in minutes
  uint8_t  wxGustHyst = 10;     // gust warning ends below wxGust - this (km/h)
  // limits of the built-in warnings, 0 = off (water, gas, battery in %)
  uint8_t  wnWater = 10, wnGas = 15, wnBat = 20;
  uint8_t  wnHystPct = 3;       // a % warning ends above limit + this
  bool     wnFrostOn = false;   // frost warning from the outside temperature (sensor or weather)
  int8_t   wnFrost = 3;         // warn below this temperature in °C
  uint8_t  wnHystT = 10;        // temperature warnings end this far past the limit, in 0.1 °C
  uint16_t poQuiet = 30;        // minutes: a warning that ended is not sent again within this time
  // outside temperature (weather page, frost): name of an own climate sensor or a camper-temp sensor,
  // "" = the first own sensor marked for the weather page
  String   outName;
  // awning: "" = switch on the display / web page / MQTT, otherwise the name of an own contact sensor (open = out)
  String   awnSrc;
  uint8_t  awnGust = 40;        // warn while the awning is out from these gusts in km/h, 0 = off
  bool     awnOut = false;      // state of the switch (stored, survives a restart)
  // departure check (depart.*): warns while driving with the door open, the awning out, a contact open
  bool     dpOn = true;
  String   dpTopic;             // GPS JSON of the router (speed, fix_status), "" = no driving detection
  String   dpDoor;              // door state topic, "" = no door
  bool     dpDoorInv = false;   // the door topic reports open for a closed door
  uint8_t  dpSpeed = 5;         // driving above this speed in km/h ...
  uint8_t  dpSecs = 10;         // ... for this many seconds
  bool     dpCont = true;       // own contact sensors (windows, roof vents) are part of the check
  // overview tiles temperature / battery: "name=icon" lines (see icons.cpp), "" = all / first 3
  String   ovTempSel, ovBatSel;
  // heater source: own camper-heater firmware or a CampMatic box (topics <prefixHeater>/climate/truma_*/...)
  uint8_t  heaterSrc = 0;       // HEAT_SRC_*
  // display
  uint8_t  brightness, dimBrightness;
  uint16_t screenTimeout;
  bool     warnWake = false;   // a new warning switches the screen on
  bool     builtin[BP_COUNT];
  // Titles of the fixed pages (index BP_*) and of the system page (index BP_COUNT), "" = default
  String   pageName[BP_COUNT + 1];
  // Icons of the fixed pages and the system page: key from icons.cpp, "" = default icon
  String   pageIcon[BP_COUNT + 1];
  // Overview tiles in order, comma separated (max. 9): water grey gas heater level fan temp bat
  // status bat1 bat2 bat3 clock none t<n> (custom tile n). "" = default layout
  String   ovTiles;
  String   webTiles;        // home of the web page, WEB_TILE_SLOTS tokens ("" = DEFAULT_WEB_TILES)
  // Icon bar: up to NAV_FREE pages between Home and System, comma separated page ids
  // (PAGE_IDS or p<n> for user page n). "" = the first NAV_FREE pages
  String   navSel;
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
  void saveAwning();           // awning switch only
  void factoryReset();
  // pages + tiles <-> JSON (shared by web API and storage)
  String pagesToJson();
  bool   pagesFromJson(const String& json, String& err, bool apply = true);   // apply=false: validate only
}
