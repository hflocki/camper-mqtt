#include "mqtt_ha.h"
#include "sensor.h"
#include "wifi_manager.h"
#include "webserver.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Topic layout (prefix = config.mqtt_prefix, default "camper/level"):
//   <prefix>/status                      online / offline (last will, retained)
//   <prefix>/pitch, /roll                degrees, 0.1 resolution
//   <prefix>/wedge_fl, _fr, _rl, _rr     wedge height in cm under each wheel
//   <prefix>/is_level                    ON / OFF (pitch and roll within the tolerance)
//   <prefix>/rssi                        WiFi signal
//   settings (on change and every minute):
//   <prefix>/tolerance, /track_width, /wheelbase, /mount, /tare_active, /tare_pitch, /tare_roll,
//   <prefix>/filter_active, /mqtt_fast
// Commands (not retained):
//   <prefix>/cmd/tare/set PRESS, cmd/tare_reset/set PRESS, cmd/sensor_restart/set PRESS
//   <prefix>/cmd/<setting>/set <value> for tolerance, track_width, wheelbase, mount,
//   tare_pitch, tare_roll, filter (ON/OFF), mqtt_speed (ON/OFF)

#define HA_NODE_ID   "camper_level"
#define OLD_NODE_ID  "wowa_level"   // node id of the original firmware, cleaned up once

static WiFiClient espClient;
static PubSubClient mqtt(espClient);

static unsigned long lastPublish = 0;
static unsigned long lastSettings = 0;
static unsigned long lastReconnect = 0;
static bool discoveryPending = true;
static volatile bool settingsPending = true;

static String topic(const char* sub) { return String(config.mqtt_prefix) + "/" + sub; }

static void callback(char* t, byte* payload, unsigned int len);
static void publishDiscovery();
static void removeOldEntities();
static void publishLive();
static void publishSettings();

void mqttInit() {
    if (strlen(config.mqtt_server) == 0) {
        Serial.println("[MQTT] No server configured");
        return;
    }
    mqtt.setServer(config.mqtt_server, config.mqtt_port);
    mqtt.setCallback(callback);
    mqtt.setBufferSize(1024);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(5);
    Serial.printf("[MQTT] Configured: %s:%d\n", config.mqtt_server, config.mqtt_port);
}

bool mqttIsConnected() { return mqtt.connected(); }

void mqttRequestSettings() { settingsPending = true; }

void mqttLoop() {
    if (!isConnected() || strlen(config.mqtt_server) == 0) return;

    if (!mqtt.connected()) {
        // connect() blocks while the broker is unreachable, so retry only every 5 s
        if (millis() - lastReconnect < 5000) return;
        lastReconnect = millis();

        String clientId = "camper-level-" + WiFi.macAddress();
        clientId.replace(":", "");
        String will = topic("status");
        const char* user = strlen(config.mqtt_user) ? config.mqtt_user : nullptr;
        const char* pass = strlen(config.mqtt_user) ? config.mqtt_pass : nullptr;

        if (!mqtt.connect(clientId.c_str(), user, pass, will.c_str(), 1, true, "offline")) {
            Serial.printf("[MQTT] Failed, rc=%d\n", mqtt.state());
            return;
        }
        Serial.println("[MQTT] Connected");
        mqtt.publish(will.c_str(), "online", true);
        mqtt.subscribe(topic("cmd/#").c_str());
        mqtt.subscribe("homeassistant/status");   // re-announce after an HA restart
        discoveryPending = true;
        settingsPending = true;
        lastPublish = 0;
    }

    mqtt.loop();

    if (discoveryPending) {
        discoveryPending = false;
        removeOldEntities();
        publishDiscovery();
        settingsPending = true;
    }
    if (settingsPending || millis() - lastSettings > MQTT_SETTINGS_MS) {
        settingsPending = false;
        lastSettings = millis();
        publishSettings();
    }
    unsigned long interval = config.mqtt_fast ? MQTT_FAST_MS : MQTT_SLOW_MS;
    if (lastPublish == 0 || millis() - lastPublish >= interval) {
        lastPublish = millis();
        publishLive();
    }
}

// ============================================================
// Commands
// ============================================================
static bool parseNumber(const String& s, float minV, float maxV, float& out) {
    char* end;
    float v = strtof(s.c_str(), &end);
    if (end == s.c_str() || v < minV || v > maxV) return false;
    out = v;
    return true;
}

static bool parseOnOff(const String& s, bool current, bool& out) {
    if (s.equalsIgnoreCase("ON") || s == "1" || s.equalsIgnoreCase("true")) { out = true; return true; }
    if (s.equalsIgnoreCase("OFF") || s == "0" || s.equalsIgnoreCase("false")) { out = false; return true; }
    if (s.equalsIgnoreCase("TOGGLE")) { out = !current; return true; }
    return false;
}

static void callback(char* t, byte* payload, unsigned int len) {
    String tp(t);
    String msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    msg.trim();

    if (tp == "homeassistant/status") {
        if (msg == "online") discoveryPending = true;
        return;
    }

    String pre = topic("cmd/");
    if (!tp.startsWith(pre)) return;
    String cmd = tp.substring(pre.length());
    if (cmd.endsWith("/set")) cmd.remove(cmd.length() - 4);
    Serial.printf("[MQTT] Command %s = %s\n", cmd.c_str(), msg.c_str());

    float f;
    bool b;
    bool changed = false;

    if (cmd == "tare") {
        sensorSetTare();
    } else if (cmd == "tare_reset") {
        sensorResetTare();
    } else if (cmd == "sensor_restart" || cmd == "calibration_reset") {
        sensorRestart();
        return;
    } else if (cmd == "tare_pitch" && parseNumber(msg, -MAX_TARE_DEG, MAX_TARE_DEG, f)) {
        sensorSetTareValues(f, config.tare_roll);
    } else if (cmd == "tare_roll" && parseNumber(msg, -MAX_TARE_DEG, MAX_TARE_DEG, f)) {
        sensorSetTareValues(config.tare_pitch, f);
    } else if (cmd == "tolerance" && parseNumber(msg, 0.1f, 5.0f, f)) {
        config.tolerance = f; changed = true;
    } else if (cmd == "track_width" && parseNumber(msg, 100.0f, 300.0f, f)) {
        config.track_width = f; changed = true;
    } else if (cmd == "wheelbase" && parseNumber(msg, 150.0f, 800.0f, f)) {
        config.wheelbase = f; changed = true;
    } else if (cmd == "mount") {
        MountOrientation m;
        if (!mountFromName(msg, m)) return;
        config.mount = m; changed = true;
    } else if (cmd == "filter" && parseOnOff(msg, config.filter_active, b)) {
        config.filter_active = b; changed = true;
    } else if (cmd == "mqtt_speed" && parseOnOff(msg, config.mqtt_fast, b)) {
        config.mqtt_fast = b; changed = true;
    } else {
        Serial.println("[MQTT] Unknown command or invalid value");
        return;
    }

    if (changed) {
        sensorRecalc();
        saveConfig();
    }
    settingsPending = true;
    lastPublish = 0;   // show the effect right away
}

// ============================================================
// State
// ============================================================
static void pubFloat(const char* sub, float v, int decimals) {
    char buf[16];
    dtostrf(v, 1, decimals, buf);
    mqtt.publish(topic(sub).c_str(), buf, true);
}

static void publishLive() {
    mqtt.publish(topic("status").c_str(), "online", true);
    if (sensorData.sensor_ok && sensorData.updates > 0) {
        pubFloat("pitch", sensorData.pitch, 1);
        pubFloat("roll", sensorData.roll, 1);
        pubFloat("wedge_fl", sensorData.wedge_fl, 1);
        pubFloat("wedge_fr", sensorData.wedge_fr, 1);
        pubFloat("wedge_rl", sensorData.wedge_rl, 1);
        pubFloat("wedge_rr", sensorData.wedge_rr, 1);
        mqtt.publish(topic("is_level").c_str(), sensorData.is_level ? "ON" : "OFF", true);
    }
    mqtt.publish(topic("sensor").c_str(), sensorData.sensor_ok ? "OK" : "ERROR", true);
    char buf[8];
    itoa(sensorData.rssi, buf, 10);
    mqtt.publish(topic("rssi").c_str(), buf, true);
}

static void publishSettings() {
    pubFloat("tolerance", config.tolerance, 1);
    pubFloat("track_width", config.track_width, 0);
    pubFloat("wheelbase", config.wheelbase, 0);
    mqtt.publish(topic("mount").c_str(), mountName(config.mount), true);
    mqtt.publish(topic("tare_active").c_str(), config.tare_active ? "ON" : "OFF", true);
    pubFloat("tare_pitch", config.tare_pitch, 2);
    pubFloat("tare_roll", config.tare_roll, 2);
    mqtt.publish(topic("filter_active").c_str(), config.filter_active ? "ON" : "OFF", true);
    mqtt.publish(topic("mqtt_fast").c_str(), config.mqtt_fast ? "ON" : "OFF", true);
}

// ============================================================
// Home Assistant discovery
// ============================================================
static void base(JsonDocument& doc, const char* name, const char* id, const char* icon) {
    doc["name"] = name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id;
    doc["icon"] = icon;
    doc["availability_topic"] = topic("status");
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = HA_NODE_ID;
    dev["name"] = "Camper Level";
    dev["model"] = "ESP32 + BNO085";
    dev["manufacturer"] = "DIY";
    dev["sw_version"] = FW_VERSION;
}

static void send(const char* component, const char* id, JsonDocument& doc) {
    char buf[768];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) { Serial.printf("[MQTT] Discovery '%s' too large\n", id); return; }
    String t = String("homeassistant/") + component + "/" HA_NODE_ID "/" + id + "/config";
    mqtt.publish(t.c_str(), buf, true);
    mqtt.loop();
}

static void sensor(const char* name, const char* id, const char* unit, const char* icon,
                   bool measurement = true, const char* category = nullptr) {
    JsonDocument doc;
    base(doc, name, id, icon);
    doc["state_topic"] = topic(id);
    if (unit[0]) doc["unit_of_measurement"] = unit;
    if (measurement) doc["state_class"] = "measurement";
    if (category) doc["entity_category"] = category;
    send("sensor", id, doc);
}

static void number(const char* name, const char* id, const char* cmd, float minV, float maxV,
                   float step, const char* unit, const char* icon) {
    JsonDocument doc;
    base(doc, name, id, icon);
    doc["state_topic"] = topic(id);
    doc["command_topic"] = topic("cmd/") + cmd + "/set";
    doc["min"] = minV;
    doc["max"] = maxV;
    doc["step"] = step;
    doc["mode"] = "box";
    if (unit[0]) doc["unit_of_measurement"] = unit;
    doc["entity_category"] = "config";
    send("number", id, doc);
}

static void button(const char* name, const char* id, const char* icon, const char* category = nullptr) {
    JsonDocument doc;
    base(doc, name, id, icon);
    doc["command_topic"] = topic("cmd/") + id + "/set";
    doc["payload_press"] = "PRESS";
    if (category) doc["entity_category"] = category;
    send("button", id, doc);
}

static void sw(const char* name, const char* id, const char* cmd, const char* icon) {
    JsonDocument doc;
    base(doc, name, id, icon);
    doc["state_topic"] = topic(id);
    doc["command_topic"] = topic("cmd/") + cmd + "/set";
    doc["entity_category"] = "config";
    send("switch", id, doc);
}

static void publishDiscovery() {
    Serial.println("[MQTT] Publishing HA discovery");
    sensor("Längsneigung", "pitch", "°", "mdi:angle-acute");
    sensor("Querneigung", "roll", "°", "mdi:angle-acute");
    sensor("Keil vorne links", "wedge_fl", "cm", "mdi:wedge");
    sensor("Keil vorne rechts", "wedge_fr", "cm", "mdi:wedge");
    sensor("Keil hinten links", "wedge_rl", "cm", "mdi:wedge");
    sensor("Keil hinten rechts", "wedge_rr", "cm", "mdi:wedge");
    sensor("WLAN Signal", "rssi", "dBm", "mdi:wifi", true, "diagnostic");
    sensor("Sensor", "sensor", "", "mdi:chip", false, "diagnostic");

    {
        JsonDocument doc;
        base(doc, "Waagerecht", "is_level", "mdi:spirit-level");
        doc["state_topic"] = topic("is_level");
        send("binary_sensor", "is_level", doc);
    }
    {
        JsonDocument doc;
        base(doc, "Tara aktiv", "tare_active", "mdi:target");
        doc["state_topic"] = topic("tare_active");
        doc["entity_category"] = "diagnostic";
        send("binary_sensor", "tare_active", doc);
    }
    {
        JsonDocument doc;
        base(doc, "Einbaulage", "mount", "mdi:rotate-3d-variant");
        doc["state_topic"] = topic("mount");
        doc["command_topic"] = topic("cmd/mount/set");
        doc["entity_category"] = "config";
        JsonArray opts = doc["options"].to<JsonArray>();
        for (int i = 0; i < MOUNT_COUNT; i++) opts.add(mountName((MountOrientation)i));
        send("select", "mount", doc);
    }

    number("Toleranz", "tolerance", "tolerance", 0.1f, 5.0f, 0.1f, "°", "mdi:arrow-expand-horizontal");
    number("Spurweite", "track_width", "track_width", 100.0f, 300.0f, 1.0f, "cm", "mdi:arrow-left-right");
    number("Radstand", "wheelbase", "wheelbase", 150.0f, 800.0f, 1.0f, "cm", "mdi:arrow-up-down");
    number("Tara Längsneigung", "tare_pitch", "tare_pitch", -MAX_TARE_DEG, MAX_TARE_DEG, 0.05f, "°", "mdi:target");
    number("Tara Querneigung", "tare_roll", "tare_roll", -MAX_TARE_DEG, MAX_TARE_DEG, 0.05f, "°", "mdi:target");

    sw("Glättungsfilter", "filter_active", "filter", "mdi:blur");
    sw("MQTT schnell (5 Hz)", "mqtt_fast", "mqtt_speed", "mdi:speedometer");

    button("Tara setzen", "tare", "mdi:target");
    button("Tara löschen", "tare_reset", "mdi:target-off", "config");
    button("Sensor neu starten", "sensor_restart", "mdi:restart", "diagnostic");
}

// The original firmware announced its entities under "wowa_level". Empty retained messages
// remove them from Home Assistant, so the old device does not stay there as a duplicate.
static void removeOldEntities() {
    static const char* const OLD[][2] = {
        {"sensor", "pitch"}, {"sensor", "roll"}, {"sensor", "temperature"},
        {"sensor", "wedge_left"}, {"sensor", "wedge_right"}, {"sensor", "jockey_wheel"},
        {"sensor", "wedge_fl"}, {"sensor", "wedge_fr"}, {"sensor", "wedge_rl"}, {"sensor", "wedge_rr"},
        {"sensor", "calibration"}, {"sensor", "rssi"}, {"sensor", "tolerance"}, {"sensor", "status"},
        {"binary_sensor", "tare_active"}, {"binary_sensor", "is_level"},
        {"binary_sensor", "filter_active"}, {"binary_sensor", "mqtt_fast"},
        {"select", "mount"}, {"select", "vehicle_type"},
        {"number", "tolerance"}, {"number", "track_width"}, {"number", "wheelbase"},
        {"number", "axle_to_jockey"}, {"number", "tare_pitch"}, {"number", "tare_roll"},
        {"button", "tare"}, {"button", "tare_reset"}, {"button", "calibration_reset"},
        {"switch", "filter_active"}, {"switch", "mqtt_fast"},
    };
    for (auto& e : OLD) {
        String t = String("homeassistant/") + e[0] + "/" OLD_NODE_ID "/" + e[1] + "/config";
        mqtt.publish(t.c_str(), "", true);
    }
    mqtt.loop();
    // Topics only the old firmware used
    static const char* const OLD_TOPICS[] = {"temperature", "wedge_left", "wedge_right", "jockey_wheel",
                                             "vehicle_type", "axle_to_jockey", "calibration"};
    for (auto s : OLD_TOPICS) mqtt.publish(topic(s).c_str(), "", true);
}
