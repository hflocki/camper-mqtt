#pragma once
#include <Arduino.h>
#include <vector>

// Own Zigbee and Bluetooth sensors, read straight from MQTT:
//  - Zigbee gateway publishing ZCL attributes (e.g. SMLIGHT hub): <zbBase>/<id>/1/<cluster>/<attribute>
//      payload {"data":{"val":..,"type_sm":"report"|"report_raw"}} or a plain number.
//      "report" values are already scaled by the hub (°C, %, kPa), raw ones use the dividers below.
//      0006/0000 on/off = contact (0 closed, 1 open), 0001/0021 battery (raw 0..200 = 0..100 %),
//      0402/0000 temperature, 0405/0000 humidity, 0403/0000 pressure, 0008/0000 light level (1..254)
//    Lights are switched through the hub's command topics <zbCmd>/<ieee>/1/<cluster>/<command>,
//    <zbCmd> = zbBase without the trailing "/data" (SMLIGHT: camper/zigbee/data -> camper/zigbee/cmd)
//  - OpenMQTTGateway BLE: <bleBase>/<MAC> JSON with tempc, hum, pres, batt (Govee, Ruuvi, Xiaomi ...)
// Climate sensors appear on the temperature page/tile, contacts as icon and optional warning.
namespace sensors {

enum Kind : uint8_t { K_CONTACT = 0, K_CLIMATE = 1, K_LIGHT = 2 };
enum Bus : uint8_t { B_ZIGBEE = 0, B_BLE = 1 };
// Pages a sensor is shown on (bit mask, "show" in the JSON)
enum Show : uint8_t { SH_TEMP = 1, SH_BAT = 2, SH_ACT = 4, SH_WX = 8 };
inline uint8_t defaultShow(uint8_t kind) { return kind == K_CLIMATE ? SH_TEMP : SH_ACT; }

struct Device {
  String  name;
  uint8_t bus = B_ZIGBEE;
  uint8_t kind = K_CLIMATE;
  String  addr;              // Zigbee: IEEE "0x00158d..." or short id "9c68"; BLE: MAC "A4C138AEF56D"
  String  alt;               // second Zigbee address (short id or IEEE), optional
  String  icon = "door";     // contacts: icon while open ("" = none)
  bool    warn = false;      // contacts: warning while open
  bool    invert = false;    // contacts: 1 = closed
  uint8_t show = SH_TEMP;    // Show bits
};

struct Value {
  float   temp = NAN, hum = NAN, pres = NAN, batt = NAN;
  int8_t  open = -1;          // contacts: -1 unknown, 0 closed, 1 open; lights: 0 off, 1 on
  int16_t level = -1;         // lights: 1..254, -1 unknown
  String  color;              // lights: last colour sent from here ("#ff8000" or "ct:370"), the hub does not report it
  uint32_t at = 0;            // millis() of the last message, 0 = never
};

void begin();
bool onMessage(const char* topic, const char* payload, unsigned int len);
std::vector<String> topics();          // subscriptions (wildcards on the base topics)

String toJson();                       // {"zbBase":..,"bleBase":..,"tempDiv":10,"devices":[..]}
bool fromJson(const String& json, String& err, bool apply);
void save();
void clear();
String valuesJson();                   // live values for the web page
String unknownJson();                  // addresses seen but not configured (to add them with one click)

// for the UI
struct TempOut { String name; float temp; float hum; bool stale; };
std::vector<TempOut> climate();        // climate sensors shown on the temperature page, with a temperature
bool outdoor(TempOut& out);            // first sensor marked for the weather page (outside temperature)
struct ContactOut { String name; int8_t open; float batt; bool stale; };
std::vector<ContactOut> contacts();    // contacts shown on the actuator page
int openCount();                       // of those, how many are open
struct BattOut { String name; float batt; bool stale; };
std::vector<BattOut> batteries();      // sensors shown on the battery page with their battery level
std::vector<String> activeIcons();     // open contacts with an icon (not shown on the actuator page)
struct LightOut { size_t idx; String name; int8_t on; int16_t level; String color; bool stale; };
std::vector<LightOut> lights();        // lights shown on the actuator page (idx = device index for lightCommand)
// Topic and payload for a light command; key: on ("ON"/"OFF"/"TOGGLE"), level (1..254),
// color ("#rrggbb"), ct (mireds). Updates the shown state at once. false = unknown light / no IEEE address.
bool lightCommand(size_t idx, const String& key, const String& value, String& topic, String& payload);
String warnings();                     // "Heki offen" ... one per line
bool changed();                        // something new since the last call (UI refresh)

}  // namespace sensors
