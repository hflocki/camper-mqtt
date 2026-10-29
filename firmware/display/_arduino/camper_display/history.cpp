#include "history.h"
#include "config.h"
#include "datastore.h"
#include "sensors.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace history {
namespace {

const int MAX_SERIES = 12;           // 8 temperature sensors + batteries
const int16_t NONE = INT16_MIN;      // no value at this point

struct Series {
  String  name;
  char    kind = 't';                // t = temperature, b = battery %, v = battery voltage
  int16_t* v = nullptr;              // value x 10, ring buffer with HISTORY_POINTS entries
};

Series series[MAX_SERIES];
int nSeries = 0;
int head = 0;                        // index of the next sample
int count = 0;                       // samples stored (<= HISTORY_POINTS)
time_t lastAt = 0;
uint32_t nextMs = 60000;             // first sample one minute after the start
SemaphoreHandle_t lock = nullptr;

int16_t* alloc() {
  size_t bytes = sizeof(int16_t) * HISTORY_POINTS;
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!p) p = malloc(bytes);
  int16_t* a = (int16_t*)p;
  if (a) for (int i = 0; i < HISTORY_POINTS; i++) a[i] = NONE;
  return a;
}

// series for this name and kind, created on first use (all older points empty)
Series* find(const String& name, char kind) {
  for (int i = 0; i < nSeries; i++)
    if (series[i].kind == kind && series[i].name == name) return &series[i];
  if (nSeries >= MAX_SERIES) return nullptr;
  int16_t* a = alloc();
  if (!a) return nullptr;
  Series& s = series[nSeries++];
  s.name = name;
  s.kind = kind;
  s.v = a;
  return &s;
}

int16_t scaled(float f) {
  if (isnan(f)) return NONE;
  float x = roundf(f * 10);
  if (x < -32000 || x > 32000) return NONE;
  return (int16_t)x;
}

void sample() {
  // new column: empty for every series, then fill what is known right now
  for (int i = 0; i < nSeries; i++) series[i].v[head] = NONE;

  if (store::devOnline(DEV_TEMP) && store::has(DP_TEMP_LIST)) {
    JsonDocument doc;
    if (!deserializeJson(doc, store::get(DP_TEMP_LIST))) {
      for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
        if (o["t"].isNull()) continue;
        Series* s = find(o["name"] | "?", 't');
        if (s) s->v[head] = scaled(o["t"].as<float>());
      }
    }
  }
  for (auto& c : sensors::climate()) {   // own Zigbee/BLE climate sensors
    if (c.stale) continue;
    Series* s = find(c.name, 't');
    if (s) s->v[head] = scaled(c.temp);
  }
  if (store::devOnline(DEV_BATTERY) && store::has(DP_BAT_LIST)) {
    JsonDocument doc;
    if (!deserializeJson(doc, store::get(DP_BAT_LIST))) {
      for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
        if (!(o["ok"] | false)) continue;
        bool bm2 = strlen(o["st"] | "") > 0;   // BM2: only the voltage is reliable
        if (bm2 ? !o["v"].is<float>() : !o["soc"].is<float>()) continue;
        Series* s = find(o["name"] | "?", bm2 ? 'v' : 'b');
        if (s) s->v[head] = scaled(bm2 ? o["v"].as<float>() : o["soc"].as<float>());
      }
    }
  }
  head = (head + 1) % HISTORY_POINTS;
  if (count < HISTORY_POINTS) count++;
  time_t now = time(nullptr);
  lastAt = now > 1700000000 ? now : 0;   // 0 = clock not set yet
}

void jsonString(Print& out, const String& s) {
  JsonDocument d;
  d.set(s);
  serializeJson(d, out);
}

}  // namespace

void begin() {
  if (!lock) lock = xSemaphoreCreateMutex();
}

void loop() {
  if ((int32_t)(millis() - nextMs) < 0) return;
  nextMs = millis() + HISTORY_STEP_S * 1000UL;
  xSemaphoreTake(lock, portMAX_DELAY);
  sample();
  xSemaphoreGive(lock);
}

void write(Print& out) {
  if (!lock) begin();
  xSemaphoreTake(lock, portMAX_DELAY);
  out.printf("{\"step\":%d,\"last\":%ld,\"n\":%d,\"s\":[", HISTORY_STEP_S, (long)lastAt, count);
  for (int i = 0; i < nSeries; i++) {
    const Series& s = series[i];
    if (i) out.print(',');
    out.print("{\"n\":");
    jsonString(out, s.name);
    out.printf(",\"k\":\"%c\",\"v\":[", s.kind);
    // oldest first
    int start = (head - count + HISTORY_POINTS) % HISTORY_POINTS;
    for (int k = 0; k < count; k++) {
      int16_t x = s.v[(start + k) % HISTORY_POINTS];
      if (k) out.print(',');
      if (x == NONE) out.print("null");
      else out.printf("%s%d.%d", x < 0 ? "-" : "", abs(x) / 10, abs(x) % 10);
    }
    out.print("]}");
  }
  out.print("]}");
  xSemaphoreGive(lock);
}

}  // namespace history
