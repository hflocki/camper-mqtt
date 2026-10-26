#include "ui.h"
#include "config.h"
#include "settings.h"
#include "datastore.h"
#include "mqtt_client.h"
#include "wifi_manager.h"
#include "display_hw.h"
#include <lvgl.h>
#include <vector>
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
#define ICON_THERMOMETER  "\xEF\x92\x91"

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

const int HDR_H = 52;
const int PAGE_W = LCD_WIDTH;
const int PAGE_H = LCD_HEIGHT - HDR_H;
const int HOME_W = 48;                  // home button right of the "<" button
const int TITLE_X = 60 + HOME_W + 10;   // page title starts here

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
lv_obj_t* hdrDots = nullptr;

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

// warnings (overview banner) and temperatures (camper-temp)
lv_obj_t *ovWarn, *ovWarnLbl;
// overview tiles with up to three single lines (temperatures, batteries)
struct ListW { lv_obj_t* line[3] = {nullptr, nullptr, nullptr}; lv_obj_t* sub = nullptr; };
ListW ovTemp, ovBat;
lv_obj_t *tStatus;
std::vector<lv_obj_t*> tRows, tVals;
// battery page: one card per battery
struct BatRowW { lv_obj_t *card, *soc, *top, *sub, *bar; };
std::vector<BatRowW> bRows;
lv_obj_t* bStatus = nullptr;
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
  // Display name: centered on the screen if there is room, otherwise in the gap
  // between the page title and the clock ("00:00" is the widest clock text)
  if (hdrName) {
    String name = dispText(settings.displayName);
    lv_label_set_text(hdrName, name.c_str());
    if (name.length()) {
      const char* title = lv_label_get_text(hdrTitle);
      int left  = TITLE_X + lv_text_get_width(title, strlen(title), &font_de_22, 0) + 14;
      int right = LCD_WIDTH - 66 - lv_text_get_width("00:00", 5, &font_de_22, 0) - 14;
      int maxW  = right - left;
      if (maxW < 20) { lv_obj_add_flag(hdrName, LV_OBJ_FLAG_HIDDEN); }
      else {
        lv_obj_remove_flag(hdrName, LV_OBJ_FLAG_HIDDEN);
        int w = lv_text_get_width(name.c_str(), name.length(), &font_de_18, 0);
        if (w > maxW) w = maxW;
        lv_obj_set_width(hdrName, w);
        int x = LCD_WIDTH / 2 - w / 2;                 // centered on the screen ...
        if (x < left) x = left;                        // ... unless it would touch the title
        if (x + w > right) x = right - w;
        lv_obj_set_pos(hdrName, x, (HDR_H - lv_font_get_line_height(&font_de_18)) / 2 - 6);
      }
    }
  }
  // page dots
  String dots;
  for (size_t p = 0; p < pages.size(); p++) dots += (p == i) ? "\xE2\x80\xA2" : "\xC2\xB7";
  lv_label_set_text(hdrDots, dots.c_str());
}

// ---- events -----------------------------------------------------------------
void onTabChanged(lv_event_t*) { updateHeaderTitle(); }
void onPrev(lv_event_t*) { int i = lv_tabview_get_tab_active(tv); gotoTab(i > 0 ? i - 1 : pages.size() - 1); }
void onNext(lv_event_t*) { int i = lv_tabview_get_tab_active(tv); gotoTab((i + 1) % pages.size()); }
void onDhcp(lv_event_t*) {
  Serial.println("[wifi] static IP switched off on the panel, restarting with DHCP");
  settings.staticIp = false;
  cfg::save();
  delay(300);
  ESP.restart();
}
void onHome(lv_event_t*) { int ov = tabIndexOfBuiltin(BP_OVERVIEW); gotoTab(ov >= 0 ? ov : 0); }
void onOverviewCard(lv_event_t* e) { gotoTab(tabIndexOfBuiltin((int)(intptr_t)lv_event_get_user_data(e))); }

void onSetpoint(lv_event_t* e) {
  int delta = (int)(intptr_t)lv_event_get_user_data(e);
  float base = !isnan(pendingSet) ? pendingSet : store::getf(DP_HEAT_SETPOINT, 20);
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

// ---- batteries (camper-battery) ---------------------------------------------
struct BatItem { String name; String state; float soc, v, w; long rt; bool ok, problem; };

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
      if (b.problem) add(b.name + " BMS-Fehler: bitte pr\xC3\xBC" "fen");
      if (b.state == "Kritisch" || b.state == "Schwach")
        add(b.name + " " + (b.state == "Kritisch" ? "kritisch" : "schwach") + ": " + commaNum(b.v, 2) + " V");
      else if (b.state.isEmpty() && !isnan(b.soc) && b.soc < BAT_WARN_SOC)
        add(b.name + " niedrig: " + String((int)lroundf(b.soc)) + " %" +
            (b.rt >= 0 ? ", noch " + runtimeText(b.rt) : String("")));
    }
  }
  return w;
}

// ---- page builders ---------------------------------------------------------------
// Overview: 3 x 3 tiles that fit on the screen without scrolling
// (480 px - 2 x 10 padding - 2 x 8 gap) / 3 = 148 px wide, (428 - 20 - 16) / 3 = 130 px high
const int TILE_GAP = 8;
const int TILE_W = (PAGE_W - 20 - 2 * TILE_GAP) / 3;
const int TILE_H = (PAGE_H - 20 - 2 * TILE_GAP) / 3;
const int TILE_IN = TILE_W - 20;   // usable width inside a tile (padding 10)

lv_obj_t* tile(lv_obj_t* parent, const char* icon, const char* title, int bp) {
  lv_obj_t* c = card(parent, TILE_W, TILE_H);
  lv_obj_set_style_pad_all(c, 10, 0);
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
  lv_obj_align(w.sub, LV_ALIGN_BOTTOM_RIGHT, 0, -18);
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
    lv_obj_align(w.line[i], LV_ALIGN_TOP_LEFT, 0, 22 + i * lh);
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

void buildOverview(lv_obj_t* tab) {
  pageContainer(tab);
  lv_obj_set_style_pad_gap(tab, TILE_GAP, 0);

  tankCard(tab, ICON_TINT, "Frischwasser", C_WATER, ovWater);
  tankCard(tab, ICON_DRIP, "Grauwasser", C_GREY, ovGrey);
  tankCard(tab, ICON_GAS, "Gas", C_GAS, ovGas);
  infoCard(tab, ICON_FIRE, pageTitle(BP_HEATER).c_str(), ovHeatVal, ovHeatSub, BP_HEATER);
  infoCard(tab, ICON_BALANCE, pageTitle(BP_LEVEL).c_str(), ovLvlVal, ovLvlSub, BP_LEVEL);
  infoCard(tab, ICON_FAN, pageTitle(BP_FAN).c_str(), ovFanVal, ovFanSub, BP_FAN);
  if (settings.prefixTemp.length())
    listCard(tab, ICON_THERMOMETER, pageTitle(BP_TEMP).c_str(), ovTemp, BP_TEMP);
  if (settings.prefixBattery.length())
    listCard(tab, LV_SYMBOL_BATTERY_FULL, pageTitle(BP_BATTERY).c_str(), ovBat, BP_BATTERY);

  // Status tile: green "OK" or the active warnings on red
  ovWarn = tile(tab, LV_SYMBOL_WARNING, "Status", -1);
  ovWarnLbl = label(ovWarn, &font_de_14, C_TEXT, "");
  lv_obj_set_width(ovWarnLbl, TILE_IN);
  lv_label_set_long_mode(ovWarnLbl, LV_LABEL_LONG_WRAP);
  lv_obj_align(ovWarnLbl, LV_ALIGN_TOP_LEFT, 0, 22);
}

void buildBattery(lv_obj_t* tab) {
  pageContainer(tab);
  bStatus = label(tab, &font_de_18, C_MUTED, "");
  lv_obj_set_width(bStatus, PAGE_W - 20);
  bRows.clear();
  bSig = "\x01";   // force a row build on the first refresh
}

// (Re)creates one card per battery when the battery list changed
void buildBatteryRows(size_t n, lv_obj_t* tab) {
  for (auto& r : bRows) lv_obj_delete(r.card);
  bRows.clear();
  for (size_t i = 0; i < n; i++) {
    BatRowW r;
    r.card = card(tab, PAGE_W - 20, 132);
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
    lv_obj_align(r.sub, LV_ALIGN_RIGHT_MID, 0, 2);
    r.bar = lv_bar_create(r.card);
    lv_obj_set_size(r.bar, PAGE_W - 48, 12);
    lv_obj_align(r.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(r.bar, 0, 100);
    lv_obj_set_style_bg_color(r.bar, C_CARD_HI, LV_PART_MAIN);
    bRows.push_back(r);
  }
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
    lv_obj_t* n = label(c, &font_de_22, C_MUTED, dispText(it.name).c_str());
    lv_obj_set_width(n, 260);
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

void buildSystem(lv_obj_t* tab) {
  pageContainer(tab);
  lv_obj_t* qc = card(tab, 224, 250);
  sQr = lv_qrcode_create(qc);
  lv_qrcode_set_size(sQr, 180);
  lv_qrcode_set_dark_color(sQr, lv_color_black());
  lv_qrcode_set_light_color(sQr, lv_color_white());
  lv_obj_set_style_border_color(sQr, lv_color_white(), 0);
  lv_obj_set_style_border_width(sQr, 6, 0);
  lv_obj_align(sQr, LV_ALIGN_TOP_MID, 0, 0);
  sQrLbl = label(qc, &font_de_14, C_MUTED, "");
  lv_obj_set_style_text_align(sQrLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(sQrLbl, 200);
  lv_obj_align(sQrLbl, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t* ic = card(tab, 224, 250);
  sIp = label(ic, &font_de_22, C_TEXT, "");
  lv_obj_align(sIp, LV_ALIGN_TOP_LEFT, 0, 0);
  sNet = label(ic, &font_de_14, C_MUTED, "");
  lv_obj_set_width(sNet, 200);
  lv_label_set_long_mode(sNet, LV_LABEL_LONG_WRAP);
  lv_obj_align(sNet, LV_ALIGN_TOP_LEFT, 0, 36);

  lv_obj_t* bc = card(tab, PAGE_W - 20, 96);
  lv_obj_t* bl = label(bc, &font_de_18, C_MUTED, LV_SYMBOL_IMAGE "  Helligkeit");
  lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 0, 0);
  sBri = lv_slider_create(bc);
  lv_slider_set_range(sBri, 5, 100);
  lv_slider_set_value(sBri, settings.brightness, LV_ANIM_OFF);
  lv_obj_set_size(sBri, PAGE_W - 80, 18);
  lv_obj_align(sBri, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(sBri, C_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sBri, C_ACCENT, LV_PART_KNOB);
  lv_obj_add_event_cb(sBri, onBrightness, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sBri, onBrightnessDone, LV_EVENT_RELEASED, nullptr);

  // Rescue: switch a static IP back to DHCP (long press, then restart)
  if (settings.staticIp) {
    lv_obj_t* b = button(tab, "Feste IP aus (DHCP) \xE2\x80\x93 lange dr\xC3\xBC" "cken", PAGE_W - 20, 56, nullptr, nullptr,
                         C_CARD_HI, &font_de_18);
    lv_obj_add_event_cb(b, onDhcp, LV_EVENT_LONG_PRESSED, nullptr);
  }

  sInfo = label(tab, &font_de_14, C_MUTED, "");
  lv_obj_set_width(sInfo, PAGE_W - 20);
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

void refreshValues() {
  bool tankOn = store::devOnline(DEV_TANK);
  bool heatOn = store::devOnline(DEV_HEATER);
  bool lvlOn  = store::devOnline(DEV_LEVEL);
  bool fanOn  = store::devOnline(DEV_FAN);

  // overview
  if (ovWater.pct) {
    setTank(ovWater, DP_WATER_PCT, DP_WATER_L, tankOn);
    setTank(ovGrey, DP_GREY_PCT, DP_GREY_L, tankOn);
    setTank(ovGas, DP_GAS_PCT, DP_GAS_L, tankOn);
    lv_label_set_text(ovHeatVal, fmt(store::get(DP_HEAT_ROOM), 1, "\xC2\xB0""C").c_str());
    String hs = !heatOn ? "offline"
              : (store::get(DP_HEAT_HVAC) == "heat" ? "An, Soll " + fmt(store::get(DP_HEAT_SETPOINT), 0, "\xC2\xB0""C") : "Aus");
    lv_label_set_text(ovHeatSub, hs.c_str());
    lv_obj_set_style_text_color(ovHeatVal, store::get(DP_HEAT_HVAC) == "heat" ? C_HEAT : C_TEXT, 0);

    bool level = store::get(DP_LVL_OK) == "ON";
    lv_label_set_text(ovLvlVal, !store::has(DP_LVL_OK) ? "--" : (level ? "Eben" : "Schief"));
    lv_obj_set_style_text_color(ovLvlVal, !store::has(DP_LVL_OK) ? C_TEXT : (level ? C_OK : C_WARN), 0);
    lv_label_set_text(ovLvlSub, lvlOn ? ("Nick " + fmt(store::get(DP_LVL_PITCH), 1, "\xC2\xB0") +
                                         "  Roll " + fmt(store::get(DP_LVL_ROLL), 1, "\xC2\xB0")).c_str() : "offline");

    lv_label_set_text(ovFanVal, fanOn ? "Bereit" : "--");
    lv_label_set_text(ovFanSub, fanOn ? (store::get(DP_FAN_HOLD) == "ON" ? "Auto-Hold aktiv" : "Tippen zum Steuern") : "offline");
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
  }
  // Only a NEW warning wakes the screen; a changing value in an existing one
  // (temperature, voltage, runtime) must not keep the display on.
  String keys = warnKeys(warn);
  if (warn.length() && newWarning(keys, "\n" + lastWarnKeys)) {
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

  // temperatures
  bool tempOn = store::devOnline(DEV_TEMP);
  if (ovTemp.line[0] || tStatus) {
    std::vector<TempItem> items = tempItems();
    bool anyAlarm = false;
    for (auto& it : items) anyAlarm |= it.alarm != 0;
    if (ovTemp.line[0]) {
      // value first, so it stays visible when a long name is cut off
      std::vector<String> lines;
      std::vector<lv_color_t> cols;
      for (size_t i = 0; i < items.size() && i < 3; i++) {
        lines.push_back(items[i].val + "  " + items[i].name);
        cols.push_back(!tempOn ? C_MUTED : (items[i].alarm ? C_WARN : C_TEXT));
      }
      String sub = !tempOn ? String("offline")
                 : anyAlarm ? String("Alarm")
                 : items.size() > 3 ? "+" + String(items.size() - 3) + " weitere"
                 : String("");
      setList(ovTemp, lines, cols, sub);
    }
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
    for (size_t i = 0; i < items.size() && i < 3; i++) {
      const BatItem& b = items[i];
      // value first: SoC, for the BM2 the voltage
      String val = !b.ok ? String("--")
                 : b.state.length() ? commaNum(b.v, 1) + " V"
                 : isnan(b.soc) ? String("--") : String((int)lroundf(b.soc)) + " %";
      lines.push_back(val + "  " + b.name);
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

      lv_label_set_text((lv_obj_t*)lv_obj_get_user_data(r.card), dispText(b.name).c_str());
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
      if (b.ok) {
        if (bm2) sub = isnan(b.soc) ? String("") : String((int)lroundf(b.soc)) + " % (gesch\xC3\xA4tzt)";
        else {
          if (!isnan(b.v)) sub = commaNum(b.v, 2) + " V";
          if (b.rt >= 0) sub += "  \xE2\x80\xA2  noch " + runtimeText(b.rt);
          else if (!isnan(b.w) && b.w > 1) sub += "  \xE2\x80\xA2  l\xC3\xA4" "dt";
        }
      }
      lv_label_set_text(r.sub, dispText(sub).c_str());

      lv_bar_set_value(r.bar, isnan(b.soc) ? 0 : constrain((int)lroundf(b.soc), 0, 100), LV_ANIM_OFF);
      lv_obj_set_style_bg_color(r.bar, socCol, LV_PART_INDICATOR);
      lv_obj_set_style_border_width(r.card, (live && (b.problem || stateBad || low)) ? 2 : 0, 0);
      lv_obj_set_style_border_color(r.card, C_WARN, 0);
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
    if (isnan(pendingSet))
      lv_label_set_text(hSet, fmt(store::get(DP_HEAT_SETPOINT), 0, "\xC2\xB0""C").c_str());

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

void refreshHeader() {
  if (wifimgr::timeValid()) {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    lv_label_set_text_fmt(hdrClock, "%02d:%02d", tm.tm_hour, tm.tm_min);
  } else {
    lv_label_set_text(hdrClock, "");
  }
  String s;
  if (wifimgr::apActive() && !wifimgr::staConnected()) s = LV_SYMBOL_WIFI " AP";
  else if (!wifimgr::staConnected()) s = LV_SYMBOL_WIFI " " LV_SYMBOL_CLOSE;
  else if (!mqttc::connected()) s = "MQTT " LV_SYMBOL_CLOSE;
  else s = LV_SYMBOL_WIFI;
  bool warn = lastWarn.length() > 0;
  if (warn) s = LV_SYMBOL_WARNING " " + s;
  lv_label_set_text(hdrStatus, s.c_str());
  lv_obj_set_style_text_color(hdrStatus, (wifimgr::staConnected() && mqttc::connected() && !warn) ? C_MUTED : C_WARN, 0);

  // system page
  if (sIp) {
    String url, qr, info;
    if (wifimgr::staConnected()) {
      url = "http://" + wifimgr::ipString() + "/";
      qr = url;
      info = "Browser: " + url + "\n" + settings.hostname + ".local";
    } else {
      // QR joins the setup access point
      qr = "WIFI:T:WPA;S:" + wifimgr::apSsid() + ";P:" + settings.apPass + ";;";
      info = "WLAN " + wifimgr::apSsid() + "\nPasswort " + settings.apPass + "\ndann http://192.168.4.1";
    }
    if (qr != lastQr) {
      lv_qrcode_update(sQr, qr.c_str(), qr.length());
      lastQr = qr;
    }
    lv_label_set_text(sQrLbl, wifimgr::staConnected() ? "Webseite öffnen" : "Setup-WLAN");
    lv_label_set_text(sIp, wifimgr::ipString().c_str());
    if (wifimgr::dhcpFallback()) info += "\nFeste IP ging nicht \xE2\x80\x93 DHCP bis zum Neustart";
    String net = info + "\n\nWLAN: " + (wifimgr::staConnected() ? settings.wifiSsid + " (" + String(wifimgr::rssi()) + " dBm)" : String("nicht verbunden")) +
                 "\nMQTT: " + (settings.mqttHost.isEmpty() ? String("nicht eingerichtet") : settings.mqttHost + (mqttc::connected() ? " ok" : " getrennt"));
    lv_label_set_text(sNet, net.c_str());
    lv_label_set_text_fmt(sInfo, "%s %s  \xE2\x80\xA2  Touch %s  \xE2\x80\xA2  Heap %u kB  PSRAM %u kB",
                          FW_NAME, FW_VERSION, disp::touchOk() ? "ok" : "fehlt",
                          (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));
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
  ovWarn = ovWarnLbl = nullptr;
  ovTemp = ovBat = ListW();
  tStatus = nullptr;
  bStatus = nullptr;
  bRows.clear();
  tRows.clear();
  tVals.clear();
  lastQr = "";
  tileW.assign(settings.tiles.size(), TileW());
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
  lv_obj_align(tv, LV_ALIGN_BOTTOM_MID, 0, 0);
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
      case PG_SYSTEM:   buildSystem(tab); break;
      default:          buildCustom(tab, p.custom); break;
    }
  }
  lv_obj_add_event_cb(tv, onTabChanged, LV_EVENT_VALUE_CHANGED, nullptr);

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

  // header: < title > , dots, clock, status
  lv_obj_t* hdr = lv_obj_create(scr);
  lv_obj_set_size(hdr, LCD_WIDTH, HDR_H);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(hdr, C_CARD, 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* prev = button(hdr, LV_SYMBOL_LEFT, 56, HDR_H, onPrev, nullptr, C_CARD);
  lv_obj_set_style_radius(prev, 0, 0);
  lv_obj_align(prev, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* next = button(hdr, LV_SYMBOL_RIGHT, 56, HDR_H, onNext, nullptr, C_CARD);
  lv_obj_set_style_radius(next, 0, 0);
  lv_obj_align(next, LV_ALIGN_RIGHT_MID, 0, 0);

  // "<"  [Home]  page title
  lv_obj_t* home = button(hdr, LV_SYMBOL_HOME, HOME_W, HDR_H - 12, onHome, nullptr, C_CARD_HI);
  lv_obj_align(home, LV_ALIGN_LEFT_MID, 60, 0);

  hdrTitle = label(hdr, &font_de_22, C_TEXT, "");
  lv_obj_align(hdrTitle, LV_ALIGN_LEFT_MID, TITLE_X, -6);
  hdrDots = label(hdr, &font_de_14, C_MUTED, "");
  lv_obj_align(hdrDots, LV_ALIGN_LEFT_MID, TITLE_X, 15);
  hdrName = label(hdr, &font_de_18, C_MUTED, "");
  lv_label_set_long_mode(hdrName, LV_LABEL_LONG_DOT);
  lv_obj_set_height(hdrName, lv_font_get_line_height(&font_de_18));
  hdrClock = label(hdr, &font_de_22, C_TEXT, "");
  lv_obj_align(hdrClock, LV_ALIGN_RIGHT_MID, -66, -6);
  hdrStatus = label(hdr, &font_de_14, C_MUTED, "");
  lv_obj_align(hdrStatus, LV_ALIGN_RIGHT_MID, -66, 15);

  build();
}

void loop() {
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
