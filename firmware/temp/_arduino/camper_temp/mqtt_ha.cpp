#include "mqtt_ha.h"
#include "wifi_manager.h"
#include "temp_sensor.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Topic layout (prefix = config.mqtt_prefix, default "camper/temp"):
//   <prefix>/status               online / offline (last will)
//   <prefix>/rssi                 WiFi signal
//   <prefix>/<id>                 temperature in °C of the sensor with this id (web UI)
//   <prefix>/<id>/available       online / offline (sensor answers on the bus)
//   <prefix>/<id>/alarm           ON / OFF (limit violated for the set delay, see web UI)
//   <prefix>/alarm                ON while at least one sensor is in alarm
//   <prefix>/alarm_text           readable text of all active alarms ("" if none), for displays
//   <prefix>/sensors              all sensors as one JSON list for displays:
//                                 [{"id":"fridge","name":"Fridge","t":4.31,"alarm":0}, ...]
//                                 t = null without a valid reading, alarm 0 = none, 1 = low, 2 = high
//
// Home Assistant: one device "Camper Temp" with one entity per sensor,
// plus one alarm binary_sensor per sensor with limits and an overall alarm.
// The unique_id uses the 1-Wire address, so renaming a sensor keeps its HA history.

#define HA_NODE_ID  "camper_temp"

static WiFiClient espClient;
static PubSubClient mqtt(espClient);

static unsigned long lastPublish = 0;
static unsigned long lastReconnect = 0;
static bool discoveryPublished = false;
static uint32_t lastPublishedRead = 0;
static uint32_t lastPublishedAlarm = 0;

// Pending removals (deleted sensors or changed ids), filled by the web task, handled in loop()
struct Removal { char addr[17]; char key[24]; bool entity; };
static Removal removals[MAX_SENSORS * 2];
static uint8_t removalCount = 0;   // Only touched from loop() and the web callback (not concurrently on ESP8266)

static void publishStates();
static void publishTempSensor(int i);
static void publishSensor(const char* name, const char* id, const char* topic, const char* unit,
                          const char* devClass, const char* icon, bool measurement, bool withAvailability);
static void addDeviceBlock(JsonDocument& doc);
static void publishJson(const String& topic, JsonDocument& doc, const char* id);
static void processRemovals();
static void publishAlarms();
static void publishSensorList();
static void publishAlarmSensor(int i);
static void publishBinary(const char* name, const char* id, const char* stateTopic, const char* icon);

static bool hasLimits(const TempSensorConfig& s) {
    return !isnan(s.alarm_lo) || !isnan(s.alarm_hi);
}

static String topic(const char* sub) {
    return String(config.mqtt_prefix) + "/" + sub;
}

void mqttInit() {
    if (strlen(config.mqtt_server) == 0) {
        Serial.println("[MQTT] No server configured");
        return;
    }

    mqtt.setServer(config.mqtt_server, config.mqtt_port);
    mqtt.setBufferSize(1280);   // sensor list JSON + topic
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

            String clientId = "camper-temp-" + WiFi.macAddress();
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
                discoveryPublished = false;   // Always re-announce after (re)connect
                mqtt.publish(willTopic.c_str(), "online", true);
                lastPublish = 0;              // Publish all states right away
            } else {
                Serial.printf("[MQTT] Failed, rc=%d\n", mqtt.state());
            }
        }
        return;
    }

    mqtt.loop();

    if (removalCount > 0) processRemovals();

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

    // Alarm changes go out right away, not only with the next reading
    if (sensorData.alarmSeq != lastPublishedAlarm) publishAlarms();
}

bool mqttIsConnected() {
    return mqtt.connected();
}

void mqttRequestDiscovery() {
    discoveryPublished = false;
}

void mqttQueueRemoval(const char* addrHex, const char* oldKey, bool removeEntity) {
    if (removalCount >= sizeof(removals) / sizeof(removals[0])) return;
    Removal& r = removals[removalCount];
    strlcpy(r.addr, addrHex, sizeof(r.addr));
    strlcpy(r.key, oldKey, sizeof(r.key));
    r.entity = removeEntity;
    removalCount++;
}

static void processRemovals() {
    for (uint8_t i = 0; i < removalCount; i++) {
        const Removal& r = removals[i];
        // Empty retained messages delete the old values from the broker
        if (r.key[0]) {
            mqtt.publish(topic(r.key).c_str(), "", true);
            mqtt.publish((topic(r.key) + "/available").c_str(), "", true);
            mqtt.publish((topic(r.key) + "/alarm").c_str(), "", true);
        }
        // Empty retained discovery removes the entity from Home Assistant
        if (r.entity && r.addr[0]) {
            String configTopic = String("homeassistant/sensor/" HA_NODE_ID "/") + r.addr + "/config";
            mqtt.publish(configTopic.c_str(), "", true);
            configTopic = String("homeassistant/binary_sensor/" HA_NODE_ID "/") + r.addr + "_alarm/config";
            mqtt.publish(configTopic.c_str(), "", true);
        }
    }
    removalCount = 0;
}

static void publishStates() {
    char buf[16];

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!config.sensors[i].used) continue;
        const TempData& t = sensorData.temps[i];
        String base = topic(config.sensors[i].key);

        // Value only when valid: HA keeps the last value and marks the entity unavailable instead
        if (t.ok) {
            dtostrf(t.temp, 1, 2, buf);
            mqtt.publish(base.c_str(), buf, true);
        }
        mqtt.publish((base + "/available").c_str(), t.ok ? "online" : "offline", true);
    }

    itoa(sensorData.rssi, buf, 10);
    mqtt.publish(topic("rssi").c_str(), buf, true);
    mqtt.publish(topic("status").c_str(), "online", true);
    publishAlarms();
}

// Builds the readable alarm text, e.g. "Fridge too warm: 9,2 °C (limit 8,0 °C)"
String alarmText() {
    String txt;
    char v[12], l[12];
    for (int i = 0; i < MAX_SENSORS; i++) {
        const TempSensorConfig& s = config.sensors[i];
        const TempData& t = sensorData.temps[i];
        if (!s.used || t.alarm == ALARM_NONE) continue;
        bool high = (t.alarm == ALARM_HIGH);
        dtostrf(t.temp, 1, 1, v);
        dtostrf(high ? s.alarm_hi : s.alarm_lo, 1, 1, l);
        for (char* c = v; *c; c++) if (*c == '.') *c = ',';
        for (char* c = l; *c; c++) if (*c == '.') *c = ',';
        if (txt.length()) txt += "; ";
        txt += s.name;
        txt += high ? " zu warm: " : " zu kalt: ";
        txt += v;
        txt += " \xC2\xB0" "C (Grenze ";
        txt += l;
        txt += " \xC2\xB0" "C)";
    }
    return txt;
}

static void publishAlarms() {
    lastPublishedAlarm = sensorData.alarmSeq;
    bool any = false;
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!config.sensors[i].used) continue;
        bool on = sensorData.temps[i].alarm != ALARM_NONE;
        any |= on;
        mqtt.publish((topic(config.sensors[i].key) + "/alarm").c_str(), on ? "ON" : "OFF", true);
    }
    mqtt.publish(topic("alarm").c_str(), any ? "ON" : "OFF", true);
    mqtt.publish(topic("alarm_text").c_str(), alarmText().c_str(), true);
    publishSensorList();
}

static void publishSensorList() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_SENSORS; i++) {
        const TempSensorConfig& s = config.sensors[i];
        if (!s.used) continue;
        const TempData& t = sensorData.temps[i];
        JsonObject o = arr.add<JsonObject>();
        o["id"] = (const char*)s.key;
        o["name"] = (const char*)s.name;
        if (t.ok) o["t"] = roundf(t.temp * 100.0f) / 100.0f; else o["t"] = nullptr;
        o["alarm"] = t.alarm;
    }
    static char buffer[1024];   // 8 sensors with long names fit; static keeps it off the stack
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) return;
    mqtt.publish(topic("sensors").c_str(), buffer, true);
}

void mqttPublishDiscovery() {
    Serial.println("[MQTT] Publishing HA Discovery...");

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!config.sensors[i].used) continue;
        publishTempSensor(i);
        publishAlarmSensor(i);
    }

    publishBinary("Temperatur-Alarm", "alarm", "alarm", "mdi:thermometer-alert");
    publishSensor("Alarmtext", "alarm_text", "alarm_text", "", "None", "mdi:alert-circle-outline", false, true);

    // Device
    publishSensor("WLAN Signal", "rssi",   "rssi",   "dBm", "signal_strength", "mdi:wifi",         true,  true);
    publishSensor("Status",      "status", "status", "",    "None",            "mdi:check-circle", false, false);

    Serial.println("[MQTT] Discovery complete");
}

static void addDeviceBlock(JsonDocument& doc) {
    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = HA_NODE_ID;
    device["name"] = "Camper Temp";
#if defined(ESP8266)
    device["model"] = "ESP8266 DS18B20";
#else
    device["model"] = "ESP32 DS18B20";
#endif
    device["manufacturer"] = "DIY";
    device["sw_version"] = FW_VERSION;
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

// One temperature entity. Available only while the device is online AND the sensor answers.
static void publishTempSensor(int i) {
    const TempSensorConfig& s = config.sensors[i];
    char addr[17];
    addrToHex(s.addr, addr);

    JsonDocument doc;
    doc["name"] = s.name;
    doc["unique_id"] = String(HA_NODE_ID "_") + addr;
    doc["state_topic"] = topic(s.key);
    doc["unit_of_measurement"] = "°C";
    doc["device_class"] = "temperature";
    doc["state_class"] = "measurement";
    doc["suggested_display_precision"] = 1;
    doc["icon"] = "mdi:thermometer";

    JsonArray av = doc["availability"].to<JsonArray>();
    av.add<JsonObject>()["topic"] = topic("status");
    av.add<JsonObject>()["topic"] = topic(s.key) + "/available";
    doc["availability_mode"] = "all";

    addDeviceBlock(doc);
    publishJson(String("homeassistant/sensor/" HA_NODE_ID "/") + addr + "/config", doc, s.key);
}

static void publishSensor(const char* name, const char* id, const char* stateTopic, const char* unit,
                          const char* devClass, const char* icon, bool measurement, bool withAvailability) {
    JsonDocument doc;

    doc["name"] = name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id;
    doc["state_topic"] = topic(stateTopic);
    doc["icon"] = icon;

    if (strlen(unit) > 0) doc["unit_of_measurement"] = unit;
    if (strcmp(devClass, "None") != 0) doc["device_class"] = devClass;
    if (measurement) doc["state_class"] = "measurement";

    // The status sensor skips availability on purpose, so HA shows "offline" instead of "unavailable"
    if (withAvailability) {
        doc["availability_topic"] = topic("status");
        doc["payload_available"] = "online";
        doc["payload_not_available"] = "offline";
    }

    addDeviceBlock(doc);
    publishJson(String("homeassistant/sensor/" HA_NODE_ID "/") + id + "/config", doc, id);
}

// Alarm entity of one sensor; removed again when the sensor has no limits
static void publishAlarmSensor(int i) {
    const TempSensorConfig& s = config.sensors[i];
    char addr[17];
    addrToHex(s.addr, addr);
    String configTopic = String("homeassistant/binary_sensor/" HA_NODE_ID "/") + addr + "_alarm/config";

    if (!hasLimits(s)) {
        mqtt.publish(configTopic.c_str(), "", true);
        return;
    }

    JsonDocument doc;
    doc["name"] = String(s.name) + " Alarm";
    doc["unique_id"] = String(HA_NODE_ID "_") + addr + "_alarm";
    doc["state_topic"] = topic(s.key) + "/alarm";
    doc["device_class"] = "problem";
    doc["icon"] = "mdi:thermometer-alert";
    doc["availability_topic"] = topic("status");
    addDeviceBlock(doc);
    publishJson(configTopic, doc, s.key);
}

static void publishBinary(const char* name, const char* id, const char* stateTopic, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id;
    doc["state_topic"] = topic(stateTopic);
    doc["device_class"] = "problem";
    doc["icon"] = icon;
    doc["availability_topic"] = topic("status");
    addDeviceBlock(doc);
    publishJson(String("homeassistant/binary_sensor/" HA_NODE_ID "/") + id + "/config", doc, id);
}
