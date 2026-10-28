#include "weather.h"
#include "settings.h"
#include "wifi_manager.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace wx {
namespace {

SemaphoreHandle_t lock = nullptr;

// The UI and the web server read the data before begin() runs, so create the mutex on first use.
void take() {
  if (!lock) lock = xSemaphoreCreateMutex();
  xSemaphoreTake(lock, portMAX_DELAY);
}
void give() { xSemaphoreGive(lock); }
TaskHandle_t task = nullptr;
Data cur;
String lastStatus = "";
float gpsLat = NAN, gpsLon = NAN;       // from the MQTT position topic
volatile bool wakeUp = false;
const uint32_t RETRY_MS = 2UL * 60 * 1000;

const char* const API = "https://api.open-meteo.com/v1/forecast";

// Root certificates of the ESP32 core (Mozilla bundle), also used by notify.cpp
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");

bool position(float& lat, float& lon) {
  if (settings.wxTopic.length() && !isnan(gpsLat)) { lat = gpsLat; lon = gpsLon; return true; }
  if (settings.wxLat.length() && settings.wxLon.length()) {
    lat = settings.wxLat.toFloat();
    lon = settings.wxLon.toFloat();
    return !(lat == 0 && lon == 0) && fabsf(lat) <= 90 && fabsf(lon) <= 180;
  }
  return false;
}

void setStatus(const String& s) {
  if (s != lastStatus) Serial.println("[wx] " + s);
  take();
  lastStatus = s;
  give();
}

float num(JsonVariantConst v) { return v.is<float>() ? v.as<float>() : NAN; }

bool fetch(float lat, float lon) {
  char url[420];
  snprintf(url, sizeof(url),
           "%s?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,"
           "wind_speed_10m,wind_direction_10m,wind_gusts_10m"
           "&hourly=precipitation,precipitation_probability&forecast_hours=2"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,"
           "precipitation_probability_max,wind_gusts_10m_max&forecast_days=%d&timezone=auto&timeformat=unixtime",
           API, lat, lon, DAYS);
  NetworkClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
  HTTPClient http;
  http.setTimeout(10000);
  http.useHTTP10(true);   // no chunked transfer encoding, so the JSON can be parsed straight from the stream
  if (!http.begin(client, url)) { setStatus("Wetter: keine Verbindung"); return false; }
  int code = http.GET();
  if (code != 200) {
    setStatus("Wetter: Fehler " + String(code) + (code < 0 ? " (" + http.errorToString(code) + ")" : String("")));
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) { setStatus(String("Wetter: ") + err.c_str()); return false; }

  Data d;
  JsonObjectConst c = doc["current"];
  d.temp = num(c["temperature_2m"]);
  d.feels = num(c["apparent_temperature"]);
  d.hum = num(c["relative_humidity_2m"]);
  d.wind = num(c["wind_speed_10m"]);
  d.gust = num(c["wind_gusts_10m"]);
  d.dir = c["wind_direction_10m"] | -1;
  d.code = c["weather_code"] | -1;
  d.day = (c["is_day"] | 1) == 1;
  JsonObjectConst h = doc["hourly"];
  // hourly values are for the preceding hour: index 1 is the coming hour
  int hi = h["precipitation"].size() > 1 ? 1 : 0;
  d.rainNext = num(h["precipitation"][hi]);
  d.popNext = num(h["precipitation_probability"][hi]);
  JsonObjectConst dl = doc["daily"];
  for (int i = 0; i < DAYS; i++) {
    d.dCode[i] = dl["weather_code"][i] | -1;
    d.tMax[i] = num(dl["temperature_2m_max"][i]);
    d.tMin[i] = num(dl["temperature_2m_min"][i]);
    d.rainSum[i] = num(dl["precipitation_sum"][i]);
    d.popMax[i] = num(dl["precipitation_probability_max"][i]);
    d.gustMax[i] = num(dl["wind_gusts_10m_max"][i]);
  }
  d.lat = lat;
  d.lon = lon;
  d.valid = !isnan(d.temp) && d.code >= 0;
  d.at = time(nullptr);
  if (!d.valid) { setStatus("Wetter: unvollständige Antwort"); return false; }
  take();
  cur = d;
  give();
  struct tm tm;
  localtime_r(&d.at, &tm);
  char b[32];
  snprintf(b, sizeof(b), "Stand %02d:%02d", tm.tm_hour, tm.tm_min);
  setStatus(b);
  Serial.printf("[wx] %.1f C, code %d, gusts %.0f km/h (%.3f, %.3f)\n", d.temp, d.code, d.gust, lat, lon);
  return true;
}

// distance in km between two positions (good enough for "moved a bit")
float distKm(float la1, float lo1, float la2, float lo2) {
  float x = (lo2 - lo1) * cosf((la1 + la2) * 0.5f * DEG_TO_RAD);
  float y = la2 - la1;
  return sqrtf(x * x + y * y) * 111.2f;
}

void run(void*) {
  uint32_t next = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!settings.wxOn) { next = 0; continue; }
    if (!wifimgr::staConnected()) continue;
    float lat, lon;
    if (!position(lat, lon)) {
      setStatus(settings.wxTopic.length() ? "Warte auf Position (MQTT)" : "Kein Standort eingestellt");
      continue;
    }
    Data d = get();
    bool moved = d.valid && !isnan(d.lat) && distKm(d.lat, d.lon, lat, lon) > 5;
    uint32_t now = millis();
    if (!wakeUp && !moved && next && (int32_t)(now - next) < 0) continue;
    wakeUp = false;
    bool ok = fetch(lat, lon);
    uint32_t every = (uint32_t)constrain((int)settings.wxEvery, 10, 180) * 60UL * 1000UL;
    next = millis() + (ok ? every : RETRY_MS);
  }
}

}  // namespace

void begin() {
  if (!lock) lock = xSemaphoreCreateMutex();
  if (task) return;
  xTaskCreatePinnedToCore(run, "wx", 12288, nullptr, 1, &task, 0);
}

void loop() {}

Data get() {
  take();
  Data d = cur;
  give();
  return d;
}

bool enabled() { return settings.wxOn; }

String status() {
  if (!settings.wxOn) return "Wetter aus";
  take();
  String s = lastStatus;
  give();
  return s;
}

void requestUpdate() { wakeUp = true; }

bool onMessage(const char* topic, const char* payload, unsigned int len) {
  if (settings.wxTopic.isEmpty() || settings.wxTopic != topic) return false;
  String p;
  p.reserve(len);
  for (unsigned i = 0; i < len; i++) p += payload[i];
  float lat = NAN, lon = NAN;
  JsonDocument doc;
  if (!deserializeJson(doc, p) && doc.is<JsonObject>()) {
    // {"lat":..,"lon":..}, {"latitude":..,"longitude":..}, {"LAT":"..","LNG":".."} ...
    for (JsonPair kv : doc.as<JsonObject>()) {
      String k = kv.key().c_str();
      k.toLowerCase();
      float v = kv.value().is<const char*>() ? String(kv.value().as<const char*>()).toFloat() : kv.value().as<float>();
      if (k == "lat" || k == "latitude") lat = v;
      else if (k == "lon" || k == "lng" || k == "long" || k == "longitude") lon = v;
    }
  } else {
    int c = p.indexOf(',');   // "52.52,13.40"
    if (c > 0) { lat = p.substring(0, c).toFloat(); lon = p.substring(c + 1).toFloat(); }
  }
  if (isnan(lat) || isnan(lon) || (lat == 0 && lon == 0) || fabsf(lat) > 90 || fabsf(lon) > 180) return true;
  bool first = isnan(gpsLat);
  gpsLat = lat;
  gpsLon = lon;
  if (first) wakeUp = true;
  return true;
}

const char* icon(int c, bool day) {
  if (c == 0) return day ? "\xEF\x86\x85" : "\xEF\x86\x86";                 // sun / moon
  if (c == 1 || c == 2) return day ? "\xEF\x9B\x84" : "\xEF\x9B\x83";       // cloud-sun / cloud-moon
  if (c == 3) return "\xEF\x83\x82";                                          // cloud
  if (c == 45 || c == 48) return "\xEF\x9D\x9F";                              // smog
  if (c >= 51 && c <= 63) return "\xEF\x9C\xBD";                              // cloud-rain
  if (c == 65 || c == 66 || c == 67 || c == 82) return "\xEF\x9D\x80";        // heavy showers
  if (c >= 71 && c <= 77) return "\xEF\x8B\x9C";                              // snowflake
  if (c == 80 || c == 81) return day ? "\xEF\x9D\x83" : "\xEF\x9C\xBD";       // sun-rain
  if (c == 85 || c == 86) return "\xEF\x8B\x9C";
  if (c >= 95) return "\xEF\x83\xA7";                                         // bolt
  return "\xEF\x83\x82";
}

const char* text(int c) {
  switch (c) {
    case 0: return "Klar";
    case 1: return "Überwiegend klar";
    case 2: return "Teils bewölkt";
    case 3: return "Bewölkt";
    case 45: case 48: return "Nebel";
    case 51: case 53: case 55: return "Nieselregen";
    case 56: case 57: return "Gefrierender Niesel";
    case 61: return "Leichter Regen";
    case 63: return "Regen";
    case 65: return "Starker Regen";
    case 66: case 67: return "Gefrierender Regen";
    case 71: return "Leichter Schnee";
    case 73: return "Schnee";
    case 75: return "Starker Schnee";
    case 77: return "Schneegriesel";
    case 80: return "Regenschauer";
    case 81: return "Kräftige Schauer";
    case 82: return "Heftige Schauer";
    case 85: case 86: return "Schneeschauer";
    case 95: return "Gewitter";
    case 96: case 99: return "Gewitter mit Hagel";
    default: return "--";
  }
}

const char* dirText(int deg) {
  static const char* const d[] = {"N", "NO", "O", "SO", "S", "SW", "W", "NW"};
  if (deg < 0) return "";
  return d[((deg + 22) % 360) / 45];
}

String warning() {
  if (!settings.wxOn) return "";
  Data d = get();
  if (!d.valid || time(nullptr) - d.at > 3 * 3600) return "";   // old data: no warnings
  String w;
  if (settings.wxGust > 0 && !isnan(d.gust) && d.gust >= settings.wxGust)
    w = "Wind: B\xC3\xB6" "en bis " + String((int)lroundf(d.gust)) + " km/h";
  if (d.code >= 95) {
    if (w.length()) w += "\n";
    w += text(d.code);   // "Gewitter" / "Gewitter mit Hagel"
  }
  return w;
}

String json() {
  Data d = get();
  JsonDocument doc;
  doc["on"] = settings.wxOn;
  doc["status"] = status();
  doc["name"] = settings.wxName;
  doc["warn"] = warning();
  if (d.valid) {
    doc["t"] = d.temp; doc["f"] = d.feels; doc["h"] = d.hum; doc["w"] = d.wind; doc["g"] = d.gust;
    doc["dir"] = dirText(d.dir); doc["c"] = d.code; doc["day"] = d.day; doc["txt"] = text(d.code);
    doc["rn"] = d.rainNext; doc["pn"] = d.popNext; doc["at"] = (long)d.at;
    JsonArray a = doc["d"].to<JsonArray>();
    for (int i = 0; i < DAYS; i++) {
      JsonObject o = a.add<JsonObject>();
      o["c"] = d.dCode[i]; o["hi"] = d.tMax[i]; o["lo"] = d.tMin[i]; o["r"] = d.rainSum[i]; o["p"] = d.popMax[i];
      o["g"] = d.gustMax[i];
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace wx
