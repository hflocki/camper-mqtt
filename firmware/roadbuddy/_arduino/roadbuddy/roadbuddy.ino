#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include <Preferences.h>
#include "config.h"
#include "webota.h"

#define FW_VERSION "1.2.2"

// ---------- Sensor selection ----------
// Choose in config.h which sensors are used (1 = enabled, 0 = disabled).
// Disabled sensors get no page and no MQTT subscription. Defaults apply if config.h does not define them.
#ifndef USE_LEVEL
#define USE_LEVEL 1   // pitch / roll level sensor
#endif
#ifndef USE_WATER
#define USE_WATER 1   // fresh water tank
#endif
#ifndef USE_GAS
#define USE_GAS 1     // gas bottle
#endif
#ifndef USE_TEMP
#define USE_TEMP 1    // temperatures from camper-temp (1-4 sensors on one page, 5-8 on a second)
#endif
#ifndef USE_BATTERY
#define USE_BATTERY 1 // batteries from camper-battery (up to 3 on one page)
#endif

// Mobile signal of the router for the header (JSON with rsrp / rssi, as on the wall display). "" = WiFi bars only
#ifndef ROUTER_TOPIC
#define ROUTER_TOPIC "camper/router/mobile"
#endif
// Login for the update page. Empty user = no login
#ifndef WEB_USER
#define WEB_USER ""
#endif
#ifndef WEB_PASS
#define WEB_PASS ""
#endif

WiFiClient espClient;
PubSubClient client(espClient);
M5Canvas canvas(&M5.Display);  // off-screen buffer, avoids flicker

// ---------- Topics ----------
// camper-tanklevel firmware: <prefix>/water/..., <prefix>/gas/..., one <prefix>/status for both
const char* TOPIC_TANK_WATER      = "camper/tanklevel/water/";
const char* TOPIC_TANK_GAS        = "camper/tanklevel/gas/";   // level, content, battery, ble_state
const char* TOPIC_TANK_STATUS     = "camper/tanklevel/status";
// camper-temp firmware: all sensors as one JSON list [{"id","name","t","alarm"}, ...]
const char* TOPIC_TEMP_SENSORS    = "camper/temp/sensors";
const char* TOPIC_TEMP_STATUS     = "camper/temp/status";
// camper-battery firmware: all batteries as one JSON list [{"id","name","ok","soc","v","w","rt","st"}, ...]
const char* TOPIC_BAT_LIST        = "camper/battery/batteries";
const char* TOPIC_BAT_STATUS      = "camper/battery/status";
String levelPrefix;   // level topic without the trailing '#', e.g. "camper/level/"

// ---------- Credentials ----------
// A build with a real config.h stores WiFi / MQTT settings in flash (NVS). The CI build from GitHub Actions
// only has the placeholders of config.example.h and reads them back from there, so its .bin can be
// installed over the update page without losing the connection.
String cfgSsid, cfgPass, cfgBroker, cfgMqttUser, cfgMqttPass, cfgLevelTopic, cfgWebUser, cfgWebPass;
int cfgPort = 1883;
bool cfgFromFlash = false;   // true = settings came from NVS (CI build)

// ---------- Power management ----------
const unsigned long DISPLAY_TIMEOUT_MS = 60UL * 1000UL;        // display off after this much inactivity
const unsigned long SLEEP_DELAY_MS     = 5UL * 60UL * 1000UL;  // deep sleep this long after the display went off
const unsigned long INPUT_LOCK_MS      = 1000;                 // ignore touches right after boot (the wake-up touch)
const unsigned long UPDATE_AWAKE_MS    = 15UL * 60UL * 1000UL; // the system page keeps the device awake this long (update mode)
// Safety wake-up: a timer is always set so this also works with M5Unified versions where 0 means "no timer".
// On a timer wake-up the device immediately goes back to sleep.
const uint64_t SLEEP_SAFETY_TIMER_US   = 24ULL * 3600ULL * 1000000ULL;

// ---------- State ----------

// Pages in display order; the system page is always present
enum PageId { PAGE_LEVEL, PAGE_WATER, PAGE_GAS, PAGE_TEMP1, PAGE_TEMP2, PAGE_BATTERY, PAGE_SYSTEM };
static const PageId PAGES[] = {
#if USE_LEVEL
  PAGE_LEVEL,
#endif
#if USE_WATER
  PAGE_WATER,
#endif
#if USE_GAS
  PAGE_GAS,
#endif
#if USE_TEMP
  PAGE_TEMP1,
  PAGE_TEMP2,   // only shown when camper-temp reports more than 4 sensors
#endif
#if USE_BATTERY
  PAGE_BATTERY,
#endif
  PAGE_SYSTEM
};
const int PAGE_COUNT = sizeof(PAGES) / sizeof(PAGES[0]);

// Small symbols drawn into the colored icon circles
enum Glyph { GL_DROP, GL_FLAME, GL_THERMO, GL_BATT, GL_LEVEL, GL_INFO };
RTC_DATA_ATTR int page_index = 0;  // index into PAGES, survives deep sleep
bool display_on = true;
bool dirty = true;
unsigned long lastActivityMs = 0;
unsigned long displayOffAtMs = 0;
unsigned long systemPageSince = 0; // when the system page was opened (update mode window)

// All values below survive deep sleep (RTC memory), so the last known value is always shown.
// Values that are not refreshed for a while are drawn dimmed instead of being hidden.

// Level sensor
RTC_DATA_ATTR float pitch = 0, roll = 0;
RTC_DATA_ATTR bool havePitch = false, haveRoll = false;
RTC_DATA_ATTR float levelTol = 0.5f;   // tolerance of the level sensor in degrees (topic "tolerance")
RTC_DATA_ATTR int8_t isLevelState = -1; // topic "is_level": 1 = ON, 0 = OFF, -1 = unknown
RTC_DATA_ATTR float wedge[4] = {NAN, NAN, NAN, NAN};   // wedge height in cm: front left, front right, rear left, rear right
unsigned long lastPitchMs = 0, lastRollMs = 0;   // time of the last update (0 = none since boot)
bool levelOnline = false;
bool levelStatusKnown = false;
const unsigned long STALE_MS = 30000;            // level values older than this are drawn dimmed

// Water tank
RTC_DATA_ATTR int waterPercent = -1;       // -1 = no data yet
RTC_DATA_ATTR float waterLiters = -1.0f;   // <0 = no data yet
bool waterOnline = false;
bool waterStatusKnown = false;

// Gas bottle
RTC_DATA_ATTR int gasPercent = -1;         // -1 = no data yet (topic "level", 0-100 %)
RTC_DATA_ATTR float gasLiters = -1.0f;     // <0 = no data yet (topic "content")
RTC_DATA_ATTR int gasBattery = -1;         // sensor battery in %, -1 = unknown
bool gasBleOk = true;        // false if the gas ESP reports a BLE problem
bool gasOnline = false;
bool gasStatusKnown = false;

// Temperatures (camper-temp)
const int TEMP_MAX = 8;
const int TEMP_PER_PAGE = 4;
struct TempEntry {
  char name[20];
  float t;        // NAN = sensor has no valid reading
  int8_t alarm;   // 0 = none, 1 = too cold, 2 = too warm
};
RTC_DATA_ATTR TempEntry temps[TEMP_MAX];
RTC_DATA_ATTR int tempCount = -1;   // -1 = no list received yet
bool tempOnline = false;
bool tempStatusKnown = false;

// Batteries (camper-battery)
const int BAT_MAX = 3;
struct BatEntry {
  char name[20];
  char state[20];  // BM2 state text, "" if the monitor measures current
  float v;         // V, NAN = unknown
  float w;         // W, positive = charging, NAN = not measured (BM2)
  int32_t rt;      // remaining runtime in minutes while discharging, -1 = unknown
  int8_t soc;      // %, -1 = unknown
  bool ok;         // monitor reachable
  bool problem;    // BMS reports an error
};
RTC_DATA_ATTR BatEntry bats[BAT_MAX];
RTC_DATA_ATTR int batCount = -1;   // -1 = no list received yet
bool batOnline = false;
bool batStatusKnown = false;

// Mobile signal of the router (not kept over deep sleep, it is refreshed every minute)
float rtRsrp = NAN, rtRssi = NAN;
String rtNet;
unsigned long rtLastMs = 0;
const unsigned long RT_FRESH_MS = 5UL * 60UL * 1000UL;   // the router sends every 60 s by default

unsigned long lastMqttAttempt = 0;
const unsigned long MQTT_RETRY_MS = 5000;
unsigned long lastRedraw = 0;

// Flip these if the bubble / icons move in the wrong direction for your mounting
// The dot sits on the LOW side of the van (where the wedges go): right when the right side is low,
// down when the rear is low. Same as the wall display and its web page.
const float BUBBLE_SIGN_X = 1.0f;
const float BUBBLE_SIGN_Y = 1.0f;
const float BUBBLE_FULL_SCALE_DEG = 3.0f;   // tilt at which the dot reaches the edge of the van

// ---------- Colors ----------
// Same palette as the wall display (firmware/display/.../ui.cpp, C_*). Keep in sync.
static constexpr uint16_t rgb(uint32_t c) {
  return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}
static const uint16_t COL_BG      = rgb(0x0B1015);
static const uint16_t COL_NAV     = rgb(0x10161D);   // header and footer
static const uint16_t COL_PANEL   = rgb(0x1A232D);   // cards
static const uint16_t COL_GRID    = rgb(0x27323E);   // empty segments, inactive elements
static const uint16_t COL_LINE    = rgb(0x263241);   // hairline border of cards
static const uint16_t COL_OUTLINE = rgb(0x55657A);   // outline of tank / bottle / level circle
static const uint16_t COL_TEXT    = rgb(0xE8EDF2);
static const uint16_t COL_DIM     = rgb(0x8696A6);
static const uint16_t COL_OK      = rgb(0x4CC27A);
static const uint16_t COL_WARN    = rgb(0xF2C94C);   // yellow: getting low
static const uint16_t COL_BAD     = rgb(0xE5484D);   // red: critical
static const uint16_t COL_ACCENT  = rgb(0x5AB0F0);
static const uint16_t COL_WATER   = rgb(0x3BA3E8);
static const uint16_t COL_WAVE    = rgb(0x8CCBF2);
static const uint16_t COL_GAS     = rgb(0xF0A030);
static const uint16_t COL_GAS_HI  = rgb(0xF7C977);
static const uint16_t COL_STEEL   = rgb(0x6F7F94);
static const uint16_t COL_LEVEL   = rgb(0x2BA58A);
static const uint16_t COL_VAN     = rgb(0x6F8FBF);   // outline of the van on the level page
static const uint16_t COL_VAN_IN  = rgb(0x1A2638);   // inside of the van
static const uint16_t COL_LVL_HI  = rgb(0xF0A030);   // wheels that need a wedge and the dot while not level

// Battery segment bar: 13 fields from red (empty) to green (full), as on the wall display
const int SEG_N = 13;
static const uint32_t SEG_COL[SEG_N] = {0xE5484D, 0xEC5F3F, 0xF07A34, 0xF2952B, 0xF2AE2A, 0xF2C94C, 0xE3D24A,
                                        0xCBD449, 0xAFD24B, 0x92CD52, 0x75C85E, 0x5EC56C, 0x4CC27A};
// Batteries without a state of charge (lead, BM2): voltage range of the segment bar
const float SEG_VMIN = 11.8f, SEG_VMAX = 12.8f;

// Layout
const int HDR_H = 32;
const int FTR_H = 22;
const int TOP = 40;                    // first pixel row below the header
const int BOTTOM = 240 - FTR_H - 4;    // last usable row above the footer

// ---------- Helpers ----------
bool isFresh(unsigned long ts) {
  return ts != 0 && (millis() - ts) < STALE_MS;
}

// Number with a decimal comma
String num(float v, int decimals) {
  String s = String(v, decimals);
  s.replace('.', ',');
  if (s == "-0" || s == "-0,0" || s == "-0,00") s.remove(0, 1);
  return s;
}

// Is the van level? The sensor's own decision (is_level) wins, otherwise the tolerance
bool vanIsLevel() {
  if (isLevelState >= 0) return isLevelState == 1;
  return fabsf(pitch) <= levelTol && fabsf(roll) <= levelTol;
}

// Tank state colors as on the wall display: fresh water yellow below 25 %, red below 10 %; gas red below 15 %
uint16_t waterColor(int p) { return p < 10 ? COL_BAD : (p < 25 ? COL_WARN : COL_WATER); }
uint16_t gasColor(int p)   { return p < 15 ? COL_BAD : COL_GAS; }

// Stores a value only if it changed (saves flash writes)
void putIfChanged(Preferences& p, const char* key, const String& v) {
  if (p.getString(key, "") != v) p.putString(key, v);
}

void loadCredentials() {
  bool placeholder = strlen(ssid) == 0 || strcmp(ssid, "YOUR_WIFI_SSID") == 0;
  Preferences p;
  p.begin("roadbuddy", false);
  if (!placeholder) {
    cfgSsid = ssid; cfgPass = password; cfgBroker = mqtt_server; cfgPort = mqtt_port;
    cfgMqttUser = mqtt_user; cfgMqttPass = mqtt_pass; cfgLevelTopic = mqtt_topic;
    putIfChanged(p, "ssid", cfgSsid);
    putIfChanged(p, "pass", cfgPass);
    putIfChanged(p, "broker", cfgBroker);
    putIfChanged(p, "muser", cfgMqttUser);
    putIfChanged(p, "mpass", cfgMqttPass);
    putIfChanged(p, "ltopic", cfgLevelTopic);
    cfgWebUser = WEB_USER; cfgWebPass = WEB_PASS;
    putIfChanged(p, "wuser", cfgWebUser);
    putIfChanged(p, "wpass", cfgWebPass);
    if (p.getInt("port", 0) != cfgPort) p.putInt("port", cfgPort);
  } else if (p.getString("ssid", "").length()) {
    cfgFromFlash = true;
    cfgSsid = p.getString("ssid", "");
    cfgPass = p.getString("pass", "");
    cfgBroker = p.getString("broker", mqtt_server);
    cfgPort = p.getInt("port", mqtt_port);
    cfgMqttUser = p.getString("muser", "");
    cfgMqttPass = p.getString("mpass", "");
    cfgLevelTopic = p.getString("ltopic", mqtt_topic);
    cfgWebUser = p.getString("wuser", "");
    cfgWebPass = p.getString("wpass", "");
  } else {
    // Placeholder build and nothing stored: flash once over USB with a real config.h
    cfgSsid = ssid; cfgPass = password; cfgBroker = mqtt_server; cfgPort = mqtt_port;
    cfgMqttUser = mqtt_user; cfgMqttPass = mqtt_pass; cfgLevelTopic = mqtt_topic;
    cfgWebUser = WEB_USER; cfgWebPass = WEB_PASS;
    Serial.println("No credentials: flash once over USB with your own config.h");
  }
  p.end();
}

void startWifi() {
  WiFi.setHostname("roadbuddy");  // must be set before WiFi.mode()
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
}

// ---------- Pages ----------
// The second temperature page only exists while more than TEMP_PER_PAGE sensors are reported
bool pageVisible(int i) {
  if (PAGES[i] == PAGE_TEMP2) return tempCount > TEMP_PER_PAGE;
  return true;
}

int visibleCount() {
  int n = 0;
  for (int i = 0; i < PAGE_COUNT; i++) if (pageVisible(i)) n++;
  return n;
}

int visiblePosition(int index) {   // position of PAGES[index] among the visible pages
  int n = 0;
  for (int i = 0; i < index; i++) if (pageVisible(i)) n++;
  return n;
}

int stepPage(int from, int dir) {
  int i = from;
  for (int k = 0; k < PAGE_COUNT; k++) {
    i = (i + dir + PAGE_COUNT) % PAGE_COUNT;
    if (pageVisible(i)) return i;
  }
  return from;
}

bool onSystemPage() { return PAGES[page_index] == PAGE_SYSTEM; }

// Update mode: the system page keeps display and WiFi on, at most UPDATE_AWAKE_MS
bool updateModeActive() {
  return onSystemPage() && display_on && millis() - systemPageSince < UPDATE_AWAKE_MS;
}

// ---------- Drawing primitives ----------
// Card: flat panel with a 1 px border, like the tiles of the wall display
void card(int x, int y, int w, int h, uint16_t border) {
  canvas.fillRoundRect(x, y, w, h, 10, COL_PANEL);
  canvas.drawRoundRect(x, y, w, h, 10, border);
}


// Small symbol in COL_TEXT, about 16 px, centered at (cx, cy)
void drawGlyph(Glyph g, int cx, int cy, uint16_t col, uint16_t bg) {
  switch (g) {
    case GL_DROP:
      canvas.fillCircle(cx, cy + 2, 5, col);
      canvas.fillTriangle(cx - 5, cy + 1, cx + 5, cy + 1, cx, cy - 8, col);
      break;
    case GL_FLAME:
      canvas.fillCircle(cx, cy + 3, 5, col);
      canvas.fillTriangle(cx - 5, cy + 2, cx + 5, cy + 2, cx + 1, cy - 8, col);
      canvas.fillCircle(cx, cy + 4, 2, bg);
      break;
    case GL_THERMO:
      canvas.fillRoundRect(cx - 2, cy - 8, 5, 12, 2, col);
      canvas.fillCircle(cx, cy + 4, 4, col);
      break;
    case GL_BATT:
      canvas.fillRoundRect(cx - 7, cy - 4, 13, 9, 1, col);
      canvas.fillRect(cx + 6, cy - 2, 2, 5, col);
      canvas.fillRect(cx - 5, cy - 2, 2, 5, bg);   // "-" / "+" hint
      canvas.fillRect(cx + 1, cy - 2, 2, 5, bg);
      break;
    case GL_LEVEL:
      canvas.drawCircle(cx, cy, 7, col);
      canvas.drawCircle(cx, cy, 6, col);
      canvas.fillCircle(cx, cy, 2, col);
      break;
    case GL_INFO:
      canvas.fillCircle(cx, cy - 6, 2, col);
      canvas.fillRect(cx - 1, cy - 2, 3, 9, col);
      break;
  }
}

// Colored circle with a symbol, grey when the source is offline
void iconDot(int cx, int cy, Glyph g, uint16_t col, bool live) {
  uint16_t c = live ? col : COL_GRID;
  canvas.fillCircle(cx, cy, 12, c);
  drawGlyph(g, cx, cy, live ? COL_TEXT : COL_DIM, c);
}

// Card head: icon circle and title
void cardHead(int x, int y, Glyph g, uint16_t col, bool live, const char* title) {
  iconDot(x + 14, y + 14, g, col, live);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(COL_TEXT);
  canvas.drawString(title, x + 34, y + 14);
}

// Small clock symbol for remaining runtimes
void clockIcon(int cx, int cy, uint16_t col) {
  canvas.drawCircle(cx, cy, 5, col);
  canvas.drawFastVLine(cx, cy - 3, 4, col);
  canvas.drawFastHLine(cx, cy, 3, col);
}

// Status line: dot + text
void statusDot(int x, int cy, uint16_t col, const String& text) {
  canvas.fillCircle(x + 5, cy, 5, col);
  if (!text.length()) return;
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(col == COL_OK ? COL_DIM : col);
  canvas.drawString(text, x + 16, cy);
}

// Segment bar (battery), frac 0..1 or NAN
void segBar(int x, int y, int w, int h, float frac, bool live) {
  const int gap = 2;
  int sw = (w - gap * (SEG_N - 1)) / SEG_N;
  int lit = isnan(frac) ? 0 : (int)lroundf(constrain(frac, 0.0f, 1.0f) * SEG_N);
  for (int i = 0; i < SEG_N; i++) {
    uint16_t c = i < lit ? (live ? rgb(SEG_COL[i]) : COL_DIM) : COL_GRID;
    canvas.fillRoundRect(x + i * (sw + gap), y, sw, h, 2, c);
  }
}

// ---------- Header / footer ----------
void drawBattery(int x, int y) {
  int level = M5.Power.getBatteryLevel();  // 0..100, may be negative if unknown
  bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging);
  if (level < 0) level = 0;
  if (level > 100) level = 100;

  uint16_t col = charging ? COL_ACCENT : (level > 40 ? COL_OK : (level > 15 ? COL_WARN : COL_BAD));

  // Percentage text
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(COL_DIM);
  canvas.setTextDatum(middle_right);
  canvas.drawString(String(level) + "%", x - 6, y + 8);

  // Battery body + nub
  canvas.drawRoundRect(x, y, 30, 16, 3, COL_DIM);
  canvas.fillRect(x + 30, y + 5, 3, 6, COL_DIM);
  int w = (26 * level) / 100;
  if (w > 0) canvas.fillRoundRect(x + 2, y + 2, w, 12, 2, col);

  // Charging marker
  if (charging) {
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(COL_BG);
    canvas.setTextDatum(middle_center);
    canvas.drawString("+", x + 15, y + 8);
  }
}

bool routerFresh() {
  return rtLastMs != 0 && millis() - rtLastMs < RT_FRESH_MS && (!isnan(rtRsrp) || !isnan(rtRssi));
}

int routerBars() {
  if (!routerFresh()) return 0;
  // RSRP describes LTE better than RSSI (same thresholds as the wall display)
  if (!isnan(rtRsrp)) return rtRsrp >= -80 ? 5 : rtRsrp >= -90 ? 4 : rtRsrp >= -100 ? 3 : rtRsrp >= -110 ? 2 : 1;
  return rtRssi >= -65 ? 5 : rtRssi >= -75 ? 4 : rtRssi >= -85 ? 3 : rtRssi >= -95 ? 2 : 1;
}

String routerNet() {
  if (rtNet.length()) return rtNet;
  return isnan(rtRsrp) ? String("") : String("LTE");   // RSRP only exists on LTE / 5G
}

// Signal bars: mobile signal of the router if available, otherwise the WiFi signal of this device.
// Green = WiFi + MQTT ok, yellow = MQTT missing, red cross = no WiFi
void drawConnectionIcon(int x, int y) {
  bool wifiOk = WiFi.status() == WL_CONNECTED;
  bool mqttOk = client.connected();
  uint16_t on = mqttOk ? COL_OK : COL_WARN;
  bool mobile = wifiOk && routerFresh();
  int n = mobile ? 5 : 4;
  int lit = 0;
  if (mobile) lit = routerBars();
  else if (wifiOk) {
    int r = WiFi.RSSI();
    lit = r > -55 ? 4 : (r > -65 ? 3 : (r > -75 ? 2 : 1));
  }
  for (int i = 0; i < n; i++) {
    int h = 4 + i * (mobile ? 3 : 4);
    canvas.fillRoundRect(x + i * 6, y + 16 - h, 4, h, 1, i < lit ? on : COL_GRID);
  }
  if (!wifiOk) {
    canvas.drawLine(x, y, x + 22, y + 16, COL_BAD);
    canvas.drawLine(x, y + 16, x + 22, y, COL_BAD);
  }
  if (mobile) {
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(routerNet(), x + n * 6 + 4, y + 9);
  }
}

void drawHeader() {
  canvas.fillRect(0, 0, canvas.width(), HDR_H, COL_NAV);
  drawConnectionIcon(10, 8);

  // Page indicator, centered (only visible pages); the active page is a pill like on the wall display
  int count = visibleCount();
  int current = visiblePosition(page_index);
  const int step = 14;
  int x0 = canvas.width() / 2 - ((count - 1) * step + 10) / 2;
  int x = x0;
  for (int i = 0; i < count; i++) {
    if (i == current) {
      canvas.fillRoundRect(x - 3, 12, 20, 8, 4, COL_ACCENT);
      x += step + 10;
    } else {
      canvas.fillCircle(x + 1, 16, 3, COL_OUTLINE);
      x += step;
    }
  }

  drawBattery(canvas.width() - 42, 8);
  canvas.drawFastHLine(0, HDR_H, canvas.width(), COL_LINE);
}

// Footer: previous page / power / next page (aligned with the three touch buttons)
void drawFooter() {
  int y = canvas.height() - FTR_H;
  int cy = y + 11;
  canvas.fillRect(0, y, canvas.width(), FTR_H, COL_NAV);
  canvas.drawFastHLine(0, y, canvas.width(), COL_LINE);

  canvas.fillTriangle(45, cy, 61, cy - 7, 61, cy + 7, COL_DIM);
  canvas.fillTriangle(275, cy, 259, cy - 7, 259, cy + 7, COL_DIM);

  // Power symbol: ring with a gap and a vertical bar
  canvas.drawCircle(160, cy + 1, 7, COL_DIM);
  canvas.fillRect(157, cy - 9, 7, 7, COL_NAV);
  canvas.drawFastVLine(160, cy - 9, 8, COL_DIM);
  canvas.drawFastVLine(161, cy - 9, 8, COL_DIM);
}

// Value with a degree sign (the GFX fonts have none); datum: middle_center or middle_right at (x, cy)
void drawDegrees(int x, int cy, float t, uint16_t col, const lgfx::IFont* font, int ring, bool alignRight) {
  String s = num(t, 1);
  canvas.setFont(font);
  int w = canvas.textWidth(s);
  int total = w + ring * 2 + 4;
  int x0 = alignRight ? x - total : x - total / 2;
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(col);
  canvas.drawString(s, x0, cy);
  canvas.drawCircle(x0 + w + ring + 3, cy - (int)(canvas.fontHeight() * 0.22f), ring, col);
}

// ---------- Level page ----------
// Top view of the van: wedge height at each wheel, the dot in the middle moves to the low side.
// Right: state and the tilt across / along in words.

bool haveWedges() {
  for (int i = 0; i < 4; i++) if (isnan(wedge[i])) return false;
  return true;
}

int wedgeCm(int i) { return isnan(wedge[i]) ? -1 : (int)lroundf(fmaxf(wedge[i], 0.0f)); }

// "0,9" + degree ring + " rechts tief", left aligned at (x, cy)
void drawTiltText(int x, int cy, float v, bool positiveIsFirst, const char* first, const char* second,
                  uint16_t col) {
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(col);
  if (fabsf(v) <= levelTol) {
    canvas.drawString("eben", x, cy);
    return;
  }
  String n = num(fabsf(v), 1);
  canvas.drawString(n, x, cy);
  int w = canvas.textWidth(n);
  canvas.drawCircle(x + w + 4, cy - 5, 2, col);
  canvas.drawString(String((v > 0) == positiveIsFirst ? first : second) + " tief", x + w + 10, cy);
}

void drawVan(int cx, int top, int w, int h, bool live) {
  bool level = vanIsLevel();
  bool have = havePitch && haveRoll;
  bool wedges = haveWedges();

  // "vorne" above the van
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(bottom_center);
  canvas.setTextColor(COL_DIM);
  canvas.drawString("vorne", cx, top - 2);

  // Body
  int x = cx - w / 2;
  canvas.fillRoundRect(x, top, w, h, 20, COL_VAN_IN);
  canvas.drawRoundRect(x, top, w, h, 20, COL_VAN);
  canvas.drawRoundRect(x + 1, top + 1, w - 2, h - 2, 19, COL_VAN);

  // Wheels with the wedge height next to them
  const int ww = 8, wh = 24;
  int wy[2] = {top + 22, top + h - 22 - wh};
  for (int i = 0; i < 4; i++) {
    bool right = i & 1;
    int y = wy[i >> 1];
    int cm = wedgeCm(i);
    bool hi = live && have && !level && cm >= 1;
    uint16_t col = hi ? COL_LVL_HI : COL_OUTLINE;
    int wx = right ? x + w - ww / 2 : x - ww / 2;
    canvas.fillRoundRect(wx, y, ww, wh, 3, col);

    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(right ? middle_left : middle_right);
    canvas.setTextColor(hi ? COL_LVL_HI : COL_DIM);
    String t = wedges ? String(cm) + " cm" : String("--");
    canvas.drawString(t, right ? wx + ww + 5 : wx - 5, y + wh / 2);
  }

  // Target ring and dot in the middle
  int cy = top + h / 2;
  int maxR = w / 2 - 10;
  canvas.drawCircle(cx, cy, 15, COL_OUTLINE);
  canvas.drawCircle(cx, cy, 14, COL_OUTLINE);
  if (!have) return;
  float dx = BUBBLE_SIGN_X * roll  / BUBBLE_FULL_SCALE_DEG * maxR;
  float dy = BUBBLE_SIGN_Y * pitch / BUBBLE_FULL_SCALE_DEG * maxR;
  float len = sqrtf(dx * dx + dy * dy);
  if (len > maxR) { dx *= maxR / len; dy *= maxR / len; }
  uint16_t col = !live ? COL_DIM : (level ? COL_OK : COL_LVL_HI);
  canvas.fillCircle(cx + (int)dx, cy + (int)dy, 8, col);
}

void drawPageLevel() {
  drawHeader();
  bool offline = levelStatusKnown && !levelOnline;
  // Colors stay on with the last known values; only an offline sensor greys the page.
  // Old values (no update for STALE_MS) get a short note instead.
  bool live = !offline;
  bool have = havePitch && haveRoll;
  bool stale = have && !(isFresh(lastPitchMs) && isFresh(lastRollMs));

  // Left: van seen from above
  card(8, TOP, 148, BOTTOM - TOP, COL_LINE);
  drawVan(82, TOP + 22, 50, BOTTOM - TOP - 30, live);

  // Right: state, then across (roll) and along (pitch)
  const int rx = 162, rw = 312 - rx, lx = rx + 10, maxW = rw - 18;
  card(rx, TOP, rw, BOTTOM - TOP, COL_LINE);

  String state;
  uint16_t stateCol;
  if (offline) { state = "offline"; stateCol = COL_BAD; }
  else if (!have) { state = "--"; stateCol = COL_DIM; }
  else if (vanIsLevel()) { state = "Eben"; stateCol = live ? COL_OK : COL_DIM; }
  else { state = "Nicht eben"; stateCol = live ? COL_LVL_HI : COL_DIM; }
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  if (canvas.textWidth(state) > maxW) canvas.setFont(&fonts::FreeSansBold9pt7b);   // keep it inside the card
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(stateCol);
  canvas.drawString(state, lx, TOP + 20);
  canvas.drawFastHLine(rx + 10, TOP + 38, rw - 20, COL_LINE);

  uint16_t valCol = live ? COL_TEXT : COL_DIM;
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(COL_DIM);
  canvas.drawString("Li/Re", lx, TOP + 56);
  canvas.drawString("Vo/Hi", lx, TOP + 110);
  if (have) {
    drawTiltText(lx, TOP + 78, roll, true, "rechts", "links", valCol);    // roll > 0: left higher, right low
    drawTiltText(lx, TOP + 132, pitch, true, "hinten", "vorne", valCol);  // pitch > 0: front higher, rear low
  } else {
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(COL_DIM);
    canvas.drawString("--", lx, TOP + 78);
    canvas.drawString("--", lx, TOP + 132);
  }

  // Bottom: note on old values, otherwise the largest wedge (the full list is on the van)
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  if (live && stale) {
    canvas.setTextColor(COL_DIM);
    canvas.drawString("Werte alt", lx, BOTTOM - 16);
  } else if (have && live && !vanIsLevel() && haveWedges()) {
    int best = -1;
    for (int i = 0; i < 4; i++) if (wedgeCm(i) >= 1 && (best < 0 || wedgeCm(i) > wedgeCm(best))) best = i;
    canvas.setTextColor(COL_LVL_HI);
    canvas.drawString(best >= 0 ? String("max. ") + wedgeCm(best) + " cm" : String("fast eben"), lx, BOTTOM - 16);
  }
}

// ---------- Tank pages (fresh water, gas) ----------
// Left side of a tank card: big percent, amount below, status at the bottom
void drawTankValues(int x, int y, int percent, bool critical, float amount, int amountDecimals,
                    bool offline) {
  canvas.setFont(&fonts::FreeSansBold24pt7b);
  canvas.setTextDatum(middle_left);
  if (percent >= 0) {
    canvas.setTextColor(offline ? COL_DIM : (critical ? COL_BAD : COL_TEXT));
    canvas.drawString(String(percent) + "%", x, y + 78);
  } else {
    canvas.setTextColor(COL_DIM);
    canvas.drawString("--", x, y + 78);
  }

  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(COL_DIM);
  canvas.drawString(amount >= 0 ? num(amount, amountDecimals) + " L" : String("--"), x + 2, y + 120);
}

void drawPageWater() {
  drawHeader();
  bool hasData = waterPercent >= 0;
  bool offline = waterStatusKnown && !waterOnline;
  uint16_t fillCol = offline ? COL_DIM : (hasData ? waterColor(waterPercent) : COL_WATER);
  uint16_t waveCol = offline ? COL_GRID : (fillCol == COL_WATER ? COL_WAVE : COL_TEXT);

  card(8, TOP, 304, BOTTOM - TOP, COL_LINE);
  cardHead(8 + 8, TOP + 8, GL_DROP, COL_WATER, !offline, "Frischwasser");
  drawTankValues(24, TOP, waterPercent, hasData && waterPercent < 10, waterLiters, 0, offline);

  // Status at the bottom: green = online, red = offline, grey = no data yet
  if (offline) statusDot(24, BOTTOM - 20, COL_BAD, "offline");
  else statusDot(24, BOTTOM - 20, waterStatusKnown ? COL_OK : COL_GRID, waterStatusKnown ? "Sensor ok" : "");

  // Right: tank drawing that fills with the state color
  const int tx = 196, ty = TOP + 12, tw = 100, th = BOTTOM - TOP - 24, rad = 10;
  const int inset = 4;
  const int ix = tx + inset, iy = ty + inset;
  const int iw = tw - 2 * inset, ih = th - 2 * inset;
  canvas.fillRoundRect(tx, ty, tw, th, rad, COL_BG);

  // Water with animated surface
  if (hasData && waterPercent > 0) {
    int fillH = ih * waterPercent / 100;
    int bottom = iy + ih;
    int surfaceY = bottom - fillH;
    float phase = millis() / 300.0f;
    for (int x = 0; x < iw; x++) {
      int top = surfaceY + (int)(2.5f * sinf(x / 9.0f + phase));
      if (top < iy) top = iy;
      if (top >= bottom) continue;
      // keep inside the rounded corners at the bottom
      int edge = min(x, iw - 1 - x);
      int cut = edge < rad - inset ? (rad - inset - edge) / 2 : 0;
      if (bottom - cut <= top) continue;
      canvas.fillRect(ix + x, top, 1, bottom - cut - top, fillCol);
      canvas.drawPixel(ix + x, top, waveCol);
    }
  }

  canvas.drawRoundRect(tx, ty, tw, th, rad, COL_OUTLINE);
  canvas.drawRoundRect(tx + 1, ty + 1, tw - 2, th - 2, rad - 1, COL_OUTLINE);
  for (int p = 25; p <= 75; p += 25) {
    int y = iy + ih - ih * p / 100;
    canvas.drawFastHLine(tx + tw - inset - 12, y, 10, COL_OUTLINE);
  }
}

// Horizontal inset of a rounded rectangle at row y (all four corners share radius r)
static int roundedInset(int y, int top, int h, int r) {
  int dy = 0;
  if (y < top + r)            dy = top + r - y;
  else if (y >= top + h - r)  dy = y - (top + h - r - 1);
  if (dy <= 0) return 0;
  if (dy > r) dy = r;
  return r - (int)sqrtf((float)(r * r - dy * dy));
}

void drawPageGas() {
  drawHeader();
  bool hasData = gasPercent >= 0;
  bool offline = gasStatusKnown && !gasOnline;
  uint16_t fillCol = offline ? COL_DIM : (hasData ? gasColor(gasPercent) : COL_GAS);

  card(8, TOP, 304, BOTTOM - TOP, COL_LINE);
  cardHead(8 + 8, TOP + 8, GL_FLAME, COL_GAS, !offline, "Gas");
  drawTankValues(24, TOP, gasPercent, hasData && gasPercent < 15, gasLiters, 1, offline);

  // Status: green = online, red "offline" = ESP offline, yellow "BLE" = no BLE link to the bottle sensor
  int sy = BOTTOM - 20;
  if (offline) statusDot(24, sy, COL_BAD, "offline");
  else if (gasStatusKnown && !gasBleOk) statusDot(24, sy, COL_WARN, "BLE");
  else statusDot(24, sy, gasStatusKnown ? COL_OK : COL_GRID, "");

  // Sensor battery (small battery symbol + percent), only while the ESP is reachable
  if (!offline && gasBattery >= 0) {
    int bx2 = 24 + (gasStatusKnown && !gasBleOk ? 70 : 20), by2 = sy - 7;
    uint16_t bcol = gasBattery > 40 ? COL_OK : (gasBattery > 15 ? COL_WARN : COL_BAD);
    canvas.drawRoundRect(bx2, by2, 20, 14, 2, COL_DIM);
    canvas.fillRect(bx2 + 20, by2 + 4, 2, 6, COL_DIM);
    int w2 = (16 * gasBattery) / 100;
    if (w2 > 0) canvas.fillRect(bx2 + 2, by2 + 2, w2, 10, bcol);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(String(gasBattery) + "%", bx2 + 27, sy);
  }

  // Right: bottle drawing (valve, neck, rounded body) that fills with the state color
  const int bw = 84, bx = 254 - bw / 2, by = TOP + 34, bh = BOTTOM - 12 - by, brad = 28;
  const int wall = 4;
  const int ix = bx + wall, iy = by + wall, iw = bw - 2 * wall, ih = bh - 2 * wall;
  const int irad = brad - wall;
  const int cx = bx + bw / 2;

  canvas.fillRect(cx - 10, by - 12, 20, 14, COL_STEEL);
  canvas.fillRoundRect(cx - 16, by - 20, 32, 9, 3, COL_STEEL);
  canvas.fillRoundRect(bx, by, bw, bh, brad, COL_BG);

  if (hasData && gasPercent > 0) {
    int fillH = ih * gasPercent / 100;
    int topY = iy + ih - fillH;
    for (int y = topY; y < iy + ih; y++) {
      int inset = roundedInset(y, iy, ih, irad);
      canvas.fillRect(ix + inset, y, iw - 2 * inset, 1, fillCol);
    }
    int inset = roundedInset(topY, iy, ih, irad);
    canvas.fillRect(ix + inset, topY, iw - 2 * inset, 2,
                    offline ? COL_GRID : (fillCol == COL_GAS ? COL_GAS_HI : COL_TEXT));
  }

  canvas.drawRoundRect(bx, by, bw, bh, brad, COL_OUTLINE);
  canvas.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, brad - 1, COL_OUTLINE);
  for (int p = 25; p <= 75; p += 25) {
    int y = iy + ih - ih * p / 100;
    canvas.drawFastHLine(bx + bw - wall - 12, y, 10, COL_OUTLINE);
  }
}

// ---------- Temperature pages ----------
uint16_t tempColor(float t) {
  if (t < 10.0f) return COL_ACCENT;   // fridge / freezer range
  if (t < 28.0f) return COL_OK;
  if (t < 45.0f) return COL_WARN;
  return COL_BAD;
}

// The GFX fonts only have ASCII: write German umlauts as ae / oe / ue / ss
String asciiDe(String s) {
  s.replace("\xC3\xA4", "ae"); s.replace("\xC3\xB6", "oe"); s.replace("\xC3\xBC", "ue");
  s.replace("\xC3\x84", "Ae"); s.replace("\xC3\x96", "Oe"); s.replace("\xC3\x9C", "Ue");
  s.replace("\xC3\x9F", "ss");
  return s;
}

// Cuts a text until it fits into maxW pixels (current font)
String fitText(String txt, int maxW) {
  while (txt.length() > 1 && canvas.textWidth(txt) > maxW) txt.remove(txt.length() - 1);
  return txt;
}

// big = one or two sensors: wide row with name left and a large value right
void drawTempTile(int x, int y, int w, int h, const TempEntry& e, bool offline, bool big) {
  bool valid = !isnan(e.t);
  bool alarm = e.alarm != 0 && !offline;
  uint16_t valCol = !valid || offline ? COL_DIM : (alarm ? COL_BAD : tempColor(e.t));
  card(x, y, w, h, alarm ? COL_BAD : COL_LINE);
  if (alarm) canvas.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, COL_BAD);
  const char* note = alarm ? (e.alarm == 2 ? "zu warm" : "zu kalt") : nullptr;
  String name = asciiDe(e.name);

  if (big) {
    int cy = y + h / 2;
    iconDot(x + 26, cy, GL_THERMO, valid ? tempColor(e.t) : COL_GRID, valid && !offline);
    int tx = x + 48;
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(note ? bottom_left : middle_left);
    canvas.setTextColor(alarm ? COL_BAD : COL_TEXT);
    canvas.drawString(fitText(name, w / 2 - 48), tx, note ? cy + 2 : cy);
    if (note) {
      canvas.setFont(&fonts::FreeSans9pt7b);
      canvas.setTextDatum(top_left);
      canvas.drawString(note, tx, cy + 8);
    }
    if (valid) drawDegrees(x + w - 14, cy + 2, e.t, valCol, &fonts::FreeSansBold24pt7b, 4, true);
    else {
      canvas.setFont(&fonts::FreeSansBold24pt7b);
      canvas.setTextDatum(middle_right);
      canvas.setTextColor(COL_DIM);
      canvas.drawString("--", x + w - 14, cy + 2);
    }
    return;
  }

  // Small tile (3-4 sensors): name on top, value below
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(alarm ? COL_BAD : COL_DIM);
  canvas.drawString(fitText(name, w - 16 - (note ? 64 : 0)), x + 10, y + 8);
  if (note) {
    canvas.setTextDatum(top_right);
    canvas.drawString(note, x + w - 10, y + 8);
  }
  int vy = y + h / 2 + 10;
  if (valid) drawDegrees(x + w / 2, vy, e.t, valCol, &fonts::FreeSansBold18pt7b, 3, false);
  else {
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_DIM);
    canvas.drawString("--", x + w / 2, vy);
  }
}

// page 0 = sensors 1-4, page 1 = sensors 5-8
void drawPageTemp(int page) {
  drawHeader();
  bool offline = tempStatusKnown && !tempOnline;
  int first = page * TEMP_PER_PAGE;
  int n = tempCount - first;
  if (n > TEMP_PER_PAGE) n = TEMP_PER_PAGE;

  const int top = TOP, bottom = BOTTOM, left = 8, right = canvas.width() - 8, gap = 8;
  if (n <= 0) {
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(tempCount < 0 ? "Noch keine Temperaturen" : "Keine Sensoren", canvas.width() / 2, (top + bottom) / 2);
  } else if (n <= 2) {
    // One or two sensors: full-width rows with large numbers
    int h = (bottom - top - gap) / 2;
    for (int i = 0; i < n; i++) {
      drawTempTile(left, top + i * (h + gap), right - left, h, temps[first + i], offline, true);
    }
  } else {
    // Three or four sensors: 2 x 2 grid
    int w = (right - left - gap) / 2;
    int h = (bottom - top - gap) / 2;
    for (int i = 0; i < n; i++) {
      drawTempTile(left + (i % 2) * (w + gap), top + (i / 2) * (h + gap), w, h, temps[first + i], offline, false);
    }
  }

  // camper-temp offline: values are drawn dimmed, plus a red dot in the first tile
  if (offline && n > 0) canvas.fillCircle(right - 12, top + 12, 4, COL_BAD);
}

// ---------- Battery page ----------
uint16_t socColor(int soc) {
  if (soc < 0) return COL_DIM;
  if (soc < 20) return COL_BAD;
  if (soc < 40) return COL_WARN;
  return COL_OK;
}

// Fill level of the segment bar: state of charge, without one (lead, BM2) the voltage 11.8-12.8 V
float batFrac(const BatEntry& b) {
  if (b.state[0] == 0 && b.soc >= 0) return b.soc / 100.0f;
  if (!isnan(b.v)) return (b.v - SEG_VMIN) / (SEG_VMAX - SEG_VMIN);
  return NAN;
}

// "3,2 Tage", "5 h 20 min", "45 min"
String formatRuntime(int32_t min) {
  if (min >= 48 * 60) return num(min / 1440.0f, 1) + " Tage";
  if (min >= 60) return String(min / 60) + " h " + String(min % 60) + " min";
  return String(min) + " min";
}

void drawBatRow(int x, int y, int w, int h, const BatEntry& b, bool offline) {
  bool live = b.ok && !offline;
  bool bm2 = b.state[0] != 0;
  bool stateBad = bm2 && (strncmp(b.state, "Kritisch", 8) == 0 || strncmp(b.state, "Schwach", 7) == 0);
  bool alert = live && (b.problem || stateBad);
  bool big = h > 70;

  card(x, y, w, h, alert ? COL_BAD : COL_LINE);
  int left = x + 10, right = x + w - 10;

  // Top line: icon + name left, power or state right
  String top;
  uint16_t topCol = COL_TEXT;
  if (!b.ok) { top = "keine Verbindung"; topCol = COL_DIM; }
  else if (b.problem) { top = "BMS-Fehler"; topCol = COL_BAD; }
  else if (bm2) {
    top = asciiDe(b.state);
    topCol = stateBad ? COL_BAD : (strncmp(b.state, "Normal", 6) == 0 ? COL_TEXT : COL_OK);
  } else if (!isnan(b.w)) {
    int pw = (int)lroundf(b.w);
    top = pw > 0 ? "+" + String(pw) + " W" : String(pw) + " W";
    topCol = pw > 0 ? COL_OK : COL_TEXT;
  }
  if (!live) topCol = COL_DIM;

  int headY = y + (big ? 18 : 12);
  int nameX = left;
  if (big) {
    iconDot(left + 12, headY, GL_BATT, COL_STEEL, live);
    nameX = left + 30;
  }
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  int topW = canvas.textWidth(top);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(alert ? COL_BAD : COL_TEXT);
  canvas.drawString(fitText(asciiDe(b.name), right - nameX - topW - 12), nameX, headY);
  canvas.setTextDatum(middle_right);
  canvas.setTextColor(topCol);
  canvas.drawString(top, right, headY);

  // Middle line: state of charge (large), voltage, runtime with a clock on the right
  int segH = big ? 10 : 7;
  int segY = y + h - segH - (big ? 10 : 6);
  int midY = big ? (headY + 14 + segY) / 2 : y + 30;
  uint16_t socCol = live ? socColor(b.soc) : COL_DIM;
  canvas.setFont(big ? &fonts::FreeSansBold18pt7b : &fonts::FreeSansBold12pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(socCol);
  String socTxt = b.soc >= 0 ? String(b.soc) + "%" : String("--");
  canvas.drawString(socTxt, left, midY);
  int vx = left + canvas.textWidth(socTxt) + 10;

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(COL_DIM);
  if (b.ok && !isnan(b.v)) canvas.drawString(num(b.v, 2) + " V", vx, midY + 1);

  if (live && !bm2) {
    String rt;
    bool clock = false;
    if (b.rt >= 0) { rt = formatRuntime(b.rt); clock = true; }
    else if (!isnan(b.w) && b.w > 1) rt = "laedt";
    else rt = "Ruhe";
    canvas.setTextDatum(middle_right);
    canvas.drawString(rt, right, midY + 1);
    if (clock) clockIcon(right - canvas.textWidth(rt) - 10, midY, COL_DIM);
  }

  // Bottom: segment bar from red to green
  segBar(left, segY, right - left, segH, batFrac(b), live);
}

void drawPageBattery() {
  drawHeader();
  bool offline = batStatusKnown && !batOnline;
  const int top = TOP, bottom = BOTTOM, left = 8, right = canvas.width() - 8, gap = 6;
  int n = batCount;
  if (n <= 0) {
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(batCount < 0 ? "Noch keine Batteriedaten" : "Keine Batterien", canvas.width() / 2, (top + bottom) / 2);
    return;
  }
  int slots = n < 2 ? 2 : n;   // a single battery uses half the height
  int h = (bottom - top - gap * (slots - 1)) / slots;
  for (int i = 0; i < n; i++) drawBatRow(left, top + i * (h + gap), right - left, h, bats[i], offline);
  if (offline) canvas.fillCircle(right - 8, top + 8, 4, COL_BAD);
}

// ---------- System page ----------
void drawRow(int y, const char* label, const String& value, uint16_t dotColor) {
  canvas.fillCircle(24, y, 5, dotColor);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(COL_DIM);
  canvas.drawString(label, 38, y);
  canvas.setTextColor(COL_TEXT);
  canvas.setTextDatum(middle_right);
  canvas.drawString(value, canvas.width() - 20, y);
}

void drawPageSystem() {
  drawHeader();
  card(8, TOP, 304, BOTTOM - TOP, COL_LINE);

  bool wifiOk = WiFi.status() == WL_CONNECTED;
  bool mqttOk = client.connected();
  int level = M5.Power.getBatteryLevel();
  bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging);
  unsigned long s = millis() / 1000;
  char up[24];
  snprintf(up, sizeof(up), "%luh %02lum", s / 3600, (s / 60) % 60);

  int y = TOP + 16;
  const int step = 27;
  drawRow(y, "WLAN", wifiOk ? String(WiFi.RSSI()) + " dBm" : String("getrennt"), wifiOk ? COL_OK : COL_BAD);
  y += step;
  if (routerFresh()) {
    String sig = routerNet();
    if (!isnan(rtRsrp)) sig += " " + String((int)rtRsrp) + " dBm";
    else if (!isnan(rtRssi)) sig += " " + String((int)rtRssi) + " dBm";
    drawRow(y, "Mobilfunk", sig, routerBars() >= 3 ? COL_OK : COL_WARN);
  } else {
    drawRow(y, "Mobilfunk", "--", COL_GRID);
  }
  y += step;
  drawRow(y, "MQTT", mqttOk ? cfgBroker : String("offline"), mqttOk ? COL_OK : COL_BAD);
  y += step;
  drawRow(y, "Akku", String(level) + "%" + (charging ? " (laedt)" : ""),
          charging ? COL_ACCENT : (level > 40 ? COL_OK : (level > 15 ? COL_WARN : COL_BAD)));
  y += step;
  drawRow(y, "Version", String("v" FW_VERSION "  ") + up, COL_GRID);
  y += step;

  // Update mode: address of the update page and how long the device stays awake
  if (wifiOk && updateModeActive()) {
    unsigned long left = (UPDATE_AWAKE_MS - (millis() - systemPageSince)) / 60000UL + 1;
    drawRow(y, "Update", WiFi.localIP().toString(), COL_ACCENT);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(String("Webseite offen, wach noch ") + left + " min", 160, y + 22);
  } else {
    drawRow(y, "Update", wifiOk ? WiFi.localIP().toString() : String("--"), COL_GRID);
  }
}

// Full screen while a firmware upload is running
void drawUpdateScreen() {
  int p = webota::progress();
  canvas.fillSprite(COL_BG);
  card(20, 60, 280, 120, COL_LINE);
  iconDot(48, 88, GL_INFO, COL_ACCENT, true);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(COL_TEXT);
  canvas.drawString(webota::finished() ? "Neustart ..." : "Update laeuft", 70, 88);
  canvas.fillRoundRect(40, 120, 240, 12, 6, COL_GRID);
  if (p > 0) canvas.fillRoundRect(40, 120, 240 * p / 100, 12, 6, COL_ACCENT);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(COL_DIM);
  canvas.drawString(String(max(p, 0)) + " %  -  nicht ausschalten", 160, 154);
  canvas.pushSprite(0, 0);
  lastRedraw = millis();
}

// Called by the update page for every uploaded chunk (the web server blocks loop() during an upload)
void onOtaProgress(int percent) {
  (void)percent;
  if (!display_on) { display_on = true; M5.Display.setBrightness(128); }
  if (millis() - lastRedraw > 250 || webota::finished()) drawUpdateScreen();
}

// ---------- Rendering / power ----------
void render() {
  if (!display_on) return;
  canvas.fillSprite(COL_BG);
  switch (PAGES[page_index]) {
#if USE_LEVEL
    case PAGE_LEVEL:  drawPageLevel();  break;
#endif
#if USE_WATER
    case PAGE_WATER:  drawPageWater();  break;
#endif
#if USE_GAS
    case PAGE_GAS:    drawPageGas();    break;
#endif
#if USE_TEMP
    case PAGE_TEMP1:  drawPageTemp(0);  break;
    case PAGE_TEMP2:  drawPageTemp(1);  break;
#endif
#if USE_BATTERY
    case PAGE_BATTERY: drawPageBattery(); break;
#endif
    default:          drawPageSystem(); break;
  }
  drawFooter();
  canvas.pushSprite(0, 0);
  dirty = false;
  lastRedraw = millis();
}

void setDisplay(bool on) {
  display_on = on;
  M5.Display.setBrightness(on ? 128 : 0);
  if (on) {
    lastActivityMs = millis();
    if (onSystemPage()) systemPageSince = millis();   // waking up on the system page starts update mode again
    render();
  } else {
    displayOffAtMs = millis();
  }
}

// Deep sleep: WiFi and MQTT off, any touch wakes the device (it then reboots and reconnects)
void goToSleep() {
  if (client.connected()) client.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  M5.Power.deepSleep(SLEEP_SAFETY_TIMER_US, true);

  // Only reached if going to sleep failed: stay awake with the display off and retry later
  startWifi();
  displayOffAtMs = millis();
}

// ---------- Status JSON for the web page ----------
String statusJson() {
  JsonDocument doc;
  unsigned long s = millis() / 1000;
  char up[24];
  snprintf(up, sizeof(up), "%luh %02lum", s / 3600, (s / 60) % 60);
  doc["version"] = FW_VERSION;
  doc["wifi"] = WiFi.status() == WL_CONNECTED;
  doc["rssi"] = WiFi.RSSI();
  doc["mqtt"] = client.connected();
  doc["broker"] = cfgBroker;
  doc["battery"] = M5.Power.getBatteryLevel();
  doc["charging"] = M5.Power.isCharging() == m5::Power_Class::is_charging;
  doc["uptime"] = up;
  if (updateModeActive()) {
    doc["awake"] = String((UPDATE_AWAKE_MS - (millis() - systemPageSince)) / 60000UL + 1) + " min (System-Seite)";
  } else {
    doc["awake"] = "bis zum Display-Timeout - System-Seite oeffnen fuer 15 min";
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// ---------- MQTT ----------
void handleWater(const String& t, const String& message) {
  if (t.endsWith("/percent")) {
    waterPercent = constrain((int)message.toInt(), 0, 100);
  } else if (t.endsWith("/liters")) {
    waterLiters = message.toFloat();
  }
}

void handleGas(const String& t, const String& message) {
  if (t.endsWith("/level")) {                  // note: "raw_level" does not match "/level"
    gasPercent = constrain((int)lroundf(message.toFloat()), 0, 100);
  } else if (t.endsWith("/content")) {
    gasLiters = message.toFloat();
  } else if (t.endsWith("/battery")) {
    gasBattery = constrain((int)message.toInt(), 0, 100);
  } else if (t.endsWith("/ble_state")) {
    gasBleOk = (message == "OK");
  }
}

// Level sensor (camper-level): pitch, roll, is_level, tolerance, status; everything else is ignored
void handleLevel(const String& sub, const String& message) {
  if (sub == "pitch") {
    pitch = message.toFloat();
    havePitch = true;
    lastPitchMs = millis();
  } else if (sub == "roll") {
    roll = message.toFloat();
    haveRoll = true;
    lastRollMs = millis();
  } else if (sub.startsWith("wedge_")) {
    static const char* const KEYS[4] = {"wedge_fl", "wedge_fr", "wedge_rl", "wedge_rr"};
    for (int i = 0; i < 4; i++) if (sub == KEYS[i]) wedge[i] = message.toFloat();
  } else if (sub == "is_level") {
    isLevelState = message == "ON" ? 1 : 0;
  } else if (sub == "tolerance") {
    float t = message.toFloat();
    if (t > 0.0f && t < 10.0f) levelTol = t;
  } else if (sub == "status") {
    levelOnline = (message == "online");
    levelStatusKnown = true;
  }
}

// <temp prefix>/sensors: [{"id":"fridge","name":"Kühlschrank","t":4.31,"alarm":0}, ...]
void handleTempList(const String& message) {
  JsonDocument doc;
  if (deserializeJson(doc, message)) return;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return;
  int n = 0;
  for (JsonObject o : arr) {
    if (n >= TEMP_MAX) break;
    TempEntry& e = temps[n];
    strlcpy(e.name, o["name"] | "", sizeof(e.name));
    e.t = o["t"].is<float>() ? o["t"].as<float>() : NAN;
    e.alarm = o["alarm"] | 0;
    n++;
  }
  tempCount = n;
  // The second page may have disappeared
  if (!pageVisible(page_index)) page_index = stepPage(page_index, -1);
}

// <battery prefix>/batteries: [{"id":"house","name":"Bordbatterie","ok":true,"soc":78,"v":13.31,"w":-56,"rt":2830}, ...]
void handleBatList(const String& message) {
  JsonDocument doc;
  if (deserializeJson(doc, message)) return;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) return;
  int n = 0;
  for (JsonObject o : arr) {
    if (n >= BAT_MAX) break;
    BatEntry& e = bats[n];
    strlcpy(e.name, o["name"] | "", sizeof(e.name));
    strlcpy(e.state, o["st"] | "", sizeof(e.state));
    e.ok = o["ok"] | false;
    e.soc = o["soc"].is<float>() ? (int8_t)constrain((int)lroundf(o["soc"].as<float>()), 0, 100) : -1;
    e.v = o["v"].is<float>() ? o["v"].as<float>() : NAN;
    e.w = o["w"].is<float>() ? o["w"].as<float>() : NAN;
    e.rt = o["rt"].is<int>() ? o["rt"].as<int>() : -1;
    e.problem = o["p"] | false;
    n++;
  }
  batCount = n;
}

// ---- Router signal (same parser as the wall display, firmware/display/.../router.cpp) ----
float jsonNum(JsonVariantConst v) {
  if (v.is<float>() || v.is<int>()) return v.as<float>();
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    char* end;
    float f = strtof(s, &end);
    if (end != s) return f;
  }
  return NAN;
}

String netLabel(String s) {
  s.toUpperCase();
  if (s.indexOf("5G") >= 0 || s.indexOf("NR") >= 0) return "5G";
  if (s.indexOf("LTE") >= 0 || s.indexOf("4G") >= 0) return "LTE";
  if (s.indexOf("WCDMA") >= 0 || s.indexOf("UMTS") >= 0 || s.indexOf("HSPA") >= 0 || s.indexOf("3G") >= 0) return "3G";
  if (s.indexOf("GSM") >= 0 || s.indexOf("EDGE") >= 0 || s.indexOf("GPRS") >= 0 || s.indexOf("2G") >= 0) return "2G";
  return "";
}

// Searches nested objects (the RUT956 wraps values in objects and arrays)
void scanRouter(JsonVariantConst v, int depth) {
  if (depth > 4) return;
  if (v.is<JsonArrayConst>()) {
    for (JsonVariantConst e : v.as<JsonArrayConst>()) scanRouter(e, depth + 1);
    return;
  }
  if (!v.is<JsonObjectConst>()) return;
  for (JsonPairConst kv : v.as<JsonObjectConst>()) {
    String k = kv.key().c_str();
    k.toLowerCase();
    JsonVariantConst val = kv.value();
    if (val.is<JsonObjectConst>() || val.is<JsonArrayConst>()) { scanRouter(val, depth + 1); continue; }
    if (k == "rsrp") { float f = jsonNum(val); if (!isnan(f)) rtRsrp = f; }
    else if (k == "rssi" || k == "signal") { float f = jsonNum(val); if (!isnan(f) && f < 0) rtRssi = f; }
    else if (k == "conntype" || k == "network_type" || k == "nettype" || k == "net_type" ||
             k == "mode" || k == "type" || k == "service_mode") {
      if (val.is<const char*>()) { String l = netLabel(val.as<const char*>()); if (l.length()) rtNet = l; }
    }
  }
}

void handleRouter(const String& message) {
  JsonDocument doc;
  if (deserializeJson(doc, message)) return;
  float oldRsrp = rtRsrp, oldRssi = rtRssi;
  rtRsrp = rtRssi = NAN;
  scanRouter(doc.as<JsonVariantConst>(), 0);
  if (isnan(rtRsrp) && isnan(rtRssi)) { rtRsrp = oldRsrp; rtRssi = oldRssi; return; }   // no signal value in it
  rtLastMs = millis();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  String t(topic);
  Serial.printf("MQTT %s = %s\n", topic, message.c_str());  // debug: shows every topic

  if (strlen(ROUTER_TOPIC) && t == ROUTER_TOPIC) {
    handleRouter(message);
    dirty = true;
    return;
  }

  // One status topic covers both water and gas
  if (t == TOPIC_TANK_STATUS) {
    bool online = (message == "online");
    waterOnline = gasOnline = online;
    waterStatusKnown = gasStatusKnown = true;
    dirty = true;
    return;
  }

#if USE_WATER
  if (t.startsWith(TOPIC_TANK_WATER)) {
    handleWater(t, message);
    dirty = true;
    return;
  }
#endif

#if USE_GAS
  if (t.startsWith(TOPIC_TANK_GAS)) {
    handleGas(t, message);
    dirty = true;
    return;
  }
#endif

#if USE_TEMP
  if (t == TOPIC_TEMP_SENSORS) {
    handleTempList(message);
    dirty = true;
    return;
  }
  if (t == TOPIC_TEMP_STATUS) {
    tempOnline = (message == "online");
    tempStatusKnown = true;
    dirty = true;
    return;
  }
#endif

#if USE_BATTERY
  if (t == TOPIC_BAT_LIST) {
    handleBatList(message);
    dirty = true;
    return;
  }
  if (t == TOPIC_BAT_STATUS) {
    batOnline = (message == "online");
    batStatusKnown = true;
    dirty = true;
    return;
  }
#endif

#if USE_LEVEL
  // Level sensor: match only exact sub-topics below the prefix, ignore e.g. wedge_* / tare_*
  if (levelPrefix.length() && t.startsWith(levelPrefix)) {
    handleLevel(t.substring(levelPrefix.length()), message);
    dirty = true;
  }
#endif
}

// Non-blocking reconnect: buttons and display stay responsive
void mqttReconnect() {
  if (client.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastMqttAttempt < MQTT_RETRY_MS) return;
  lastMqttAttempt = millis();

  String clientId = "roadbuddy-" + String(random(0xffff), HEX);
  if (client.connect(clientId.c_str(), cfgMqttUser.c_str(), cfgMqttPass.c_str())) {
    // Retained values arrive immediately after subscribing
#if USE_LEVEL
    client.subscribe(cfgLevelTopic.c_str());
#endif
#if USE_WATER || USE_GAS
    client.subscribe(TOPIC_TANK_STATUS);  // shared online/offline status of the tank firmware
#endif
#if USE_WATER
    client.subscribe((String(TOPIC_TANK_WATER) + "#").c_str());
#endif
#if USE_GAS
    client.subscribe((String(TOPIC_TANK_GAS) + "#").c_str());
#endif
#if USE_TEMP
    client.subscribe(TOPIC_TEMP_SENSORS);
    client.subscribe(TOPIC_TEMP_STATUS);
#endif
#if USE_BATTERY
    client.subscribe(TOPIC_BAT_LIST);
    client.subscribe(TOPIC_BAT_STATUS);
#endif
    if (strlen(ROUTER_TOPIC)) client.subscribe(ROUTER_TOPIC);
    dirty = true;
  }
}

// ---------- Arduino ----------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  // Safety timer wake-up: no WiFi, no display, straight back to sleep
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    M5.Power.deepSleep(SLEEP_SAFETY_TIMER_US, true);
  }

  Serial.println("RoadBuddy v" FW_VERSION);
  if (page_index < 0 || page_index >= PAGE_COUNT || !pageVisible(page_index)) page_index = 0;

  loadCredentials();
  levelPrefix = cfgLevelTopic;
  if (levelPrefix.endsWith("#")) levelPrefix.remove(levelPrefix.length() - 1);
  if (levelPrefix.length() && !levelPrefix.endsWith("/")) levelPrefix += "/";

  M5.Display.setBrightness(128);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());

  startWifi();
  client.setServer(cfgBroker.c_str(), cfgPort);
  client.setBufferSize(1280);   // the temperature list with 8 long names is about 1 kB
  client.setCallback(callback);
  webota::begin("roadbuddy", FW_VERSION, cfgWebUser.c_str(), cfgWebPass.c_str(), statusJson, onOtaProgress);

  lastActivityMs = millis();
  systemPageSince = millis();
  render();
}

void loop() {
  M5.update();

  mqttReconnect();
  client.loop();
  webota::loop();

  // Firmware upload running: keep everything awake and show the progress only
  if (webota::busy()) {
    lastActivityMs = millis();
    if (!display_on) { display_on = true; M5.Display.setBrightness(128); }
    if (millis() - lastRedraw > 250) drawUpdateScreen();
    return;
  }

  bool a = M5.BtnA.wasPressed();
  bool b = M5.BtnB.wasPressed();
  bool c = M5.BtnC.wasPressed();
  bool touched = a || b || c || M5.Touch.getDetail().wasPressed();

  // Swallow the touch that woke the device from deep sleep
  if (millis() < INPUT_LOCK_MS) {
    a = b = c = touched = false;
  }

  if (touched) lastActivityMs = millis();

  if (!display_on) {
    // Display off: any touch only wakes the display
    if (touched) setDisplay(true);
  } else {
    if (b) {
      setDisplay(false);
    } else if (a || c) {
      page_index = stepPage(page_index, a ? -1 : 1);
      if (onSystemPage()) systemPageSince = millis();
      dirty = true;
    }
  }

  // Update mode: the system page keeps display and WiFi on (at most UPDATE_AWAKE_MS)
  if (updateModeActive()) lastActivityMs = millis();

  // Automatic display timeout, then deep sleep a while later
  if (display_on && millis() - lastActivityMs > DISPLAY_TIMEOUT_MS) {
    setDisplay(false);
  }
  if (!display_on && millis() - displayOffAtMs > SLEEP_DELAY_MS) {
    goToSleep();
  }

  // Redraw on new data (at most every 150 ms); the water page animates, the others refresh once per second
  if (display_on) {
    unsigned long since = millis() - lastRedraw;
    PageId cur = PAGES[page_index];
    unsigned long interval = (cur == PAGE_WATER) ? 120 : 1000;  // tank pages animate
    if ((dirty && since >= 150) || since >= interval) {
      render();
    }
  }
}
