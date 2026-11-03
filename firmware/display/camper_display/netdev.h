#pragma once
#include <Arduino.h>
#include <vector>

// All devices of the camper network, collected from <root>/<id>/status (online/offline,
// last will) and <root>/<id>/info (diagnostics JSON from devinfo.cpp). <root> is the
// first part of the own prefix ("camper" for "camper/display").
namespace netdev {

struct Entry {
  String   id;               // "tanklevel", "heater" ...
  int8_t   online = -1;      // -1 no status seen, 0 offline, 1 online
  bool     hasInfo = false;
  String   version, ip, reset;
  int      rssi = 0;         // dBm, 0 = unknown
  uint32_t uptime = 0;       // s, as reported
  uint32_t infoAt = 0;       // millis() of the last info message
};

std::vector<String> topics();   // subscriptions
bool onMessage(const char* topic, const char* payload, unsigned int len);
std::vector<Entry> list();      // sorted: online first, then by name
String name(const String& id);  // readable name: "Heizung", "Road Buddy" ... or the id
bool infoStale(const Entry& e); // no info for 3 intervals (older firmware or device gone)
String json();                  // for /api/devices
bool changed();                 // something new since the last call (UI refresh)

}  // namespace netdev
