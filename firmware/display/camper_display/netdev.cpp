#include "netdev.h"
#include "settings.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

const size_t MAX_ENTRIES = 16;
const uint32_t INFO_STALE_MS = 3UL * 60UL * 1000UL;   // devices send their info every minute

std::vector<netdev::Entry> entries;
bool dirty = false;
SemaphoreHandle_t mtx = nullptr;

struct Lock {
  Lock() {
    if (!mtx) mtx = xSemaphoreCreateMutex();
    xSemaphoreTake(mtx, portMAX_DELAY);
  }
  ~Lock() { xSemaphoreGive(mtx); }
};

// "camper/display" -> "camper"; a prefix without '/' has no common root
String root() {
  int i = settings.prefixSelf.indexOf('/');
  return i > 0 ? settings.prefixSelf.substring(0, i) : String();
}

netdev::Entry& entry(const String& id) {
  for (auto& e : entries) if (e.id == id) return e;
  if (entries.size() >= MAX_ENTRIES) {
    // drop the oldest device that is not online
    auto it = std::find_if(entries.begin(), entries.end(), [](const netdev::Entry& e) { return e.online != 1; });
    entries.erase(it != entries.end() ? it : entries.begin());
  }
  entries.push_back(netdev::Entry());
  entries.back().id = id;
  return entries.back();
}

}  // namespace

namespace netdev {

std::vector<String> topics() {
  String r = root();
  if (r.isEmpty()) return {};
  return {r + "/+/status", r + "/+/info"};
}

bool onMessage(const char* topic, const char* payload, unsigned int len) {
  String r = root();
  if (r.isEmpty()) return false;
  size_t rl = r.length();
  if (strncmp(topic, r.c_str(), rl) != 0 || topic[rl] != '/') return false;
  const char* id = topic + rl + 1;
  const char* slash = strchr(id, '/');
  if (!slash || slash == id) return false;
  const char* leaf = slash + 1;
  bool isStatus = strcmp(leaf, "status") == 0, isInfo = strcmp(leaf, "info") == 0;
  if (!isStatus && !isInfo) return false;
  String devId(id, slash - id);

  Lock lock;
  if (isStatus) {
    if (len == 0) {   // retained status deleted: device removed
      entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.id == devId; }),
                    entries.end());
      dirty = true;
      return true;
    }
    int8_t on = (len == 6 && strncasecmp(payload, "online", 6) == 0) ? 1 : 0;
    Entry& e = entry(devId);
    if (e.online != on) { e.online = on; dirty = true; }
    return true;
  }
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return true;
  Entry& e = entry(devId);
  e.hasInfo = true;
  e.version = doc["version"] | "";
  e.ip = doc["ip"] | "";
  e.reset = doc["reset"] | "";
  e.rssi = doc["rssi"] | 0;
  e.uptime = doc["uptime"] | 0;
  e.infoAt = millis();
  dirty = true;
  return true;
}

std::vector<Entry> list() {
  std::vector<Entry> out;
  {
    Lock lock;
    out = entries;
  }
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
    if ((a.online == 1) != (b.online == 1)) return a.online == 1;
    return name(a.id) < name(b.id);
  });
  return out;
}

String name(const String& id) {
  static const char* const MAP[][2] = {
    {"display", "Display"}, {"roadbuddy", "Road Buddy"}, {"tanklevel", "Tanks"}, {"level", "Level"},
    {"heater", "Heizung"}, {"truma", "Heizung"}, {"maxxfan", "Dachl\xC3\xBC" "fter"}, {"temp", "Temperatur"},
    {"battery", "Batterie"}, {"zigbee", "Zigbee"}, {"ble", "Bluetooth"}, {"router", "Router"},
  };
  for (auto& m : MAP) if (id == m[0]) return m[1];
  return id;
}

bool infoStale(const Entry& e) {
  return !e.hasInfo || millis() - e.infoAt > INFO_STALE_MS;
}

String json() {
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  uint32_t now = millis();
  for (auto& e : list()) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = e.id;
    o["n"] = name(e.id);
    o["on"] = e.online;
    if (e.hasInfo) {
      o["v"] = e.version;
      o["ip"] = e.ip;
      o["rssi"] = e.rssi;
      o["up"] = e.uptime + (now - e.infoAt) / 1000;   // estimated current uptime
      o["rst"] = e.reset;
      o["age"] = (now - e.infoAt) / 1000;
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool changed() {
  Lock lock;
  bool d = dirty;
  dirty = false;
  return d;
}

}  // namespace netdev
