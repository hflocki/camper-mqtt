#include "mqtt_ha.h"
#include "wifi_manager.h"
#include "bms_client.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Topic layout (prefix = config.mqtt_prefix, default "camper/battery"):
//   <prefix>/status                 online / offline (last will)
//   <prefix>/rssi                   WiFi signal
//   <prefix>/<id>/available         online while the BMS answers, offline otherwise
//   <prefix>/<id>/voltage           V
//   <prefix>/<id>/current           A, positive = charging, negative = discharging
//   <prefix>/<id>/power             W, same sign as the current
//   <prefix>/<id>/soc               state of charge in %
//   <prefix>/<id>/charge            remaining capacity in Ah
//   <prefix>/<id>/energy            stored energy in Wh
//   <prefix>/<id>/runtime           remaining runtime in min while discharging ("None" otherwise)
//   <prefix>/<id>/temperature       °C (BMS with temperature sensors only)
//   <prefix>/<id>/cycles            charge cycles (if reported)
//   <prefix>/<id>/delta             highest minus lowest cell in V (BMS with cell voltages only)
//   <prefix>/<id>/cell_<n>          cell voltage in V, n = 1 ...
//   <prefix>/<id>/state             battery state text (BM2 only): Kritisch, Schwach, Normal, Lädt, Erhaltungsladung
//   <prefix>/<id>/charging          ON / OFF
//   <prefix>/<id>/problem           ON / OFF (BMS reports an error or protection)
//   <prefix>/<id>/problem_text      readable text of the problem, "" without one
//   <prefix>/batteries              all batteries as one JSON list for displays:
//                                   [{"id":"house","name":"Bordbatterie","soc":87,"v":13.31,"a":-2.4,"w":-32,"ok":true}, ...]
//                                   optional: "st" = state text (BM2), "rt" = remaining runtime in min
//                                   while discharging, "p":true = BMS reports a problem, "pt" = its text, "t" = type (1 Daly, 2 Ective, 3 BM2)
//
// Monitors without a current sensor (BM2) get no current, power, capacity, energy and runtime entities.
//
// Home Assistant: one device per battery (linked to the "Camper Battery" device),
// entity unique_ids use the BLE address, so renaming a battery keeps its history.

#define HA_NODE_ID  "camper_battery"

static WiFiClient espClient;
static PubSubClient mqtt(espClient);

static unsigned long lastPublish = 0;
static unsigned long lastReconnect = 0;
static bool discoveryPublished = false;
static uint32_t lastPublishedRead = 0;
static uint8_t announcedCells[MAX_BMS];   // Cell entities announced per battery
static bool announcedTemp[MAX_BMS];
static bool announcedCycles[MAX_BMS];

// Pending removals (deleted batteries or changed ids), filled by the web task, handled in loop()
struct Removal { char mac[18]; char key[24]; bool entity; };
static Removal removals[MAX_BMS * 2];
static volatile uint8_t removalCount = 0;

// Value entities of one battery
struct Field {
    const char* sub;       // Topic below <prefix>/<id>/ and unique_id suffix
    const char* name;      // Entity name in HA
    const char* unit;
    const char* devClass;  // nullptr = none
    const char* icon;
    uint8_t precision;
    bool total;            // state_class total_increasing instead of measurement
};
static const Field FIELDS[] = {
    {"soc",     "Ladestand",     "%",   "battery",       "mdi:battery",          0, false},
    {"voltage", "Spannung",      "V",   "voltage",       "mdi:sine-wave",        2, false},
    {"current", "Strom",         "A",   "current",       "mdi:current-dc",       2, false},
    {"power",   "Leistung",      "W",   "power",         "mdi:flash",            0, false},
    {"charge",  "Restkapazität", "Ah",  nullptr,         "mdi:battery-plus",     1, false},
    {"energy",  "Energie",       "Wh",  "energy_storage","mdi:battery-charging", 0, false},
    {"runtime", "Restlaufzeit",  "min", "duration",      "mdi:timer-sand",       0, false},
};
static const Field F_TEMP   = {"temperature", "Temperatur", "°C", "temperature", "mdi:thermometer", 1, false};
static const Field F_CYCLES = {"cycles", "Zyklen", "", nullptr, "mdi:battery-sync", 0, true};
static const Field F_DELTA  = {"delta", "Zelldifferenz", "V", "voltage", "mdi:battery-heart-variant", 3, false};

// Fields that need a current measurement
static bool needsCurrent(const Field& f) {
    return strcmp(f.sub, "soc") != 0 && strcmp(f.sub, "voltage") != 0;
}

// The BM2 only measures the voltage
static bool hasCurrent(int i) {
    return config.bms[i].type != BMS_BM2;
}

static String topic(const char* sub) {
    return String(config.mqtt_prefix) + "/" + sub;
}

static String batTopic(int i, const char* sub) {
    return String(config.mqtt_prefix) + "/" + config.bms[i].key + "/" + sub;
}

// "AA:BB:CC:DD:EE:FF" -> "aabbccddeeff"
static void macId(const char* mac, char* out) {
    int j = 0;
    for (int i = 0; mac[i] && j < 12; i++) if (mac[i] != ':') out[j++] = tolower(mac[i]);
    out[j] = '\0';
}

void mqttInit() {
    if (strlen(config.mqtt_server) == 0) {
        Serial.println("[MQTT] No server configured");
        return;
    }
    mqtt.setServer(config.mqtt_server, config.mqtt_port);
    mqtt.setBufferSize(1280);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(5);
    Serial.printf("[MQTT] Configured: %s:%d\n", config.mqtt_server, config.mqtt_port);
}

static void publishStates();
static void processRemovals();

void mqttLoop() {
    if (!isConnected()) return;
    if (strlen(config.mqtt_server) == 0) return;

    if (!mqtt.connected()) {
        // connect() blocks while the broker is unreachable, so retry only every 5 s
        if (millis() - lastReconnect > 5000) {
            lastReconnect = millis();
            Serial.println("[MQTT] Connection attempt...");
            String clientId = "camper-battery-" + WiFi.macAddress();
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
                discoveryPublished = false;   // Always re-announce after (re)connect
                mqtt.publish(willTopic.c_str(), "online", true);
                lastPublish = 0;
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

    // After every reading, and at least every MQTT_REPUBLISH_MS
    uint32_t seq = bmsReadSeq();
    if (seq != lastPublishedRead || lastPublish == 0 || millis() - lastPublish > MQTT_REPUBLISH_MS) {
        lastPublish = millis();
        lastPublishedRead = seq;
        publishStates();
    }
}

bool mqttIsConnected() { return mqtt.connected(); }
void mqttRequestDiscovery() { discoveryPublished = false; }

void mqttQueueRemoval(const char* mac, const char* oldKey, bool removeEntity) {
    if (removalCount >= sizeof(removals) / sizeof(removals[0])) return;
    Removal& r = removals[removalCount];
    strlcpy(r.mac, mac, sizeof(r.mac));
    strlcpy(r.key, oldKey, sizeof(r.key));
    r.entity = removeEntity;
    removalCount = removalCount + 1;
}

static void removeDiscovery(const char* component, const char* id, const char* sub) {
    String t = String("homeassistant/") + component + "/" HA_NODE_ID "/" + id + "_" + sub + "/config";
    mqtt.publish(t.c_str(), "", true);
}

static void processRemovals() {
    static const char* const subs[] = {"soc", "voltage", "current", "power", "charge", "energy", "runtime",
                                       "temperature", "cycles", "delta", "state", "available", "charging", "problem"};
    for (uint8_t i = 0; i < removalCount; i++) {
        const Removal& r = removals[i];
        char id[13];
        macId(r.mac, id);
        // Empty retained messages delete the old values from the broker
        if (r.key[0]) {
            String base = String(config.mqtt_prefix) + "/" + r.key + "/";
            for (const char* s : subs) mqtt.publish((base + s).c_str(), "", true);
            for (int c = 1; c <= BMS_MAX_CELLS; c++) mqtt.publish((base + "cell_" + c).c_str(), "", true);
        }
        // Empty retained discovery removes the entities from Home Assistant
        if (r.entity && id[0]) {
            for (const char* s : subs) removeDiscovery("sensor", id, s);
            removeDiscovery("binary_sensor", id, "charging");
            removeDiscovery("binary_sensor", id, "problem");
            for (int c = 1; c <= BMS_MAX_CELLS; c++) removeDiscovery("sensor", id, (String("cell_") + c).c_str());
        }
    }
    removalCount = 0;
}

// ============================================================
// States
// ============================================================

static void pubFloat(int i, const char* sub, float v, int decimals) {
    if (isnan(v)) return;
    char buf[16];
    dtostrf(v, 1, decimals, buf);
    mqtt.publish(batTopic(i, sub).c_str(), buf, true);
}

static void publishBattery(int i, const BmsLive& l) {
    mqtt.publish(batTopic(i, "available").c_str(), l.valid ? "online" : "offline", true);
    if (!l.valid) return;   // HA keeps the last values and shows the entities as unavailable
    const BmsSample& s = l.s;
    pubFloat(i, "voltage", s.voltage, 2);
    pubFloat(i, "current", s.current, 2);
    pubFloat(i, "power", s.power, 0);
    pubFloat(i, "soc", s.soc, 0);
    pubFloat(i, "charge", s.charge_ah, 1);
    pubFloat(i, "energy", s.energy_wh, 0);
    // "None" = unknown in HA (not discharging); an empty payload would keep the old value
    if (hasCurrent(i)) {
        if (isnan(s.runtime_s)) mqtt.publish(batTopic(i, "runtime").c_str(), "None", true);
        else pubFloat(i, "runtime", s.runtime_s / 60.0f, 0);
    }
    const char* st = batStateName(s.bat_state);
    if (st) mqtt.publish(batTopic(i, "state").c_str(), st, true);
    pubFloat(i, "temperature", s.temperature, 1);
    if (s.cycles >= 0) mqtt.publish(batTopic(i, "cycles").c_str(), String(s.cycles).c_str(), true);
    pubFloat(i, "delta", s.delta_v, 3);
    for (uint8_t c = 0; c < s.cell_count; c++) {
        pubFloat(i, (String("cell_") + (c + 1)).c_str(), s.cell_v[c], 3);
    }
    bool charging = (!isnan(s.current) && s.current > 0.05f) ||
                    s.bat_state == BAT_STATE_CHARGING || s.bat_state == BAT_STATE_FLOATING;
    mqtt.publish(batTopic(i, "charging").c_str(), charging ? "ON" : "OFF", true);
    mqtt.publish(batTopic(i, "problem").c_str(), s.problem ? "ON" : "OFF", true);
    {
        char pt[160];
        bmsProblemText(s, pt, sizeof(pt));
        mqtt.publish(batTopic(i, "problem_text").c_str(), pt, true);   // "" when there is no problem
    }

    // New sensors appeared (cell count known only after the first reading): announce them
    if (s.cell_count != announcedCells[i] || (!isnan(s.temperature) && !announcedTemp[i]) ||
        (s.cycles >= 0 && !announcedCycles[i])) {
        discoveryPublished = false;
    }
}

static void publishList(const BmsLive* all) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_BMS; i++) {
        if (!config.bms[i].used) continue;
        const BmsLive& l = all[i];
        JsonObject o = arr.add<JsonObject>();
        o["id"] = (const char*)config.bms[i].key;
        o["name"] = (const char*)config.bms[i].name;
        o["ok"] = l.valid;
        o["t"] = config.bms[i].type;
        if (l.valid) {
            if (!isnan(l.s.soc)) o["soc"] = roundf(l.s.soc);
            if (!isnan(l.s.voltage)) o["v"] = roundf(l.s.voltage * 100) / 100;
            if (!isnan(l.s.current)) o["a"] = roundf(l.s.current * 100) / 100;
            if (!isnan(l.s.power)) o["w"] = roundf(l.s.power);
            const char* st = batStateName(l.s.bat_state);
            if (st) o["st"] = st;
            if (!isnan(l.s.runtime_s)) o["rt"] = (long)lroundf(l.s.runtime_s / 60.0f);
            if (l.s.problem) {
                o["p"] = true;
                char pt[100];   // short: the whole list has to fit into one MQTT message
                bmsProblemText(l.s, pt, sizeof(pt));
                o["pt"] = pt;
            }
        }
    }
    char buf[1000];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return;
    mqtt.publish(topic("batteries").c_str(), buf, true);
}

static void publishStates() {
    static BmsLive all[MAX_BMS];
    for (int i = 0; i < MAX_BMS; i++) {
        if (!config.bms[i].used) continue;
        bmsGetLive(i, all[i]);
        publishBattery(i, all[i]);
    }
    publishList(all);
    mqtt.publish(topic("rssi").c_str(), String(sensorData.rssi).c_str(), true);
    mqtt.publish(topic("status").c_str(), "online", true);
}

// ============================================================
// Home Assistant discovery
// ============================================================

static void publishJson(const String& configTopic, JsonDocument& doc) {
    static char buffer[900];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) {
        Serial.printf("[MQTT] Discovery payload too large: %s\n", configTopic.c_str());
        return;
    }
    mqtt.publish(configTopic.c_str(), buffer, true);
}

static void addMainDevice(JsonDocument& doc) {
    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = HA_NODE_ID;
    device["name"] = "Camper Battery";
    device["model"] = "ESP32 BLE BMS gateway";
    device["manufacturer"] = "DIY";
    device["sw_version"] = FW_VERSION;
}

static void addBatteryDevice(JsonDocument& doc, int i, const char* id) {
    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = String(HA_NODE_ID "_") + id;
    device["name"] = (const char*)config.bms[i].name;
    device["model"] = bmsTypeName(config.bms[i].type);
    device["manufacturer"] = config.bms[i].type == BMS_DALY ? "Daly" : config.bms[i].type == BMS_ECTIVE ? "Ective" : "Leagend";
    device["via_device"] = HA_NODE_ID;
}

static void addAvailability(JsonDocument& doc, int i) {
    JsonArray av = doc["availability"].to<JsonArray>();
    av.add<JsonObject>()["topic"] = topic("status");
    av.add<JsonObject>()["topic"] = batTopic(i, "available");
    doc["availability_mode"] = "all";
}

static void publishField(int i, const char* id, const Field& f, const char* sub = nullptr, const char* name = nullptr) {
    if (!sub) sub = f.sub;
    JsonDocument doc;
    doc["name"] = name ? name : f.name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id + "_" + sub;
    doc["object_id"] = String("battery_") + config.bms[i].key + "_" + sub;
    doc["state_topic"] = batTopic(i, sub);
    if (f.unit[0]) doc["unit_of_measurement"] = f.unit;
    if (f.devClass) doc["device_class"] = f.devClass;
    doc["state_class"] = f.total ? "total_increasing" : "measurement";
    doc["suggested_display_precision"] = f.precision;
    doc["icon"] = f.icon;
    addAvailability(doc, i);
    addBatteryDevice(doc, i, id);
    publishJson(String("homeassistant/sensor/" HA_NODE_ID "/") + id + "_" + sub + "/config", doc);
}

static void publishText(int i, const char* id, const char* sub, const char* name, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id + "_" + sub;
    doc["object_id"] = String("battery_") + config.bms[i].key + "_" + sub;
    doc["state_topic"] = batTopic(i, sub);
    doc["icon"] = icon;
    addAvailability(doc, i);
    addBatteryDevice(doc, i, id);
    publishJson(String("homeassistant/sensor/" HA_NODE_ID "/") + id + "_" + sub + "/config", doc);
}

static void publishBinary(int i, const char* id, const char* sub, const char* name, const char* devClass, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id + "_" + sub;
    doc["object_id"] = String("battery_") + config.bms[i].key + "_" + sub;
    doc["state_topic"] = batTopic(i, sub);
    doc["device_class"] = devClass;
    doc["icon"] = icon;
    addAvailability(doc, i);
    addBatteryDevice(doc, i, id);
    publishJson(String("homeassistant/binary_sensor/" HA_NODE_ID "/") + id + "_" + sub + "/config", doc);
}

static void publishDeviceSensor(const char* name, const char* id, const char* unit, const char* devClass,
                                const char* icon, bool withAvailability) {
    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = String(HA_NODE_ID "_") + id;
    doc["state_topic"] = topic(id);
    doc["icon"] = icon;
    if (unit[0]) doc["unit_of_measurement"] = unit;
    if (devClass) { doc["device_class"] = devClass; doc["state_class"] = "measurement"; }
    // The status sensor skips availability on purpose, so HA shows "offline" instead of "unavailable"
    if (withAvailability) doc["availability_topic"] = topic("status");
    addMainDevice(doc);
    publishJson(String("homeassistant/sensor/" HA_NODE_ID "/") + id + "/config", doc);
}

void mqttPublishDiscovery() {
    Serial.println("[MQTT] Publishing HA Discovery...");

    // Device first, the battery devices refer to it with via_device
    publishDeviceSensor("WLAN Signal", "rssi",   "dBm", "signal_strength", "mdi:wifi", true);
    publishDeviceSensor("Status",      "status", "",    nullptr,           "mdi:check-circle", false);

    for (int i = 0; i < MAX_BMS; i++) {
        if (!config.bms[i].used) continue;
        char id[13];
        macId(config.bms[i].mac, id);
        BmsLive l;
        bmsGetLive(i, l);

        for (const Field& f : FIELDS) {
            if (hasCurrent(i) || !needsCurrent(f)) publishField(i, id, f);
            else removeDiscovery("sensor", id, f.sub);   // announced by 1.2.0, the BM2 has no such value
        }
        if (config.bms[i].type == BMS_BM2) publishText(i, id, "state", "Zustand", "mdi:car-battery");
        else removeDiscovery("sensor", id, "state");
        publishBinary(i, id, "charging", "Lädt", "battery_charging", "mdi:battery-charging");
        publishBinary(i, id, "problem", "Problem", "problem", "mdi:battery-alert");

        // Optional values: only once the BMS has reported them
        announcedTemp[i] = l.valid && !isnan(l.s.temperature);
        if (announcedTemp[i]) publishField(i, id, F_TEMP);
        announcedCycles[i] = l.valid && l.s.cycles >= 0;
        if (announcedCycles[i]) publishField(i, id, F_CYCLES);

        uint8_t cells = l.valid ? l.s.cell_count : 0;
        if (cells > 1) publishField(i, id, F_DELTA);
        for (uint8_t c = 1; c <= cells; c++) {
            Field fc = {"", "", "V", "voltage", "mdi:battery-outline", 3, false};
            String sub = String("cell_") + c;
            String name = String("Zelle ") + c;
            publishField(i, id, fc, sub.c_str(), name.c_str());
        }
        // Fewer cells than before (other battery at this address): remove the extra entities
        for (uint8_t c = cells + 1; c <= announcedCells[i]; c++) {
            removeDiscovery("sensor", id, (String("cell_") + c).c_str());
        }
        announcedCells[i] = cells;
    }

    Serial.println("[MQTT] Discovery complete");
}
