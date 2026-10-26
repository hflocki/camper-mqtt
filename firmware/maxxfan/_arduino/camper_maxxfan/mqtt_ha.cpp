#include "mqtt_ha.h"
#include "config.h"
#include "settings.h"
#include "fan_control.h"
#include "wifi_manager.h"
#include "system.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

namespace {

WiFiClient   net;
PubSubClient mqtt(net);

uint32_t lastAttempt = 0;
uint32_t lastFull    = 0;
volatile bool dirty  = true;

String nodeId;        // e.g. camper_maxxfan
String availTopic;    // <prefix>/status

String topic(const String& sub) { return settings.mqttPrefix + "/" + sub; }

void pub(const String& t, const String& payload, bool retain = true) {
  mqtt.publish(t.c_str(), payload.c_str(), retain);
}

String portKey(uint8_t p) { return "p" + String(p); }

// ---------------------------------------------------------------------
//  Home Assistant discovery
// ---------------------------------------------------------------------
String discoveryTopic(const char* component, const String& object) {
  return String("homeassistant/") + component + "/" + nodeId + "/" + object + "/config";
}

void sendConfig(const char* component, const String& object, JsonDocument& doc) {
  doc["unique_id"]          = nodeId + "_" + object;
  doc["availability_topic"] = availTopic;
  doc["has_entity_name"]    = true;
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["identifiers"].add(nodeId + "_" + wifimgr::macSuffix());
  dev["name"]              = "Camper MaxxFan";
  dev["manufacturer"]      = "DIY";
  dev["model"]             = "VanTurtle + ESP32-C3";
  dev["sw_version"]        = FW_VERSION;
  dev["configuration_url"] = "http://" + WiFi.localIP().toString();

  String out;
  serializeJson(doc, out);
  mqtt.publish(discoveryTopic(component, object).c_str(), out.c_str(), true);
}

void removeConfig(const char* component, const String& object) {
  mqtt.publish(discoveryTopic(component, object).c_str(), "", true);
}

void sendDiscovery() {
  for (uint8_t p = 0; p < 2; p++) {
    const String pk = portKey(p);
    const String label = PORT_LABELS[p];
    const bool en = settings.portEnabled[p];

    for (uint8_t b = 0; b < BTN_COUNT; b++) {
      const String obj = pk + "_" + BUTTON_KEYS[b];
      if (!en) { removeConfig("button", obj); continue; }
      JsonDocument d;
      d["name"]          = label + " " + BUTTON_NAMES[b];
      d["command_topic"] = topic("cmd/" + pk + "/" + BUTTON_KEYS[b]);
      d["payload_press"] = "PRESS";
      d["icon"]          = b == BTN_BEEP ? "mdi:volume-high" : "mdi:fan";
      if (b == BTN_BEEP) d["entity_category"] = "config";
      sendConfig("button", obj, d);
    }

    const String ledObj = pk + "_led";
    if (en) {
      JsonDocument d;
      d["name"]          = label + " Controller LED";
      d["state_topic"]   = topic(pk + "/led");
      d["command_topic"] = topic("cmd/" + pk + "/led/set");
      d["icon"]          = "mdi:led-on";
      d["entity_category"] = "config";
      sendConfig("switch", ledObj, d);
    } else removeConfig("switch", ledObj);

    const String speedObj = pk + "_speed";
    if (en) {
      JsonDocument d;
      d["name"]          = label + " Set Speed";
      d["command_topic"] = topic("cmd/" + pk + "/speed/set");
      d["min"]           = 0;   // 0 = close the lid
      d["max"]           = 10;
      d["step"]          = 1;
      d["mode"]          = "slider";
      d["optimistic"]    = true;
      d["icon"]          = "mdi:speedometer";
      sendConfig("number", speedObj, d);
    } else removeConfig("number", speedObj);

    const String closeObj = pk + "_close";
    if (en) {
      JsonDocument d;
      d["name"]          = label + " Close Lid";
      d["command_topic"] = topic("cmd/" + pk + "/close");
      d["payload_press"] = "PRESS";
      d["icon"]          = "mdi:window-shutter";
      sendConfig("button", closeObj, d);
    } else removeConfig("button", closeObj);

    const String holdObj = pk + "_auto_hold";
    if (en) {
      JsonDocument d;
      d["name"]        = label + " Auto Hold";
      d["state_topic"] = topic(pk + "/auto_hold");
      d["icon"]        = "mdi:fan-auto";
      sendConfig("binary_sensor", holdObj, d);
    } else removeConfig("binary_sensor", holdObj);
  }

  {
    JsonDocument d;
    d["name"]          = "Sync to OFF";
    d["command_topic"] = topic("cmd/sync_off");
    d["payload_press"] = "PRESS";
    d["icon"]          = "mdi:fan-off";
    sendConfig("button", "sync_off", d);
  }
  {
    JsonDocument d;
    d["name"]                = "WiFi Signal";
    d["state_topic"]         = topic("rssi");
    d["unit_of_measurement"] = "dBm";
    d["device_class"]        = "signal_strength";
    d["state_class"]         = "measurement";
    d["entity_category"]     = "diagnostic";
    sendConfig("sensor", "rssi", d);
  }
  {
    JsonDocument d;
    d["name"]            = "Port Expander";
    d["state_topic"]     = topic("expander");
    d["device_class"]    = "connectivity";
    d["entity_category"] = "diagnostic";
    sendConfig("binary_sensor", "expander", d);
  }
  {
    JsonDocument d;
    d["name"]            = "Restart";
    d["command_topic"]   = topic("cmd/reboot");
    d["payload_press"]   = "PRESS";
    d["device_class"]    = "restart";
    d["entity_category"] = "config";
    sendConfig("button", "reboot", d);
  }
}

// ---------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------
void publishState() {
  for (uint8_t p = 0; p < 2; p++) {
    if (!settings.portEnabled[p]) continue;
    pub(topic(portKey(p) + "/auto_hold"), fan::autoHold(p) ? "ON" : "OFF");
    pub(topic(portKey(p) + "/led"),       fan::led(p)      ? "ON" : "OFF");
  }
  pub(topic("expander"), fan::expanderOk() ? "ON" : "OFF");
  pub(topic("rssi"), String(wifimgr::rssi()));
}

// ---------------------------------------------------------------------
//  Commands: <prefix>/cmd/...
// ---------------------------------------------------------------------
void onMessage(char* t, byte* payload, unsigned int len) {
  String tp(t);
  String msg;
  msg.reserve(len);
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();

  const String base = settings.mqttPrefix + "/cmd/";
  if (!tp.startsWith(base)) return;
  const String sub = tp.substring(base.length());

  if (sub == "sync_off") { fan::requestSyncOff(); return; }
  if (sub == "reboot")   { requestReboot(); return; }

  // p<n>/<something>
  if (sub.length() < 4 || sub[0] != 'p' || sub[2] != '/') return;
  const uint8_t port = sub[1] - '0';
  if (port > 1) return;
  const String rest = sub.substring(3);

  if (rest == "led/set") {
    String m = msg; m.toUpperCase();
    bool on = (m == "TOGGLE") ? !fan::led(port) : (m == "ON" || m == "1" || m == "TRUE");
    fan::requestLed(port, on);
    return;
  }

  if (rest == "close") {
    fan::requestClose(port);
    return;
  }

  if (rest == "speed/set") {
    fan::requestSpeed(port, (uint8_t)msg.toInt());
    return;
  }

  // p<n>/press with button key as payload, or p<n>/<button>
  FanButton b = (rest == "press") ? fan::buttonFromKey(msg) : fan::buttonFromKey(rest);
  if (b != BTN_COUNT) fan::requestPress(port, b);
}

void connect() {
  uint32_t now = millis();
  if (settings.mqttHost.isEmpty() || !wifimgr::staConnected()) return;
  if (now - lastAttempt < 5000) return;
  lastAttempt = now;

  const String clientId = nodeId + "-" + wifimgr::macSuffix();
  const char* user = settings.mqttUser.isEmpty() ? nullptr : settings.mqttUser.c_str();
  const char* pass = settings.mqttUser.isEmpty() ? nullptr : settings.mqttPass.c_str();

  if (!mqtt.connect(clientId.c_str(), user, pass, availTopic.c_str(), 1, true, "offline")) {
    Serial.printf("[mqtt] connect to %s failed (state %d)\n", settings.mqttHost.c_str(), mqtt.state());
    return;
  }
  Serial.printf("[mqtt] connected to %s\n", settings.mqttHost.c_str());
  pub(availTopic, "online");
  mqtt.subscribe(topic("cmd/#").c_str());
  if (settings.discovery) sendDiscovery();
  dirty = true;
}

} // namespace

namespace mqttha {

void begin() {
  nodeId = settings.hostname;
  nodeId.toLowerCase();
  nodeId.replace("-", "_");
  nodeId.replace(" ", "_");
  availTopic = topic("status");

  mqtt.setServer(settings.mqttHost.c_str(), settings.mqttPort);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(onMessage);
  fan::onChange(markDirty);
}

void loop() {
  if (!mqtt.connected()) { connect(); return; }
  mqtt.loop();

  uint32_t now = millis();
  if (dirty || now - lastFull > STATE_REPUBLISH_MS) {
    dirty = false;
    lastFull = now;
    publishState();
  }
}

bool connected() { return mqtt.connected(); }
void markDirty() { dirty = true; }

} // namespace mqttha
