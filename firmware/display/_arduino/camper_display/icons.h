#pragma once
#include <Arduino.h>
#include <vector>

// Icons for batteries and temperature sensors. The overview tiles show up to 3 chosen
// sensors/batteries, each with its own icon (settings.ovTempSel / ovBatSel, one "name=key" per line,
// key "" = automatic from the name). Without a choice the first 3 are shown.
namespace icons {

struct Slot { String name, key; };

struct Icon {
  const char* key;     // stored in the settings
  const char* glyph;   // Font Awesome glyph (UTF-8) for the display
};

extern const Icon LIST[];
extern const size_t COUNT;

bool known(const String& key);
String autoKey(const String& name, bool temp);   // icon from the name ("Kühlschrank" -> fridge ...)
std::vector<Slot> slots(bool temp);             // chosen entries of the overview tile, max. 3
String keyFor(const String& name, bool temp);    // icon of the slot with this name, or autoKey()
const char* glyph(const String& key);
const char* forName(const String& name, bool temp);
String clean(const String& raw);                 // validate "name=key" lines from the web page (max. 3)

}  // namespace icons
