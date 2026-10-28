#include "icons.h"
#include "settings.h"

namespace icons {

// Keep in sync with ICONS in web/index.html and the glyph list in tools/make_fonts.py
const Icon LIST[] = {
  {"bat",    "\xEF\x89\x80"},   // battery-full
  {"carbat", "\xEF\x97\x9F"},   // car-battery
  {"home",   "\xEF\x80\x95"},   // home
  {"sun",    "\xEF\x86\x85"},   // sun
  {"solar",  "\xEF\x96\xBA"},   // solar-panel
  {"car",    "\xEF\x86\xB9"},   // car
  {"plug",   "\xEF\x87\xA6"},   // plug (shore power)
  {"bolt",   "\xEF\x83\xA7"},   // bolt
  {"therm",  "\xEF\x8B\x89"},   // thermometer-half
  {"fridge", "\xEF\x9D\xAB"},   // temperature-low
  {"snow",   "\xEF\x8B\x9C"},   // snowflake
  {"couch",  "\xEF\x92\xB8"},   // couch (inside)
  {"tree",   "\xEF\x86\xBB"},   // tree (outside)
  {"bed",    "\xEF\x88\xB6"},   // bed
  {"water",  "\xEF\x81\x83"},   // tint
  {"fire",   "\xEF\x81\xAD"},   // fire
  {"shower", "\xEF\x8B\x8C"},   // shower
  {"box",    "\xEF\x91\xA6"},   // box (storage, garage)
};
const size_t COUNT = sizeof(LIST) / sizeof(LIST[0]);

bool known(const String& key) {
  for (size_t i = 0; i < COUNT; i++) if (key == LIST[i].key) return true;
  return false;
}

String autoKey(const String& name, bool temp) {
  String n = name;
  n.toLowerCase();   // ASCII only; umlauts are matched below in both spellings
  auto has = [&](const char* k) { return n.indexOf(k) >= 0; };
  if (temp) {
    if (has("gefrier") || has("tiefk") || has("frost") || has("freez")) return "snow";
    if (has("kühl") || has("kuehl") || has("fridge")) return "fridge";
    if (has("außen") || has("aussen") || has("draußen") || has("outdoor") || has("outside")) return "tree";
    if (has("bad") || has("dusch") || has("wc")) return "shower";
    if (has("schlaf") || has("bett") || has("alkoven")) return "bed";
    if (has("wasser") || has("boiler") || has("tank")) return "water";
    if (has("heiz") || has("truma")) return "fire";
    if (has("garage") || has("stau") || has("keller")) return "box";
    if (has("innen") || has("wohn") || has("raum") || has("inside")) return "couch";
    if (has("motor") || has("fahrer") || has("cockpit")) return "car";
    return "therm";
  }
  if (has("solar") || has("pv")) return "sun";
  if (has("start") || has("motor") || has("fahrzeug") || has("kfz") || has("auto") || has("lkw")) return "car";
  if (has("land") || has("230")) return "plug";
  if (has("aufbau") || has("wohn") || has("bord") || has("versorg") || has("house") || has("service")) return "home";
  return "bat";
}

std::vector<Slot> slots(bool temp) {
  std::vector<Slot> out;
  const String& s = temp ? settings.ovTempSel : settings.ovBatSel;
  int pos = 0;
  while (pos < (int)s.length() && out.size() < 3) {
    int end = s.indexOf('\n', pos);
    if (end < 0) end = s.length();
    String line = s.substring(pos, end);
    pos = end + 1;
    int eq = line.lastIndexOf('=');
    Slot sl;
    sl.name = eq >= 0 ? line.substring(0, eq) : line;
    sl.key = eq >= 0 ? line.substring(eq + 1) : String("");
    if (!known(sl.key)) sl.key = "";
    if (sl.name.length()) out.push_back(sl);
  }
  return out;
}

String keyFor(const String& name, bool temp) {
  for (auto& sl : slots(temp))
    if (sl.name == name && sl.key.length()) return sl.key;
  return autoKey(name, temp);
}

const char* glyph(const String& key) {
  for (size_t i = 0; i < COUNT; i++) if (key == LIST[i].key) return LIST[i].glyph;
  return LIST[0].glyph;
}

const char* forName(const String& name, bool temp) { return glyph(keyFor(name, temp)); }

String clean(const String& raw) {
  String out;
  int pos = 0, n = 0;
  while (pos < (int)raw.length() && n < 3) {
    int end = raw.indexOf('\n', pos);
    if (end < 0) end = raw.length();
    String line = raw.substring(pos, end);
    pos = end + 1;
    int eq = line.lastIndexOf('=');
    String name = eq >= 0 ? line.substring(0, eq) : line, key = eq >= 0 ? line.substring(eq + 1) : String("");
    name.trim();
    key.trim();
    if (name.isEmpty() || name.length() > 40) continue;
    if (!known(key)) key = "";
    out += name + "=" + key + "\n";
    n++;
  }
  return out;
}

}  // namespace icons
