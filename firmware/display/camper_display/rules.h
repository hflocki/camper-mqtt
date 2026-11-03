#pragma once
#include <Arduino.h>
#include <vector>

// Simple automations: "when <topic/key> <condition> <value>, send <on payload>; when it is no longer true,
// send <off payload> after a delay". Examples: entrance light on while the door is open, awning light.
// Rules only act on changes, so lights can still be switched by hand (tile, app, wall switch).
namespace rules {

enum Cond : uint8_t { C_EQ = 0, C_NE = 1, C_LT = 2, C_GT = 3 };
enum When : uint8_t { W_ALWAYS = 0, W_TIME = 1, W_DARK = 2 };

struct Rule {
  String  name;
  bool    on = true;           // rule enabled
  // trigger
  String  inTopic, inKey;      // key "" = whole payload, "a.b" = nested JSON
  uint8_t cond = C_EQ;
  String  inVal;               // compared text (case ignored) or number for < / >
  // action
  String  outTopic;            // e.g. zigbee2mqtt/light/set, "" = no action (only icon / warning)
  String  onPayload;           // sent when the condition becomes true
  String  offPayload;          // sent when it is no longer true, "" = nothing
  uint16_t offDelay = 10;      // seconds
  bool    onlyOwn = true;      // switch off only what this rule switched on
  // when the "on" action may run (the off action always may)
  uint8_t when = W_ALWAYS;
  String  from = "19:00", to = "07:00";
  // shown while the condition is true
  String  icon;                // icons.cpp key, "" = none (status tile on display and web page)
  bool    warn = false;        // also as warning (status tile, banner, Pushover)
};

void begin();                 // load from NVS
void loop();                  // delayed off actions
bool onMessage(const char* topic, const char* payload, unsigned int len);
std::vector<String> topics(); // MQTT topics to subscribe

String toJson();              // {"rules":[...]} for the web page
bool fromJson(const String& json, String& err, bool apply);
void save();
void clear();                 // factory reset
String statusJson();          // live state of every rule for the web page

std::vector<String> activeIcons();   // icon keys of true rules
String warnings();                   // names of true rules with warn, one per line

}  // namespace rules
