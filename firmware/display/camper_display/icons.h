#pragma once
#include <Arduino.h>
#include <vector>

// Icons for batteries and temperature sensors. The overview tiles show chosen sensors (up to
// MAX_TEMP_SLOTS, 3 at a time, then the next 3) / up to 3 batteries, each with its own icon
// (settings.ovTempSel / ovBatSel, one "name=key" per line, key "" = automatic from the name).
// Without a choice: all temperature sensors, the first 3 batteries.
namespace icons {

struct Slot { String name, key; };
const int MAX_TEMP_SLOTS = 6;

struct Icon {
  const char* key;     // stored in the settings
  const char* glyph;   // Font Awesome glyph (UTF-8) for the display
};

extern const Icon LIST[];
extern const size_t COUNT;

bool known(const String& key);
String autoKey(const String& name, bool temp);   // icon from the name ("Kühlschrank" -> fridge ...)
std::vector<Slot> slots(bool temp);             // chosen entries of the overview tile
String keyFor(const String& name, bool temp);    // icon of the slot with this name, or autoKey()
const char* glyph(const String& key);
const char* forName(const String& name, bool temp);
String clean(const String& raw, int maxN = 3);   // validate "name=key" lines from the web page

}  // namespace icons
