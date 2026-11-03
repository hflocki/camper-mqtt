#include "alerts.h"
#include "settings.h"
#include "datastore.h"
#include "sensors.h"
#include "weather.h"
#include "mqtt_client.h"
#include <map>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

std::map<String, bool> states;
SemaphoreHandle_t mtx = nullptr;

struct Lock {
  Lock() {
    if (!mtx) mtx = xSemaphoreCreateMutex();   // first use is in setup, before other tasks run
    xSemaphoreTake(mtx, portMAX_DELAY);
  }
  ~Lock() { xSemaphoreGive(mtx); }
};

// "8,5" with one decimal and a comma, like everywhere on the display
String comma1(float v) {
  String s(v, 1);
  s.replace('.', ',');
  return s;
}

}  // namespace

namespace alerts {

bool latch(const String& key, bool on, bool off) {
  Lock lock;
  bool& st = states[key];
  if (!st && on) st = true;
  else if (st && off) st = false;
  return st;
}

bool below(const String& key, float value, float limit, float hyst) {
  if (limit == 0 || isnan(value)) { latch(key, false, true); return false; }
  return latch(key, value < limit, value >= limit + hyst);
}

bool above(const String& key, float value, float limit, float hyst) {
  if (isnan(limit) || isnan(value)) { latch(key, false, true); return false; }
  return latch(key, value > limit, value <= limit - hyst);
}

String warnings() {
  String w;
  auto add = [&](const String& s) { if (w.length()) w += "\n"; w += s; };
  const float hp = settings.wnHystPct;

  // tanks: only while the tank firmware is online (old retained values are not a warning)
  bool tank = store::devOnline(DEV_TANK);
  float water = tank && store::has(DP_WATER_PCT) ? store::getf(DP_WATER_PCT) : NAN;
  if (below("water", water, settings.wnWater, hp))
    add("Frischwasser niedrig: " + String((int)lroundf(water)) + " %");
  float gas = tank && store::has(DP_GAS_PCT) ? store::getf(DP_GAS_PCT) : NAN;
  if (below("gas", gas, settings.wnGas, hp))
    add("Gas niedrig: " + String((int)lroundf(gas)) + " %");

  // frost: measured outside temperature first, otherwise the current weather
  float out = NAN;
  String src;
  if (settings.wnFrostOn) {
    sensors::TempOut o;
    if (sensors::outdoor(o) && !o.stale && !isnan(o.temp)) {
      out = o.temp;
      src = "au\xC3\x9F" "en";
    } else if (settings.wxOn) {
      wx::Data d = wx::get();
      if (d.valid && time(nullptr) - d.at < 3 * 3600) { out = d.temp; src = "Wetter"; }
    }
  }
  // limit 0 °C is a valid frost limit, so the on switch decides here, not the value
  if (!settings.wnFrostOn) latch("frost", false, true);
  else if (latch("frost", !isnan(out) && out < settings.wnFrost,
                 isnan(out) || out >= settings.wnFrost + settings.wnHystT / 10.0f))
    add("Frostgefahr: " + (isnan(out) ? String("--") : comma1(out)) + " \xC2\xB0" "C " + src);

  // awning out and gusts above its own (lower) limit
  float gust = NAN;
  if (settings.wxOn && settings.awnGust > 0 && awning() == 1) {
    wx::Data d = wx::get();
    if (d.valid && time(nullptr) - d.at < 3 * 3600) gust = d.gust;
  }
  if (latch("awning", !isnan(gust) && gust >= settings.awnGust,
            isnan(gust) || gust < settings.awnGust - settings.wxGustHyst))
    add("Markise einfahren: B\xC3\xB6" "en bis " + (isnan(gust) ? String("--") : String((int)lroundf(gust))) + " km/h");
  return w;
}

bool awningIsSwitch() { return settings.awnSrc.isEmpty(); }

int8_t awning() {
  if (awningIsSwitch()) return settings.awnOut ? 1 : 0;
  bool stale;
  int8_t s = sensors::contactState(settings.awnSrc, stale);
  return stale ? -1 : s;
}

void setAwning(bool out) {
  if (!awningIsSwitch() || settings.awnOut == out) return;
  settings.awnOut = out;
  cfg::saveAwning();
  mqttc::publishSelfState();
}

}  // namespace alerts
