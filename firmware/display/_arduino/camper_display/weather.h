#pragma once
#include <Arduino.h>

// Weather from Open-Meteo (free, no API key) for a fixed place or the position from an MQTT topic
// (e.g. the GPS of the router). Fetched in a background task so the UI never blocks.
namespace wx {

const int DAYS = 4;            // today + 3

struct Data {
  bool   valid = false;
  time_t at = 0;               // time of the last successful update (0 = never)
  // current
  float  temp = NAN, feels = NAN, hum = NAN, wind = NAN, gust = NAN;
  int    dir = -1, code = -1;
  bool   day = true;
  // next hour
  float  rainNext = NAN, popNext = NAN;
  // daily, [0] = today
  int    dCode[DAYS] = {-1, -1, -1, -1};
  float  tMax[DAYS], tMin[DAYS], rainSum[DAYS], popMax[DAYS], gustMax[DAYS];
  float  lat = NAN, lon = NAN;  // position the data is for
};

void begin();
void loop();                    // starts fetches, reads the position topic
Data get();                     // copy of the latest data (thread safe)
bool enabled();
String status();                // "Stand 07:30", "Kein Standort", error text ...
void requestUpdate();           // fetch as soon as possible (settings changed)
// MQTT position topic, called by the MQTT client for every message
bool onMessage(const char* topic, const char* payload, unsigned int len);

// helpers for UI and web page
const char* icon(int code, bool day);      // Font Awesome glyph (UTF-8)
const char* text(int code);                // German description
const char* dirText(int deg);              // N, NO, O ...
String warning();                          // "Wind: Böen bis 62 km/h", "Gewitter", "" = none
String json();                             // current data for the web page

}  // namespace wx
