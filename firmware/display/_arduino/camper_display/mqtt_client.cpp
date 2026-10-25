#include "mqtt_client.h"
#include "config.h"
#include "settings.h"
#include "datastore.h"
#include "wifi_manager.h"
#include "display_hw.h"
#include "ui.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <map>

namespace {

WiFiClient   net;
PubSubClient mqtt(net);

uint32_t lastAttempt = 0;
uint32_t lastSelf = 0;
bool     wasConnected = false;
String   clientId;
std::vector<String> tileSubs;   // currently subscribed tile topics

// ---- topic finder state
bool     finderOn = false;
String   finderSub;
uint32_t finderStarted = 0;
const uint32_t FINDER_TIMEOUT_MS = 15UL * 60UL * 1000UL;
const size_t   FINDER_MAX = 120;
std::map<String, String> finderSeen;
std::vector<mqttc::Found> finderNew;

String self(const char* sub) { return settings.prefixSelf + "/" + sub; }

bool publish(const String& topic, const String& payload, bool retain = false) {
  if (!mqtt.connected() || topic.isEmpty()) return false;
  bool ok = mqtt.publish(topic.c_str(), payload.c_str(), retain);
  Serial.printf("[mqtt] > %s = %s%s\n", topic.c_str(), payload.c_str(), ok ? "" : " (failed)");
  return ok;
}

void handleSelfCommand(const String& sub, const String& msg) {
  if (sub == "brightness") {
    int v = constrain(msg.toInt(), 5, 100);
    settings.brightness = v;
    disp::setBrightness(v);
    disp::wake();
    mqttc::publishSelfState();
  } else if (sub == "screen") {
    if (msg.equalsIgnoreCase("OFF")) disp::sleep(); else disp::wake();
    mqttc::publishSelfState();
  } else if (sub == "page") {
    ui::showPageByName(msg);
    disp::wake();
  } else if (sub == "reboot") {
    delay(200);
    ESP.restart();
  }
}

bool matchesFilter(const String& filter, const char* topic) {
  // MQTT wildcard match for '+' and trailing '#'
  const char* f = filter.c_str();
  const char* t = topic;
  while (*f) {
    if (*f == '#') return true;
    if (*f == '+') {
      while (*t && *t != '/') t++;
      f++;
      continue;
    }
    if (*f != *t) return false;
    f++; t++;
  }
  return *t == 0;
}

void onMessage(char* topic, byte* payload, unsigned int len) {
  // own commands
  String cmdPrefix = settings.prefixSelf + "/cmd/";
  if (strncmp(topic, cmdPrefix.c_str(), cmdPrefix.length()) == 0) {
    String msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    handleSelfCommand(String(topic + cmdPrefix.length()), msg);
    return;
  }

  store::onMessage(topic, (const char*)payload, len);

  if (finderOn && matchesFilter(finderSub, topic)) {
    // skip the big zigbee2mqtt bridge dumps, they are not useful as tiles
    if (strstr(topic, "/bridge/") != nullptr) return;
    String p;
    unsigned int n = len > 300 ? 300 : len;
    p.reserve(n + 3);
    for (unsigned int i = 0; i < n; i++) p += (char)payload[i];
    if (len > n) p += "...";
    auto it = finderSeen.find(topic);
    if (it == finderSeen.end()) {
      if (finderSeen.size() >= FINDER_MAX) return;
      finderSeen[topic] = p;
      finderNew.push_back({topic, p});
    } else if (it->second != p) {
      it->second = p;
      finderNew.push_back({topic, p});
    }
  }
}

void subscribeAll() {
  const String* prefixes[] = {&settings.prefixTank, &settings.prefixLevel,
                              &settings.prefixHeater, &settings.prefixFan, &settings.prefixTemp};
  for (auto p : prefixes) {
    if (p->isEmpty()) continue;
    mqtt.subscribe((*p + "/#").c_str());
  }
  mqtt.subscribe(self("cmd/#").c_str());

  tileSubs.clear();
  for (auto& t : settings.tiles) {
    if (t.topic.isEmpty()) continue;
    bool dup = false;
    for (auto& s : tileSubs) if (s == t.topic) { dup = true; break; }
    if (dup) continue;
    mqtt.subscribe(t.topic.c_str());
    tileSubs.push_back(t.topic);
  }
  if (finderOn) mqtt.subscribe(finderSub.c_str());
}

void connect() {
  if (settings.mqttHost.isEmpty() || !wifimgr::staConnected()) return;
  uint32_t now = millis();
  if (lastAttempt != 0 && now - lastAttempt < MQTT_RECONNECT_MS) return;
  lastAttempt = now;

  mqtt.setServer(settings.mqttHost.c_str(), settings.mqttPort);
  String will = self("status");
  const char* user = settings.mqttUser.isEmpty() ? nullptr : settings.mqttUser.c_str();
  const char* pass = settings.mqttUser.isEmpty() ? nullptr : settings.mqttPass.c_str();
  Serial.printf("[mqtt] connecting to %s:%u\n", settings.mqttHost.c_str(), settings.mqttPort);
  if (mqtt.connect(clientId.c_str(), user, pass, will.c_str(), 1, true, "offline")) {
    Serial.println("[mqtt] connected");
    publish(will, "online", true);
    subscribeAll();
    mqttc::publishSelfState();
  } else {
    Serial.printf("[mqtt] connect failed, rc=%d\n", mqtt.state());
  }
}

String tileCmdTopic(const Tile& t) {
  if (t.cmdTopic.length()) return t.cmdTopic;
  return t.topic + "/set";
}

} // namespace

namespace mqttc {

void begin() {
  clientId = String(FW_NAME) + "-" + wifimgr::apSsid().substring(strlen(AP_SSID_PREFIX));
  mqtt.setBufferSize(MQTT_BUFFER_SIZE);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  mqtt.setCallback(onMessage);
}

void loop() {
  if (!mqtt.connected()) {
    if (wasConnected) Serial.println("[mqtt] disconnected");
    wasConnected = false;
    connect();
    return;
  }
  wasConnected = true;
  mqtt.loop();

  if (millis() - lastSelf > SELF_PUBLISH_MS) publishSelfState();
  if (finderOn && millis() - finderStarted > FINDER_TIMEOUT_MS) finderStop();
}

bool connected() { return mqtt.connected(); }

void resubscribe() {
  if (!mqtt.connected()) return;
  // Unsubscribing everything is not possible with PubSubClient, so a clean
  // reconnect is the simplest way to drop stale tile subscriptions.
  mqtt.disconnect();
  lastAttempt = 0;
}

void publishSelfState() {
  lastSelf = millis();
  if (!mqtt.connected()) return;
  publish(self("rssi"), String(wifimgr::rssi()), true);
  publish(self("ip"), wifimgr::ipString(), true);
  publish(self("brightness"), String(settings.brightness), true);
  publish(self("screen"), disp::isAwake() ? "ON" : "OFF", true);
}

void finderStart(const String& filter) {
  String f = filter;
  f.trim();
  if (f.isEmpty()) f = DEFAULT_FINDER_FILTER;
  if (finderOn && f != finderSub && mqtt.connected()) mqtt.unsubscribe(finderSub.c_str());
  finderSub = f;
  finderOn = true;
  finderStarted = millis();
  finderSeen.clear();
  finderNew.clear();
  if (mqtt.connected()) mqtt.subscribe(finderSub.c_str());
  Serial.printf("[finder] listening on %s\n", finderSub.c_str());
}

void finderStop() {
  if (!finderOn) return;
  finderOn = false;
  if (mqtt.connected()) {
    // keep the subscription if a device prefix or tile still needs it
    bool needed = false;
    for (auto& s : tileSubs) if (s == finderSub) needed = true;
    if (!needed) mqtt.unsubscribe(finderSub.c_str());
  }
  Serial.println("[finder] stopped");
}

bool finderActive() { return finderOn; }
const String& finderFilter() { return finderSub; }

std::vector<Found> finderTake() {
  std::vector<Found> out;
  out.swap(finderNew);
  return out;
}

std::vector<Found> finderAll() {
  std::vector<Found> out;
  for (auto& kv : finderSeen) out.push_back({kv.first, kv.second});
  return out;
}

} // namespace mqttc

// ---------------------------------------------------------------------
//  Commands
// ---------------------------------------------------------------------
namespace cmd {

bool heater(const String& key, const String& value) {
  if (settings.prefixHeater.isEmpty()) return false;
  String v = value;
  if (key == "hvac_mode") {
    if (v != "heat" && v != "off") return false;
  } else if (key == "room_setpoint") {
    float t = v.toFloat();
    if (t < 5 || t > 30) return false;
    v = String((int)lroundf(t));
  } else if (key == "fan_mode") {
    if (v != "eco" && v != "high" && v != "boost") return false;
  } else if (key == "water_mode") {
    if (v != "off" && v != "eco" && v != "high" && v != "boost") return false;
  } else if (key == "decoke" || key == "timer_active") {
    v.toUpperCase();
    if (v != "ON" && v != "OFF") return false;
  } else {
    return false;
  }
  return publish(settings.prefixHeater + "/set/" + key, v);
}

bool fan(const String& key, const String& value) {
  if (settings.prefixFan.isEmpty()) return false;
  String port = settings.prefixFan + "/cmd/p" + String(settings.fanPort) + "/";
  static const char* buttons[] = {"auto", "direction", "on_off", "faster", "slower", "beep"};
  for (auto b : buttons) if (key == b) return publish(port + key, "PRESS");
  if (key == "speed") {
    int s = value.toInt();
    if (s < 1 || s > 10) return false;
    return publish(port + "speed/set", String(s));
  }
  if (key == "close")    return publish(port + "close", "PRESS");   // camper-maxxfan 1.4.0+
  if (key == "led")      return publish(port + "led/set", value.length() ? value : "TOGGLE");
  if (key == "sync_off") return publish(settings.prefixFan + "/cmd/sync_off", "PRESS");
  return false;
}

bool level(const String& key) {
  if (settings.prefixLevel.isEmpty()) return false;
  if (key != "tare" && key != "tare_reset") return false;
  return publish(settings.prefixLevel + "/cmd/" + key + "/set", "PRESS");
}

bool tile(size_t i, const String& action) {
  if (i >= settings.tiles.size()) return false;
  const Tile& t = settings.tiles[i];
  String topic = tileCmdTopic(t);
  switch (t.type) {
    case TILE_SWITCH: {
      bool on;
      if (action == "on") on = true;
      else if (action == "off") on = false;
      else on = !store::tileOn(i);   // toggle
      String p = on ? t.payloadOn : t.payloadOff;
      if (p.isEmpty()) p = on ? "ON" : "OFF";
      return publish(topic, p);
    }
    case TILE_BUTTON:
      return publish(topic, t.payloadOn.length() ? t.payloadOn : "PRESS");
    case TILE_SLIDER: {
      float v = constrain(action.toFloat(), t.min, t.max);
      String num = (t.step >= 1 && fmodf(t.step, 1.0f) == 0) ? String((long)lroundf(v)) : String(v, 2);
      String p = t.payloadOn.length() ? t.payloadOn : String("{v}");
      p.replace("{v}", num);
      return publish(topic, p);
    }
    default:
      return false;
  }
}

} // namespace cmd
