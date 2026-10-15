#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_sleep.h>
#include "config.h"

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
#ifndef USE_GREY
#define USE_GREY 0    // grey water tank (combined firmware only)
#endif

WiFiClient espClient;
PubSubClient client(espClient);
M5Canvas canvas(&M5.Display);  // off-screen buffer, avoids flicker

// ---------- Topics ----------
// Combined water + gas firmware (camper-tanklevel): <prefix>/water/..., <prefix>/gas/..., <prefix>/status
const char* TOPIC_TANK_WATER      = "camper/tanklevel/water/";
const char* TOPIC_TANK_GREY       = "camper/tanklevel/grey/";
const char* TOPIC_TANK_GAS        = "camper/tanklevel/gas/";
const char* TOPIC_TANK_STATUS     = "camper/tanklevel/status";
// Separate devices (camper-waterlevel / camper-gaslevel), still supported
const char* TOPIC_WATER_ALL    = "camper/waterlevel/#";
const char* TOPIC_WATER_PREFIX = "camper/waterlevel/";
// Gas bottle topics: level, content, battery, ble_state, status
const char* TOPIC_GAS_ALL     = "camper/gaslevel/#";
const char* TOPIC_GAS_PREFIX   = "camper/gaslevel/";

// ---------- Power management ----------
const unsigned long DISPLAY_TIMEOUT_MS = 60UL * 1000UL;        // display off after this much inactivity
const unsigned long SLEEP_DELAY_MS     = 5UL * 60UL * 1000UL;  // deep sleep this long after the display went off
const unsigned long INPUT_LOCK_MS      = 1000;                 // ignore touches right after boot (the wake-up touch)
// Safety wake-up: a timer is always set so this also works with M5Unified versions where 0 means "no timer".
// On a timer wake-up the device immediately goes back to sleep.
const uint64_t SLEEP_SAFETY_TIMER_US   = 24ULL * 3600ULL * 1000000ULL;

// ---------- State ----------

// Pages in display order; the system page is always present
enum PageId { PAGE_LEVEL, PAGE_WATER, PAGE_GREY, PAGE_GAS, PAGE_SYSTEM };
static const PageId PAGES[] = {
#if USE_LEVEL
  PAGE_LEVEL,
#endif
#if USE_WATER
  PAGE_WATER,
#endif
#if USE_GREY
  PAGE_GREY,
#endif
#if USE_GAS
  PAGE_GAS,
#endif
  PAGE_SYSTEM
};
const int PAGE_COUNT = sizeof(PAGES) / sizeof(PAGES[0]);
RTC_DATA_ATTR int page_index = 0;  // index into PAGES, survives deep sleep
bool display_on = true;
bool dirty = true;
unsigned long lastActivityMs = 0;
unsigned long displayOffAtMs = 0;

// All values below survive deep sleep (RTC memory), so the last known value is always shown.
// Values that are not refreshed for a while are drawn dimmed instead of being hidden.

// Level sensor
RTC_DATA_ATTR float pitch = 0, roll = 0;
RTC_DATA_ATTR bool havePitch = false, haveRoll = false;
unsigned long lastPitchMs = 0, lastRollMs = 0;   // time of the last update (0 = none since boot)
const unsigned long STALE_MS = 30000;            // level values older than this are drawn dimmed

// Water tank
RTC_DATA_ATTR int waterPercent = -1;       // -1 = no data yet
RTC_DATA_ATTR float waterLiters = -1.0f;   // <0 = no data yet
bool waterOnline = false;
bool waterStatusKnown = false;

// Grey water tank
RTC_DATA_ATTR int greyPercent = -1;        // -1 = no data yet
RTC_DATA_ATTR float greyLiters = -1.0f;    // <0 = no data yet

// Gas bottle
RTC_DATA_ATTR int gasPercent = -1;         // -1 = no data yet (topic "level", 0-100 %)
RTC_DATA_ATTR float gasLiters = -1.0f;     // <0 = no data yet (topic "content", liters)
RTC_DATA_ATTR int gasBattery = -1;         // sensor battery in %, -1 = unknown
bool gasBleOk = true;        // false if the gas ESP reports a BLE problem
bool gasOnline = false;
bool gasStatusKnown = false;

unsigned long lastMqttAttempt = 0;
const unsigned long MQTT_RETRY_MS = 5000;
unsigned long lastRedraw = 0;

// Flip these if the bubble / icons move in the wrong direction for your mounting
const float BUBBLE_SIGN_X = 1.0f;
const float BUBBLE_SIGN_Y = 1.0f;
const float BUBBLE_FULL_SCALE_DEG = 10.0f;  // deflection at the circle edge
const float ICON_SIGN_PITCH = -1.0f;
const float ICON_SIGN_ROLL  = 1.0f;
const float ICON_GAIN = 3.0f;               // exaggerates the tilt so small angles are visible
const float ICON_MAX_DEG = 25.0f;

// ---------- Colors ----------
static const uint16_t COL_BG     = lgfx::color565(10, 14, 20);
static const uint16_t COL_PANEL  = lgfx::color565(24, 30, 40);
static const uint16_t COL_GRID   = lgfx::color565(60, 70, 85);
static const uint16_t COL_TEXT   = lgfx::color565(235, 240, 245);
static const uint16_t COL_DIM    = lgfx::color565(130, 140, 155);
static const uint16_t COL_OK     = lgfx::color565(60, 210, 110);
static const uint16_t COL_WARN   = lgfx::color565(255, 190, 40);
static const uint16_t COL_BAD    = lgfx::color565(240, 70, 70);
static const uint16_t COL_ACCENT = lgfx::color565(60, 160, 255);
static const uint16_t COL_WAVE   = lgfx::color565(130, 205, 255);
static const uint16_t COL_GREY_W = lgfx::color565(135, 145, 115);
static const uint16_t COL_GREY_HI = lgfx::color565(185, 195, 160);
static const uint16_t COL_GAS    = lgfx::color565(255, 150, 40);
static const uint16_t COL_GAS_HI = lgfx::color565(255, 205, 110);
static const uint16_t COL_STEEL  = lgfx::color565(150, 160, 175);

// ---------- Helpers ----------
bool isFresh(unsigned long ts) {
  return ts != 0 && (millis() - ts) < STALE_MS;
}

uint16_t levelColor(float v) {
  float a = fabsf(v);
  if (a <= 1.0f) return COL_OK;
  if (a <= 3.0f) return COL_WARN;
  return COL_BAD;
}

void startWifi() {
  WiFi.setHostname("roadbuddy");  // must be set before WiFi.mode()
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
}

// ---------- Header / footer ----------
void drawBattery(int x, int y) {
  int level = M5.Power.getBatteryLevel();  // 0..100, may be negative if unknown
  bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging);
  if (level < 0) level = 0;
  if (level > 100) level = 100;

  uint16_t col = charging ? COL_ACCENT : (level > 40 ? COL_OK : (level > 15 ? COL_WARN : COL_BAD));

  // Percentage text
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(COL_TEXT);
  canvas.setTextDatum(middle_right);
  canvas.drawString(String(level) + "%", x - 6, y + 8);

  // Battery body + nub
  canvas.drawRoundRect(x, y, 30, 16, 3, COL_TEXT);
  canvas.fillRect(x + 30, y + 5, 3, 6, COL_TEXT);
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

// WiFi bars: green = WiFi + MQTT ok, yellow = WiFi ok but MQTT down, red cross = no WiFi
void drawConnectionIcon(int x, int y) {
  bool wifiOk = WiFi.status() == WL_CONNECTED;
  bool mqttOk = client.connected();
  int lit = 0;
  if (wifiOk) {
    int r = WiFi.RSSI();
    lit = r > -55 ? 4 : (r > -65 ? 3 : (r > -75 ? 2 : 1));
  }
  uint16_t on = mqttOk ? COL_OK : COL_WARN;
  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 4;
    canvas.fillRect(x + i * 6, y + 16 - h, 4, h, i < lit ? on : COL_GRID);
  }
  if (!wifiOk) {
    canvas.drawLine(x, y, x + 22, y + 16, COL_BAD);
    canvas.drawLine(x, y + 16, x + 22, y, COL_BAD);
  }
}

void drawHeader() {
  canvas.fillRect(0, 0, canvas.width(), 32, COL_PANEL);
  drawConnectionIcon(10, 8);

  // Page dots, centered
  for (int i = 0; i < PAGE_COUNT; i++) {
    int cx = canvas.width() / 2 + (int)((i - (PAGE_COUNT - 1) / 2.0f) * 16);
    if (i == page_index) canvas.fillCircle(cx, 16, 5, COL_ACCENT);
    else canvas.fillCircle(cx, 16, 3, COL_GRID);
  }

  drawBattery(canvas.width() - 42, 8);
  canvas.drawFastHLine(0, 32, canvas.width(), COL_GRID);
}

// Footer: previous page / power / next page (aligned with the three touch buttons)
void drawFooter() {
  int y = canvas.height() - 22;
  int cy = y + 11;
  canvas.fillRect(0, y, canvas.width(), 22, COL_PANEL);

  canvas.fillTriangle(45, cy, 61, cy - 7, 61, cy + 7, COL_DIM);
  canvas.fillTriangle(275, cy, 259, cy - 7, 259, cy + 7, COL_DIM);

  // Power symbol: ring with a gap and a vertical bar
  canvas.drawCircle(160, cy + 1, 7, COL_DIM);
  canvas.fillRect(157, cy - 9, 7, 7, COL_PANEL);
  canvas.drawFastVLine(160, cy - 9, 8, COL_DIM);
  canvas.drawFastVLine(161, cy - 9, 8, COL_DIM);
}

// ---------- Level page ----------
// Fills a rotated rectangle given in local coordinates (relative to the icon center)
static void fillRotRect(int cx, int cy, float ca, float sa,
                        float x0, float y0, float x1, float y1, uint16_t col) {
  auto T = [&](float x, float y, int &ox, int &oy) {
    ox = cx + (int)lroundf(x * ca - y * sa);
    oy = cy + (int)lroundf(x * sa + y * ca);
  };
  int ax, ay, bx, by, px, py, qx, qy;
  T(x0, y0, ax, ay);
  T(x1, y0, bx, by);
  T(x1, y1, px, py);
  T(x0, y1, qx, qy);
  canvas.fillTriangle(ax, ay, bx, by, px, py, col);
  canvas.fillTriangle(ax, ay, px, py, qx, qy, col);
}

// Small vehicle icon that tilts: side view (pitch, front on the right) or rear view (roll)
void drawTiltIcon(int cx, int cy, float angDeg, bool sideView, uint16_t bodyCol) {
  float a = angDeg * (float)M_PI / 180.0f;
  float ca = cosf(a), sa = sinf(a);
  float hw = sideView ? 26.0f : 16.0f;  // half width
  float hh = sideView ? 9.0f : 12.0f;   // half height

  fillRotRect(cx, cy, ca, sa, -hw, -hh, hw, hh, bodyCol);

  // Windows
  if (sideView) {
    fillRotRect(cx, cy, ca, sa, hw - 16, -hh + 3, hw - 4, -1, COL_BG);
    fillRotRect(cx, cy, ca, sa, -hw + 6, -hh + 3, -hw + 16, -1, COL_BG);
  } else {
    fillRotRect(cx, cy, ca, sa, -hw + 4, -hh + 3, hw - 4, -2, COL_BG);
  }

  // Wheels
  float wxs[2] = { sideView ? -15.0f : -hw + 3, sideView ? 15.0f : hw - 3 };
  for (int i = 0; i < 2; i++) {
    int px = cx + (int)lroundf(wxs[i] * ca - (hh + 1) * sa);
    int py = cy + (int)lroundf(wxs[i] * sa + (hh + 1) * ca);
    canvas.fillCircle(px, py, 4, COL_TEXT);
  }
}

void drawTiltValue(int cx, int iconY, int numY, float v, bool have, unsigned long ts,
                   bool sideView, float iconSign) {
  uint16_t col = !have ? COL_GRID : (isFresh(ts) ? levelColor(v) : COL_DIM);
  float ang = have ? v * ICON_GAIN * iconSign : 0.0f;
  ang = fmaxf(-ICON_MAX_DEG, fminf(ICON_MAX_DEG, ang));
  drawTiltIcon(cx, iconY, ang, sideView, col);

  canvas.setFont(&fonts::FreeSansBold24pt7b);
  if (have) {
    String s = String(v, 1);
    int w = canvas.textWidth(s);
    int x0 = cx - (w + 14) / 2;
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(col);
    canvas.drawString(s, x0, numY);
    canvas.drawCircle(x0 + w + 7, numY - 12, 4, col);  // degree sign (GFX fonts have none)
  } else {
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_DIM);
    canvas.drawString("--", cx, numY);
  }
}

void drawBubble(int cx, int cy, int r) {
  canvas.fillCircle(cx, cy, r, COL_PANEL);
  canvas.drawCircle(cx, cy, r, COL_GRID);
  canvas.drawCircle(cx, cy, r / 2, COL_GRID);
  canvas.drawCircle(cx, cy, 8, COL_GRID);
  canvas.drawFastHLine(cx - r, cy, 2 * r, COL_GRID);
  canvas.drawFastVLine(cx, cy - r, 2 * r, COL_GRID);

  if (!(havePitch && haveRoll)) return;
  bool fresh = isFresh(lastPitchMs) && isFresh(lastRollMs);

  float maxR = r - 10;
  float dx = BUBBLE_SIGN_X * roll  / BUBBLE_FULL_SCALE_DEG * maxR;
  float dy = BUBBLE_SIGN_Y * pitch / BUBBLE_FULL_SCALE_DEG * maxR;
  float len = sqrtf(dx * dx + dy * dy);
  if (len > maxR) {
    dx *= maxR / len;
    dy *= maxR / len;
  }

  float worst = fmaxf(fabsf(pitch), fabsf(roll));
  uint16_t col = fresh ? levelColor(worst) : COL_DIM;
  canvas.fillCircle(cx + (int)dx, cy + (int)dy, 9, col);
  canvas.drawCircle(cx + (int)dx, cy + (int)dy, 9, COL_TEXT);
}

void drawPageLevel() {
  drawHeader();
  drawBubble(92, 124, 72);
  drawTiltValue(242, 58, 98,  pitch, havePitch, lastPitchMs, true,  ICON_SIGN_PITCH);
  drawTiltValue(242, 142, 182, roll,  haveRoll,  lastRollMs,  false, ICON_SIGN_ROLL);
}

// ---------- System page ----------
void drawRow(int y, const char* label, const String& value, uint16_t dotColor) {
  canvas.fillCircle(18, y + 10, 5, dotColor);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(COL_DIM);
  canvas.drawString(label, 32, y + 10);
  canvas.setTextColor(COL_TEXT);
  canvas.setTextDatum(middle_right);
  canvas.drawString(value, canvas.width() - 12, y + 10);
}

void drawPageSystem() {
  drawHeader();

  bool wifiOk = WiFi.status() == WL_CONNECTED;
  bool mqttOk = client.connected();
  int level = M5.Power.getBatteryLevel();
  bool charging = (M5.Power.isCharging() == m5::Power_Class::is_charging);

  drawRow(44,  "WLAN",   wifiOk ? String("Verbunden (") + WiFi.RSSI() + " dBm)" : String("Getrennt"),
          wifiOk ? COL_OK : COL_BAD);
  drawRow(72,  "IP",     wifiOk ? WiFi.localIP().toString() : String("--"),
          wifiOk ? COL_OK : COL_DIM);
  drawRow(100, "Broker", String(mqtt_server), COL_ACCENT);
  drawRow(128, "MQTT",   mqttOk ? String("Verbunden") : String("Offline"),
          mqttOk ? COL_OK : COL_BAD);
  drawRow(156, "Akku",   String(level) + "%" + (charging ? " (Laden)" : ""),
          charging ? COL_ACCENT : (level > 40 ? COL_OK : (level > 15 ? COL_WARN : COL_BAD)));

  unsigned long s = millis() / 1000;
  char up[24];
  snprintf(up, sizeof(up), "%luh %02lum", s / 3600, (s / 60) % 60);
  drawRow(184, "Laufzeit", String(up), COL_DIM);
}

// ---------- Tank pages (fresh water / grey water) ----------
// highIsBad: grey water warns when the tank is nearly FULL, fresh water when it is nearly empty
void drawTankPage(int percent, float liters, bool online, bool statusKnown,
                  bool highIsBad, uint16_t liquidCol, uint16_t surfaceCol) {
  drawHeader();

  // Tank geometry
  const int tx = 22, ty = 42, tw = 112, th = 168, rad = 10;
  const int inset = 5;
  const int ix = tx + inset, iy = ty + inset;
  const int iw = tw - 2 * inset, ih = th - 2 * inset;

  bool hasData = percent >= 0;
  bool offline = statusKnown && !online;
  uint16_t waterCol = offline ? COL_DIM : liquidCol;
  uint16_t waveCol  = offline ? COL_GRID : surfaceCol;

  // Tank body
  canvas.fillRoundRect(tx, ty, tw, th, rad, COL_PANEL);

  // Water with animated surface
  if (hasData && percent > 0) {
    int fillH = ih * percent / 100;
    int bottom = iy + ih;
    int surfaceY = bottom - fillH;
    float phase = millis() / 300.0f;
    for (int x = 0; x < iw; x++) {
      int top = surfaceY + (int)(2.5f * sinf(x / 9.0f + phase));
      if (top < iy) top = iy;
      if (top >= bottom) continue;
      canvas.fillRect(ix + x, top, 1, bottom - top, waterCol);
      canvas.drawPixel(ix + x, top, waveCol);
    }
  }

  // Outline (double for a thicker wall)
  canvas.drawRoundRect(tx, ty, tw, th, rad, COL_TEXT);
  canvas.drawRoundRect(tx + 1, ty + 1, tw - 2, th - 2, rad - 1, COL_TEXT);

  // Scale ticks at 25 / 50 / 75 %
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  for (int p = 25; p <= 75; p += 25) {
    int y = iy + ih - ih * p / 100;
    canvas.drawFastHLine(tx + tw - inset - 12, y, 12, COL_TEXT);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(String(p), tx + tw + 5, y);
  }

  // Right side: percent, liters, sensor status
  const int pcx = 238;
  canvas.setTextDatum(middle_center);

  canvas.setFont(&fonts::FreeSansBold24pt7b);
  if (hasData) {
    uint16_t col = highIsBad ? (percent >= 90 ? COL_BAD : (percent >= 75 ? COL_WARN : COL_TEXT))
                             : (percent <= 15 ? COL_BAD : (percent <= 30 ? COL_WARN : COL_TEXT));
    canvas.setTextColor(offline ? COL_DIM : col);
    canvas.drawString(String(percent) + "%", pcx, 84);
  } else {
    canvas.setTextColor(COL_DIM);
    canvas.drawString("--", pcx, 84);
  }

  canvas.drawFastHLine(pcx - 50, 118, 100, COL_GRID);

  canvas.setFont(&fonts::FreeSansBold18pt7b);
  if (liters >= 0) {
    canvas.setTextColor(offline ? COL_DIM : COL_TEXT);
    canvas.drawString(String(liters, 1) + " L", pcx, 152);
  } else {
    canvas.setTextColor(COL_DIM);
    canvas.drawString("--", pcx, 152);
  }

  // Status dot: green = online, red = offline (with a short label), grey = no data yet
  if (offline) {
    canvas.fillCircle(pcx - 36, 196, 5, COL_BAD);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextColor(COL_BAD);
    canvas.setTextDatum(middle_left);
    canvas.drawString("offline", pcx - 24, 196);
  } else {
    canvas.fillCircle(pcx, 196, 5, statusKnown ? COL_OK : COL_GRID);
  }
}

void drawPageWater() {
  drawTankPage(waterPercent, waterLiters, waterOnline, waterStatusKnown, false, COL_ACCENT, COL_WAVE);
}

void drawPageGrey() {
  // The grey water sensor shares the online/offline status of the water device
  drawTankPage(greyPercent, greyLiters, waterOnline, waterStatusKnown, true, COL_GREY_W, COL_GREY_HI);
}

// ---------- Gas page ----------
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

  // Bottle geometry: rounded body, neck and valve on top
  const int bx = 34, by = 62, bw = 88, bh = 148, brad = 30;
  const int wall = 5;
  const int ix = bx + wall, iy = by + wall, iw = bw - 2 * wall, ih = bh - 2 * wall;
  const int irad = brad - wall;
  const int cx = bx + bw / 2;

  bool hasData = gasPercent >= 0;
  bool offline = gasStatusKnown && !gasOnline;
  uint16_t fillCol = offline ? COL_DIM : (gasPercent <= 10 ? COL_BAD : COL_GAS);

  // Neck, valve and body
  canvas.fillRect(cx - 12, 46, 24, 20, COL_STEEL);
  canvas.fillRoundRect(cx - 18, 38, 36, 10, 3, COL_STEEL);
  canvas.fillRoundRect(bx, by, bw, bh, brad, COL_PANEL);

  // Gas level, clipped to the rounded inner shape
  if (hasData && gasPercent > 0) {
    int fillH = ih * gasPercent / 100;
    int topY = iy + ih - fillH;
    for (int y = topY; y < iy + ih; y++) {
      int inset = roundedInset(y, iy, ih, irad);
      canvas.fillRect(ix + inset, y, iw - 2 * inset, 1, fillCol);
    }
    int inset = roundedInset(topY, iy, ih, irad);
    canvas.fillRect(ix + inset, topY, iw - 2 * inset, 2, offline ? COL_GRID : COL_GAS_HI);
  }

  // Outline (double for a thicker wall)
  canvas.drawRoundRect(bx, by, bw, bh, brad, COL_TEXT);
  canvas.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, brad - 1, COL_TEXT);

  // Scale ticks at 25 / 50 / 75 %
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  for (int p = 25; p <= 75; p += 25) {
    int y = iy + ih - ih * p / 100;
    canvas.drawFastHLine(bx + bw - wall - 12, y, 12, COL_TEXT);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(String(p), bx + bw + 5, y);
  }

  // Right side: percent, remaining content in liters, sensor status
  const int pcx = 238;
  canvas.setTextDatum(middle_center);

  canvas.setFont(&fonts::FreeSansBold24pt7b);
  if (hasData) {
    uint16_t col = gasPercent <= 10 ? COL_BAD : (gasPercent <= 20 ? COL_WARN : COL_TEXT);
    canvas.setTextColor(offline ? COL_DIM : col);
    canvas.drawString(String(gasPercent) + "%", pcx, 84);
  } else {
    canvas.setTextColor(COL_DIM);
    canvas.drawString("--", pcx, 84);
  }

  canvas.drawFastHLine(pcx - 50, 118, 100, COL_GRID);

  canvas.setFont(&fonts::FreeSansBold18pt7b);
  if (gasLiters >= 0) {
    canvas.setTextColor(offline ? COL_DIM : COL_TEXT);
    canvas.drawString(String(gasLiters, 1) + " L", pcx, 152);
  } else {
    canvas.setTextColor(COL_DIM);
    canvas.drawString("--", pcx, 152);
  }

  // Status: green dot = online, red + "offline" = ESP offline, yellow + "BLE" = ESP up but no BLE link to the bottle sensor
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  if (offline) {
    canvas.fillCircle(pcx - 36, 196, 5, COL_BAD);
    canvas.setTextColor(COL_BAD);
    canvas.drawString("offline", pcx - 24, 196);
  } else if (gasStatusKnown && !gasBleOk) {
    canvas.fillCircle(pcx - 20, 196, 5, COL_WARN);
    canvas.setTextColor(COL_WARN);
    canvas.drawString("BLE", pcx - 8, 196);
  } else {
    canvas.fillCircle(pcx - 22, 196, 5, gasStatusKnown ? COL_OK : COL_GRID);
  }

  // Sensor battery (small battery symbol + percent), only while the ESP is reachable
  if (!offline && gasBattery >= 0) {
    int bx2 = pcx + 2, by2 = 189;
    uint16_t bcol = gasBattery > 40 ? COL_OK : (gasBattery > 15 ? COL_WARN : COL_BAD);
    canvas.drawRoundRect(bx2, by2, 20, 14, 2, COL_DIM);
    canvas.fillRect(bx2 + 20, by2 + 4, 2, 6, COL_DIM);
    int w2 = (16 * gasBattery) / 100;
    if (w2 > 0) canvas.fillRect(bx2 + 2, by2 + 2, w2, 10, bcol);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(String(gasBattery) + "%", bx2 + 27, 196);
  }
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
#if USE_GREY
    case PAGE_GREY:   drawPageGrey();   break;
#endif
#if USE_GAS
    case PAGE_GAS:    drawPageGas();    break;
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

// ---------- MQTT ----------
void handleWater(const String& t, const String& message) {
  if (t.endsWith("/percent")) {
    waterPercent = constrain((int)message.toInt(), 0, 100);
  } else if (t.endsWith("/liters")) {
    waterLiters = message.toFloat();
  } else if (t.endsWith("/status")) {          // separate device only
    waterOnline = (message == "online");
    waterStatusKnown = true;
  }
}

void handleGrey(const String& t, const String& message) {
  if (t.endsWith("/percent")) {
    greyPercent = constrain((int)message.toInt(), 0, 100);
  } else if (t.endsWith("/liters")) {
    greyLiters = message.toFloat();
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
  } else if (t.endsWith("/status")) {          // separate device only
    gasOnline = (message == "online");
    gasStatusKnown = true;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  String t(topic);
  Serial.printf("MQTT %s = %s\n", topic, message.c_str());  // debug: shows every topic

  // Combined device: one status topic covers both water and gas
  if (t == TOPIC_TANK_STATUS) {
    bool online = (message == "online");
    waterOnline = gasOnline = online;
    waterStatusKnown = gasStatusKnown = true;
    dirty = true;
    return;
  }

#if USE_WATER
  // Water tank topics (combined or separate device)
  if (t.startsWith(TOPIC_TANK_WATER) || t.startsWith(TOPIC_WATER_PREFIX)) {
    handleWater(t, message);
    dirty = true;
    return;
  }
#endif

#if USE_GREY
  // Grey water tank topics (combined firmware only)
  if (t.startsWith(TOPIC_TANK_GREY)) {
    handleGrey(t, message);
    dirty = true;
    return;
  }
#endif

#if USE_GAS
  // Gas bottle topics (combined or separate device)
  if (t.startsWith(TOPIC_TANK_GAS) || t.startsWith(TOPIC_GAS_PREFIX)) {
    handleGas(t, message);
    dirty = true;
    return;
  }
#endif

#if USE_LEVEL
  // Level sensor: match only the exact topic suffix, ignore e.g. pitch_raw / roll_offset
  if (t.endsWith("/pitch")) {
    pitch = message.toFloat();
    havePitch = true;
    lastPitchMs = millis();
    dirty = true;
  } else if (t.endsWith("/roll")) {
    roll = message.toFloat();
    haveRoll = true;
    lastRollMs = millis();
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
  if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
    // Retained values arrive immediately after subscribing
#if USE_LEVEL
    client.subscribe(mqtt_topic);
#endif
#if USE_WATER || USE_GREY || USE_GAS
    client.subscribe(TOPIC_TANK_STATUS);  // combined firmware: shared online/offline status
#endif
#if USE_WATER
    client.subscribe((String(TOPIC_TANK_WATER) + "#").c_str());
    client.subscribe(TOPIC_WATER_ALL);
#endif
#if USE_GREY
    client.subscribe((String(TOPIC_TANK_GREY) + "#").c_str());
#endif
#if USE_GAS
    client.subscribe((String(TOPIC_TANK_GAS) + "#").c_str());
    client.subscribe(TOPIC_GAS_ALL);
#endif
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

  if (page_index < 0 || page_index >= PAGE_COUNT) page_index = 0;

  M5.Display.setBrightness(128);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());

  startWifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  lastActivityMs = millis();
  render();
}

void loop() {
  M5.update();

  mqttReconnect();
  client.loop();

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
      page_index = (page_index + (a ? PAGE_COUNT - 1 : 1)) % PAGE_COUNT;
      dirty = true;
    }
  }

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
    unsigned long interval = (cur == PAGE_WATER || cur == PAGE_GREY) ? 120 : 1000;  // tank pages animate
    if ((dirty && since >= 150) || since >= interval) {
      render();
    }
  }
}
