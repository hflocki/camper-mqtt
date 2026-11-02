#pragma once
#include <Arduino.h>
#include <vector>

// Departure check: detects driving from the router's GPS and warns while the door is open,
// the awning is out or an own contact sensor (window, roof vent) reports open.
//  - GPS: JSON topic (RUT956: camper/rut with GPS.speed in km/h and GPS.fix_status), keys are
//    searched in nested objects, numbers may come as strings.
//    Driving = speed above settings.dpSpeed for settings.dpSecs seconds with a GPS fix,
//    it ends after STOP_MS at or below the limit or when the GPS goes quiet.
//  - Door: JSON (RUT956 digital input {"Door":{"state":"open"|"close",..}}) or a plain payload
//    open/closed/ON/OFF/1/0/true/false; settings.dpDoorInv swaps open and closed.
// Settings: web page Einstellungen > Dienste > Warnungen > Abfahrtscheck.
namespace depart {

bool onMessage(const char* topic, const char* payload, unsigned int len);   // true = handled
std::vector<String> topics();   // subscriptions
void loop();                    // driving state machine, call often

bool   driving();
float  speed();                 // km/h of the last GPS message, NAN = no fix / no data
bool   gpsFresh();
int8_t door();                  // -1 unknown / no topic, 0 closed, 1 open

// One line of the check list. ok: 1 fine, 0 problem, -1 unknown
struct Item { String name; String state; int8_t ok; };
std::vector<Item> items();
String issues();                // problems as one line "Tür offen, Markise ausgefahren", "" = none
String warnings();              // while driving: one line per problem (own warning keys)
bool   changed();               // driving or issues changed since the last call (publish, UI)

}  // namespace depart
