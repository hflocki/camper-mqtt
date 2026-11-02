#pragma once
#include <Arduino.h>

// Built-in limit warnings (fresh water, gas, frost) and the hysteresis helper used by all
// threshold warnings, so a value hovering around a limit does not switch a warning on and off.
// Limits and hysteresis are in the settings (web page: Einstellungen > Dienste > Warnungen).
namespace alerts {

// Hysteresis per warning key: switches on when `on` is true and off again only when `off`
// is true. Returns the current state. Thread safe.
bool latch(const String& key, bool on, bool off);

// Same for "value below limit": on below `limit`, off at or above `limit + hyst`.
// A limit of 0 (off) or NAN value clears the key and returns false.
bool below(const String& key, float value, float limit, float hyst);
// "value above limit": on above `limit`, off at or below `limit - hyst`
bool above(const String& key, float value, float limit, float hyst);

String warnings();   // one line per active warning, "Frischwasser niedrig: 8 %" ...

}  // namespace alerts
