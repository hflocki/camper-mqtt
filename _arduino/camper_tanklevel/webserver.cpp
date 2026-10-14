#include "webserver.h"
#include "wifi_manager.h"
#include "gas_sensor.h"
#include "mqtt_ha.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static unsigned long lastWsUpdate = 0;

static bool fsReady = false;
static bool restartPending = false;
static unsigned long restartAt = 0;
static volatile bool gasSensorChanged = false;  // MAC changed (set by the async web task, handled in loop())
static volatile bool gasTankChanged = false;    // Bottle size / fill stop changed
static volatile bool configDirty = false;       // Save request from the async web task, handled in loop()

static const size_t MAX_BODY_SIZE = 4096;

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len);
static void handleApiGetConfig(AsyncWebServerRequest* request);
static void handleApiSaveConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
static void handleApiRestart(AsyncWebServerRequest* request);

// ============================================================
// Helpers
// ============================================================

// Default Votronic reference points (fill level in % per voltage)
static void setDefaultCalib() {
    config.calibTable[0] = {0.130f, 0.0f};
    config.calibTable[1] = {0.137f, 10.0f};
    config.calibTable[2] = {0.973f, 20.0f};
    config.calibTable[3] = {1.040f, 30.0f};
    config.calibTable[4] = {1.350f, 40.0f};
    config.calibTable[5] = {1.515f, 50.0f};
    config.calibTable[6] = {1.770f, 60.0f};
    config.calibTable[7] = {2.178f, 80.0f};
    config.calibTable[8] = {2.386f, 100.0f};
    config.calibCount = 9;
}

static void setDefaults() {
    strlcpy(config.ap_ssid, defaultApSsid(), sizeof(config.ap_ssid));
    strlcpy(config.ap_pass, DEFAULT_AP_PASSWORD, sizeof(config.ap_pass));
    memset(config.wifi_ssid, 0, sizeof(config.wifi_ssid));
    memset(config.wifi_pass, 0, sizeof(config.wifi_pass));
    strlcpy(config.mqtt_server, DEFAULT_MQTT_SERVER, sizeof(config.mqtt_server));
    config.mqtt_port = DEFAULT_MQTT_PORT;
    memset(config.mqtt_user, 0, sizeof(config.mqtt_user));
    memset(config.mqtt_pass, 0, sizeof(config.mqtt_pass));
    strlcpy(config.mqtt_prefix, DEFAULT_MQTT_PREFIX, sizeof(config.mqtt_prefix));

    // Fresh water
    config.tank_size = DEFAULT_TANK_SIZE;
    config.filter_active = true;
    setDefaultCalib();

    // Gas
    memset(config.sensor_mac, 0, sizeof(config.sensor_mac));
    config.capacity = DEFAULT_CAPACITY;
    config.fill_stop_on = DEFAULT_FILL_STOP_ON;
    config.fill_stop = DEFAULT_FILL_STOP;
}

// Parses a JSON calibration array into a local table, sorted by voltage.
// Returns true only if at least 2 valid points were found.
static bool parseCalib(JsonArrayConst arr, CalibPoint* out, uint8_t& count) {
    count = 0;
    for (JsonObjectConst item : arr) {
        if (count >= MAX_CALIB_POINTS) break;
        if (!item["v"].is<float>() || !item["l"].is<float>()) continue;

        CalibPoint p;
        p.volt = item["v"].as<float>();
        p.percent = item["l"].as<float>();

        // Insertion sort by voltage
        int pos = count;
        while (pos > 0 && out[pos - 1].volt > p.volt) {
            out[pos] = out[pos - 1];
            pos--;
        }
        out[pos] = p;
        count++;
    }
    return count >= 2;
}

// Copies the table first and sets the count last, so the sensor loop never sees a half-written table
static void applyCalib(const CalibPoint* tbl, uint8_t count) {
    config.calibCount = 0;
    for (uint8_t i = 0; i < count; i++) config.calibTable[i] = tbl[i];
    config.calibCount = count;
}

// Normalizes "aa-bb-cc-dd-ee-ff" / "aabbccddeeff" / "AA:BB:..." to "AA:BB:CC:DD:EE:FF".
// Empty input is valid (= no sensor). Returns false on invalid input.
static bool normalizeMac(const char* in, char* out, size_t size) {
    char hex[13];
    int n = 0;
    for (const char* p = in; *p; p++) {
        if (*p == ':' || *p == '-' || *p == ' ') continue;
        if (!isxdigit((unsigned char)*p) || n >= 12) return false;
        hex[n++] = toupper((unsigned char)*p);
    }
    if (n == 0) { out[0] = '\0'; return true; }
    if (n != 12 || size < 18) return false;
    for (int i = 0; i < 6; i++) {
        out[i * 3] = hex[i * 2];
        out[i * 3 + 1] = hex[i * 2 + 1];
        out[i * 3 + 2] = (i < 5) ? ':' : '\0';
    }
    return true;
}

static bool isNumber(JsonVariantConst v) {
    return v.is<float>() || v.is<int>();
}

static void copyIfPresent(JsonDocument& doc, const char* key, char* dest, size_t size, bool skipEmpty) {
    if (doc[key].isNull()) return;
    const char* v = doc[key].as<const char*>();
    if (!v) return;
    if (skipEmpty && v[0] == '\0') return;
    strlcpy(dest, v, size);
}

static void sendError(AsyncWebServerRequest* request, const char* msg) {
    request->send(400, "application/json", String("{\"error\":\"") + msg + "\"}");
}

// ============================================================
// Setup / loop
// ============================================================

void webserverInit() {
    fsReady = LittleFS.begin(true);
    if (!fsReady) {
        Serial.println("[Web] LittleFS mount failed - running with defaults");
    }

    // Always load the config (falls back to defaults if the filesystem is unavailable)
    loadConfig();

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // Serve ONLY index.html. Serving the whole filesystem would expose /config.json
    // (WiFi and MQTT credentials) to everybody in the network.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (fsReady && LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(200, "text/html",
                "<h1>Camper Tanklevel</h1><p>Please upload index.html to LittleFS.</p>");
        }
    });

    server.on("/api/config", HTTP_GET, handleApiGetConfig);
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, handleApiSaveConfig);
    server.on("/api/restart", HTTP_POST, handleApiRestart);

    // Fresh water smoothing filter (state-changing endpoint, so POST instead of GET)
    server.on("/api/filter", HTTP_POST, [](AsyncWebServerRequest* request) {
        config.filter_active = !config.filter_active;
        configDirty = true;
        request->send(200, "application/json",
            String("{\"filter\":\"") + (config.filter_active ? "ON" : "OFF") + "\"}");
    });

    // Nearby BLE devices (for picking the gas sensor MAC in the web UI)
    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        gasFillSeenList(doc["devices"].to<JsonArray>());
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
}

void webserverStart() {
    server.begin();
    Serial.println("[Web] Server started on port 80");
}

void webserverLoop() {
    ws.cleanupClients(2);

    if (configDirty) {
        configDirty = false;
        saveConfig();
    }

    // Gas settings changed in the web UI
    if (gasSensorChanged) {
        gasSensorChanged = false;
        gasTankChanged = false;
        gasReset();             // Other sensor: forget the old readings
    } else if (gasTankChanged) {
        gasTankChanged = false;
        gasRecalculate();       // Same sensor: recalculate level/content from the last raw value
    }

    // Deferred restart: the HTTP response can be sent before the chip resets
    if (restartPending && millis() >= restartAt) {
        ESP.restart();
    }

    if (millis() - lastWsUpdate > WS_UPDATE_MS) {
        lastWsUpdate = millis();
        if (ws.count() > 0) {
            wsSendSensorData();
        }
    }
}

void wsSendSensorData() {
    JsonDocument doc;
    const WaterData& w = sensorData.water;
    const GasData& g = sensorData.gas;

    JsonObject wo = doc["water"].to<JsonObject>();
    wo["voltage"] = round(w.avg_voltage * 1000.0f) / 1000.0f;
    wo["liters"] = round(w.liters * 10.0f) / 10.0f;
    wo["percent"] = round(w.percent);
    wo["filter"] = config.filter_active;

    JsonObject go = doc["gas"].to<JsonObject>();
    go["has_data"] = g.has_data;
    go["level"] = round(g.level * 10.0f) / 10.0f;
    go["raw"] = round(g.raw_level * 10.0f) / 10.0f;
    go["content"] = round(g.content * 10.0f) / 10.0f;
    if (isnan(g.battery)) go["battery"] = nullptr;
    else go["battery"] = round(g.battery);
    go["ble_rssi"] = g.ble_rssi;
    go["ble_state"] = (const char*)g.ble_state;
    go["age"] = g.has_data ? (long)((millis() - g.last_update) / 1000) : -1;
    go["raw_hex"] = (const char*)g.raw_hex;
    go["adv_count"] = g.adv_count;
    go["scanning"] = g.scanning;

    doc["rssi"] = sensorData.rssi;
    doc["wifi"] = getWiFiStateString();
    doc["mqtt"] = mqttIsConnected();

    char buffer[768];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) return;
    ws.textAll(buffer, len);
}

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        wsSendSensorData();
    }
}

// ============================================================
// REST API
// ============================================================

static void handleApiGetConfig(AsyncWebServerRequest* request) {
    JsonDocument doc;

    // Passwords are never sent back to the browser
    doc["wifi_ssid"] = (const char*)config.wifi_ssid;
    doc["ap_ssid"] = (const char*)config.ap_ssid;
    doc["wifi_pass_set"] = strlen(config.wifi_pass) > 0;
    doc["mqtt_server"] = (const char*)config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = (const char*)config.mqtt_user;
    doc["mqtt_pass_set"] = strlen(config.mqtt_pass) > 0;
    doc["mqtt_prefix"] = (const char*)config.mqtt_prefix;
    doc["version"] = FW_VERSION;

    // Fresh water. "l" holds the fill level in % (key kept for compatibility with stored configs)
    doc["tank_size"] = config.tank_size;
    doc["filter_active"] = config.filter_active;
    JsonArray arr = doc["calib"].to<JsonArray>();
    for (int i = 0; i < config.calibCount; i++) {
        JsonObject item = arr.add<JsonObject>();
        item["v"] = config.calibTable[i].volt;
        item["l"] = config.calibTable[i].percent;
    }

    // Gas
    doc["sensor_mac"] = (const char*)config.sensor_mac;
    doc["capacity"] = config.capacity;
    doc["fill_stop_on"] = config.fill_stop_on;
    doc["fill_stop"] = config.fill_stop;

    // Compile-time gas sensor definition (read-only in the web UI)
    JsonObject def = doc["sensor_def"].to<JsonObject>();
    def["model"] = SENSOR_MODEL;
    def["source"] = SENSOR_SOURCE;
    if (SENSOR_COMPANY_ID >= 0) {
        char buf[7];
        snprintf(buf, sizeof(buf), "%04X", (unsigned)SENSOR_COMPANY_ID);
        def["company"] = (const char*)buf;
    }
    def["uuid"] = SENSOR_UUID;
    def["level_offset"] = LEVEL_OFFSET;
    def["level_len"] = LEVEL_LEN;
    def["battery"] = BATTERY_ENABLED;
    def["battery_offset"] = BATTERY_OFFSET;
    def["battery_len"] = BATTERY_LEN;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

static void handleApiSaveConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    static String body;
    static bool rejected = false;

    if (index == 0) {
        body = "";
        rejected = (total > MAX_BODY_SIZE);
    }
    if (rejected) {
        if (index + len == total) {
            request->send(413, "application/json", "{\"error\":\"Body too large\"}");
        }
        return;
    }

    body += String((char*)data, len);

    if (index + len != total) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    body = "";

    if (err) {
        sendError(request, "Invalid JSON");
        return;
    }

    // ---- Validate everything first, before anything is changed ----

    // The access point password needs at least 8 characters (WPA2)
    if (doc["ap_pass"].is<const char*>()) {
        size_t apLen = strlen(doc["ap_pass"].as<const char*>());
        if (apLen > 0 && apLen < 8) { sendError(request, "AP password needs at least 8 characters"); return; }
    }

    // Fresh water
    CalibPoint tmp[MAX_CALIB_POINTS];
    uint8_t tmpCount = 0;
    bool hasCalib = !doc["calib"].isNull();
    if (hasCalib && !parseCalib(doc["calib"].as<JsonArrayConst>(), tmp, tmpCount)) {
        sendError(request, "At least 2 valid calibration points required"); return;
    }

    bool hasTank = isNumber(doc["tank_size"]);
    float newTank = hasTank ? doc["tank_size"].as<float>() : 0.0f;
    if (hasTank && (newTank < 1.0f || newTank > 5000.0f)) { sendError(request, "Tank size must be 1-5000 L"); return; }

    // Gas
    bool hasMac = doc["sensor_mac"].is<const char*>();
    char newMac[18] = "";
    if (hasMac && !normalizeMac(doc["sensor_mac"].as<const char*>(), newMac, sizeof(newMac))) {
        sendError(request, "Invalid MAC address"); return;
    }

    bool hasCap = isNumber(doc["capacity"]);
    float newCap = hasCap ? doc["capacity"].as<float>() : 0.0f;
    if (hasCap && (newCap < 0.5f || newCap > 1000.0f)) { sendError(request, "Capacity must be 0.5-1000"); return; }

    bool hasStop = isNumber(doc["fill_stop"]);
    float newStop = hasStop ? doc["fill_stop"].as<float>() : 0.0f;
    if (hasStop && (newStop < 1.0f || newStop > 100.0f)) { sendError(request, "Fill stop must be 1-100 %"); return; }

    bool hasStopOn = doc["fill_stop_on"].is<bool>();
    bool newStopOn = hasStopOn ? doc["fill_stop_on"].as<bool>() : false;

    // ---- Apply ----

    // Empty values clear SSID/server/user; empty passwords are ignored (= keep the stored one)
    copyIfPresent(doc, "wifi_ssid",   config.wifi_ssid,   sizeof(config.wifi_ssid),   false);
    copyIfPresent(doc, "wifi_pass",   config.wifi_pass,   sizeof(config.wifi_pass),   true);
    copyIfPresent(doc, "ap_ssid",     config.ap_ssid,     sizeof(config.ap_ssid),     true);
    copyIfPresent(doc, "ap_pass",     config.ap_pass,     sizeof(config.ap_pass),     true);
    copyIfPresent(doc, "mqtt_server", config.mqtt_server, sizeof(config.mqtt_server), false);
    copyIfPresent(doc, "mqtt_user",   config.mqtt_user,   sizeof(config.mqtt_user),   false);
    copyIfPresent(doc, "mqtt_pass",   config.mqtt_pass,   sizeof(config.mqtt_pass),   true);
    copyIfPresent(doc, "mqtt_prefix", config.mqtt_prefix, sizeof(config.mqtt_prefix), true);

    if (doc["mqtt_port"].is<int>()) {
        int port = doc["mqtt_port"].as<int>();
        config.mqtt_port = (port > 0 && port <= 65535) ? (uint16_t)port : DEFAULT_MQTT_PORT;
    }

    if (hasTank) config.tank_size = newTank;
    if (doc["filter_active"].is<bool>()) config.filter_active = doc["filter_active"].as<bool>();
    if (hasCalib) applyCalib(tmp, tmpCount);

    if (hasMac && strcmp(newMac, config.sensor_mac) != 0) {
        strlcpy(config.sensor_mac, newMac, sizeof(config.sensor_mac));
        gasSensorChanged = true;
    }
    if (hasCap && newCap != config.capacity)           { config.capacity = newCap;       gasTankChanged = true; }
    if (hasStopOn && newStopOn != config.fill_stop_on) { config.fill_stop_on = newStopOn; gasTankChanged = true; }
    if (hasStop && newStop != config.fill_stop)        { config.fill_stop = newStop;     gasTankChanged = true; }

    saveConfig();
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleApiRestart(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
    // Do not block the async web task: restart from webserverLoop() instead
    restartPending = true;
    restartAt = millis() + 500;
}

// ============================================================
// Persistence
// ============================================================

void loadConfig() {
    setDefaults();

    if (!fsReady) return;

    if (!LittleFS.exists(CONFIG_FILE)) {
        saveConfig();
        return;
    }

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.printf("[Config] Parse error (%s) - using defaults\n", err.c_str());
        return;
    }

    strlcpy(config.wifi_ssid,   doc["wifi_ssid"]   | "",                  sizeof(config.wifi_ssid));
    strlcpy(config.wifi_pass,   doc["wifi_pass"]   | "",                  sizeof(config.wifi_pass));
    strlcpy(config.ap_ssid,     doc["ap_ssid"]     | defaultApSsid(),     sizeof(config.ap_ssid));
    strlcpy(config.ap_pass,     doc["ap_pass"]     | DEFAULT_AP_PASSWORD, sizeof(config.ap_pass));
    strlcpy(config.mqtt_server, doc["mqtt_server"] | DEFAULT_MQTT_SERVER, sizeof(config.mqtt_server));
    config.mqtt_port = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
    strlcpy(config.mqtt_user,   doc["mqtt_user"]   | "",                  sizeof(config.mqtt_user));
    strlcpy(config.mqtt_pass,   doc["mqtt_pass"]   | "",                  sizeof(config.mqtt_pass));
    strlcpy(config.mqtt_prefix, doc["mqtt_prefix"] | DEFAULT_MQTT_PREFIX, sizeof(config.mqtt_prefix));

    // Fresh water
    config.filter_active = doc["filter_active"] | true;
    float storedTank = doc["tank_size"] | DEFAULT_TANK_SIZE;
    config.tank_size = (storedTank >= 1.0f && storedTank <= 5000.0f) ? storedTank : DEFAULT_TANK_SIZE;

    // Keep the factory table if the stored one is missing or invalid
    CalibPoint tmp[MAX_CALIB_POINTS];
    uint8_t tmpCount = 0;
    if (parseCalib(doc["calib"].as<JsonArrayConst>(), tmp, tmpCount)) {
        applyCalib(tmp, tmpCount);
    }

    // Gas
    char mac[18];
    if (normalizeMac(doc["sensor_mac"] | "", mac, sizeof(mac))) {
        strlcpy(config.sensor_mac, mac, sizeof(config.sensor_mac));
    }
    float cap = doc["capacity"] | DEFAULT_CAPACITY;
    config.capacity = (cap >= 0.5f && cap <= 1000.0f) ? cap : DEFAULT_CAPACITY;
    config.fill_stop_on = doc["fill_stop_on"] | DEFAULT_FILL_STOP_ON;
    float stop = doc["fill_stop"] | DEFAULT_FILL_STOP;
    config.fill_stop = (stop >= 1.0f && stop <= 100.0f) ? stop : DEFAULT_FILL_STOP;
}

void saveConfig() {
    if (!fsReady) return;

    JsonDocument doc;
    doc["wifi_ssid"] = (const char*)config.wifi_ssid;
    doc["wifi_pass"] = (const char*)config.wifi_pass;
    doc["ap_ssid"] = (const char*)config.ap_ssid;
    doc["ap_pass"] = (const char*)config.ap_pass;
    doc["mqtt_server"] = (const char*)config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = (const char*)config.mqtt_user;
    doc["mqtt_pass"] = (const char*)config.mqtt_pass;
    doc["mqtt_prefix"] = (const char*)config.mqtt_prefix;

    // Fresh water
    doc["filter_active"] = config.filter_active;
    doc["tank_size"] = config.tank_size;
    JsonArray arr = doc["calib"].to<JsonArray>();
    for (int i = 0; i < config.calibCount; i++) {
        JsonObject item = arr.add<JsonObject>();
        item["v"] = config.calibTable[i].volt;
        item["l"] = config.calibTable[i].percent;
    }

    // Gas
    doc["sensor_mac"] = (const char*)config.sensor_mac;
    doc["capacity"] = config.capacity;
    doc["fill_stop_on"] = config.fill_stop_on;
    doc["fill_stop"] = config.fill_stop;

    // Write to a temp file first so a power loss cannot leave a half-written config
    const char* tmpPath = "/config.tmp";
    File file = LittleFS.open(tmpPath, "w");
    if (!file) {
        Serial.println("[Config] Cannot open temp file for writing");
        return;
    }
    size_t written = serializeJsonPretty(doc, file);
    file.close();

    if (written == 0) {
        LittleFS.remove(tmpPath);
        Serial.println("[Config] Write failed");
        return;
    }

    LittleFS.remove(CONFIG_FILE);
    if (!LittleFS.rename(tmpPath, CONFIG_FILE)) {
        Serial.println("[Config] Rename failed");
    }
}
