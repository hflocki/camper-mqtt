#include "ui.h"
#include "config.h"
#include "settings.h"
#include "datastore.h"
#include "mqtt_client.h"
#include "wifi_manager.h"
#include "display_hw.h"
#include "weather.h"
#include "icons.h"
#include "otaprog.h"
#include "rules.h"
#include "sensors.h"
#include <lvgl.h>
#include <vector>
#include <algorithm>
#include <time.h>
#include <ArduinoJson.h>

LV_FONT_DECLARE(font_de_14);
LV_FONT_DECLARE(font_de_18);
LV_FONT_DECLARE(font_de_22);
LV_FONT_DECLARE(font_de_28);
LV_FONT_DECLARE(font_de_56);

// Icons that are part of the generated fonts (Font Awesome 5)
#define ICON_FIRE         "\xEF\x81\xAD"
#define ICON_GAS          "\xEF\x94\xAF"
#define ICON_BALANCE      "\xEF\x89\x8E"
#define ICON_TINT         "\xEF\x81\x83"
#define ICON_FAN          "\xEF\x9C\xAE"
#define ICON_DRIP         "\xEF\x95\xB6"
#define ICON_THERMOMETER  "\xEF\x8B\x89"   // thermometer-half
#define ICON_CLOUD_SUN    "\xEF\x9B\x84"
#define ICON_MARKER       "\xEF\x8F\x85"
#define ICON_UMBRELLA     "\xEF\x83\xA9"
#define ICON_WIND         "\xEF\x9C\xAE"

namespace {

// ---- colours ---------------------------------------------------------------
const lv_color_t C_BG      = lv_color_hex(0x0F1419);
const lv_color_t C_CARD    = lv_color_hex(0x1B232B);
const lv_color_t C_CARD_HI = lv_color_hex(0x26313B);
const lv_color_t C_TEXT    = lv_color_hex(0xE8EDF2);
const lv_color_t C_MUTED   = lv_color_hex(0x8696A6);
const lv_color_t C_WATER   = lv_color_hex(0x3BA3E8);
const lv_color_t C_GREY    = lv_color_hex(0xA88B6A);
const lv_color_t C_GAS     = lv_color_hex(0xF0A030);
const lv_color_t C_HEAT    = lv_color_hex(0xF06A3B);
const lv_color_t C_OK      = lv_color_hex(0x4CC27A);
const lv_color_t C_WARN    = lv_color_hex(0xE5484D);
const lv_color_t C_ACCENT  = lv_color_hex(0x5AB0F0);
const lv_color_t C_BANNER  = lv_color_hex(0xF2C94C);   // warning banner (yellow) ...
const lv_color_t C_ONBANNER = lv_color_hex(0x1A1F24);  // ... and its text
const lv_color_t C_NAV     = lv_color_hex(0x161D24);

const int HDR_H = 40;                   // header: page title left, date + clock right
const int NAV_H = 56;                   // icon bar at the bottom, one button per page
const int PAGE_W = LCD_WIDTH;
const int PAGE_H = LCD_HEIGHT - HDR_H - NAV_H;
const int TITLE_X = 14;

// ---- page bookkeeping ----------------------------------------------------
enum : int { PG_SYSTEM = 100 };
struct PageInfo { String title; int builtin; int custom; };
std::vector<PageInfo> pages;

lv_obj_t* scr = nullptr;
lv_obj_t* tv = nullptr;
lv_obj_t* hdrTitle = nullptr;
lv_obj_t* hdrClock = nullptr;
lv_obj_t* hdrName = nullptr;    // optional display name between page title and clock
lv_obj_t* hdrStatus = nullptr;
lv_obj_t* hdrDate = nullptr;
lv_obj_t* hdr = nullptr;
lv_obj_t* hdrWarn = nullptr;            // scrolling warning text, replaces the title while warnings are active
lv_obj_t* nav = nullptr;
std::vector<lv_obj_t*> navBtns;
std::vector<int> navTabs;               // tab index of each button in the icon bar

bool rebuildPending = false;
uint32_t lastHeader = 0;
uint32_t lastValues = 0;

// ---- widget handles ------------------------------------------------------
struct TankW { lv_obj_t* pct = nullptr; lv_obj_t* sub = nullptr; lv_obj_t* bar = nullptr; };
TankW ovWater, ovGrey, ovGas;
lv_obj_t *ovHeatVal, *ovHeatSub, *ovLvlVal, *ovLvlSub, *ovFanVal, *ovFanSub;

lv_obj_t *hRoom, *hWater, *hSet, *hPower, *hPowerLbl, *hFanBm, *hWaterBm, *hStatus, *hDecoke, *hDecokeLbl;
lv_obj_t *fStatus, *fSpeed, *fSpeedLbl;
lv_obj_t *lvArea, *lvBubble, *lvZone, *lvPitch, *lvRoll, *lvState, *lvWedge[4];
lv_obj_t *sIp, *sNet, *sQr, *sQrLbl, *sBri, *sInfo;
std::vector<lv_obj_t*> sVals;           // system page: value column, order of SYS_ROWS

// weather page
lv_obj_t *wIcon, *wTemp, *wText, *wFeels, *wPlace, *wStatus;
struct SmallW { lv_obj_t *val = nullptr, *sub = nullptr; };
SmallW wWind, wHum, wNext, wToday;
struct DayW { lv_obj_t *day = nullptr, *icon = nullptr, *temp = nullptr, *rain = nullptr; };
DayW wDays[3];
time_t wShown = -1;                     // data time shown on the weather page / tile
String wShownStatus;
lv_obj_t *ovWxIcon = nullptr, *ovWxVal = nullptr, *ovWxSub = nullptr;

// warnings (overview banner) and temperatures (camper-temp)
lv_obj_t *ovWarn, *ovWarnLbl, *ovInd = nullptr;
// overview tiles with up to three single lines (temperatures, batteries)
struct ListW { lv_obj_t* line[3] = {nullptr, nullptr, nullptr}; lv_obj_t* sub = nullptr; };
ListW ovTemp, ovBat, ovAct, ovLight, ovSw;
int ovTempPage = 0;                     // temperature tile: which 3 of more chosen sensors are shown
uint32_t ovTempFlip = 0;
const uint32_t TEMP_FLIP_MS = 4000;
// optional overview tiles: one battery each, clock, custom MQTT tiles
struct OneW { lv_obj_t *title = nullptr, *val = nullptr, *sub = nullptr; int idx = 0; };
std::vector<OneW> ovOneBat, ovCustom;
OneW ovClock;
lv_obj_t *tStatus;
std::vector<lv_obj_t*> tRows, tVals;
// battery page: one card per battery
struct BatRowW { lv_obj_t *card, *soc, *top, *sub, *bar; };
std::vector<BatRowW> bRows;
lv_obj_t* bStatus = nullptr;
lv_obj_t *bSensCard = nullptr, *bSensLbl = nullptr;   // battery levels of own Zigbee/BLE sensors
// Device pages (Sensoren, Licht, Schalter, Energie, Extra): the same builder for all of them.
// Lights and switches get a card with a switch, contacts and climate sensors a row with their value.
struct LightW { lv_obj_t *card, *sw, *slider, *lvl; size_t dev; };
struct RowW { lv_obj_t *card, *val, *sub; size_t dev; };
struct DevPageW { int bp; uint8_t bit; lv_obj_t* status; String sig; std::vector<LightW> cards; std::vector<RowW> rows; };
DevPageW devPages[] = {{BP_ACTORS, sensors::SH_ACT, nullptr, "", {}, {}}, {BP_LIGHT, sensors::SH_LIGHT, nullptr, "", {}, {}},
                       {BP_SWITCH, sensors::SH_SWITCH, nullptr, "", {}, {}}, {BP_EXTRA1, sensors::SH_X1, nullptr, "", {}, {}},
                       {BP_EXTRA2, sensors::SH_X2, nullptr, "", {}, {}}};
// colour buttons: key "color" with #rrggbb or "ct" with mireds; shown colour of the button
struct Swatch { const char* key; const char* val; uint32_t show; const char* text; };
const Swatch SWATCHES[] = {
  {"ct", "370", 0xFFC27A, "Warm"}, {"ct", "153", 0xE6F0FF, "Kalt"}, {"color", "#ff0000", 0xFF2020, ""},
  {"color", "#ff8000", 0xFF8A00, ""}, {"color", "#00ff00", 0x30D050, ""}, {"color", "#0040ff", 0x2F6BFF, ""},
  {"color", "#a000ff", 0xA040FF, ""}};
const int SWATCH_N = sizeof(SWATCHES) / sizeof(SWATCHES[0]);
lv_obj_t *wnCard = nullptr, *wnLbl = nullptr;
lv_obj_t* wOut = nullptr;               // weather page: outside sensor
String bSig;
String tSig;          // sensor names the rows were built for
String lastWarn;      // to wake the screen when a new warning appears
String lastWarnKeys;  // warning identities (text before ':'), values excluded

// "Kühlschrank zu warm: 9,2 °C\nHeizung: Fehler 12" -> "Kühlschrank zu warm\nHeizung"
String warnKeys(const String& all) {
  String keys;
  int start = 0;
  while (start < (int)all.length()) {
    int nl = all.indexOf('\n', start);
    if (nl < 0) nl = all.length();
    String l = all.substring(start, nl);
    int c = l.indexOf(':');
    if (c > 0) l = l.substring(0, c);
    keys += l;
    keys += '\n';
    start = nl + 1;
  }
  return keys;
}

// true if `now` contains a warning that is not in `before` (both one key per line, before with a leading '\n')
bool newWarning(const String& now, const String& before) {
  int start = 0;
  while (start < (int)now.length()) {
    int nl = now.indexOf('\n', start);
    if (nl < 0) break;
    String k = now.substring(start, nl + 1);
    if (before.indexOf("\n" + k) < 0) return true;   // before starts with '\n', so whole lines match
    start = nl + 1;
  }
  return false;
}

struct TileW { lv_obj_t* val = nullptr; lv_obj_t* sw = nullptr; lv_obj_t* slider = nullptr; lv_obj_t* sval = nullptr; lv_obj_t* card = nullptr; };
std::vector<TileW> tileW;

// heater setpoint is changed locally first and sent after a short pause
float    pendingSet = NAN;
uint32_t pendingAt = 0;
uint32_t pendingSentAt = 0;
const uint32_t SETPOINT_DEBOUNCE_MS = 1200;
const uint32_t SETPOINT_HOLD_MS = 6000;

String lastQr;

// ---- helpers ---------------------------------------------------------------
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t col, const char* txt = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, col, 0);
  lv_label_set_text(l, txt);
  return l;
}

lv_obj_t* card(lv_obj_t* parent, int w, int h) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, C_CARD, 0);
  lv_obj_set_style_bg_color(c, C_CARD_HI, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, 16, 0);
  lv_obj_set_style_pad_all(c, 12, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t* button(lv_obj_t* parent, const char* txt, int w, int h, lv_event_cb_t cb, void* ud,
                 lv_color_t bg = C_CARD_HI, const lv_font_t* font = &font_de_22) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = label(b, font, C_TEXT, txt);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

lv_obj_t* pageContainer(lv_obj_t* tab) {
  lv_obj_set_style_pad_all(tab, 10, 0);
  lv_obj_set_style_pad_gap(tab, 10, 0);
  lv_obj_set_style_bg_color(tab, C_BG, 0);
  lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(tab, LV_DIR_VER);
  return tab;
}

// German umlauts are in the fonts; replace only characters we do not have.
String dispText(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (c < 0x80) { out += (char)c; continue; }
    // copy known 2-byte sequences, replace the rest with '?'
    if (c == 0xC2 || c == 0xC3) {
      if (i + 1 < s.length()) {
        uint8_t n = s[i + 1];
        bool ok = (c == 0xC2 && (n == 0xB0 || n == 0xB7)) ||
                  (c == 0xC3 && (n == 0x84 || n == 0x96 || n == 0x9C || n == 0x9F || n == 0xA4 || n == 0xB6 || n == 0xBC));
        if (ok) { out += (char)c; out += (char)n; i++; continue; }
      }
    }
    // 3-byte sequences in the fonts: en dash, bullet, ellipsis and the icon range U+F000-U+FFFF
    if (i + 2 < s.length() && ((c == 0xE2 && (uint8_t)s[i + 1] == 0x80 &&
                                 ((uint8_t)s[i + 2] == 0x93 || (uint8_t)s[i + 2] == 0xA2 || (uint8_t)s[i + 2] == 0xA6)) ||
                                c == 0xEF)) {
      out += (char)c; out += s[i + 1]; out += s[i + 2]; i += 2; continue;
    }
    // skip continuation bytes of an unsupported character
    while (i + 1 < s.length() && ((uint8_t)s[i + 1] & 0xC0) == 0x80) i++;
    out += '?';
  }
  return out;
}

String fmt(const String& v, int decimals, const char* unit) {
  if (v.isEmpty()) return "--";
  String s = v;
  if (decimals >= 0) {
    char* end;
    float f = strtof(v.c_str(), &end);
    if (end != v.c_str()) s = String(f, (unsigned)decimals);
  }
  if (unit && *unit) { s += " "; s += unit; }
  return s;
}

int tabIndexOfBuiltin(int bp) {
  for (size_t i = 0; i < pages.size(); i++) if (pages[i].builtin == bp) return i;
  return -1;
}

void updateHeaderTitle();

void gotoTab(int idx, bool anim = true) {
  if (!tv || idx < 0 || idx >= (int)pages.size()) return;
  lv_tabview_set_active(tv, idx, anim ? LV_ANIM_ON : LV_ANIM_OFF);
  updateHeaderTitle();   // set_active does not send VALUE_CHANGED, only swiping does
}

void updateHeaderTitle() {
  if (!tv) return;
  uint32_t i = lv_tabview_get_tab_active(tv);
  if (i < pages.size()) lv_label_set_text(hdrTitle, dispText(pages[i].title).c_str());
  // display name in muted text right of the page title, if there is room before the date
  if (hdrName) {
    String name = dispText(settings.displayName);
    lv_label_set_text(hdrName, name.c_str());
    const char* title = lv_label_get_text(hdrTitle);
    int left = TITLE_X + lv_text_get_width(title, strlen(title), &font_de_22, 0) + 12;
    int right = LCD_WIDTH - 196;
    if (name.isEmpty() || right - left < 30) lv_obj_add_flag(hdrName, LV_OBJ_FLAG_HIDDEN);
    else {
      lv_obj_remove_flag(hdrName, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_width(hdrName, right - left);
      lv_obj_align(hdrName, LV_ALIGN_LEFT_MID, left, 2);
    }
  }
  // icon bar: highlight the active page (none if it is not in the bar)
  for (size_t p = 0; p < navBtns.size() && p < navTabs.size(); p++) {
    bool on = navTabs[p] == (int)i;
    lv_obj_set_style_bg_color(navBtns[p], on ? C_CARD_HI : C_NAV, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(navBtns[p], 0), on ? C_ACCENT : C_MUTED, 0);
    lv_obj_set_style_border_width(navBtns[p], on ? 3 : 0, 0);
  }
}

// ---- events -----------------------------------------------------------------
void onTabChanged(lv_event_t*) { updateHeaderTitle(); }
void onNav(lv_event_t* e) { gotoTab((int)(intptr_t)lv_event_get_user_data(e)); }
void onDhcp(lv_event_t*) {
  Serial.println("[wifi] static IP switched off on the panel, restarting with DHCP");
  settings.staticIp = false;
  cfg::save();
  delay(300);
  ESP.restart();
}
bool hdrLong = false;   // header was long pressed: screen off when the finger is lifted
void onHome(lv_event_t*) {
  if (hdrLong) return;
  int ov = tabIndexOfBuiltin(BP_OVERVIEW);
  gotoTab(ov >= 0 ? ov : 0);
}
void onHeaderLong(lv_event_t*) { hdrLong = true; }
// Switch off on release, otherwise the finger still on the glass would wake it again at once
void onHeaderReleased(lv_event_t*) {
  if (!hdrLong) return;
  hdrLong = false;
  Serial.println("[disp] screen off: header long press");
  disp::sleep();
}
void onScreenOff(lv_event_t*) {
  Serial.println("[disp] screen off: button");
  disp::sleep();
}
void onOverviewCard(lv_event_t* e) { gotoTab(tabIndexOfBuiltin((int)(intptr_t)lv_event_get_user_data(e))); }

void onSetpoint(lv_event_t* e) {
  int delta = (int)(intptr_t)lv_event_get_user_data(e);
  float base = !isnan(pendingSet) ? pendingSet : store::getf(DP_HEAT_SETPOINT, NAN);
  if (isnan(base)) base = store::lastHeaterSetpoint();
  if (isnan(base)) base = 20;
  pendingSet = constrain(roundf(base) + delta, 5.0f, 30.0f);
  pendingAt = millis();
  pendingSentAt = 0;
  lv_label_set_text_fmt(hSet, "%d \xC2\xB0""C", (int)pendingSet);
}

void onPower(lv_event_t*) {
  bool on = store::get(DP_HEAT_HVAC) == "heat";
  cmd::heater("hvac_mode", on ? "off" : "heat");
}

void onHeaterFan(lv_event_t* e) {
  lv_obj_t* bm = (lv_obj_t*)lv_event_get_target(e);
  uint32_t id = lv_buttonmatrix_get_selected_button(bm);
  static const char* modes[] = {"eco", "high"};
  if (id < 2) cmd::heater("fan_mode", modes[id]);
}

void onHeaterWater(lv_event_t* e) {
  lv_obj_t* bm = (lv_obj_t*)lv_event_get_target(e);
  uint32_t id = lv_buttonmatrix_get_selected_button(bm);
  static const char* modes[] = {"off", "eco", "high"};
  if (id < 3) cmd::heater("water_mode", modes[id]);
}

void onDecoke(lv_event_t*) {
  bool on = store::get(DP_HEAT_DECOKE) == "ON";
  cmd::heater("decoke", on ? "OFF" : "ON");
}

void onFanBtn(lv_event_t* e) { cmd::fan((const char*)lv_event_get_user_data(e)); }

void onFanSpeed(lv_event_t*) {
  int v = lv_slider_get_value(fSpeed);
  lv_label_set_text_fmt(fSpeedLbl, "Stufe %d", v);
  cmd::fan("speed", String(v));
}
void onFanSpeedDrag(lv_event_t*) { lv_label_set_text_fmt(fSpeedLbl, "Stufe %d", (int)lv_slider_get_value(fSpeed)); }

void onTare(lv_event_t*) { cmd::level("tare"); }

void onBrightness(lv_event_t*) {
  disp::setBrightness(lv_slider_get_value(sBri));
}
void onBrightnessDone(lv_event_t*) {
  disp::setBrightness(lv_slider_get_value(sBri));
  cfg::save();
  mqttc::publishSelfState();
}

void onTileSwitch(lv_event_t* e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  cmd::tile(i, lv_obj_has_state(sw, LV_STATE_CHECKED) ? "on" : "off");
}
void onTileButton(lv_event_t* e) { cmd::tile((size_t)(intptr_t)lv_event_get_user_data(e), "press"); }
void onTileSlider(lv_event_t* e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= tileW.size()) return;
  int32_t v = lv_slider_get_value(tileW[i].slider);
  const Tile& t = settings.tiles[i];
  float val = t.min + v * t.step;
  cmd::tile(i, String(val, 2));
}
void onTileSliderDrag(lv_event_t* e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= tileW.size()) return;
  const Tile& t = settings.tiles[i];
  float val = t.min + lv_slider_get_value(tileW[i].slider) * t.step;
  int dec = t.step >= 1 ? 0 : 1;
  lv_label_set_text(tileW[i].sval, fmt(String(val, dec), dec, t.unit.c_str()).c_str());
}

// ---- temperatures / warnings -----------------------------------------------
struct TempItem { String name; String val; uint8_t alarm; };

// <temp prefix>/sensors: [{"id":"...","name":"...","t":4.31,"alarm":0}, ...], t = null on error
std::vector<TempItem> tempItems() {
  std::vector<TempItem> out;
  if (!store::has(DP_TEMP_LIST)) return out;
  JsonDocument doc;
  if (deserializeJson(doc, store::get(DP_TEMP_LIST))) return out;
  for (JsonObject o : doc.as<JsonArray>()) {
    TempItem it;
    it.name = (const char*)(o["name"] | "?");
    it.val = o["t"].isNull() ? String("--") : String(o["t"].as<float>(), 1) + " \xC2\xB0""C";
    it.alarm = o["alarm"] | 0;
    out.push_back(it);
  }
  return out;
}

// camper-temp sensors plus own Zigbee/BLE climate sensors (sensors.cpp)
std::vector<TempItem> allTempItems() {
  std::vector<TempItem> out = tempItems();
  for (auto& c : sensors::climate()) {
    TempItem it;
    it.name = c.name;
    it.val = String(c.temp, 1) + " \xC2\xB0""C";
    it.alarm = 0;
    out.push_back(it);
  }
  return out;
}

void refreshTempTile();

// ---- batteries (camper-battery) ---------------------------------------------
struct BatItem { String name; String state; String ptext; float soc, v, w; long rt; bool ok, problem; };

// icon of the overview slot, or picked from the name (living area, solar, starter ...)
const char* batIcon(const String& name) { return icons::forName(name, false); }
const char* tempIcon(const String& name) { return icons::forName(name, true); }

// <battery prefix>/batteries: [{"name":"...","ok":true,"soc":78,"v":13.31,"w":-56,"rt":2830,"st":"Normal","p":true}, ...]
std::vector<BatItem> batItems() {
  std::vector<BatItem> out;
  if (!store::has(DP_BAT_LIST)) return out;
  JsonDocument doc;
  if (deserializeJson(doc, store::get(DP_BAT_LIST))) return out;
  for (JsonObject o : doc.as<JsonArray>()) {
    BatItem b;
    b.name = (const char*)(o["name"] | "?");
    b.state = (const char*)(o["st"] | "");
    b.ok = o["ok"] | false;
    b.soc = o["soc"].is<float>() ? o["soc"].as<float>() : NAN;
    b.v = o["v"].is<float>() ? o["v"].as<float>() : NAN;
    b.w = o["w"].is<float>() ? o["w"].as<float>() : NAN;
    b.rt = o["rt"].is<long>() ? o["rt"].as<long>() : -1;
    b.problem = o["p"] | false;
    b.ptext = (const char*)(o["pt"] | "");   // camper-battery 1.3.1+: readable BMS problem
    out.push_back(b);
  }
  return out;
}

String commaNum(float v, int dec) {
  String s = String(v, (unsigned)dec);
  s.replace('.', ',');
  return s;
}

// "3,2 Tage", "5 h 20 min", "45 min"
String runtimeText(long min) {
  if (min >= 48 * 60) return commaNum(min / 1440.0f, 1) + " Tage";
  if (min >= 60) return String(min / 60) + " h " + String(min % 60) + " min";
  return String(min) + " min";
}

// All active warnings, one per line ("" = none)
String warnings() {
  String w;
  auto add = [&](const String& s) { if (s.isEmpty()) return; if (w.length()) w += "\n"; w += s; };
  if (store::devOnline(DEV_TEMP)) {
    String a = store::get(DP_TEMP_ALARM);
    a.replace("; ", "\n");
    add(a);
  }
  if (store::devOnline(DEV_HEATER) && store::get(DP_HEAT_ERROR) == "ON")
    add("Heizung: Fehler " + store::get(DP_HEAT_ERRCODE));
  // Batteries: the text before ':' identifies a warning for the push messages
  if (store::devOnline(DEV_BATTERY)) {
    for (auto& b : batItems()) {
      if (!b.ok) continue;
      if (b.problem) add(b.name + " BMS: " + (b.ptext.length() ? b.ptext : String("Fehler, bitte pr\xC3\xBC" "fen")));
      if (b.state == "Kritisch" || b.state == "Schwach")
        add(b.name + " " + (b.state == "Kritisch" ? "kritisch" : "schwach") + ": " + commaNum(b.v, 2) + " V");
      else if (b.state.isEmpty() && !isnan(b.soc) && b.soc < BAT_WARN_SOC)
        add(b.name + " niedrig: " + String((int)lroundf(b.soc)) + " %" +
            (b.rt >= 0 ? ", noch " + runtimeText(b.rt) : String("")));
    }
  }
  // own tiles with a warning rule (e.g. shore power from a Zigbee clamp)
  for (size_t i = 0; i < settings.tiles.size(); i++) {
    const Tile& t = settings.tiles[i];
    if (!t.warnMode && !t.warnStale) continue;
    String head = t.warnText.length() ? t.warnText : t.name;
    if (t.warnStale && store::tileAgeMs(i) > (uint32_t)t.warnStale * 60000UL) {
      add(head + ": keine Daten seit " + String(store::tileAgeMs(i) / 60000UL) + " min");
      continue;
    }
    if (!t.warnMode || !store::tileHas(i)) continue;
    if (t.warnMode == WARN_EQUAL) {
      // text states like a door contact: "open" (received) or "Offen" (shown)
      if (t.warnEq.length() && (store::tileRaw(i).equalsIgnoreCase(t.warnEq) || store::tile(i).equalsIgnoreCase(t.warnEq)))
        add(t.warnText.length() ? t.warnText : t.name + ": " + store::tile(i));
      continue;
    }
    const String& raw = store::tile(i);
    char* end;
    float v = strtof(raw.c_str(), &end);
    if (end == raw.c_str()) continue;   // not a number
    bool hit = t.warnMode == WARN_BELOW ? v < t.warnVal : v > t.warnVal;
    if (!hit) continue;
    if (!t.warnText.length()) head = t.name + (t.warnMode == WARN_BELOW ? " zu niedrig" : " zu hoch");
    add(head + ": " + raw + (t.unit.length() ? " " + t.unit : String("")));
  }
  // automation rules marked as warning, open Zigbee contacts
  add(rules::warnings());
  add(sensors::warnings());
  // weather: gusts above the limit, thunderstorm
  String ww = wx::warning();
  if (ww.length()) add(ww);
  return w;
}

// ---- page builders ---------------------------------------------------------------
// Overview: 3 x 3 tiles that fit on the screen without scrolling
// (480 px - 2 x 10 padding - 2 x 8 gap) / 3 = 148 px wide, (384 - 20 - 16) / 3 = 116 px high
const int TILE_GAP = 8;
const int TILE_PAD = 8;
const int TILE_W = (PAGE_W - 20 - 2 * TILE_GAP) / 3;
const int TILE_H = (PAGE_H - 20 - 2 * TILE_GAP) / 3;
const int TILE_IN = TILE_W - 2 * TILE_PAD;   // usable width inside a tile

lv_obj_t* tile(lv_obj_t* parent, const char* icon, const char* title, int bp) {
  lv_obj_t* c = card(parent, TILE_W, TILE_H);
  lv_obj_set_style_pad_all(c, TILE_PAD, 0);
  lv_obj_set_style_radius(c, 14, 0);
  if (bp >= 0) {
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, onOverviewCard, LV_EVENT_CLICKED, (void*)(intptr_t)bp);
  }
  lv_obj_t* t = label(c, &font_de_14, C_MUTED, (String(icon) + "  " + title).c_str());
  lv_obj_set_size(t, TILE_IN, lv_font_get_line_height(&font_de_14));
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
  return c;
}

lv_obj_t* tankCard(lv_obj_t* parent, const char* icon, const char* title, lv_color_t col, TankW& w) {
  lv_obj_t* c = tile(parent, icon, title, -1);
  w.pct = label(c, &font_de_28, C_TEXT, "--");
  lv_obj_align(w.pct, LV_ALIGN_LEFT_MID, 0, -4);
  w.sub = label(c, &font_de_14, C_MUTED, "");
  lv_obj_align(w.sub, LV_ALIGN_BOTTOM_RIGHT, 0, -16);
  w.bar = lv_bar_create(c);
  lv_obj_set_size(w.bar, TILE_IN, 10);
  lv_obj_align(w.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_bar_set_range(w.bar, 0, 100);
  lv_obj_set_style_bg_color(w.bar, C_CARD_HI, LV_PART_MAIN);
  lv_obj_set_style_bg_color(w.bar, col, LV_PART_INDICATOR);
  return c;
}

lv_obj_t* infoCard(lv_obj_t* parent, const char* icon, const char* title, lv_obj_t*& val, lv_obj_t*& sub, int bp) {
  lv_obj_t* c = tile(parent, icon, title, bp);
  val = label(c, &font_de_28, C_TEXT, "--");
  lv_obj_align(val, LV_ALIGN_LEFT_MID, 0, 0);
  sub = label(c, &font_de_14, C_MUTED, "");
  lv_obj_set_size(sub, TILE_IN, lv_font_get_line_height(&font_de_14));
  lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
  lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  return c;
}

// Tile with up to three single lines ("21,3 °C  Kühlschrank") and a status line at the bottom
void listCard(lv_obj_t* parent, const char* icon, const char* title, ListW& w, int bp) {
  lv_obj_t* c = tile(parent, icon, title, bp);
  int lh = lv_font_get_line_height(&font_de_18);
  for (int i = 0; i < 3; i++) {
    w.line[i] = label(c, &font_de_18, C_TEXT, i == 0 ? "--" : "");
    lv_obj_set_size(w.line[i], TILE_IN, lh);   // fixed height: cut with dots instead of wrapping
    lv_label_set_long_mode(w.line[i], LV_LABEL_LONG_DOT);
    lv_obj_align(w.line[i], LV_ALIGN_TOP_LEFT, 0, 18 + i * lh);
  }
  w.sub = label(c, &font_de_14, C_MUTED, "");
  lv_obj_set_size(w.sub, TILE_IN, lv_font_get_line_height(&font_de_14));
  lv_label_set_long_mode(w.sub, LV_LABEL_LONG_DOT);
  lv_obj_align(w.sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

// Fills the lines of a list tile; `col` per line
void setList(ListW& w, const std::vector<String>& lines, const std::vector<lv_color_t>& cols, const String& subText) {
  for (int i = 0; i < 3; i++) {
    String t = i < (int)lines.size() ? lines[i] : (i == 0 ? String("--") : String(""));
    lv_label_set_text(w.line[i], dispText(t).c_str());
    lv_obj_set_style_text_color(w.line[i], i < (int)cols.size() ? cols[i] : C_MUTED, 0);
  }
  lv_label_set_text(w.sub, dispText(subText).c_str());
}

void buildBattery(lv_obj_t* tab);
void buildBatteryRows(size_t n, lv_obj_t* tab);
const char* builtinIcon(int bp);

void buildOverview(lv_obj_t* tab) {
  pageContainer(tab);
  lv_obj_set_style_pad_gap(tab, TILE_GAP, 0);

  // Tiles in the order set on the web page (Einstellungen -> Display & Seiten)
  String list = settings.ovTiles.length() ? settings.ovTiles : String(DEFAULT_OV_TILES);
  int start = 0, count = 0;
  while (start <= (int)list.length() && count < 9) {
    int c = list.indexOf(',', start);
    String t = list.substring(start, c < 0 ? list.length() : c);
    t.trim();
    start = (c < 0) ? list.length() + 1 : c + 1;
    count++;
    if (t == "water")       tankCard(tab, ICON_TINT, "Frischwasser", C_WATER, ovWater);
    else if (t == "grey")   tankCard(tab, ICON_DRIP, "Grauwasser", C_GREY, ovGrey);
    else if (t == "gas")    tankCard(tab, ICON_GAS, "Gas", C_GAS, ovGas);
    else if (t == "heater") infoCard(tab, ICON_FIRE, pageTitle(BP_HEATER).c_str(), ovHeatVal, ovHeatSub, BP_HEATER);
    else if (t == "level")  infoCard(tab, ICON_BALANCE, pageTitle(BP_LEVEL).c_str(), ovLvlVal, ovLvlSub, BP_LEVEL);
    else if (t == "fan")    infoCard(tab, ICON_FAN, pageTitle(BP_FAN).c_str(), ovFanVal, ovFanSub, BP_FAN);
    else if (t == "temp")   listCard(tab, ICON_THERMOMETER, pageTitle(BP_TEMP).c_str(), ovTemp, BP_TEMP);
    else if (t == "bat")    listCard(tab, LV_SYMBOL_BATTERY_FULL, pageTitle(BP_BATTERY).c_str(), ovBat, BP_BATTERY);
    else if (t == "actors")   listCard(tab, builtinIcon(BP_ACTORS), pageTitle(BP_ACTORS).c_str(), ovAct, BP_ACTORS);
    else if (t == "lights")   listCard(tab, builtinIcon(BP_LIGHT), pageTitle(BP_LIGHT).c_str(), ovLight, BP_LIGHT);
    else if (t == "switches") listCard(tab, builtinIcon(BP_SWITCH), pageTitle(BP_SWITCH).c_str(), ovSw, BP_SWITCH);
    else if (t == "status") {
      // green "OK" or the active warnings on red; tap = warnings page with the full text
      ovWarn = tile(tab, LV_SYMBOL_WARNING, "Status", BP_WARN);
      ovWarnLbl = label(ovWarn, &font_de_14, C_TEXT, "");
      lv_obj_set_width(ovWarnLbl, TILE_IN);
      lv_label_set_long_mode(ovWarnLbl, LV_LABEL_LONG_WRAP);
      lv_obj_align(ovWarnLbl, LV_ALIGN_TOP_LEFT, 0, 20);
      // number of open windows and icons of true automation rules, e.g. an open door
      ovInd = label(ovWarn, &font_de_22, C_ACCENT, "");
      lv_obj_align(ovInd, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    } else if (t == "bat1" || t == "bat2" || t == "bat3") {
      OneW w;
      w.idx = t[3] - '1';
      lv_obj_t* c = infoCard(tab, LV_SYMBOL_BATTERY_FULL, "Batterie", w.val, w.sub, BP_BATTERY);
      w.title = lv_obj_get_child(c, 0);
      ovOneBat.push_back(w);
    } else if (t == "weather") {
      lv_obj_t* c = tile(tab, ICON_CLOUD_SUN, pageTitle(BP_WEATHER).c_str(), BP_WEATHER);
      ovWxIcon = label(c, &font_de_28, C_GAS, "");
      lv_obj_align(ovWxIcon, LV_ALIGN_LEFT_MID, 0, 0);
      ovWxVal = label(c, &font_de_28, C_TEXT, "--");
      lv_obj_align(ovWxVal, LV_ALIGN_LEFT_MID, 44, 0);
      ovWxSub = label(c, &font_de_14, C_MUTED, "");
      lv_obj_set_size(ovWxSub, TILE_IN, lv_font_get_line_height(&font_de_14));
      lv_label_set_long_mode(ovWxSub, LV_LABEL_LONG_DOT);
      lv_obj_align(ovWxSub, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    } else if (t == "clock") {
      lv_obj_t* c = infoCard(tab, LV_SYMBOL_BELL, "Uhr", ovClock.val, ovClock.sub, -1);
      ovClock.title = lv_obj_get_child(c, 0);
    } else if (t.length() > 1 && t[0] == 't' && isdigit(t[1])) {
      int n = t.substring(1).toInt();
      if (n >= (int)settings.tiles.size()) continue;
      OneW w;
      w.idx = n;
      lv_obj_t* c = infoCard(tab, LV_SYMBOL_LIST, settings.tiles[n].name.c_str(), w.val, w.sub, -1);
      w.title = lv_obj_get_child(c, 0);
      ovCustom.push_back(w);
    } else {
      // "none" or unknown: empty slot, keeps the grid position
      lv_obj_t* c = card(tab, TILE_W, TILE_H);
      lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    }
  }
}

void buildBattery(lv_obj_t* tab) {
  pageContainer(tab);
  bStatus = label(tab, &font_de_18, C_MUTED, "");
  lv_obj_set_width(bStatus, PAGE_W - 20);
  bRows.clear();
  bSig = "\x01";   // force a row build on the first refresh
  // battery levels of own sensors (Einstellungen -> Sensoren, page "Batterie"), always last
  bSensCard = card(tab, PAGE_W - 20, LV_SIZE_CONTENT);
  lv_obj_t* t = label(bSensCard, &font_de_18, C_MUTED, "Sensor-Batterien");
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
  bSensLbl = label(bSensCard, &font_de_18, C_TEXT, "");
  lv_obj_set_width(bSensLbl, PAGE_W - 44);
  lv_obj_set_pos(bSensLbl, 0, 28);
  lv_obj_add_flag(bSensCard, LV_OBJ_FLAG_HIDDEN);
}

// (Re)creates one card per battery when the battery list changed
void buildBatteryRows(size_t n, lv_obj_t* tab) {
  for (auto& r : bRows) lv_obj_delete(r.card);
  bRows.clear();
  // up to 2 batteries get roomy cards, from 3 on they shrink so 3 fit on the page without scrolling
  // (page height minus padding and gaps, divided by 3)
  int h = n <= 2 ? 132 : (PAGE_H - 20 - 2 * 10) / 3;
  for (size_t i = 0; i < n; i++) {
    BatRowW r;
    r.card = card(tab, PAGE_W - 20, h);
    lv_obj_t* name = label(r.card, &font_de_22, C_MUTED, "");
    lv_obj_set_width(name, 240);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_user_data(r.card, name);
    r.top = label(r.card, &font_de_22, C_TEXT, "");
    lv_obj_align(r.top, LV_ALIGN_TOP_RIGHT, 0, 0);
    r.soc = label(r.card, &font_de_28, C_TEXT, "--");
    lv_obj_align(r.soc, LV_ALIGN_LEFT_MID, 0, 0);
    r.sub = label(r.card, &font_de_18, C_MUTED, "");
    lv_obj_set_width(r.sub, PAGE_W - 48 - 110);   // room for the SoC on the left
    lv_label_set_long_mode(r.sub, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(r.sub, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(r.sub, LV_ALIGN_RIGHT_MID, 0, 2);
    r.bar = lv_bar_create(r.card);
    lv_obj_set_size(r.bar, PAGE_W - 48, 12);
    lv_obj_align(r.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(r.bar, 0, 100);
    lv_obj_set_style_bg_color(r.bar, C_CARD_HI, LV_PART_MAIN);
    bRows.push_back(r);
  }
  if (bSensCard) lv_obj_move_to_index(bSensCard, -1);
}

void onLightSwitch(lv_event_t* e) {
  size_t dev = (size_t)(intptr_t)lv_event_get_user_data(e);
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  cmd::light(dev, "on", lv_obj_has_state(sw, LV_STATE_CHECKED) ? "ON" : "OFF");
}
void onLightLevel(lv_event_t* e) {
  size_t dev = (size_t)(intptr_t)lv_event_get_user_data(e);
  lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
  cmd::light(dev, "level", String(lv_slider_get_value(sl)));
}
void onLightLevelDrag(lv_event_t* e) {
  for (auto& p : devPages)
    for (auto& r : p.cards)
      if (r.slider && r.slider == lv_event_get_target(e))
        lv_label_set_text_fmt(r.lvl, "%d %%", (int)lroundf(lv_slider_get_value(r.slider) * 100.0f / 254));
}
// user data: device index * 16 + swatch
void onLightSwatch(lv_event_t* e) {
  uint32_t ud = (uint32_t)(intptr_t)lv_event_get_user_data(e);
  const Swatch& sw = SWATCHES[ud % 16];
  cmd::light(ud / 16, sw.key, sw.val);
}

// Card for a light (switch, brightness slider, colour buttons) or a switch (only the switch)
LightW buildLightCard(lv_obj_t* tab, const sensors::DevOut& li) {
  bool full = li.kind == sensors::K_LIGHT;
  LightW w{};
  w.dev = li.idx;
  w.card = card(tab, PAGE_W - 20, full ? 150 : 64);
  lv_obj_t* n = label(w.card, &font_de_22, C_TEXT,
                      dispText(String(icons::glyph(full ? "light" : "plug")) + "  " + li.name).c_str());
  lv_obj_set_width(n, 300);
  lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
  lv_obj_align(n, full ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 0, full ? 4 : 0);
  w.sw = lv_switch_create(w.card);
  lv_obj_set_size(w.sw, 72, 38);
  lv_obj_align(w.sw, full ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(w.sw, C_OK, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(w.sw, onLightSwitch, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)li.idx);
  if (!full) return w;
  w.slider = lv_slider_create(w.card);
  lv_slider_set_range(w.slider, 1, 254);
  lv_obj_set_size(w.slider, PAGE_W - 44 - 80, 14);
  lv_obj_align(w.slider, LV_ALIGN_LEFT_MID, 8, 4);
  lv_obj_set_style_bg_color(w.slider, C_GAS, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(w.slider, C_TEXT, LV_PART_KNOB);
  lv_obj_add_event_cb(w.slider, onLightLevel, LV_EVENT_RELEASED, (void*)(intptr_t)li.idx);
  lv_obj_add_event_cb(w.slider, onLightLevelDrag, LV_EVENT_VALUE_CHANGED, nullptr);
  w.lvl = label(w.card, &font_de_18, C_MUTED, "");
  lv_obj_align(w.lvl, LV_ALIGN_RIGHT_MID, 0, 4);
  int bw = (PAGE_W - 44 - (SWATCH_N - 1) * 6) / SWATCH_N;
  for (int k = 0; k < SWATCH_N; k++) {
    lv_obj_t* b = button(w.card, SWATCHES[k].text, bw, 34, onLightSwatch,
                         (void*)(intptr_t)(li.idx * 16 + k), lv_color_hex(SWATCHES[k].show), &font_de_14);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), C_ONBANNER, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, k * (bw + 6), 0);
  }
  return w;
}

// Row for a contact or climate sensor: name, small line below, value right
RowW buildDevRow(lv_obj_t* tab, const sensors::DevOut& d) {
  RowW r{};
  r.dev = d.idx;
  r.card = card(tab, PAGE_W - 20, 58);
  lv_obj_set_style_pad_ver(r.card, 6, 0);
  const char* ic = d.kind == sensors::K_CONTACT ? icons::glyph("door") : icons::glyph(icons::autoKey(d.name, true));
  lv_obj_t* n = label(r.card, &font_de_22, C_TEXT, dispText(String(ic) + "  " + d.name).c_str());
  lv_obj_set_width(n, 280);
  lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
  lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, -8);
  r.sub = label(r.card, &font_de_14, C_MUTED, "");
  lv_obj_align(r.sub, LV_ALIGN_BOTTOM_LEFT, 0, 2);
  r.val = label(r.card, &font_de_22, C_TEXT, "--");
  lv_obj_align(r.val, LV_ALIGN_RIGHT_MID, 0, 0);
  return r;
}

void buildDevPage(lv_obj_t* tab, DevPageW& p) {
  pageContainer(tab);
  p.status = label(tab, &font_de_18, C_MUTED, "");
  lv_obj_set_width(p.status, PAGE_W - 20);
  p.cards.clear();
  p.rows.clear();
  p.sig = "\x01";
}

DevPageW* devPageFor(int bp) {
  for (auto& p : devPages) if (p.bp == bp) return &p;
  return nullptr;
}

void refreshDevPage(DevPageW& p) {
  if (!p.status) return;
  std::vector<sensors::DevOut> ds = sensors::devices(p.bit);
  // lights and switches first: they are what you touch
  std::stable_sort(ds.begin(), ds.end(), [](const sensors::DevOut& a, const sensors::DevOut& b) {
    auto rank = [](uint8_t k) { return k == sensors::K_LIGHT ? 0 : k == sensors::K_SWITCH ? 1 : 2; };
    return rank(a.kind) < rank(b.kind);
  });
  String sig;
  for (auto& d : ds) { sig += String(d.idx) + ":" + String(d.kind) + d.name; sig += '\x01'; }
  if (sig != p.sig) {
    p.sig = sig;
    for (auto& c : p.cards) lv_obj_delete(c.card);
    for (auto& r : p.rows) lv_obj_delete(r.card);
    p.cards.clear();
    p.rows.clear();
    lv_obj_t* tab = lv_obj_get_parent(p.status);
    for (auto& d : ds) {
      if (d.kind == sensors::K_LIGHT || d.kind == sensors::K_SWITCH) p.cards.push_back(buildLightCard(tab, d));
      else p.rows.push_back(buildDevRow(tab, d));
    }
  }
  size_t ci = 0, ri = 0;
  for (auto& d : ds) {
    if (d.kind == sensors::K_LIGHT || d.kind == sensors::K_SWITCH) {
      if (ci >= p.cards.size()) break;
      LightW& w = p.cards[ci++];
      if (d.on == 1) lv_obj_add_state(w.sw, LV_STATE_CHECKED);
      else           lv_obj_remove_state(w.sw, LV_STATE_CHECKED);
      if (!w.slider) continue;
      if (d.level >= 0 && !lv_obj_has_state(w.slider, LV_STATE_PRESSED)) {
        lv_slider_set_value(w.slider, d.level, LV_ANIM_OFF);
        lv_label_set_text_fmt(w.lvl, "%d %%", (int)lroundf(d.level * 100.0f / 254));
      } else if (d.level < 0) lv_label_set_text(w.lvl, "");
      lv_obj_set_style_opa(w.slider, d.on == 1 ? LV_OPA_COVER : LV_OPA_50, 0);
    } else {
      if (ri >= p.rows.size()) break;
      RowW& r = p.rows[ri++];
      String val, sub;
      lv_color_t col = C_TEXT;
      if (d.kind == sensors::K_CONTACT) {
        val = d.on == 1 ? "offen" : d.on == 0 ? "zu" : "?";
        col = d.on == 1 ? C_WARN : d.on == 0 ? C_OK : C_MUTED;
      } else {
        val = isnan(d.temp) ? String("--") : commaNum(d.temp, 1) + " \xC2\xB0""C";
        if (!isnan(d.hum)) sub = String((int)lroundf(d.hum)) + " % Luftfeuchte";
      }
      if (!isnan(d.batt)) sub += (sub.length() ? "  \xE2\x80\xA2  " : "") + String("Batterie ") + String((int)lroundf(d.batt)) + " %";
      if (d.stale) { sub += sub.length() ? "  \xE2\x80\xA2  keine Daten" : "keine Daten"; col = C_MUTED; }
      lv_label_set_text(r.val, dispText(val).c_str());
      lv_obj_set_style_text_color(r.val, col, 0);
      lv_label_set_text(r.sub, dispText(sub).c_str());
    }
  }
  String st = ds.empty() ? "Noch nichts auf dieser Seite. Im Browser unter Einstellungen / Sensoren bei einem Sensor \""
                           + pageTitle(p.bp) + "\" ankreuzen." : String("");
  lv_label_set_text(p.status, dispText(st).c_str());
  if (st.length()) lv_obj_remove_flag(p.status, LV_OBJ_FLAG_HIDDEN);
  else             lv_obj_add_flag(p.status, LV_OBJ_FLAG_HIDDEN);
}

// Overview tile with the lights / switches that are on
void setOnTile(ListW& w, uint8_t pages, uint8_t kind, const char* none) {
  if (!w.line[0]) return;
  std::vector<sensors::LightOut> all = sensors::lights(pages, kind);
  std::vector<String> lines;
  std::vector<lv_color_t> cols;
  int on = 0;
  for (auto& l : all) {
    if (l.on != 1) continue;
    on++;
    if (lines.size() < 3) {
      lines.push_back(l.name + (l.level >= 0 ? "  " + String((int)lroundf(l.level * 100.0f / 254)) + " %" : String("")));
      cols.push_back(C_GAS);
    }
  }
  if (all.empty()) { lines.push_back("--"); cols.push_back(C_MUTED); }
  else if (!on) { lines.push_back("Alle aus"); cols.push_back(C_MUTED); }
  String sub = all.empty() ? String(none) : on > 3 ? "+" + String(on - 3) + " an" : on ? String(on) + " an" : String("");
  setList(w, lines, cols, sub);
}

// Warnings page: all active warnings with their full text
void buildWarnPage(lv_obj_t* tab) {
  pageContainer(tab);
  wnCard = card(tab, PAGE_W - 20, LV_SIZE_CONTENT);
  wnLbl = label(wnCard, &font_de_22, C_TEXT, "");
  lv_obj_set_width(wnLbl, PAGE_W - 44);
  lv_label_set_long_mode(wnLbl, LV_LABEL_LONG_WRAP);
}

void buildTemp(lv_obj_t* tab) {
  pageContainer(tab);
  tStatus = label(tab, &font_de_18, C_MUTED, "");
  lv_obj_set_width(tStatus, PAGE_W - 20);
  tRows.clear();
  tVals.clear();
  tSig = "\x01";   // force a row build on the first refresh
}

// (Re)creates one row per sensor when the sensor list changed
void buildTempRows(const std::vector<TempItem>& items, lv_obj_t* tab) {
  for (auto r : tRows) lv_obj_delete(r);
  tRows.clear();
  tVals.clear();
  for (auto& it : items) {
    lv_obj_t* c = card(tab, PAGE_W - 20, 70);
    lv_obj_t* n = label(c, &font_de_22, C_MUTED, dispText(String(tempIcon(it.name)) + "  " + it.name).c_str());
    lv_obj_set_width(n, 280);
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* v = label(c, &font_de_28, C_TEXT, "--");
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    tRows.push_back(c);
    tVals.push_back(v);
  }
}

lv_obj_t* modeMatrix(lv_obj_t* parent, const char** map, lv_event_cb_t cb) {
  lv_obj_t* bm = lv_buttonmatrix_create(parent);
  lv_buttonmatrix_set_map(bm, map);
  lv_buttonmatrix_set_button_ctrl_all(bm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
  lv_buttonmatrix_set_one_checked(bm, true);
  lv_obj_set_size(bm, 300, 56);
  lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bm, 0, 0);
  lv_obj_set_style_pad_all(bm, 0, 0);
  lv_obj_set_style_pad_gap(bm, 8, 0);
  lv_obj_set_style_text_font(bm, &font_de_18, 0);
  lv_obj_set_style_bg_color(bm, C_CARD_HI, LV_PART_ITEMS);
  lv_obj_set_style_text_color(bm, C_TEXT, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(bm, C_HEAT, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_radius(bm, 12, LV_PART_ITEMS);
  lv_obj_add_event_cb(bm, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return bm;
}

lv_obj_t* rowCard(lv_obj_t* parent, const char* title, int h = 76) {
  lv_obj_t* c = card(parent, PAGE_W - 20, h);
  lv_obj_t* l = label(c, &font_de_18, C_MUTED, title);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  return c;
}

void buildHeater(lv_obj_t* tab) {
  pageContainer(tab);
  static const char* fanMap[] = {"Eco", "High", ""};
  static const char* waterMap[] = {"Aus", "Eco", "High", ""};

  // temperatures + power
  lv_obj_t* top = card(tab, PAGE_W - 20, 130);
  hRoom = label(top, &font_de_56, C_TEXT, "--");
  lv_obj_align(hRoom, LV_ALIGN_TOP_LEFT, 0, -4);
  hWater = label(top, &font_de_18, C_MUTED, "");
  lv_obj_align(hWater, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  hPower = button(top, "", 130, 96, onPower, nullptr);
  lv_obj_align(hPower, LV_ALIGN_RIGHT_MID, 0, 0);
  hPowerLbl = lv_obj_get_child(hPower, 0);

  // setpoint
  lv_obj_t* sp = rowCard(tab, "Soll");
  lv_obj_t* minus = button(sp, LV_SYMBOL_MINUS, 70, 52, onSetpoint, (void*)(intptr_t)-1);
  lv_obj_align(minus, LV_ALIGN_LEFT_MID, 90, 0);
  hSet = label(sp, &font_de_28, C_TEXT, "--");
  lv_obj_align(hSet, LV_ALIGN_CENTER, 50, 0);
  lv_obj_t* plus = button(sp, LV_SYMBOL_PLUS, 70, 52, onSetpoint, (void*)(intptr_t)1);
  lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* fr = rowCard(tab, "Gebläse");
  hFanBm = modeMatrix(fr, fanMap, onHeaterFan);
  lv_obj_align(hFanBm, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* wr = rowCard(tab, "Warmwasser");
  hWaterBm = modeMatrix(wr, waterMap, onHeaterWater);
  lv_obj_align(hWaterBm, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* st = card(tab, PAGE_W - 20, 76);
  hStatus = label(st, &font_de_14, C_MUTED, "");
  lv_obj_set_width(hStatus, 250);
  lv_label_set_long_mode(hStatus, LV_LABEL_LONG_WRAP);
  lv_obj_align(hStatus, LV_ALIGN_LEFT_MID, 0, 0);
  hDecoke = button(st, "Freibrennen", 170, 52, nullptr, nullptr, C_CARD_HI, &font_de_18);
  lv_obj_add_event_cb(hDecoke, onDecoke, LV_EVENT_LONG_PRESSED, nullptr);
  lv_obj_align(hDecoke, LV_ALIGN_RIGHT_MID, 0, 0);
  hDecokeLbl = lv_obj_get_child(hDecoke, 0);
  if (settings.heaterSrc == HEAT_SRC_CAMPMATIC) lv_obj_add_flag(hDecoke, LV_OBJ_FLAG_HIDDEN);   // not on the box
}

void buildFan(lv_obj_t* tab) {
  pageContainer(tab);
  lv_obj_t* c = card(tab, PAGE_W - 20, 60);
  fStatus = label(c, &font_de_18, C_MUTED, "");
  lv_obj_align(fStatus, LV_ALIGN_LEFT_MID, 0, 0);

  struct B { const char* txt; const char* key; };
  // On/Off only opens the lid; "close" presses Slower until the lid shuts (camper-maxxfan 1.4.0+)
  static const B row1[] = {{LV_SYMBOL_POWER "  Ein", "on_off"}, {"Schließen", "close"}, {"Auto", "auto"}};
  static const B row2[] = {{LV_SYMBOL_DOWN "  Langsamer", "slower"}, {LV_SYMBOL_UP "  Schneller", "faster"}};
  static const B row3[] = {{LV_SYMBOL_REFRESH "  Richtung", "direction"}, {"Sync AUS", "sync_off"}};
  for (auto& b : row1) button(tab, b.txt, 146, 76, onFanBtn, (void*)b.key, C_CARD);
  for (auto& b : row2) button(tab, b.txt, 225, 76, onFanBtn, (void*)b.key, C_CARD);

  lv_obj_t* sc = card(tab, PAGE_W - 20, 96);
  fSpeedLbl = label(sc, &font_de_18, C_MUTED, "Stufe");
  lv_obj_align(fSpeedLbl, LV_ALIGN_TOP_LEFT, 0, 0);
  fSpeed = lv_slider_create(sc);
  lv_slider_set_range(fSpeed, 1, 10);
  lv_slider_set_value(fSpeed, 5, LV_ANIM_OFF);
  lv_obj_set_size(fSpeed, PAGE_W - 80, 18);
  lv_obj_align(fSpeed, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(fSpeed, C_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(fSpeed, C_ACCENT, LV_PART_KNOB);
  lv_obj_add_event_cb(fSpeed, onFanSpeed, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(fSpeed, onFanSpeedDrag, LV_EVENT_VALUE_CHANGED, nullptr);

  for (auto& b : row3) button(tab, b.txt, 225, 64, onFanBtn, (void*)b.key, C_CARD, &font_de_18);
}

void buildLevel(lv_obj_t* tab) {
  pageContainer(tab);
  // bubble
  lv_obj_t* bc = card(tab, 224, 250);
  lvArea = lv_obj_create(bc);
  lv_obj_set_size(lvArea, 196, 196);
  lv_obj_align(lvArea, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(lvArea, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(lvArea, C_BG, 0);
  lv_obj_set_style_border_color(lvArea, C_MUTED, 0);
  lv_obj_set_style_border_width(lvArea, 2, 0);
  lv_obj_set_style_pad_all(lvArea, 0, 0);
  lv_obj_remove_flag(lvArea, LV_OBJ_FLAG_SCROLLABLE);
  lvZone = lv_obj_create(lvArea);
  lv_obj_set_size(lvZone, 40, 40);
  lv_obj_center(lvZone);
  lv_obj_set_style_radius(lvZone, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(lvZone, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(lvZone, C_OK, 0);
  lv_obj_set_style_border_width(lvZone, 2, 0);
  lv_obj_remove_flag(lvZone, LV_OBJ_FLAG_CLICKABLE);
  lvBubble = lv_obj_create(lvArea);
  lv_obj_set_size(lvBubble, 30, 30);
  lv_obj_center(lvBubble);
  lv_obj_set_style_radius(lvBubble, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(lvBubble, C_MUTED, 0);
  lv_obj_set_style_border_width(lvBubble, 0, 0);
  lv_obj_remove_flag(lvBubble, LV_OBJ_FLAG_CLICKABLE);
  lvState = label(bc, &font_de_18, C_MUTED, "--");
  lv_obj_align(lvState, LV_ALIGN_BOTTOM_MID, 0, 0);

  // vehicle with wedge heights
  lv_obj_t* vc = card(tab, 224, 250);
  lv_obj_t* car = lv_obj_create(vc);
  lv_obj_set_size(car, 70, 140);
  lv_obj_align(car, LV_ALIGN_CENTER, 0, 6);
  lv_obj_set_style_bg_color(car, C_CARD_HI, 0);
  lv_obj_set_style_border_width(car, 0, 0);
  lv_obj_set_style_radius(car, 10, 0);
  lv_obj_t* front = label(vc, &font_de_14, C_MUTED, LV_SYMBOL_UP " Front");
  lv_obj_align(front, LV_ALIGN_TOP_MID, 0, 0);
  const lv_align_t al[4] = {LV_ALIGN_LEFT_MID, LV_ALIGN_RIGHT_MID, LV_ALIGN_LEFT_MID, LV_ALIGN_RIGHT_MID};
  const int yo[4] = {-44, -44, 56, 56};
  for (int i = 0; i < 4; i++) {
    lvWedge[i] = label(vc, &font_de_18, C_TEXT, "--");
    lv_obj_align(lvWedge[i], al[i], 0, yo[i]);
  }

  lv_obj_t* vals = card(tab, PAGE_W - 20, 76);
  lvPitch = label(vals, &font_de_22, C_TEXT, "Nick --");
  lv_obj_align(lvPitch, LV_ALIGN_LEFT_MID, 0, 0);
  lvRoll = label(vals, &font_de_22, C_TEXT, "Roll --");
  lv_obj_align(lvRoll, LV_ALIGN_LEFT_MID, 140, 0);
  lv_obj_t* tare = button(vals, "Tara", 110, 52, nullptr, nullptr, C_CARD_HI, &font_de_18);
  lv_obj_add_event_cb(tare, onTare, LV_EVENT_LONG_PRESSED, nullptr);
  lv_obj_align(tare, LV_ALIGN_RIGHT_MID, 0, 0);
}

void buildCustom(lv_obj_t* tab, int pageIdx) {
  pageContainer(tab);
  bool any = false;
  for (size_t i = 0; i < settings.tiles.size(); i++) {
    const Tile& t = settings.tiles[i];
    if (t.page != pageIdx) continue;
    any = true;
    TileW& w = tileW[i];
    bool wide = t.type == TILE_SLIDER;
    w.card = card(tab, wide ? PAGE_W - 20 : 224, 110);
    lv_obj_t* n = label(w.card, &font_de_18, C_MUTED, dispText(t.name).c_str());
    lv_obj_set_width(n, wide ? PAGE_W - 60 : 196);
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
    lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 0);
    switch (t.type) {
      case TILE_VALUE:
        w.val = label(w.card, &font_de_28, C_TEXT, "--");
        lv_obj_align(w.val, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        break;
      case TILE_SWITCH:
        w.val = label(w.card, &font_de_18, C_TEXT, "--");
        lv_obj_align(w.val, LV_ALIGN_BOTTOM_LEFT, 0, -6);
        w.sw = lv_switch_create(w.card);
        lv_obj_set_size(w.sw, 84, 44);
        lv_obj_align(w.sw, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_set_style_bg_color(w.sw, C_OK, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(w.sw, onTileSwitch, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
        break;
      case TILE_BUTTON: {
        lv_obj_t* b = button(w.card, LV_SYMBOL_PLAY, 196, 52, onTileButton, (void*)(intptr_t)i);
        lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);
        break;
      }
      case TILE_SLIDER: {
        w.sval = label(w.card, &font_de_18, C_TEXT, "--");
        lv_obj_align(w.sval, LV_ALIGN_TOP_RIGHT, 0, 0);
        w.slider = lv_slider_create(w.card);
        int32_t steps = (int32_t)lroundf((t.max - t.min) / t.step);
        lv_slider_set_range(w.slider, 0, steps > 0 ? steps : 1);
        lv_obj_set_size(w.slider, PAGE_W - 80, 18);
        lv_obj_align(w.slider, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_color(w.slider, C_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(w.slider, C_ACCENT, LV_PART_KNOB);
        lv_obj_add_event_cb(w.slider, onTileSlider, LV_EVENT_RELEASED, (void*)(intptr_t)i);
        lv_obj_add_event_cb(w.slider, onTileSliderDrag, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
        break;
      }
    }
  }
  if (!any) {
    lv_obj_t* l = label(tab, &font_de_18, C_MUTED, "Noch keine Kacheln.\nIm Browser unter\nEinstellungen \xE2\x80\xA2 Seiten hinzufügen.");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, PAGE_W - 20);
  }
}

const char* const SYS_ROWS[] = {"Netzwerk", "IP-Adresse", "Signal", "MQTT", "Hostname", "Firmware", "Laufzeit", "Speicher"};
const int SYS_N = sizeof(SYS_ROWS) / sizeof(SYS_ROWS[0]);

void buildSystem(lv_obj_t* tab) {
  pageContainer(tab);
  // label / value list like a settings screen
  const int rowH = 24;
  lv_obj_t* ic = card(tab, PAGE_W - 20, SYS_N * rowH + 20);
  lv_obj_set_style_pad_ver(ic, 10, 0);
  sVals.clear();
  for (int i = 0; i < SYS_N; i++) {
    lv_obj_t* k = label(ic, &font_de_18, C_MUTED, SYS_ROWS[i]);
    lv_obj_align(k, LV_ALIGN_TOP_LEFT, 0, i * rowH);
    lv_obj_t* v = label(ic, &font_de_18, C_TEXT, "--");
    lv_obj_set_width(v, PAGE_W - 20 - 24 - 140);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, 140, i * rowH);
    sVals.push_back(v);
  }
  sIp = sVals[1];

  // QR code to open the web page (or join the setup access point)
  lv_obj_t* qc = card(tab, 150, 150);
  lv_obj_set_style_pad_all(qc, 8, 0);
  sQr = lv_qrcode_create(qc);
  lv_qrcode_set_size(sQr, 104);
  lv_qrcode_set_dark_color(sQr, lv_color_black());
  lv_qrcode_set_light_color(sQr, lv_color_white());
  lv_obj_set_style_border_color(sQr, lv_color_white(), 0);
  lv_obj_set_style_border_width(sQr, 4, 0);
  lv_obj_align(sQr, LV_ALIGN_TOP_MID, 0, 0);
  sQrLbl = label(qc, &font_de_14, C_MUTED, "");
  lv_obj_set_style_text_align(sQrLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(sQrLbl, 134);
  lv_obj_align(sQrLbl, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t* bc = card(tab, PAGE_W - 20 - 160, 150);
  lv_obj_t* bl = label(bc, &font_de_18, C_MUTED, LV_SYMBOL_IMAGE "  Helligkeit");
  lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 0, 0);
  // screen off right away (touch wakes it again)
  lv_obj_t* off = button(bc, LV_SYMBOL_POWER " Aus", 96, 40, onScreenOff, nullptr, C_CARD_HI, &font_de_18);
  lv_obj_align(off, LV_ALIGN_TOP_RIGHT, 0, -6);
  sBri = lv_slider_create(bc);
  lv_slider_set_range(sBri, 5, 100);
  lv_slider_set_value(sBri, settings.brightness, LV_ANIM_OFF);
  lv_obj_set_size(sBri, PAGE_W - 20 - 160 - 48, 18);
  lv_obj_align(sBri, LV_ALIGN_LEFT_MID, 8, 0);
  lv_obj_set_style_bg_color(sBri, C_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sBri, C_ACCENT, LV_PART_KNOB);
  lv_obj_add_event_cb(sBri, onBrightness, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sBri, onBrightnessDone, LV_EVENT_RELEASED, nullptr);
  sNet = label(bc, &font_de_14, C_MUTED, "");
  lv_obj_set_width(sNet, PAGE_W - 20 - 160 - 24);
  lv_label_set_long_mode(sNet, LV_LABEL_LONG_WRAP);
  lv_obj_align(sNet, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // Rescue: switch a static IP back to DHCP (long press, then restart)
  if (settings.staticIp) {
    lv_obj_t* b = button(tab, "Feste IP aus (DHCP) \xE2\x80\x93 lange dr\xC3\xBC" "cken", PAGE_W - 20, 56, nullptr, nullptr,
                         C_CARD_HI, &font_de_18);
    lv_obj_add_event_cb(b, onDhcp, LV_EVENT_LONG_PRESSED, nullptr);
  }
  sInfo = nullptr;
}

// ---- weather page --------------------------------------------------------------
SmallW smallCard(lv_obj_t* parent, const char* icon, const char* title) {
  SmallW w;
  lv_obj_t* c = card(parent, (PAGE_W - 30) / 2, 62);
  lv_obj_set_style_pad_all(c, 8, 0);
  lv_obj_t* t = label(c, &font_de_14, C_MUTED, (String(icon) + "  " + title).c_str());
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
  w.val = label(c, &font_de_22, C_TEXT, "--");
  lv_obj_align(w.val, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  w.sub = label(c, &font_de_14, C_MUTED, "");
  lv_obj_align(w.sub, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
  return w;
}

void buildWeather(lv_obj_t* tab) {
  pageContainer(tab);
  lv_obj_set_style_pad_gap(tab, 10, 0);
  // current conditions
  lv_obj_t* hero = card(tab, PAGE_W - 20, 112);
  wIcon = label(hero, &font_de_56, C_GAS, "");
  lv_obj_align(wIcon, LV_ALIGN_LEFT_MID, 4, 0);
  wTemp = label(hero, &font_de_56, C_TEXT, "--");
  lv_obj_align(wTemp, LV_ALIGN_LEFT_MID, 92, -10);
  // measured outside temperature from an own sensor (Einstellungen -> Sensoren, page "Wetter")
  wOut = label(hero, &font_de_14, C_ACCENT, "");
  lv_obj_set_width(wOut, PAGE_W - 44 - 196 - 8);
  lv_label_set_long_mode(wOut, LV_LABEL_LONG_DOT);
  lv_obj_align(wOut, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  wText = label(hero, &font_de_22, C_TEXT, "");
  lv_obj_set_width(wText, 196);
  lv_label_set_long_mode(wText, LV_LABEL_LONG_DOT);
  lv_obj_align(wText, LV_ALIGN_TOP_RIGHT, 0, 4);
  wFeels = label(hero, &font_de_14, C_MUTED, "");
  lv_obj_set_width(wFeels, 196);
  lv_obj_align(wFeels, LV_ALIGN_RIGHT_MID, 0, 6);
  wPlace = label(hero, &font_de_14, C_MUTED, "");
  lv_obj_set_width(wPlace, 196);
  lv_label_set_long_mode(wPlace, LV_LABEL_LONG_DOT);
  lv_obj_align(wPlace, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  wWind = smallCard(tab, ICON_WIND, "Wind");
  wHum = smallCard(tab, ICON_TINT, "Luftfeuchte");
  wNext = smallCard(tab, ICON_UMBRELLA, "Nächste Stunde");
  wToday = smallCard(tab, ICON_UMBRELLA, "Regen heute");

  for (int i = 0; i < 3; i++) {
    lv_obj_t* c = card(tab, (PAGE_W - 40) / 3, 78);
    lv_obj_set_style_pad_all(c, 8, 0);
    wDays[i].day = label(c, &font_de_14, C_MUTED, "");
    lv_obj_align(wDays[i].day, LV_ALIGN_TOP_LEFT, 0, 0);
    wDays[i].icon = label(c, &font_de_28, C_GAS, "");
    lv_obj_align(wDays[i].icon, LV_ALIGN_LEFT_MID, 0, 8);
    wDays[i].temp = label(c, &font_de_18, C_TEXT, "");
    lv_obj_align(wDays[i].temp, LV_ALIGN_RIGHT_MID, 0, 2);
    wDays[i].rain = label(c, &font_de_14, C_MUTED, "");
    lv_obj_align(wDays[i].rain, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  }
  wStatus = label(tab, &font_de_14, C_MUTED, "");
  lv_obj_set_width(wStatus, PAGE_W - 20);
  wShown = -1;
}

// ---- value refresh -------------------------------------------------------------
void setTank(TankW& w, DpId pct, DpId liters, bool online) {
  if (!w.pct) return;
  String p = store::get(pct);
  lv_label_set_text(w.pct, fmt(p, 0, "%").c_str());
  lv_label_set_text(w.sub, store::has(liters) ? fmt(store::get(liters), 0, "L").c_str() : "");
  lv_bar_set_value(w.bar, p.length() ? constrain((int)p.toFloat(), 0, 100) : 0, LV_ANIM_ON);
  lv_obj_set_style_text_color(w.pct, online ? C_TEXT : C_MUTED, 0);
}

void setMatrix(lv_obj_t* bm, int checked) {
  lv_buttonmatrix_clear_button_ctrl_all(bm, LV_BUTTONMATRIX_CTRL_CHECKED);
  if (checked >= 0) lv_buttonmatrix_set_button_ctrl(bm, checked, LV_BUTTONMATRIX_CTRL_CHECKED);
}

String wedgeText(DpId id) {
  if (!store::has(id)) return "--";
  float v = store::getf(id, 0);
  if (fabsf(v) < 0.05f) return "OK";
  return String(v, 1) + " cm";
}

// Weather tile, bottom line: measured outside temperature if there is a sensor, else the forecast text
String wxTileBase;
void setWxTileSub() {
  if (!ovWxSub) return;
  sensors::TempOut o{};
  String sub = wxTileBase;
  if (sensors::outdoor(o) && !o.stale) {
    sub = "Au\xC3\x9F" "en " + commaNum(o.temp, 1) + "\xC2\xB0";
    if (!isnan(o.hum)) sub += "  " + String((int)lroundf(o.hum)) + " %";
  }
  lv_label_set_text(ovWxSub, sub.c_str());
}

// Temperature tile: the chosen sensors (or all), 3 at a time; more than 3 flip every few seconds
void refreshTempTile() {
  if (!ovTemp.line[0]) return;
  bool tempOn = store::devOnline(DEV_TEMP) || !sensors::climate().empty();
  std::vector<TempItem> items = allTempItems();
  bool anyAlarm = false;
  for (auto& it : items) anyAlarm |= it.alarm != 0;
  std::vector<icons::Slot> sel = icons::slots(true);
  if (sel.empty())
    for (auto& it : items) sel.push_back({it.name, ""});
  int pagesN = sel.empty() ? 1 : (sel.size() + 2) / 3;
  if (ovTempPage >= pagesN) ovTempPage = 0;
  std::vector<String> lines;
  std::vector<lv_color_t> cols;
  for (size_t k = ovTempPage * 3; k < sel.size() && k < (size_t)ovTempPage * 3 + 3; k++) {
    const icons::Slot& sl = sel[k];
    const char* ic = sl.key.length() ? icons::glyph(sl.key) : icons::glyph(icons::autoKey(sl.name, true));
    const TempItem* it = nullptr;
    for (auto& x : items) if (x.name == sl.name) { it = &x; break; }
    // value first, so it stays visible when a long name is cut off
    lines.push_back(String(ic) + "  " + (it ? it->val : String("--")));
    cols.push_back(!tempOn || !it ? C_MUTED : (it->alarm ? C_WARN : C_TEXT));
  }
  String sub = !tempOn ? String("offline")
             : anyAlarm ? String("Alarm")
             : pagesN > 1 ? String(ovTempPage + 1) + " / " + String(pagesN)
             : String("");
  setList(ovTemp, lines, cols, sub);
}

void refreshValues() {
  bool tankOn = store::devOnline(DEV_TANK);
  bool heatOn = store::devOnline(DEV_HEATER);
  bool lvlOn  = store::devOnline(DEV_LEVEL);
  bool fanOn  = store::devOnline(DEV_FAN);

  // overview
  setTank(ovWater, DP_WATER_PCT, DP_WATER_L, tankOn);   // each returns if its tile is not shown
  setTank(ovGrey, DP_GREY_PCT, DP_GREY_L, tankOn);
  setTank(ovGas, DP_GAS_PCT, DP_GAS_L, tankOn);
  if (ovHeatVal) {
    lv_label_set_text(ovHeatVal, fmt(store::get(DP_HEAT_ROOM), 1, "\xC2\xB0""C").c_str());
    String hs = !heatOn ? "offline"
              : (store::get(DP_HEAT_HVAC) == "heat" ? "An, Soll " + fmt(store::get(DP_HEAT_SETPOINT), 0, "\xC2\xB0""C") : "Aus");
    lv_label_set_text(ovHeatSub, hs.c_str());
    lv_obj_set_style_text_color(ovHeatVal, store::get(DP_HEAT_HVAC) == "heat" ? C_HEAT : C_TEXT, 0);
  }
  if (ovLvlVal) {
    bool level = store::get(DP_LVL_OK) == "ON";
    lv_label_set_text(ovLvlVal, !store::has(DP_LVL_OK) ? "--" : (level ? "Eben" : "Schief"));
    lv_obj_set_style_text_color(ovLvlVal, !store::has(DP_LVL_OK) ? C_TEXT : (level ? C_OK : C_WARN), 0);
    lv_label_set_text(ovLvlSub, lvlOn ? ("Nick " + fmt(store::get(DP_LVL_PITCH), 1, "\xC2\xB0") +
                                         "  Roll " + fmt(store::get(DP_LVL_ROLL), 1, "\xC2\xB0")).c_str() : "offline");
  }
  if (ovFanVal) {
    lv_label_set_text(ovFanVal, fanOn ? "Bereit" : "--");
    lv_label_set_text(ovFanSub, fanOn ? (store::get(DP_FAN_HOLD) == "ON" ? "Auto-Hold aktiv" : "Tippen zum Steuern") : "offline");
  }

  // single batteries
  if (!ovOneBat.empty()) {
    bool batOn = store::devOnline(DEV_BATTERY);
    std::vector<BatItem> items = batItems();
    for (auto& w : ovOneBat) {
      if (w.idx >= (int)items.size()) {
        lv_label_set_text(w.val, "--");
        lv_label_set_text(w.sub, batOn ? "keine Batterie" : "offline");
        continue;
      }
      const BatItem& b = items[w.idx];
      lv_label_set_text(w.title, dispText(String(batIcon(b.name)) + "  " + b.name).c_str());
      bool bm2 = b.state.length() > 0;
      String v = !b.ok ? String("--") : bm2 ? commaNum(b.v, 1) + " V"
               : isnan(b.soc) ? String("--") : String((int)lroundf(b.soc)) + " %";
      bool bad = b.ok && (b.problem || b.state == "Kritisch" || b.state == "Schwach" ||
                          (!bm2 && !isnan(b.soc) && b.soc < BAT_WARN_SOC));
      lv_label_set_text(w.val, v.c_str());
      lv_obj_set_style_text_color(w.val, !batOn || !b.ok ? C_MUTED : (bad ? C_WARN : C_TEXT), 0);
      String sub = !batOn ? String("offline") : !b.ok ? String("keine Verbindung")
                 : bm2 ? b.state
                 : (!isnan(b.w) && b.w > 1) ? LV_SYMBOL_UP " " + String((int)lroundf(b.w)) + " W"
                 : b.rt >= 0 ? "noch " + runtimeText(b.rt)
                 : !isnan(b.w) ? String((int)lroundf(b.w)) + " W" : String("");
      lv_label_set_text(w.sub, dispText(sub).c_str());
    }
  }
  // clock
  if (ovClock.val) {
    struct tm tm;
    if (getLocalTime(&tm, 0)) {
      static const char* const days[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
      lv_label_set_text_fmt(ovClock.val, "%02d:%02d", tm.tm_hour, tm.tm_min);
      lv_label_set_text_fmt(ovClock.sub, "%s, %02d.%02d.%04d", days[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    } else {
      lv_label_set_text(ovClock.val, "--:--");
      lv_label_set_text(ovClock.sub, "keine Uhrzeit");
    }
  }
  // custom MQTT tiles (read only on the overview)
  for (auto& w : ovCustom) {
    if (w.idx >= (int)settings.tiles.size()) continue;
    const Tile& t = settings.tiles[w.idx];
    String v = store::tileHas(w.idx) ? store::tile(w.idx) : String("--");
    if (t.type == TILE_SWITCH) v = store::tileOn(w.idx) ? "An" : "Aus";
    else if (t.unit.length() && store::tileHas(w.idx)) v += " " + t.unit;
    lv_label_set_text(w.val, dispText(v).c_str());
  }

  // warnings
  String warn = warnings();
  if (ovWarn) {
    bool any = warn.length() > 0;
    lv_obj_set_style_bg_color(ovWarn, any ? C_WARN : C_CARD, 0);
    lv_obj_set_style_bg_color(ovWarn, any ? C_WARN : C_CARD_HI, LV_STATE_PRESSED);
    lv_obj_set_style_text_font(ovWarnLbl, any ? &font_de_14 : &font_de_28, 0);
    lv_obj_set_style_text_color(ovWarnLbl, any ? C_TEXT : C_OK, 0);
    lv_label_set_text(ovWarnLbl, any ? dispText(warn).c_str() : "OK");
    if (ovInd) {
      String ic;
      int open = sensors::openCount();
      if (open > 0) ic = String(open) + " " + icons::glyph("door");
      std::vector<String> keys = rules::activeIcons();
      for (auto& k : sensors::activeIcons()) keys.push_back(k);
      for (auto& k : keys) { if (ic.length()) ic += "  "; ic += icons::glyph(k); }
      lv_label_set_text(ovInd, ic.c_str());
      lv_obj_set_style_text_color(ovInd, any ? C_TEXT : C_ACCENT, 0);
    }
  }
  // Only a NEW warning wakes the screen, and only if enabled on the web page; a changing value
  // in an existing one (temperature, voltage, runtime) must not keep the display on.
  String keys = warnKeys(warn);
  if (settings.warnWake && warn.length() && newWarning(keys, "\n" + lastWarnKeys)) {
    Serial.printf("[disp] wake: new warning \"%s\"\n", warn.c_str());
    // A new warning should be seen: wake the screen. If it was dark (nobody is using
    // it right now), also switch to the overview where the banner is.
    bool wasAwake = disp::isAwake();
    disp::wake();
    int ov = tabIndexOfBuiltin(BP_OVERVIEW);
    if (!wasAwake && ov >= 0) gotoTab(ov);
  }
  lastWarn = warn;
  lastWarnKeys = keys;

  if (wnLbl) {
    String t = warn.length() ? warn : String("Keine Warnungen");
    t.replace("\n", "\n\n");
    lv_label_set_text(wnLbl, dispText(t).c_str());
    lv_obj_set_style_text_color(wnLbl, warn.length() ? C_WARN : C_OK, 0);
  }

  // windows / doors: overview tile with the open ones
  if (ovAct.line[0]) {
    std::vector<sensors::ContactOut> cs = sensors::contacts();
    std::vector<String> lines;
    std::vector<lv_color_t> cols;
    int open = 0;
    for (auto& c : cs) {
      if (c.open != 1) continue;
      open++;
      if (lines.size() < 3) { lines.push_back(c.name); cols.push_back(C_WARN); }
    }
    if (cs.empty()) { lines.push_back("--"); cols.push_back(C_MUTED); }
    else if (!open) { lines.push_back("Alles zu"); cols.push_back(C_OK); }
    String sub = cs.empty() ? String("keine Kontakte")
               : open > 3 ? "+" + String(open - 3) + " offen"
               : open ? String(open) + " offen" : String("");
    setList(ovAct, lines, cols, sub);
  }
  // lights / switches that are on, device pages
  setOnTile(ovLight, sensors::SH_DEVPAGES, sensors::K_LIGHT, "keine Lichter");
  setOnTile(ovSw, sensors::SH_DEVPAGES, sensors::K_SWITCH, "keine Schalter");
  for (auto& p : devPages) refreshDevPage(p);

  // weather page / tile: measured outside temperature
  if (wOut) {
    sensors::TempOut o{};
    String t;
    if (sensors::outdoor(o)) {
      t = "Au\xC3\x9F" "en " + commaNum(o.temp, 1) + "\xC2\xB0";
      if (!isnan(o.hum)) t += "  \xE2\x80\xA2  " + String((int)lroundf(o.hum)) + " %";
    }
    lv_label_set_text(wOut, dispText(t).c_str());
    lv_obj_set_style_text_color(wOut, o.stale ? C_MUTED : C_ACCENT, 0);
  }
  setWxTileSub();

  // temperatures
  bool tempOn = store::devOnline(DEV_TEMP) || !sensors::climate().empty();
  if (ovTemp.line[0] || tStatus) {
    std::vector<TempItem> items = allTempItems();
    refreshTempTile();
    if (tStatus) {
      String sig;
      for (auto& it : items) { sig += it.name; sig += '\x01'; }
      if (sig != tSig) {
        tSig = sig;
        buildTempRows(items, lv_obj_get_parent(tStatus));
      }
      for (size_t i = 0; i < items.size() && i < tVals.size(); i++) {
        lv_label_set_text(tVals[i], dispText(items[i].val).c_str());
        lv_obj_set_style_text_color(tVals[i], items[i].alarm ? C_WARN : (tempOn ? C_TEXT : C_MUTED), 0);
      }
      String st = !tempOn ? String("Temperatursensoren offline")
                : items.empty() ? String("Keine Sensoren gemeldet")
                : (store::get(DP_TEMP_ALARM).length() ? store::get(DP_TEMP_ALARM) : String(""));
      st.replace("; ", "\n");
      lv_label_set_text(tStatus, dispText(st).c_str());
      lv_obj_set_style_text_color(tStatus, store::get(DP_TEMP_ALARM).length() && tempOn ? C_WARN : C_MUTED, 0);
      if (st.length()) lv_obj_remove_flag(tStatus, LV_OBJ_FLAG_HIDDEN);
      else             lv_obj_add_flag(tStatus, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // batteries
  if (ovBat.line[0]) {
    bool batOn = store::devOnline(DEV_BATTERY);
    std::vector<BatItem> items = batItems();
    std::vector<String> lines;
    std::vector<lv_color_t> cols;
    String sub;
    // chosen batteries with their icons, otherwise the first 3
    std::vector<icons::Slot> sel = icons::slots(false);
    if (sel.empty())
      for (size_t i = 0; i < items.size() && i < 3; i++) sel.push_back({items[i].name, ""});
    for (auto& sl : sel) {
      const char* ic = sl.key.length() ? icons::glyph(sl.key) : icons::glyph(icons::autoKey(sl.name, false));
      const BatItem* bp = nullptr;
      for (auto& x : items) if (x.name == sl.name) { bp = &x; break; }
      if (!bp) {
        lines.push_back(String(ic) + "  --");
        cols.push_back(C_MUTED);
        continue;
      }
      const BatItem& b = *bp;
      // value first: SoC, for the BM2 the voltage
      String val = !b.ok ? String("--")
                 : b.state.length() ? commaNum(b.v, 1) + " V"
                 : isnan(b.soc) ? String("--") : String((int)lroundf(b.soc)) + " %";
      lines.push_back(String(ic) + "  " + val);
      bool bad = b.ok && (b.problem || b.state == "Kritisch" || b.state == "Schwach" ||
                          (b.state.isEmpty() && !isnan(b.soc) && b.soc < BAT_WARN_SOC));
      cols.push_back(!batOn || !b.ok ? C_MUTED : (bad ? C_WARN : C_TEXT));
      if (b.ok && !b.state.length() && !isnan(b.w) && sub.isEmpty()) {
        if (b.w > 1)        sub = LV_SYMBOL_UP " " + String((int)lroundf(b.w)) + " W";
        else if (b.rt >= 0) sub = "noch " + runtimeText(b.rt);
      }
    }
    if (!batOn) sub = "offline";
    setList(ovBat, lines, cols, sub);
  }

  if (bStatus) {
    bool batOn = store::devOnline(DEV_BATTERY);
    std::vector<BatItem> items = batItems();
    String sig;
    for (auto& b : items) { sig += b.name; sig += '\x01'; }
    if (sig != bSig) {
      bSig = sig;
      buildBatteryRows(items.size(), lv_obj_get_parent(bStatus));
    }
    for (size_t i = 0; i < items.size() && i < bRows.size(); i++) {
      const BatItem& b = items[i];
      BatRowW& r = bRows[i];
      bool live = b.ok && batOn;
      bool bm2 = b.state.length() > 0;
      bool stateBad = b.state == "Kritisch" || b.state == "Schwach";
      bool low = !bm2 && !isnan(b.soc) && b.soc < BAT_WARN_SOC;
      lv_color_t socCol = !live || isnan(b.soc) ? C_MUTED
                        : (b.soc < 20 ? C_WARN : (b.soc < 40 ? C_GAS : C_OK));

      lv_label_set_text((lv_obj_t*)lv_obj_get_user_data(r.card), dispText(String(batIcon(b.name)) + "  " + b.name).c_str());
      // large value: state of charge, for the BM2 the voltage (its SoC is only an estimate)
      String big = bm2 ? (isnan(b.v) ? String("--") : commaNum(b.v, 2) + " V")
                       : (isnan(b.soc) ? String("--") : String((int)lroundf(b.soc)) + " %");
      lv_label_set_text(r.soc, big.c_str());
      lv_obj_set_style_text_color(r.soc, bm2 ? (live ? (stateBad ? C_WARN : C_TEXT) : C_MUTED) : socCol, 0);

      // top right: power with direction, or the BM2 state
      String top;
      lv_color_t topCol = C_TEXT;
      if (!b.ok) { top = "keine Verbindung"; topCol = C_MUTED; }
      else if (b.problem) { top = "BMS-Fehler"; topCol = C_WARN; }
      else if (bm2) { top = b.state; topCol = stateBad ? C_WARN : (b.state == "Normal" ? C_TEXT : C_OK); }
      else if (!isnan(b.w)) {
        int pw = (int)lroundf(b.w);
        top = pw > 1 ? LV_SYMBOL_UP " " + String(pw) + " W" : (pw < -1 ? LV_SYMBOL_DOWN " " + String(-pw) + " W" : String("Ruhe"));
        topCol = pw > 1 ? C_OK : C_TEXT;
      }
      if (!batOn) topCol = C_MUTED;
      lv_label_set_text(r.top, dispText(top).c_str());
      lv_obj_set_style_text_color(r.top, topCol, 0);

      // middle right: voltage / SoC and runtime
      String sub;
      if (b.ok && b.problem && b.ptext.length()) sub = b.ptext;   // what the BMS reports
      else if (b.ok) {
        if (bm2) sub = isnan(b.soc) ? String("") : String((int)lroundf(b.soc)) + " % (gesch\xC3\xA4tzt)";
        else {
          if (!isnan(b.v)) sub = commaNum(b.v, 2) + " V";
          if (b.rt >= 0) sub += "  \xE2\x80\xA2  noch " + runtimeText(b.rt);
          else if (!isnan(b.w) && b.w > 1) sub += "  \xE2\x80\xA2  l\xC3\xA4" "dt";
        }
      }
      lv_label_set_text(r.sub, dispText(sub).c_str());
      lv_obj_set_style_text_color(r.sub, b.ok && b.problem ? C_WARN : C_MUTED, 0);

      lv_bar_set_value(r.bar, isnan(b.soc) ? 0 : constrain((int)lroundf(b.soc), 0, 100), LV_ANIM_OFF);
      lv_obj_set_style_bg_color(r.bar, socCol, LV_PART_INDICATOR);
      lv_obj_set_style_border_width(r.card, (live && (b.problem || stateBad || low)) ? 2 : 0, 0);
      lv_obj_set_style_border_color(r.card, C_WARN, 0);
    }
    if (bSensLbl) {
      String t;
      for (auto& sb : sensors::batteries()) {
        if (t.length()) t += "\n";
        t += sb.name + ":  " + String((int)lroundf(sb.batt)) + " %" + (sb.stale ? " (alt)" : "");
      }
      lv_label_set_text(bSensLbl, dispText(t).c_str());
      if (t.length()) lv_obj_remove_flag(bSensCard, LV_OBJ_FLAG_HIDDEN);
      else            lv_obj_add_flag(bSensCard, LV_OBJ_FLAG_HIDDEN);
    }
    String st = !batOn ? String("Batterie-Gateway offline")
              : items.empty() ? String("Keine Batterien gemeldet") : String("");
    lv_label_set_text(bStatus, st.c_str());
    if (st.length()) lv_obj_remove_flag(bStatus, LV_OBJ_FLAG_HIDDEN);
    else             lv_obj_add_flag(bStatus, LV_OBJ_FLAG_HIDDEN);
  }

  // heater
  if (hRoom) {
    lv_label_set_text(hRoom, fmt(store::get(DP_HEAT_ROOM), 1, "\xC2\xB0""C").c_str());
    String w = store::has(DP_HEAT_WATER) ? "Wasser " + fmt(store::get(DP_HEAT_WATER), 0, "\xC2\xB0""C") : "";
    if (!heatOn) w = "Heizung offline";
    else if (store::get(DP_HEAT_ALIVE) == "OFF") w = "CP Plus nicht verbunden";
    lv_label_set_text(hWater, w.c_str());

    bool on = store::get(DP_HEAT_HVAC) == "heat";
    lv_label_set_text(hPowerLbl, on ? LV_SYMBOL_POWER "\nAn" : LV_SYMBOL_POWER "\nAus");
    lv_obj_set_style_text_align(hPowerLbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(hPower, on ? C_HEAT : C_CARD_HI, 0);

    uint32_t now = millis();
    if (!isnan(pendingSet) && pendingSentAt && now - pendingSentAt > SETPOINT_HOLD_MS) pendingSet = NAN;
    if (isnan(pendingSet)) {
      // CampMatic sends no setpoint while off: show the last one, it is used when switching on
      float last = store::lastHeaterSetpoint();
      bool shown = store::has(DP_HEAT_SETPOINT) || isnan(last);
      lv_label_set_text(hSet, shown ? fmt(store::get(DP_HEAT_SETPOINT), 0, "\xC2\xB0""C").c_str()
                                    : (String((int)lroundf(last)) + " \xC2\xB0""C").c_str());
      lv_obj_set_style_text_color(hSet, shown ? C_TEXT : C_MUTED, 0);
    }

    String fm = store::get(DP_HEAT_FANMODE);
    setMatrix(hFanBm, fm == "eco" ? 0 : (fm == "high" || fm == "boost") ? 1 : -1);
    String wm = store::get(DP_HEAT_WATERMODE);
    setMatrix(hWaterBm, wm == "off" ? 0 : wm == "eco" ? 1 : (wm == "high" || wm == "boost") ? 2 : -1);

    String st = store::get(DP_HEAT_OPSTATUS);
    lv_label_set_text(hStatus, st.c_str());
    bool err = store::get(DP_HEAT_ERROR) == "ON";
    if (err) lv_label_set_text_fmt(hStatus, "Fehler %s\n%s", store::get(DP_HEAT_ERRCODE).c_str(), st.c_str());
    lv_obj_set_style_text_color(hStatus, err ? C_WARN : C_MUTED, 0);

    bool dec = store::get(DP_HEAT_DECOKE) == "ON";
    if (dec) lv_label_set_text_fmt(hDecokeLbl, "Stop (%s min)", store::get(DP_HEAT_DECOKE_REM).c_str());
    else     lv_label_set_text(hDecokeLbl, "Freibrennen");
    lv_obj_set_style_bg_color(hDecoke, dec ? C_HEAT : C_CARD_HI, 0);
  }

  // fan
  if (fStatus) {
    String s = fanOn ? "Online" : "Offline";
    if (store::get(DP_FAN_HOLD) == "ON") s += "  \xE2\x80\xA2  Auto-Hold";
    lv_label_set_text(fStatus, (String(ICON_FAN) + "  " + s).c_str());
  }

  // level
  if (lvArea) {
    float pitch = store::getf(DP_LVL_PITCH, 0), roll = store::getf(DP_LVL_ROLL, 0);
    float tol = store::getf(DP_LVL_TOL, 0.5f);
    const float range = 5.0f;   // degrees at the edge of the circle
    const int r = (196 - 30) / 2;
    int x = (int)(constrain(roll / range, -1.0f, 1.0f) * r);
    int y = (int)(constrain(-pitch / range, -1.0f, 1.0f) * r);
    lv_obj_align(lvBubble, LV_ALIGN_CENTER, x, y);
    int z = constrain((int)(tol / range * 196), 16, 196);
    lv_obj_set_size(lvZone, z, z);
    lv_obj_center(lvZone);
    bool level = store::get(DP_LVL_OK) == "ON";
    lv_obj_set_style_bg_color(lvBubble, !store::has(DP_LVL_OK) ? C_MUTED : (level ? C_OK : C_GAS), 0);
    lv_label_set_text(lvState, !lvlOn ? "offline" : (level ? "Eben" : "Nicht eben"));
    lv_label_set_text(lvPitch, ("Nick " + fmt(store::get(DP_LVL_PITCH), 1, "\xC2\xB0")).c_str());
    lv_label_set_text(lvRoll, ("Roll " + fmt(store::get(DP_LVL_ROLL), 1, "\xC2\xB0")).c_str());
    const DpId w[4] = {DP_LVL_FL, DP_LVL_FR, DP_LVL_RL, DP_LVL_RR};
    for (int i = 0; i < 4; i++) lv_label_set_text(lvWedge[i], wedgeText(w[i]).c_str());
  }

  // user tiles
  for (size_t i = 0; i < tileW.size() && i < settings.tiles.size(); i++) {
    TileW& w = tileW[i];
    if (!w.card || !store::tileDirty(store::C_UI, i)) continue;
    const Tile& t = settings.tiles[i];
    String v = store::tile(i);
    switch (t.type) {
      case TILE_VALUE:
        lv_label_set_text(w.val, dispText(fmt(v, -1, t.unit.c_str())).c_str());
        break;
      case TILE_SWITCH:
        lv_label_set_text(w.val, store::tileHas(i) ? (store::tileOn(i) ? "An" : "Aus") : "--");
        if (store::tileOn(i)) lv_obj_add_state(w.sw, LV_STATE_CHECKED);
        else                  lv_obj_remove_state(w.sw, LV_STATE_CHECKED);
        break;
      case TILE_SLIDER:
        if (store::tileHas(i) && !lv_obj_has_state(w.slider, LV_STATE_PRESSED)) {
          float f = v.toFloat();
          lv_slider_set_value(w.slider, (int32_t)lroundf((f - t.min) / t.step), LV_ANIM_OFF);
          lv_label_set_text(w.sval, dispText(fmt(v, -1, t.unit.c_str())).c_str());
        }
        break;
      default: break;
    }
  }
}

String num1(float v) { return isnan(v) ? String("--") : commaNum(v, 1); }
String num0(float v) { return isnan(v) ? String("--") : String((int)lroundf(v)); }

void refreshWeather() {
  if (!wIcon && !ovWxVal) return;
  wx::Data d = wx::get();
  String st = wx::status();
  if (d.at == wShown && st == wShownStatus) return;
  wShown = d.at;
  wShownStatus = st;
  bool ok = d.valid && wx::enabled();
  if (ovWxVal) {
    lv_label_set_text(ovWxIcon, ok ? wx::icon(d.code, d.day) : "");
    lv_label_set_text(ovWxVal, ok ? (num0(d.temp) + "\xC2\xB0").c_str() : "--");
    String sub = !wx::enabled() ? String("aus") : ok ? dispText(wx::text(d.code)) : dispText(st);
    if (ok && !isnan(d.gust)) sub = num0(d.gust) + " km/h B\xC3\xB6" "en";
    if (ok && d.code >= 51) sub = dispText(wx::text(d.code));
    wxTileBase = sub;
    setWxTileSub();
  }
  if (!wIcon) return;
  lv_label_set_text(wIcon, ok ? wx::icon(d.code, d.day) : "");
  lv_obj_set_style_text_color(wIcon, !ok ? C_MUTED : (d.code <= 2 && d.day) ? C_GAS : C_ACCENT, 0);
  lv_label_set_text(wTemp, ok ? (num0(d.temp) + "\xC2\xB0").c_str() : "--");
  lv_label_set_text(wText, ok ? dispText(wx::text(d.code)).c_str() : (wx::enabled() ? "Keine Daten" : "Wetter aus"));
  String feels = ok ? "Gef\xC3\xBChlt " + num0(d.feels) + "\xC2\xB0  \xE2\x80\xA2  " + num0(d.tMax[0]) + "\xC2\xB0 / " + num0(d.tMin[0]) + "\xC2\xB0"
                    : String(wx::enabled() ? "" : "Im Browser unter Einstellungen einrichten");
  lv_label_set_text(wFeels, feels.c_str());
  String gpsName = ok && d.gps ? wx::place() : String("");
  String place = gpsName.length() ? gpsName
               : (!d.gps && settings.wxName.length()) ? settings.wxName
               : (ok ? String(d.lat, 2) + ", " + String(d.lon, 2) : String(""));
  if (ok && d.gps) place += " (GPS)";
  lv_label_set_text(wPlace, place.length() ? dispText(String(ICON_MARKER " ") + place).c_str() : "");

  lv_label_set_text(wWind.val, ok ? (num0(d.wind) + " km/h " + wx::dirText(d.dir)).c_str() : "--");
  lv_label_set_text(wWind.sub, ok ? ("B\xC3\xB6" "en " + num0(d.gust)).c_str() : "");
  bool gusty = ok && settings.wxGust > 0 && !isnan(d.gust) && d.gust >= settings.wxGust;
  lv_obj_set_style_text_color(wWind.sub, gusty ? C_WARN : C_MUTED, 0);
  lv_label_set_text(wHum.val, ok ? (num0(d.hum) + " %").c_str() : "--");
  lv_label_set_text(wHum.sub, "");
  lv_label_set_text(wNext.val, ok ? (num1(d.rainNext) + " mm").c_str() : "--");
  lv_label_set_text(wNext.sub, ok ? (num0(d.popNext) + " %").c_str() : "");
  lv_label_set_text(wToday.val, ok ? (num1(d.rainSum[0]) + " mm").c_str() : "--");
  lv_label_set_text(wToday.sub, ok ? (num0(d.popMax[0]) + " %").c_str() : "");

  static const char* const days[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  struct tm tm;
  time_t base = ok ? d.at : time(nullptr);
  for (int i = 0; i < 3; i++) {
    time_t t = base + (time_t)(i + 1) * 86400;
    localtime_r(&t, &tm);
    int k = i + 1;
    lv_label_set_text(wDays[i].day, i == 0 ? "Morgen" : days[tm.tm_wday]);
    lv_label_set_text(wDays[i].icon, ok ? wx::icon(d.dCode[k], true) : "");
    lv_obj_set_style_text_color(wDays[i].icon, ok && d.dCode[k] <= 2 ? C_GAS : C_ACCENT, 0);
    lv_label_set_text(wDays[i].temp, ok ? (num0(d.tMax[k]) + "\xC2\xB0 / " + num0(d.tMin[k]) + "\xC2\xB0").c_str() : "--");
    lv_label_set_text(wDays[i].rain, ok ? (num1(d.rainSum[k]) + " mm  " + num0(d.popMax[k]) + " %").c_str() : "");
  }
  lv_label_set_text(wStatus, dispText(st + "  \xE2\x80\xA2  Daten: Open-Meteo").c_str());
}

String uptimeText(uint32_t sec) {
  uint32_t d = sec / 86400, h = sec % 86400 / 3600, m = sec % 3600 / 60;
  if (d) return String(d) + " d " + String(h) + " h";
  if (h) return String(h) + " h " + String(m) + " min";
  return String(m) + " min";
}

const char* signalText(int rssi) {
  if (rssi >= -55) return "sehr gut";
  if (rssi >= -67) return "gut";
  if (rssi >= -75) return "mittel";
  return "schwach";
}

void refreshHeader() {
  static const char* const days[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  if (wifimgr::timeValid()) {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    lv_label_set_text_fmt(hdrClock, "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text_fmt(hdrDate, "%s %02d.%02d.", days[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1);
  } else {
    lv_label_set_text(hdrClock, "");
    lv_label_set_text(hdrDate, "");
  }
  // connection state as one icon left of the date (only when something is wrong)
  String s;
  if (wifimgr::apActive() && !wifimgr::staConnected()) s = LV_SYMBOL_WIFI " AP";
  else if (!wifimgr::staConnected()) s = LV_SYMBOL_WIFI " " LV_SYMBOL_CLOSE;
  else if (!mqttc::connected()) s = "MQTT " LV_SYMBOL_CLOSE;
  lv_label_set_text(hdrStatus, s.c_str());

  // warnings: the header turns into a yellow banner with the scrolling warning text
  bool warn = lastWarn.length() > 0;
  lv_obj_set_style_bg_color(hdr, warn ? C_BANNER : C_BG, 0);
  lv_obj_set_style_text_color(hdrClock, warn ? C_ONBANNER : C_TEXT, 0);
  lv_obj_set_style_text_color(hdrDate, warn ? C_ONBANNER : C_MUTED, 0);
  lv_obj_set_style_text_color(hdrStatus, warn ? C_ONBANNER : C_WARN, 0);
  if (warn) {
    String t = lastWarn;
    t.replace("\n", "   \xE2\x80\xA2   ");
    t = String(LV_SYMBOL_WARNING "  ") + dispText(t);
    if (t != String(lv_label_get_text(hdrWarn))) lv_label_set_text(hdrWarn, t.c_str());
    lv_obj_remove_flag(hdrWarn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hdrTitle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hdrName, LV_OBJ_FLAG_HIDDEN);
  } else if (!lv_obj_has_flag(hdrWarn, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_flag(hdrWarn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(hdrTitle, LV_OBJ_FLAG_HIDDEN);
    updateHeaderTitle();
  }

  refreshWeather();

  // system page
  if (sIp) {
    String url, qr, info;
    bool sta = wifimgr::staConnected();
    if (sta) {
      url = "http://" + wifimgr::ipString() + "/";
      qr = url;
      info = url;
    } else {
      // QR joins the setup access point
      qr = "WIFI:T:WPA;S:" + wifimgr::apSsid() + ";P:" + settings.apPass + ";;";
      info = "WLAN " + wifimgr::apSsid() + "  Passwort " + settings.apPass + ", dann http://192.168.4.1";
    }
    if (qr != lastQr) {
      lv_qrcode_update(sQr, qr.c_str(), qr.length());
      lastQr = qr;
    }
    lv_label_set_text(sQrLbl, sta ? "Webseite \xC3\xB6" "ffnen" : "Setup-WLAN");
    if (wifimgr::dhcpFallback()) info += "\nFeste IP ging nicht \xE2\x80\x93 DHCP bis zum Neustart";
    lv_label_set_text(sNet, dispText(info).c_str());
    String vals[SYS_N];
    vals[0] = sta ? settings.wifiSsid : (wifimgr::apActive() ? "Setup-WLAN " + wifimgr::apSsid() : String("nicht verbunden"));
    vals[1] = wifimgr::ipString();
    vals[2] = sta ? String(wifimgr::rssi()) + " dBm (" + signalText(wifimgr::rssi()) + ")" : String("--");
    vals[3] = settings.mqttHost.isEmpty() ? String("nicht eingerichtet") : settings.mqttHost + (mqttc::connected() ? " \xE2\x80\xA2 verbunden" : " \xE2\x80\xA2 getrennt");
    vals[4] = settings.hostname + ".local";
    vals[5] = String(FW_NAME " " FW_VERSION) + (disp::touchOk() ? "" : " \xE2\x80\xA2 Touch fehlt");
    vals[6] = uptimeText(millis() / 1000);
    vals[7] = String((unsigned)(ESP.getFreeHeap() / 1024)) + " kB  \xE2\x80\xA2  PSRAM " + String((unsigned)(ESP.getFreePsram() / 1024)) + " kB";
    for (int i = 0; i < SYS_N && i < (int)sVals.size(); i++) lv_label_set_text(sVals[i], dispText(vals[i]).c_str());
    lv_obj_set_style_text_color(sVals[3], mqttc::connected() ? C_TEXT : C_WARN, 0);
  }
}

void clearHandles() {
  ovWater = ovGrey = ovGas = TankW();
  ovHeatVal = ovHeatSub = ovLvlVal = ovLvlSub = ovFanVal = ovFanSub = nullptr;
  hRoom = hWater = hSet = hPower = hPowerLbl = hFanBm = hWaterBm = hStatus = hDecoke = hDecokeLbl = nullptr;
  fStatus = fSpeed = fSpeedLbl = nullptr;
  lvArea = lvBubble = lvZone = lvPitch = lvRoll = lvState = nullptr;
  for (auto& w : lvWedge) w = nullptr;
  sIp = sNet = sQr = sQrLbl = sBri = sInfo = nullptr;
  sVals.clear();
  wIcon = wTemp = wText = wFeels = wPlace = wStatus = nullptr;
  wWind = wHum = wNext = wToday = SmallW();
  for (auto& d : wDays) d = DayW();
  ovWxIcon = ovWxVal = ovWxSub = nullptr;
  wShown = -1;
  ovWarn = ovWarnLbl = ovInd = nullptr;
  ovTemp = ovBat = ovAct = ovLight = ovSw = ListW();
  for (auto& p : devPages) { p.status = nullptr; p.cards.clear(); p.rows.clear(); p.sig = ""; }
  bSensCard = bSensLbl = nullptr;
  wnCard = wnLbl = nullptr;
  wOut = nullptr;
  wxTileBase = "";
  ovOneBat.clear();
  ovCustom.clear();
  ovClock = OneW();
  tStatus = nullptr;
  bStatus = nullptr;
  bRows.clear();
  tRows.clear();
  tVals.clear();
  lastQr = "";
  tileW.assign(settings.tiles.size(), TileW());
}

// Icon of a fixed page (or of the system page with BP_COUNT): the chosen one, otherwise the default
const char* builtinIcon(int bp) {
  if (bp >= 0 && bp <= BP_COUNT && settings.pageIcon[bp].length()) return icons::glyph(settings.pageIcon[bp]);
  switch (bp) {
    case BP_OVERVIEW: return LV_SYMBOL_HOME;
    case BP_HEATER:   return ICON_FIRE;
    case BP_FAN:      return ICON_FAN;
    case BP_LEVEL:    return ICON_BALANCE;
    case BP_TEMP:     return ICON_THERMOMETER;
    case BP_BATTERY:  return LV_SYMBOL_BATTERY_3;
    case BP_WEATHER:  return ICON_CLOUD_SUN;
    case BP_ACTORS:   return icons::glyph("door");
    case BP_WARN:     return LV_SYMBOL_WARNING;
    case BP_LIGHT:    return icons::glyph("light");
    case BP_SWITCH:   return icons::glyph("plug");
    case BP_EXTRA1:   return icons::glyph("bolt");
    case BP_EXTRA2:   return icons::glyph("box");
    case BP_COUNT:    return LV_SYMBOL_SETTINGS;
    default:          return LV_SYMBOL_LIST;
  }
}

// Icon of a page in the bottom bar
const char* pageIcon(const PageInfo& p) {
  if (p.builtin == PG_SYSTEM) return builtinIcon(BP_COUNT);
  if (p.builtin >= 0) return builtinIcon(p.builtin);
  return LV_SYMBOL_LIST;
}

// Icon bar: Home, up to NAV_FREE chosen pages, System. Pages not in the bar stay reachable by
// swiping and through their overview tiles.
std::vector<int> navPages() {
  std::vector<int> out;
  int home = tabIndexOfBuiltin(BP_OVERVIEW);
  if (home >= 0) out.push_back(home);
  auto idOf = [](const PageInfo& p) -> String {
    if (p.builtin >= 0 && p.builtin < BP_COUNT) return PAGE_IDS[p.builtin];
    if (p.custom >= 0) return "p" + String(p.custom);
    return "";
  };
  int added = 0;
  if (settings.navSel.length()) {
    int start = 0;
    while (start <= (int)settings.navSel.length() && added < NAV_FREE) {
      int c = settings.navSel.indexOf(',', start);
      String id = settings.navSel.substring(start, c < 0 ? settings.navSel.length() : c);
      id.trim();
      for (size_t i = 0; i < pages.size(); i++)
        if ((int)i != home && pages[i].builtin != PG_SYSTEM && idOf(pages[i]) == id) { out.push_back(i); added++; break; }
      if (c < 0) break;
      start = c + 1;
    }
  } else {
    for (size_t i = 0; i < pages.size() && added < NAV_FREE; i++)
      if ((int)i != home && pages[i].builtin != PG_SYSTEM) { out.push_back(i); added++; }
  }
  out.push_back(pages.size() - 1);   // system page, always last
  return out;
}

void buildNav() {
  if (nav) lv_obj_delete(nav);
  navBtns.clear();
  navTabs = navPages();
  nav = lv_obj_create(scr);
  lv_obj_set_size(nav, LCD_WIDTH, NAV_H);
  lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(nav, C_NAV, 0);
  lv_obj_set_style_border_width(nav, 0, 0);
  lv_obj_set_style_radius(nav, 0, 0);
  lv_obj_set_style_pad_all(nav, 0, 0);
  lv_obj_set_style_pad_gap(nav, 0, 0);
  lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
  lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
  int n = navTabs.size();
  int w = LCD_WIDTH / n;
  for (int i = 0; i < n; i++) {
    lv_obj_t* b = lv_button_create(nav);
    lv_obj_set_size(b, i == n - 1 ? LCD_WIDTH - w * (n - 1) : w, NAV_H);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, C_NAV, 0);
    lv_obj_set_style_bg_color(b, C_CARD_HI, LV_STATE_PRESSED);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(b, C_ACCENT, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_t* l = label(b, &font_de_28, C_MUTED, pageIcon(pages[navTabs[i]]));
    lv_obj_center(l);
    lv_obj_add_event_cb(b, onNav, LV_EVENT_CLICKED, (void*)(intptr_t)navTabs[i]);
    navBtns.push_back(b);
  }
}

void build() {
  int keep = tv ? (int)lv_tabview_get_tab_active(tv) : 0;
  if (tv) lv_obj_delete(tv);
  tv = nullptr;
  clearHandles();

  pages.clear();
  for (int b = 0; b < BP_COUNT; b++) if (settings.builtin[b]) pages.push_back({pageTitle(b), b, -1});
  for (size_t p = 0; p < settings.pages.size(); p++) pages.push_back({settings.pages[p].name, -1, (int)p});
  pages.push_back({pageTitle(BP_COUNT), PG_SYSTEM, -1});

  tv = lv_tabview_create(scr);
  lv_tabview_set_tab_bar_size(tv, 0);
  lv_obj_set_size(tv, PAGE_W, PAGE_H);
  lv_obj_align(tv, LV_ALIGN_TOP_MID, 0, HDR_H);
  lv_obj_set_style_bg_color(tv, C_BG, 0);
  lv_obj_t* content = lv_tabview_get_content(tv);
  lv_obj_set_style_bg_color(content, C_BG, 0);

  for (auto& p : pages) {
    lv_obj_t* tab = lv_tabview_add_tab(tv, dispText(p.title).c_str());
    switch (p.builtin) {
      case BP_OVERVIEW: buildOverview(tab); break;
      case BP_HEATER:   buildHeater(tab); break;
      case BP_FAN:      buildFan(tab); break;
      case BP_LEVEL:    buildLevel(tab); break;
      case BP_TEMP:     buildTemp(tab); break;
      case BP_BATTERY:  buildBattery(tab); break;
      case BP_WEATHER:  buildWeather(tab); break;
      case BP_WARN:     buildWarnPage(tab); break;
      case BP_ACTORS: case BP_LIGHT: case BP_SWITCH: case BP_EXTRA1: case BP_EXTRA2:
        buildDevPage(tab, *devPageFor(p.builtin));
        break;
      case PG_SYSTEM:   buildSystem(tab); break;
      default:          buildCustom(tab, p.custom); break;
    }
  }
  lv_obj_add_event_cb(tv, onTabChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  buildNav();

  // without network setup, start on the system page so the QR code is visible
  if (settings.wifiSsid.isEmpty()) keep = pages.size() - 1;
  gotoTab(constrain(keep, 0, (int)pages.size() - 1), false);
  updateHeaderTitle();

  store::markAllDirty();
  refreshValues();
  store::clearDirty(store::C_UI);
  refreshHeader();
}

} // namespace

namespace ui {

void begin() {
  scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_text_font(scr, &font_de_18, 0);
  lv_obj_set_style_text_color(scr, C_TEXT, 0);

  // header: page title (or the warning banner) left, status + date + clock right. Tap = overview.
  hdr = lv_obj_create(scr);
  lv_obj_set_size(hdr, LCD_WIDTH, HDR_H);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(hdr, C_BG, 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(hdr, onHome, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(hdr, onHeaderLong, LV_EVENT_LONG_PRESSED, nullptr);
  lv_obj_add_event_cb(hdr, onHeaderReleased, LV_EVENT_RELEASED, nullptr);

  hdrTitle = label(hdr, &font_de_22, C_TEXT, "");
  lv_obj_align(hdrTitle, LV_ALIGN_LEFT_MID, TITLE_X, 0);
  hdrName = label(hdr, &font_de_18, C_MUTED, "");
  lv_label_set_long_mode(hdrName, LV_LABEL_LONG_DOT);
  lv_obj_set_height(hdrName, lv_font_get_line_height(&font_de_18));
  hdrClock = label(hdr, &font_de_22, C_TEXT, "");
  lv_obj_align(hdrClock, LV_ALIGN_RIGHT_MID, -14, 0);
  hdrDate = label(hdr, &font_de_18, C_MUTED, "");
  lv_obj_align(hdrDate, LV_ALIGN_RIGHT_MID, -84, 1);
  hdrStatus = label(hdr, &font_de_14, C_WARN, "");
  lv_obj_align(hdrStatus, LV_ALIGN_RIGHT_MID, -184, 1);
  hdrWarn = label(hdr, &font_de_18, C_ONBANNER, "");
  lv_obj_set_width(hdrWarn, LCD_WIDTH - 200 - TITLE_X);
  lv_label_set_long_mode(hdrWarn, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(hdrWarn, LV_ALIGN_LEFT_MID, TITLE_X, 1);
  lv_obj_add_flag(hdrWarn, LV_OBJ_FLAG_HIDDEN);

  build();
}

// Full screen "update running" with a progress bar on the top layer, above every page.
// While the flash is written the RGB panel may flicker or shift: the frame buffer sits in PSRAM,
// which is paused during flash writes. It settles by itself after the restart.
lv_obj_t *updScr = nullptr, *updBar = nullptr, *updPct = nullptr, *updSrc = nullptr;
int updShown = -2;

void refreshUpdateScreen() {
  int p = otaprog::get();
  if (p == updShown) return;
  updShown = p;
  if (p < 0) {
    if (updScr) { lv_obj_delete(updScr); updScr = nullptr; }
    return;
  }
  if (!updScr) {
    disp::wake();
    updScr = lv_obj_create(lv_layer_top());
    lv_obj_set_size(updScr, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(updScr, C_BG, 0);
    lv_obj_set_style_bg_opa(updScr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(updScr, 0, 0);
    lv_obj_set_style_radius(updScr, 0, 0);
    lv_obj_remove_flag(updScr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* ic = label(updScr, &font_de_28, C_ACCENT, LV_SYMBOL_DOWNLOAD);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -110);
    lv_obj_t* t = label(updScr, &font_de_28, C_TEXT, "Firmware-Update");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -64);
    updSrc = label(updScr, &font_de_18, C_MUTED, "Upload \xC3\xBC" "ber die Webseite");
    lv_obj_align(updSrc, LV_ALIGN_CENTER, 0, -28);
    updBar = lv_bar_create(updScr);
    lv_obj_set_size(updBar, LCD_WIDTH - 100, 22);
    lv_obj_align(updBar, LV_ALIGN_CENTER, 0, 14);
    lv_bar_set_range(updBar, 0, 100);
    lv_obj_set_style_bg_color(updBar, C_CARD_HI, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(updBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(updBar, C_ACCENT, LV_PART_INDICATOR);
    updPct = label(updScr, &font_de_28, C_TEXT, "0 %");
    lv_obj_align(updPct, LV_ALIGN_CENTER, 0, 56);
    lv_obj_t* n = label(updScr, &font_de_18, C_MUTED, "Bitte nicht ausschalten.\nDas Bild kann dabei flackern.");
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(n, LV_ALIGN_CENTER, 0, 120);
  }
  lv_bar_set_value(updBar, p, LV_ANIM_OFF);
  lv_label_set_text(updPct, p >= 100 ? "Fertig, Neustart ..." : (String(p) + " %").c_str());
}

void loop() {
  refreshUpdateScreen();
  if (sensors::changed()) store::markAllDirty();   // new Zigbee/BLE value: redraw tiles
  if (rebuildPending) {
    rebuildPending = false;
    build();
  }
  uint32_t now = millis();

  // send the heater setpoint once the user stopped tapping
  if (!isnan(pendingSet) && !pendingSentAt && now - pendingAt > SETPOINT_DEBOUNCE_MS) {
    cmd::heater("room_setpoint", String((int)pendingSet));
    pendingSentAt = now;
  }

  if (now - lastValues > 200) {
    lastValues = now;
    bool d, t;
    if (store::takeDirty(store::C_UI, d, t)) {
      refreshValues();
      store::clearDirty(store::C_UI);
    }
  }
  if (now - lastHeader > 1000) {
    lastHeader = now;
    refreshHeader();
  }
  if (ovTemp.line[0] && now - ovTempFlip > TEMP_FLIP_MS) {
    ovTempFlip = now;
    ovTempPage++;          // wraps in refreshTempTile
    refreshTempTile();
  }
}

void requestRebuild() { rebuildPending = true; }

void showPageByName(const String& nameOrIndex) {
  String n = nameOrIndex;
  n.trim();
  bool numeric = n.length() > 0;
  for (char c : n) if (!isDigit(c)) numeric = false;
  if (numeric) { gotoTab(n.toInt()); return; }
  for (size_t i = 0; i < pages.size(); i++) {
    if (pages[i].title.equalsIgnoreCase(n)) { gotoTab(i); return; }
  }
}

String warningText() { return warnings(); }

} // namespace ui
