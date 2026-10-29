#pragma once
#include <Arduino.h>
#include <vector>

// Own Zigbee and Bluetooth sensors, read straight from MQTT:
//  - Zigbee gateway publishing raw ZCL attributes (e.g. SMLIGHT): <zbBase>/<id>/1/<cluster>/<attribute>
//      0006/0000 on/off = contact (0 closed, 1 open), 0001/0021 battery (0..200 = 0..100 %),
//      0402/0000 temperature, 0405/0000 humidity, 0403/0000 pressure (hPa)
//  - OpenMQTTGateway BLE: <bleBase>/<MAC> JSON with tempc, hum, pres, batt (Govee, Ruuvi, Xiaomi ...)
// Climate sensors appear on the temperature page/tile, contacts as icon and optional warning.
namespace sensors {

enum Kind : uint8_t { K_CONTACT = 0, K_CLIMATE = 1 };
enum Bus : uint8_t { B_ZIGBEE = 0, B_BLE = 1 };

struct Device {
  String  name;
  uint8_t bus = B_ZIGBEE;
  uint8_t kind = K_CLIMATE;
  String  addr;              // Zigbee: IEEE "0x00158d..." or short id "9c68"; BLE: MAC "A4C138AEF56D"
  String  alt;               // second Zigbee address (short id or IEEE), optional
  String  icon = "door";     // contacts: icon while open ("" = none)
  bool    warn = false;      // contacts: warning while open
  bool    invert = false;    // contacts: 1 = closed
};

struct Value {
  float   temp = NAN, hum = NAN, pres = NAN, batt = NAN;
  int8_t  open = -1;          // contacts: -1 unknown, 0 closed, 1 open
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
std::vector<TempOut> climate();        // configured climate sensors with a temperature
std::vector<String> activeIcons();     // open contacts with an icon
String warnings();                     // "Heki offen" ... one per line
bool changed();                        // something new since the last call (UI refresh)

}  // namespace sensors
