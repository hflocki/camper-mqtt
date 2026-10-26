#include "webserver.h"
#include "wifi_manager.h"
#include "bms_client.h"
#include "mqtt_ha.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static unsigned long lastWsUpdate = 0;

static bool fsReady = false;
static bool restartPending = false;

// ---- Upload (web UI): firmware .bin or the web page index.html.gz --------------
// A complete LittleFS image is NOT accepted here: it would also replace /config.json.
static bool upAuthorized = false;
static bool upWebPage = false;
static bool upFailed = false;
static File upFile;
static const char* WEB_FILE = "/index.html.gz";
static const char* WEB_TMP = "/index.html.gz.tmp";
static unsigned long restartAt = 0;
static volatile bool configDirty = false;       // Save request from the async web task, handled in loop()
static volatile bool batteriesChanged = false;  // Battery list changed: reconnect + new HA discovery

static const size_t MAX_BODY_SIZE = 4096;

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len);
static void handleApiGetConfig(AsyncWebServerRequest* request);
static void handleApiSaveConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
static void handleApiRestart(AsyncWebServerRequest* request);

// Web UI login (HTTP basic auth). Empty user = no login.
static bool authOk(AsyncWebServerRequest* request) {
    if (config.web_user[0] == '\0') return true;
    return request->authenticate(config.web_user, config.web_pass);
}

static bool auth(AsyncWebServerRequest* request) {
    if (authOk(request)) return true;
    request->requestAuthentication();
    return false;
}

// ============================================================
// Helpers
// ============================================================

static void setDefaults() {
    strlcpy(config.ap_ssid, defaultApSsid(), sizeof(config.ap_ssid));
    strlcpy(config.ap_pass, DEFAULT_AP_PASSWORD, sizeof(config.ap_pass));
    config.wifi_tx_power = DEFAULT_TX_POWER;
    config.ap_always = DEFAULT_AP_ALWAYS;
    strlcpy(config.wifi_ssid, DEFAULT_WIFI_SSID, sizeof(config.wifi_ssid));
    strlcpy(config.wifi_pass, DEFAULT_WIFI_PASSWORD, sizeof(config.wifi_pass));
    strlcpy(config.mqtt_server, DEFAULT_MQTT_SERVER, sizeof(config.mqtt_server));
    config.mqtt_port = DEFAULT_MQTT_PORT;
    strlcpy(config.mqtt_user, DEFAULT_MQTT_USER, sizeof(config.mqtt_user));
    strlcpy(config.mqtt_pass, DEFAULT_MQTT_PASSWORD, sizeof(config.mqtt_pass));
    strlcpy(config.mqtt_prefix, DEFAULT_MQTT_PREFIX, sizeof(config.mqtt_prefix));
    strlcpy(config.web_user, DEFAULT_WEB_USER, sizeof(config.web_user));
    strlcpy(config.web_pass, DEFAULT_WEB_PASSWORD, sizeof(config.web_pass));

    config.interval_s = DEFAULT_INTERVAL_S;
    memset(config.bms, 0, sizeof(config.bms));
}

static bool isNumber(JsonVariantConst v) {
    return v.is<float>() || v.is<int>();
}

// Battery id = MQTT sub topic: a-z, 0-9, "_" and "-", must not collide with the device topics
static bool isValidKey(const char* k) {
    size_t n = strlen(k);
    if (n == 0 || n >= sizeof(BmsConfig::key)) return false;
    for (size_t i = 0; i < n; i++) {
        char c = k[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return strcmp(k, "status") != 0 && strcmp(k, "rssi") != 0 && strcmp(k, "cmd") != 0 &&
           strcmp(k, "batteries") != 0;
}

// Parses the "batteries" array of the web UI / config file into out[].
// Returns an error message or nullptr if valid.
static const char* parseBatteries(JsonArrayConst arr, BmsConfig* out) {
    memset(out, 0, sizeof(BmsConfig) * MAX_BMS);
    int n = 0;
    for (JsonObjectConst item : arr) {
        if (n >= MAX_BMS) return "Too many batteries";
        BmsConfig& b = out[n];

        char mac[18];
        strlcpy(mac, item["mac"] | "", sizeof(mac));
        for (char* c = mac; *c; c++) *c = toupper(*c);
        if (!isValidMac(mac)) return "Invalid BLE address";
        strlcpy(b.mac, mac, sizeof(b.mac));

        int type = item["type"] | 0;
        if (type != BMS_DALY && type != BMS_ECTIVE && type != BMS_BM2) return "Unknown BMS type";
        b.type = (uint8_t)type;

        const char* name = item["name"] | "";
        const char* key = item["key"] | "";
        if (strlen(name) == 0 || strlen(name) >= sizeof(b.name)) return "Name must be 1-31 characters";
        if (!isValidKey(key)) return "Invalid id (a-z, 0-9, _ and -)";
        strlcpy(b.name, name, sizeof(b.name));
        strlcpy(b.key, key, sizeof(b.key));

        for (int j = 0; j < n; j++) {
            if (strcmp(out[j].mac, b.mac) == 0) return "Battery added twice";
            if (strcmp(out[j].key, b.key) == 0) return "Id used twice";
        }
        b.used = true;
        n++;
    }
    return nullptr;
}

static void batteriesToJson(JsonArray arr) {
    for (int i = 0; i < MAX_BMS; i++) {
        const BmsConfig& b = config.bms[i];
        if (!b.used) continue;
        JsonObject o = arr.add<JsonObject>();
        o["mac"] = (const char*)b.mac;
        o["type"] = b.type;
        o["name"] = (const char*)b.name;
        o["key"] = (const char*)b.key;
    }
}

// Compares old and new list and queues MQTT clean-up for removed batteries / changed ids
static void queueRemovals(const BmsConfig* oldList, const BmsConfig* newList) {
    for (int i = 0; i < MAX_BMS; i++) {
        if (!oldList[i].used) continue;
        int found = -1;
        for (int j = 0; j < MAX_BMS; j++) {
            if (newList[j].used && strcmp(newList[j].mac, oldList[i].mac) == 0) { found = j; break; }
        }
        if (found < 0) {
            mqttQueueRemoval(oldList[i].mac, oldList[i].key, true);            // Deleted
        } else if (strcmp(newList[found].key, oldList[i].key) != 0) {
            mqttQueueRemoval(oldList[i].mac, oldList[i].key, false);           // Same entities, new topics
        }
    }
}

// NAN would be written as invalid JSON, so missing values are sent as null
static void putNum(JsonObject o, const char* k, float v, float scale = 100.0f) {
    if (isnan(v)) o[k] = nullptr;
    else o[k] = roundf(v * scale) / scale;
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
    fsReady = LittleFS.begin(true);   // Formats automatically if the mount fails
    if (!fsReady) {
        Serial.println("[Web] LittleFS mount failed - running with defaults");
    }

    // Always load the config (falls back to defaults if the filesystem is unavailable)
    loadConfig();

    ws.onEvent(onWsEvent);
    // Browsers send the basic auth credentials of the page along with the WebSocket upgrade
    ws.handleHandshake([](AsyncWebServerRequest* request) { return authOk(request); });
    server.addHandler(&ws);

    // Serve ONLY index.html. Serving the whole filesystem would expose /config.json
    // (WiFi and MQTT credentials) to everybody in the network.
    // The web UI is stored gzip compressed (about 1/4 of the size).
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        if (fsReady && LittleFS.exists("/index.html.gz")) {
            AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/index.html.gz", "text/html");
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
        } else if (fsReady && LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");   // Uncompressed (older filesystem image)
        } else {
                    request->send(200, "text/html",
                "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                "<h2>Camper Battery</h2><p>Die Web-Oberfl&auml;che fehlt. Hier <code>data/index.html.gz</code> "
                "oder eine Firmware (.bin) hochladen:</p><form method=POST action=/api/update enctype=multipart/form-data>"
                "<input type=file name=f accept='.bin,.gz'> <button>Hochladen</button></form>");
        }
    });

    server.on("/api/config", HTTP_GET, handleApiGetConfig);
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL,
              [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                  // Checked per chunk: the body handler runs before the request handler
                  if (!authOk(request)) {
                      if (index + len == total) request->requestAuthentication();
                      return;
                  }
                  handleApiSaveConfig(request, data, len, index, total);
              });
    server.on("/api/restart", HTTP_POST, handleApiRestart);

    // Firmware (.bin) or web page (.gz) upload; the file name decides
    server.on("/api/update", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (!auth(request)) return;
            if (upWebPage) {
                bool ok = upAuthorized && !upFailed;
                request->send(ok ? 200 : 500, "text/plain", ok ? "OK web" : "Upload failed");
                return;
            }
            bool ok = upAuthorized && !Update.hasError();
            request->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Update failed");
            if (ok) { restartPending = true; restartAt = millis() + 1000; }
        },
        [](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (index == 0) {
                upAuthorized = authOk(request);
                if (!upAuthorized) return;
                String n = filename;
                n.toLowerCase();
                upWebPage = n.endsWith(".gz");
                upFailed = false;
                Serial.printf("[OTA] Web upload (%s): %s\n", upWebPage ? "web page" : "firmware", filename.c_str());
                if (upWebPage) {
                    upFile = fsReady ? LittleFS.open(WEB_TMP, "w") : File();
                    if (!upFile) upFailed = true;
                } else if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Update.printError(Serial);
                }
            }
            if (!upAuthorized) return;
            if (upWebPage) {
                if (upFailed) return;
                if (len && upFile.write(data, len) != len) upFailed = true;
                if (final) {
                    upFile.close();
                    if (!upFailed) { LittleFS.remove(WEB_FILE); upFailed = !LittleFS.rename(WEB_TMP, WEB_FILE); }
                    else LittleFS.remove(WEB_TMP);
                    Serial.printf("[OTA] Web page %s\n", upFailed ? "FAILED" : "replaced");
                }
                return;
            }
            if (len && Update.write(data, len) != len) Update.printError(Serial);
            if (final) {
                if (Update.end(true)) Serial.printf("[OTA] Firmware ok, %u bytes\n", (unsigned)(index + len));
                else Update.printError(Serial);
            }
        });

    // Search for BMS (the scan itself runs in the BLE task)
    server.on("/api/scan", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        bmsRequestScan();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
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

    if (batteriesChanged) {
        batteriesChanged = false;
        bmsRequestReconfigure();    // Connect to the new / changed BMS
        mqttRequestDiscovery();     // Publish the new / renamed entities
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
    if (!ws.availableForWriteAll()) return;   // Slow client: skip this update instead of queueing

    JsonDocument doc;
    unsigned long now = millis();
    static BmsLive l;

    JsonArray arr = doc["batteries"].to<JsonArray>();
    for (int i = 0; i < MAX_BMS; i++) {
        const BmsConfig& b = config.bms[i];
        if (!b.used) continue;
        bmsGetLive(i, l);
        JsonObject o = arr.add<JsonObject>();
        o["mac"] = (const char*)b.mac;
        o["name"] = (const char*)b.name;
        o["key"] = (const char*)b.key;
        o["type"] = b.type;
        o["state"] = (const char*)l.state;
        o["conn"] = l.connected;
        o["ok"] = l.valid;
        o["age"] = l.last_ok ? (long)((now - l.last_ok) / 1000) : -1;
        o["reads"] = l.reads;
        o["errors"] = l.errors;
        o["rssi"] = l.rssi;
        const BmsSample& s = l.s;
        putNum(o, "v", s.voltage);
        putNum(o, "a", s.current);
        putNum(o, "w", s.power, 1);
        putNum(o, "soc", s.soc, 10);
        putNum(o, "ah", s.charge_ah, 10);
        putNum(o, "wh", s.energy_wh, 1);
        putNum(o, "t", s.temperature, 10);
        putNum(o, "dv", s.delta_v, 1000);
        putNum(o, "rt", s.runtime_s, 1);
        o["cyc"] = s.cycles;
        o["prob"] = s.problem;
        o["cfet"] = s.charge_fet;
        o["dfet"] = s.discharge_fet;
        JsonArray cells = o["cells"].to<JsonArray>();
        for (uint8_t c = 0; c < s.cell_count; c++) cells.add(roundf(s.cell_v[c] * 1000) / 1000);
    }

    // Everything the scan found, for adding new batteries
    static FoundDevice found[MAX_FOUND];
    uint8_t n = bmsGetFound(found, MAX_FOUND);
    JsonArray fl = doc["found"].to<JsonArray>();
    for (uint8_t i = 0; i < n; i++) {
        JsonObject o = fl.add<JsonObject>();
        o["mac"] = (const char*)found[i].mac;
        o["name"] = (const char*)found[i].name;
        o["type"] = found[i].type;
        o["rssi"] = found[i].rssi;
        o["age"] = (long)((now - found[i].seen) / 1000);
    }
    doc["scanning"] = bmsScanning();

    doc["rssi"] = sensorData.rssi;
    doc["uptime"] = now / 1000;
    doc["heap"] = (uint32_t)ESP.getFreeHeap();
    doc["wifi_drops"] = sensorData.wifi_drops;
    doc["wifi_reason"] = (const char*)sensorData.wifi_reason;
    doc["wifi"] = getWiFiStateString();
    doc["mqtt"] = mqttIsConnected();

    static char buffer[4096];   // static: keeps it off the stack
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) return;
    ws.textAll(buffer, len);
}

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        lastWsUpdate = 0;   // Send the current values with the next loop
    }
}

// ============================================================
// REST API
// ============================================================

static void handleApiGetConfig(AsyncWebServerRequest* request) {
    if (!auth(request)) return;
    JsonDocument doc;

    // Passwords are never sent back to the browser
    doc["wifi_ssid"] = (const char*)config.wifi_ssid;
    doc["ap_ssid"] = (const char*)config.ap_ssid;
    doc["wifi_pass_set"] = strlen(config.wifi_pass) > 0;
    doc["wifi_tx_power"] = config.wifi_tx_power;
    doc["ap_always"] = config.ap_always;
    doc["reset_reason"] = resetReasonString();
    doc["mqtt_server"] = (const char*)config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = (const char*)config.mqtt_user;
    doc["mqtt_pass_set"] = strlen(config.mqtt_pass) > 0;
    doc["mqtt_prefix"] = (const char*)config.mqtt_prefix;
    doc["web_user"] = (const char*)config.web_user;
    doc["web_pass_set"] = strlen(config.web_pass) > 0;
    doc["version"] = FW_VERSION;
    doc["max_batteries"] = MAX_BMS;
    doc["chip"] = ESP.getChipModel();

    doc["interval"] = config.interval_s;
    batteriesToJson(doc["batteries"].to<JsonArray>());

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

    body.concat((const char*)data, len);   // Body is not 0-terminated

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
    bool hasTx = doc["wifi_tx_power"].is<int>();
    if (hasTx && !isValidTxPower(doc["wifi_tx_power"].as<int>())) { sendError(request, "Invalid TX power"); return; }

    bool hasInterval = isNumber(doc["interval"]);
    int newInterval = hasInterval ? doc["interval"].as<int>() : 0;
    if (hasInterval && (newInterval < 5 || newInterval > 300)) { sendError(request, "Interval must be 5-300 s"); return; }

    // A login needs a password: either sent now or already stored
    if (doc["web_user"].is<const char*>() && doc["web_user"].as<const char*>()[0]) {
        const char* wp = doc["web_pass"] | "";
        if (wp[0] == '\0' && config.web_pass[0] == '\0') { sendError(request, "Login needs a password"); return; }
    }

    if (doc["mqtt_prefix"].is<const char*>()) {
        const char* p = doc["mqtt_prefix"].as<const char*>();
        if (p[0] && (strchr(p, '#') || strchr(p, '+') || p[strlen(p) - 1] == '/')) {
            sendError(request, "Invalid topic prefix"); return;
        }
    }

    static BmsConfig newList[MAX_BMS];
    bool hasList = doc["batteries"].is<JsonArray>();
    if (hasList) {
        const char* bErr = parseBatteries(doc["batteries"].as<JsonArrayConst>(), newList);
        if (bErr) { sendError(request, bErr); return; }
    }

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
    copyIfPresent(doc, "web_user",    config.web_user,    sizeof(config.web_user),    false);
    copyIfPresent(doc, "web_pass",    config.web_pass,    sizeof(config.web_pass),    true);
    if (config.web_user[0] == '\0') config.web_pass[0] = '\0';   // No user = login off

    // WiFi TX power and AP mode take effect after the restart triggered by the web UI
    if (hasTx) config.wifi_tx_power = (int8_t)doc["wifi_tx_power"].as<int>();
    if (doc["ap_always"].is<bool>()) config.ap_always = doc["ap_always"].as<bool>();

    if (doc["mqtt_port"].is<int>()) {
        int port = doc["mqtt_port"].as<int>();
        config.mqtt_port = (port > 0 && port <= 65535) ? (uint16_t)port : DEFAULT_MQTT_PORT;
    }

    if (hasInterval) config.interval_s = (uint16_t)newInterval;

    if (hasList) {
        queueRemovals(config.bms, newList);
        memcpy(config.bms, newList, sizeof(config.bms));
        batteriesChanged = true;
    }

    configDirty = true;   // Flash write in loop(), not in the web task
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleApiRestart(AsyncWebServerRequest* request) {
    if (!auth(request)) return;
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

    strlcpy(config.wifi_ssid,   doc["wifi_ssid"]   | DEFAULT_WIFI_SSID,     sizeof(config.wifi_ssid));
    strlcpy(config.wifi_pass,   doc["wifi_pass"]   | DEFAULT_WIFI_PASSWORD, sizeof(config.wifi_pass));
    strlcpy(config.ap_ssid,     doc["ap_ssid"]     | defaultApSsid(),     sizeof(config.ap_ssid));
    strlcpy(config.ap_pass,     doc["ap_pass"]     | DEFAULT_AP_PASSWORD, sizeof(config.ap_pass));
    int tx = doc["wifi_tx_power"] | DEFAULT_TX_POWER;
    config.wifi_tx_power = isValidTxPower(tx) ? (int8_t)tx : DEFAULT_TX_POWER;
    config.ap_always = doc["ap_always"] | DEFAULT_AP_ALWAYS;
    strlcpy(config.mqtt_server, doc["mqtt_server"] | DEFAULT_MQTT_SERVER, sizeof(config.mqtt_server));
    config.mqtt_port = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
    strlcpy(config.mqtt_user,   doc["mqtt_user"]   | DEFAULT_MQTT_USER,     sizeof(config.mqtt_user));
    strlcpy(config.mqtt_pass,   doc["mqtt_pass"]   | DEFAULT_MQTT_PASSWORD, sizeof(config.mqtt_pass));
    strlcpy(config.mqtt_prefix, doc["mqtt_prefix"] | DEFAULT_MQTT_PREFIX, sizeof(config.mqtt_prefix));
    strlcpy(config.web_user,    doc["web_user"]    | DEFAULT_WEB_USER,      sizeof(config.web_user));
    strlcpy(config.web_pass,    doc["web_pass"]    | DEFAULT_WEB_PASSWORD,  sizeof(config.web_pass));
    if (config.web_user[0] == '\0') config.web_pass[0] = '\0';

    int iv = doc["interval"] | DEFAULT_INTERVAL_S;
    config.interval_s = (iv >= 5 && iv <= 300) ? (uint16_t)iv : DEFAULT_INTERVAL_S;

    static BmsConfig tmp[MAX_BMS];
    if (doc["batteries"].is<JsonArray>()) {
        const char* bErr = parseBatteries(doc["batteries"].as<JsonArrayConst>(), tmp);
        if (bErr) Serial.printf("[Config] Battery list ignored: %s\n", bErr);
        else memcpy(config.bms, tmp, sizeof(config.bms));
    }
}

void saveConfig() {
    if (!fsReady) return;

    JsonDocument doc;
    doc["wifi_ssid"] = (const char*)config.wifi_ssid;
    doc["wifi_pass"] = (const char*)config.wifi_pass;
    doc["ap_ssid"] = (const char*)config.ap_ssid;
    doc["ap_pass"] = (const char*)config.ap_pass;
    doc["wifi_tx_power"] = config.wifi_tx_power;
    doc["ap_always"] = config.ap_always;
    doc["mqtt_server"] = (const char*)config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = (const char*)config.mqtt_user;
    doc["mqtt_pass"] = (const char*)config.mqtt_pass;
    doc["mqtt_prefix"] = (const char*)config.mqtt_prefix;
    doc["web_user"] = (const char*)config.web_user;
    doc["web_pass"] = (const char*)config.web_pass;

    doc["interval"] = config.interval_s;
    batteriesToJson(doc["batteries"].to<JsonArray>());

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
