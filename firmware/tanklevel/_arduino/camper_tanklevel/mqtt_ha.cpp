#include "mqtt_ha.h"
#include "wifi_manager.h"
#include "webserver.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Topic layout (prefix = config.mqtt_prefix, default "camper/tanklevel"):
//   <prefix>/status                 online / offline (last will)
//   <prefix>/rssi                   WiFi signal
//   <prefix>/water/<value>          fresh water values
//   <prefix>/grey/<value>           grey water values (optional)
//   <prefix>/gas/<value>            gas values (Mopeka additionally: distance mm, temperature, quality)
//   <prefix>/cmd/water/filter/set   ON / OFF / TOGGLE (fresh water filter)
//   <prefix>/cmd/grey/filter/set    ON / OFF / TOGGLE (grey water filter)
//
// Home Assistant: one device "Camper Tanklevel" with all entities.

#define HA_NODE_ID  "camper_tanklevel"

static WiFiClient espClient;
static PubSubClient mqtt(espClient);

static unsigned long lastWaterPublish = 0;
static unsigned long lastGasPublish = 0;
static unsigned long lastReconnect = 0;
static bool discoveryPublished = false;
static uint32_t lastPublishedGasUpdate = 0;

static void mqttCallback(char* topic, byte* payload, unsigned int length);
static void publishTankState(const char* sub, const WaterData& w, const TankConfig& t);
static void publishGasState();
static void publishSensor(const char* name, const char* id, const char* topic, const char* unit,
                          const char* devClass, const char* icon, bool measurement, bool keepLastValue);
static void publishSwitch(const char* name, const char* id, const char* topic,
                          const char* icon, const char* cmdTopic);
static void addDeviceBlock(JsonDocument& doc, bool withAvailability);
static void publishJson(const String& topic, JsonDocument& doc, const char* id);
static void removeEntity(const char* component, const char* id);

static String topic(const char* sub) {
    return String(config.mqtt_prefix) + "/" + sub;
}

void mqttInit() {
    if (strlen(config.mqtt_server) == 0) {
        Serial.println("[MQTT] No server configured");
        return;
    }

    mqtt.setServer(config.mqtt_server, config.mqtt_port);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(1024);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(5);

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

            String clientId = "camper-tanklevel-" + WiFi.macAddress();
            clientId.replace(":", "");

            String willTopic = topic("status");
            bool connected;

            if (strlen(config.mqtt_user) > 0) {
                connected = mqtt.connect(clientId.c_str(), config.mqtt_user, config.mqtt_pass,
                                         willTopic.c_str(), 1, true, "offline");
            } else {
                connected = mqtt.connect(clientId.c_str(),
                                         NULL, NULL, willTopic.c_str(), 1, true, "offline");
            }

            if (connected) {
                Serial.println("[MQTT] Connected!");
                mqtt.subscribe(topic("cmd/#").c_str());

                discoveryPublished = false;   // Always re-announce after (re)connect
                mqtt.publish(willTopic.c_str(), "online", true);
                lastWaterPublish = 0;         // Publish all states right away
                lastGasPublish = 0;
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
    }

    // Fresh water: periodically (also carries WiFi RSSI and status)
    if (lastWaterPublish == 0 || millis() - lastWaterPublish > MQTT_WATER_MS) {
        lastWaterPublish = millis();
        if (config.water_enabled) publishTankState("water", sensorData.water, config.fresh);
        if (config.grey_enabled)  publishTankState("grey", sensorData.grey, config.grey);

        char buf[8];
        itoa(sensorData.rssi, buf, 10);
        mqtt.publish(topic("rssi").c_str(), buf, true);
        mqtt.publish(topic("status").c_str(), "online", true);
    }

    // Gas: immediately when a new BLE reading arrived, otherwise periodically
    bool newGas = (sensorData.gas.updates != lastPublishedGasUpdate);
    if (config.gas_enabled && (newGas || lastGasPublish == 0 || millis() - lastGasPublish > MQTT_GAS_MS)) {
        lastGasPublish = millis();
        lastPublishedGasUpdate = sensorData.gas.updates;
        publishGasState();
    }
}

bool mqttIsConnected() {
    return mqtt.connected();
}

void mqttRequestDiscovery() {
    discoveryPublished = false;
}

static void mqttCallback(char* topicName, byte* payload, unsigned int length) {
    String topicStr = String(topicName);
    String payloadStr;
    for (unsigned int i = 0; i < length; i++) payloadStr += (char)payload[i];
    payloadStr.trim();
    payloadStr.toLowerCase();

    // Smoothing filter of the fresh or grey water tank
    struct { bool enabled; const char* cmd; const char* state; TankConfig* t; } tanks[] = {
        {config.water_enabled, "cmd/water/filter/set", "water/filter_active", &config.fresh},
        {config.grey_enabled,  "cmd/grey/filter/set",  "grey/filter_active",  &config.grey},
    };
    for (auto& tk : tanks) {
        if (!tk.enabled || topicStr != topic(tk.cmd)) continue;
        bool newState;
        if (payloadStr == "toggle") newState = !tk.t->filter_active;
        else newState = (payloadStr == "on" || payloadStr == "true" || payloadStr == "1");

        // Only write to flash if the value really changed (reduces flash wear)
        if (newState != tk.t->filter_active) {
            tk.t->filter_active = newState;
            saveConfig();
        }
        mqtt.publish(topic(tk.state).c_str(), tk.t->filter_active ? "ON" : "OFF", true);
    }
}

static void publishTankState(const char* sub, const WaterData& w, const TankConfig& t) {
    String base = String(sub) + "/";
    char buf[16];

    dtostrf(w.avg_voltage, 1, 3, buf);
    mqtt.publish(topic((base + "voltage").c_str()).c_str(), buf, true);

    dtostrf(w.liters, 1, 1, buf);
    mqtt.publish(topic((base + "liters").c_str()).c_str(), buf, true);

    dtostrf(w.percent, 1, 0, buf);
    mqtt.publish(topic((base + "percent").c_str()).c_str(), buf, true);

    mqtt.publish(topic((base + "filter_active").c_str()).c_str(), t.filter_active ? "ON" : "OFF", true);
}

static void publishGasState() {
    const GasData& g = sensorData.gas;
    char buf[16];

    // Level values only once a real reading exists, so HA keeps the last known value after a reboot
    if (g.has_data) {
        dtostrf(g.level, 1, 1, buf);
        mqtt.publish(topic("gas/level").c_str(), buf, true);

        dtostrf(g.raw_level, 1, 1, buf);
        mqtt.publish(topic("gas/raw_level").c_str(), buf, true);

        dtostrf(g.content, 1, 1, buf);
        mqtt.publish(topic("gas/content").c_str(), buf, true);

        itoa(g.ble_rssi, buf, 10);
        mqtt.publish(topic("gas/ble_rssi").c_str(), buf, true);
    }
    if (!isnan(g.battery)) {
        dtostrf(g.battery, 1, 0, buf);
        mqtt.publish(topic("gas/battery").c_str(), buf, true);
    }
    if (config.gas_type == GAS_TYPE_MOPEKA) {
        if (!isnan(g.distance_mm)) {
            itoa((int)roundf(g.distance_mm), buf, 10);
            mqtt.publish(topic("gas/distance").c_str(), buf, true);
        }
        if (!isnan(g.temperature)) {
            itoa((int)roundf(g.temperature), buf, 10);
            mqtt.publish(topic("gas/temperature").c_str(), buf, true);
        }
        if (g.quality >= 0) {
            itoa(g.quality, buf, 10);
            mqtt.publish(topic("gas/quality").c_str(), buf, true);
        }
    }
    mqtt.publish(topic("gas/ble_state").c_str(), g.ble_state, true);
}

void mqttPublishDiscovery() {
    Serial.println("[MQTT] Publishing HA Discovery...");

    // Fresh water. No device_class for percent values: "battery" would show a battery icon in HA
    if (config.water_enabled) {
        publishSensor("Frischwasser Inhalt",  "water_liters",  "water/liters",  "L", "volume_storage", "mdi:water",         true, true);
        publishSensor("Frischwasser Prozent", "water_percent", "water/percent", "%", "None",           "mdi:water-percent", true, true);
        publishSensor("Frischwasser Spannung","water_voltage", "water/voltage", "V", "voltage",        "mdi:current-dc",    true, true);
        publishSwitch("Frischwasser Glättungsfilter", "water_filter", "water/filter_active", "mdi:blur",
                      topic("cmd/water/filter/set").c_str());
    } else {
        // Disabled: remove the entities from Home Assistant
        removeEntity("sensor", "water_liters");
        removeEntity("sensor", "water_percent");
        removeEntity("sensor", "water_voltage");
        removeEntity("switch", "water_filter");
    }

    // Grey water (optional)
    if (config.grey_enabled) {
        publishSensor("Grauwasser Inhalt",   "grey_liters",  "grey/liters",  "L", "volume_storage", "mdi:water-opacity",  true, true);
        publishSensor("Grauwasser Prozent",  "grey_percent", "grey/percent", "%", "None",           "mdi:water-percent",  true, true);
        publishSensor("Grauwasser Spannung", "grey_voltage", "grey/voltage", "V", "voltage",        "mdi:current-dc",     true, true);
        publishSwitch("Grauwasser Glättungsfilter", "grey_filter", "grey/filter_active", "mdi:blur",
                      topic("cmd/grey/filter/set").c_str());
    } else {
        removeEntity("sensor", "grey_liters");
        removeEntity("sensor", "grey_percent");
        removeEntity("sensor", "grey_voltage");
        removeEntity("switch", "grey_filter");
    }

    // Gas
    if (config.gas_enabled) {
        publishSensor("Gas Füllstand",         "gas_level",     "gas/level",     "%",   "None",            "mdi:gauge",             true,  true);
        publishSensor("Gas Füllstand Rohwert", "gas_raw_level", "gas/raw_level", "%",   "None",            "mdi:gauge-low",         true,  true);
        publishSensor("Gas Inhalt",            "gas_content",   "gas/content",   "L",   "volume_storage",  "mdi:gas-cylinder",      true,  true);
        bool mopeka = (config.gas_type == GAS_TYPE_MOPEKA);
        if (mopeka) {
            publishSensor("Gas Füllhöhe",          "gas_distance",    "gas/distance",    "mm", "distance",    "mdi:arrow-expand-vertical", true, true);
            publishSensor("Gassensor Temperatur",  "gas_temperature", "gas/temperature", "\u00b0C", "temperature", "mdi:thermometer",      true, true);
            publishSensor("Gassensor Messqualität","gas_quality",     "gas/quality",     "",   "None",        "mdi:signal",                true, true);
        } else {
            const char* ids[] = {"gas_distance", "gas_temperature", "gas_quality"};
            for (const char* id : ids) removeEntity("sensor", id);
        }
        if (BATTERY_ENABLED || mopeka) {
            publishSensor("Gassensor Batterie", "gas_battery",  "gas/battery",   "%",   "battery",         "mdi:battery",           true,  true);
        } else {
            removeEntity("sensor", "gas_battery");
        }
        publishSensor("Gassensor Signal",      "gas_ble_rssi",  "gas/ble_rssi",  "dBm", "signal_strength", "mdi:bluetooth",         true,  false);
        publishSensor("Gassensor Status",      "gas_ble_state", "gas/ble_state", "",    "None",            "mdi:bluetooth-connect", false, false);
    } else {
        const char* gasIds[] = {"gas_level", "gas_raw_level", "gas_content", "gas_battery", "gas_ble_rssi", "gas_ble_state",
                                "gas_distance", "gas_temperature", "gas_quality"};
        for (const char* id : gasIds) removeEntity("sensor", id);
    }

    // Device
    publishSensor("WLAN Signal", "rssi",   "rssi",   "dBm", "signal_strength", "mdi:wifi",         true,  false);
    publishSensor("Status",      "status", "status", "",    "None",            "mdi:check-circle", false, true);

    Serial.println("[MQTT] Discovery complete");
}

static void addDeviceBlock(JsonDocument& doc, bool withAvailability) {
    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = HA_NODE_ID;
    device["name"] = "Camper Tanklevel";
    device["model"] = "ESP32 Tank Level (" SENSOR_MODEL ")";
    device["manufacturer"] = "DIY";
    device["sw_version"] = FW_VERSION;

    // Level sensors and the status sensor skip availability on purpose, so Home Assistant
    // keeps showing the last known value (and "offline") while the device is switched off
    if (withAvailability) {
        doc["availability_topic"] = topic("status");
        doc["payload_available"] = "online";
        doc["payload_not_available"] = "offline";
    }
}

static void publishJson(const String& configTopic, JsonDocument& doc, const char* id) {
    char buffer[768];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) {
        Serial.printf("[MQTT] Discovery payload for '%s' too large\n", id);
        return;
    }
    mqtt.publish(configTopic.c_str(), buffer, true);
}

static void publishSensor(const char* name, const char* id, const char* stateTopic, const char* unit,
                          const char* devClass, const char* icon, bool measurement, bool keepLastValue) {
    JsonDocument doc;

    doc["name"] = name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id;
    doc["state_topic"] = topic(stateTopic);
    doc["icon"] = icon;

    if (strlen(unit) > 0) doc["unit_of_measurement"] = unit;
    if (strcmp(devClass, "None") != 0) doc["device_class"] = devClass;
    if (measurement) doc["state_class"] = "measurement";

    addDeviceBlock(doc, !keepLastValue);
    publishJson(String("homeassistant/sensor/" HA_NODE_ID "/") + id + "/config", doc, id);
}

static void publishSwitch(const char* name, const char* id, const char* stateTopic,
                          const char* icon, const char* cmdTopic) {
    JsonDocument doc;

    doc["name"] = name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id + "_switch";
    doc["state_topic"] = topic(stateTopic);
    doc["command_topic"] = cmdTopic;
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["icon"] = icon;

    addDeviceBlock(doc, true);
    publishJson(String("homeassistant/switch/" HA_NODE_ID "/") + id + "/config", doc, id);
}

// An empty retained discovery message removes the entity from Home Assistant
static void removeEntity(const char* component, const char* id) {
    String configTopic = String("homeassistant/") + component + "/" HA_NODE_ID "/" + id + "/config";
    mqtt.publish(configTopic.c_str(), "", true);
}
