#pragma once
#include <Arduino.h>

// Mobile signal of the router (e.g. RUT956 "Data to Server", type GSM) from an MQTT topic.
// The JSON keys differ between router firmwares, so the usual names are searched:
// rsrp / rssi / signal for the strength, conntype / network_type / nettype / mode / type for the network.
namespace rt {

bool onMessage(const char* topic, const char* payload, unsigned int len);   // true = handled
bool fresh();          // a value arrived within the last few minutes
int bars();            // 0..5
String type();         // "LTE", "5G", "3G", "2G" or ""

}  // namespace rt
