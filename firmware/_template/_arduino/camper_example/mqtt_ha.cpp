#include "mqtt_ha.h"
#include "wifi_manager.h"
#include "sensor.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Topic layout (prefix = config.mqtt_prefix, default "camper/example"):
//   <prefix>/status          online / offline (last will, retained)
//   <prefix>/rssi            WiFi signal
//   <prefix>/<key>           one topic per entry of VALUES[] (sensor.cpp), retained
//   <prefix>/cmd/measure     any payload: measure right away (example command)
//
// Home Assistant: one device DEVICE_NAME, one sensor entity per value,
// plus WiFi signal, status and a "measure now" button.
// TODO: own commands -> add a topic in onMessage() and, if wanted, a button/switch in mqttPublishDiscovery().

#define HA_NODE_ID  "camper_example"

static WiFiClient espClient;
static PubSubClient mqtt(espClient);

static unsigned long lastPublish = 0;
static unsigned long lastReconnect = 0;
static bool discoveryPublished = false;
static uint32_t lastPublishedRead = 0;

static String topic(const String& sub) {
    return String(config.mqtt_prefix) + "/" + sub;
}

static void publishStates();

// Commands from MQTT (runs in loop(), so it may call everything)
static void onMessage(char* t, byte* payload, unsigned int len) {
    String tp(t);
    String cmdBase = topic("cmd/");
    if (!tp.startsWith(cmdBase)) return;
    String cmd = tp.substring(cmdBase.length());
    String msg;
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    Serial.printf("[MQTT] Command %s = %s\n", cmd.c_str(), msg.c_str());

    if (cmd == "measure") {
        sensorRequestMeasure();
    }
    // TODO: further commands, e.g. else if (cmd == "relay/set") { ... }
}

void mqttInit() {
    if (strlen(config.mqtt_server) == 0) {
        Serial.println("[MQTT] No server configured");
        return;
    }
    mqtt.setServer(config.mqtt_server, config.mqtt_port);
    mqtt.setBufferSize(1024);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(5);
    mqtt.setCallback(onMessage);
    Serial.printf("[MQTT] Configured: %s:%d\n", config.mqtt_server, config.mqtt_port);
}

void mqttLoop() {
    if (!isConnected()) return;
    if (strlen(config.mqtt_server) == 0) return;

    if (!mqtt.connected()) {
        // connect() blocks while the broker is unreachable, so retry only every 5 s
        if (millis() - lastReconnect > 5000) {
            lastReconnect = millis();
            Serial.println("[MQTT] Connection attempt...");
            String clientId = String(OTA_HOSTNAME) + "-" + WiFi.macAddress();
            clientId.replace(":", "");
            String willTopic = topic("status");
            bool connected;
            if (strlen(config.mqtt_user) > 0) {
                connected = mqtt.connect(clientId.c_str(), config.mqtt_user, config.mqtt_pass,
                                         willTopic.c_str(), 1, true, "offline");
            } else {
                connected = mqtt.connect(clientId.c_str(), NULL, NULL, willTopic.c_str(), 1, true, "offline");
            }
            if (connected) {
                Serial.println("[MQTT] Connected!");
                mqtt.publish(willTopic.c_str(), "online", true);
                mqtt.subscribe(topic("cmd/#").c_str());
                discoveryPublished = false;   // Always re-announce after (re)connect
                lastPublish = 0;              // Publish all states right away
            } else {
                Serial.printf("[MQTT] Failed, rc=%d\n", mqtt.state());
            }
        }
        return;
    }

    mqtt.loop();

    if (!discoveryPublished) {
        mqttPublishDiscovery();
        discoveryPublished = true;
        lastPublish = 0;
    }

    // After every measurement cycle, and at least every MQTT_REPUBLISH_MS
    bool newData = (sensorData.readCount != lastPublishedRead);
    if (newData || lastPublish == 0 || millis() - lastPublish > MQTT_REPUBLISH_MS) {
        lastPublish = millis();
        lastPublishedRead = sensorData.readCount;
        publishStates();
    }
}

bool mqttIsConnected() { return mqtt.connected(); }
void mqttRequestDiscovery() { discoveryPublished = false; }

// ============================================================
// States
// ============================================================

static void publishStates() {
    for (uint8_t i = 0; i < VALUE_COUNT; i++) {
        float v = sensorData.values[i];
        if (isnan(v)) continue;   // HA keeps the last value
        char buf[24];
        dtostrf(v, 1, VALUES[i].decimals, buf);
        mqtt.publish(topic(VALUES[i].key).c_str(), buf, true);
    }
    mqtt.publish(topic("rssi").c_str(), String(sensorData.rssi).c_str(), true);
    mqtt.publish(topic("status").c_str(), "online", true);
}

// ============================================================
// Home Assistant discovery
// ============================================================

static void addDevice(JsonDocument& doc) {
    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = HA_NODE_ID;
    device["name"] = DEVICE_NAME;
    device["model"] = "ESP32";
    device["manufacturer"] = "DIY";
    device["sw_version"] = FW_VERSION;
    device["configuration_url"] = String("http://") + WiFi.localIP().toString() + "/";
}

static void publishJson(const String& configTopic, JsonDocument& doc) {
    static char buffer[768];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) {
        Serial.printf("[MQTT] Discovery payload too large: %s\n", configTopic.c_str());
        return;
    }
    mqtt.publish(configTopic.c_str(), buffer, true);
}

static void publishSensor(const char* name, const char* id, const String& stateTopic, const char* unit,
                          const char* devClass, const char* icon, int precision, bool withAvailability) {
    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id;
    doc["state_topic"] = stateTopic;
    doc["icon"] = icon;
    if (unit && unit[0]) doc["unit_of_measurement"] = unit;
    if (devClass) { doc["device_class"] = devClass; doc["state_class"] = "measurement"; }
    if (precision >= 0) doc["suggested_display_precision"] = precision;
    // The status sensor skips availability on purpose, so HA shows "offline" instead of "unavailable"
    if (withAvailability) doc["availability_topic"] = topic("status");
    addDevice(doc);
    publishJson(String("homeassistant/sensor/" HA_NODE_ID "/") + id + "/config", doc);
}

static void publishButton(const char* name, const char* id, const char* cmd, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id;
    doc["command_topic"] = topic(String("cmd/") + cmd);
    doc["payload_press"] = "1";
    doc["icon"] = icon;
    doc["availability_topic"] = topic("status");
    addDevice(doc);
    publishJson(String("homeassistant/button/" HA_NODE_ID "/") + id + "/config", doc);
}

void mqttPublishDiscovery() {
    Serial.println("[MQTT] Publishing HA Discovery...");
    for (uint8_t i = 0; i < VALUE_COUNT; i++) {
        const ValueDef& v = VALUES[i];
        publishSensor(v.name, v.key, topic(v.key), v.unit, v.devClass, v.icon, v.decimals, true);
    }
    publishSensor("WLAN Signal", "rssi", topic("rssi"), "dBm", "signal_strength", "mdi:wifi", 0, true);
    publishSensor("Status", "status", topic("status"), "", nullptr, "mdi:check-circle", -1, false);
    publishButton("Jetzt messen", "measure", "measure", "mdi:refresh");
    // TODO: own entities (switch, number, select ...) for your commands
    Serial.println("[MQTT] Discovery complete");
}
