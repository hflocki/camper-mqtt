#include "webserver.h"
#include "wifi_manager.h"
#include "sensor.h"
#include "mqtt_ha.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>

// Routes:
//   GET  /               web UI (LittleFS /index.html.gz), upload form if missing
//   GET  /api/config     all settings (passwords never)
//   POST /api/config     partial settings as JSON, validated first, saved in loop()
//   POST /api/update     file upload: .bin = firmware (restart), .gz = web page (no restart)
//   POST /api/measure    measure right away (example action)
//   POST /api/restart
//   WS   /ws             live values every WS_UPDATE_MS

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static unsigned long lastWsUpdate = 0;

static bool fsReady = false;
static bool restartPending = false;
static unsigned long restartAt = 0;
static volatile bool configDirty = false;   // Save request from the async web task, handled in loop()

// ---- Upload (web UI): firmware .bin or the web page index.html.gz --------------
// A complete LittleFS image is NOT accepted here: it would also replace /config.json.
static bool upAuthorized = false;
static bool upWebPage = false;
static bool upFailed = false;
static File upFile;
static const char* WEB_FILE = "/index.html.gz";
static const char* WEB_TMP = "/index.html.gz.tmp";

static const size_t MAX_BODY_SIZE = 4096;

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len);
static void handleApiGetConfig(AsyncWebServerRequest* request);
static void handleApiSaveConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

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

    // TODO: defaults of your own settings
    config.interval_s = DEFAULT_INTERVAL_S;
    config.scale = DEFAULT_SCALE;
    config.offset = DEFAULT_OFFSET;
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
    if (!fsReady) Serial.println("[Web] LittleFS mount failed - running with defaults");

    // Always load the config (falls back to defaults if the filesystem is unavailable)
    loadConfig();

    ws.onEvent(onWsEvent);
    // Browsers send the basic auth credentials of the page along with the WebSocket upgrade
    ws.handleHandshake([](AsyncWebServerRequest* request) { return authOk(request); });
    server.addHandler(&ws);

    // Serve ONLY index.html. Serving the whole filesystem would expose /config.json
    // (WiFi and MQTT credentials) to everybody in the network.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        if (fsReady && LittleFS.exists(WEB_FILE)) {
            AsyncWebServerResponse* response = request->beginResponse(LittleFS, WEB_FILE, "text/html");
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
        } else {
            request->send(200, "text/html",
                "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                "<h2>" DEVICE_NAME "</h2><p>Web page missing. Upload <code>data/index.html.gz</code> "
                "or a firmware (.bin):</p><form method=POST action=/api/update enctype=multipart/form-data>"
                "<input type=file name=f accept='.bin,.gz'> <button>Upload</button></form>");
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

    // TODO: own actions, same pattern (keep the handler short, real work in loop())
    server.on("/api/measure", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        sensorRequestMeasure();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        request->send(200, "application/json", "{\"status\":\"restarting\"}");
        restartPending = true;   // do not block the async web task
        restartAt = millis() + 500;
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

    // Deferred restart: the HTTP response can be sent before the chip resets
    if (restartPending && millis() >= restartAt) ESP.restart();

    if (millis() - lastWsUpdate > WS_UPDATE_MS) {
        lastWsUpdate = millis();
        if (ws.count() > 0) wsSendLiveData();
    }
}

// Live data for the web UI: the value list (with names and units) plus device status
void wsSendLiveData() {
    if (!ws.availableForWriteAll()) return;   // Slow client: skip this update instead of queueing

    JsonDocument doc;
    unsigned long now = millis();
    JsonArray arr = doc["values"].to<JsonArray>();
    for (uint8_t i = 0; i < VALUE_COUNT; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["key"] = VALUES[i].key;
        o["name"] = VALUES[i].name;
        o["unit"] = VALUES[i].unit;
        o["dec"] = VALUES[i].decimals;
        float v = sensorData.values[i];
        if (isnan(v)) o["v"] = nullptr; else o["v"] = v;
    }
    doc["ok"] = sensorData.ok;
    doc["age"] = sensorData.last_ok ? (long)((now - sensorData.last_ok) / 1000) : -1;
    doc["errors"] = sensorData.errors;
    doc["reads"] = sensorData.readCount;
    doc["rssi"] = sensorData.rssi;
    doc["uptime"] = now / 1000;
    doc["heap"] = (uint32_t)ESP.getFreeHeap();
    doc["wifi_drops"] = sensorData.wifi_drops;
    doc["wifi_reason"] = (const char*)sensorData.wifi_reason;
    doc["wifi"] = getWiFiStateString();
    doc["mqtt"] = mqttIsConnected();

    static char buffer[2048];   // static: keeps it off the stack
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) return;
    ws.textAll(buffer, len);
}

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) lastWsUpdate = 0;   // Send the current values with the next loop
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
    doc["name"] = DEVICE_NAME;
    doc["hostname"] = OTA_HOSTNAME;

    // TODO: your settings
    doc["interval"] = config.interval_s;
    doc["scale"] = config.scale;
    doc["offset"] = config.offset;

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
        if (index + len == total) request->send(413, "application/json", "{\"error\":\"Body too large\"}");
        return;
    }
    body.concat((const char*)data, len);   // Body is not 0-terminated
    if (index + len != total) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    body = "";
    if (err) { sendError(request, "Invalid JSON"); return; }

    // ---- Validate everything first, before anything is changed ----
    if (doc["ap_pass"].is<const char*>()) {
        size_t apLen = strlen(doc["ap_pass"].as<const char*>());
        if (apLen > 0 && apLen < 8) { sendError(request, "AP password needs at least 8 characters"); return; }
    }
    bool hasTx = doc["wifi_tx_power"].is<int>();
    if (hasTx && !isValidTxPower(doc["wifi_tx_power"].as<int>())) { sendError(request, "Invalid TX power"); return; }

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

    // TODO: validate your settings here
    bool hasInterval = isNumber(doc["interval"]);
    int newInterval = hasInterval ? doc["interval"].as<int>() : 0;
    if (hasInterval && (newInterval < 1 || newInterval > 600)) { sendError(request, "Interval must be 1-600 s"); return; }
    bool hasScale = isNumber(doc["scale"]);
    float newScale = hasScale ? doc["scale"].as<float>() : 0;
    if (hasScale && (newScale <= 0 || newScale > 1000)) { sendError(request, "Scale must be > 0 and <= 1000"); return; }
    bool hasOffset = isNumber(doc["offset"]);

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

    // TODO: apply your settings
    if (hasInterval) config.interval_s = (uint16_t)newInterval;
    if (hasScale) config.scale = newScale;
    if (hasOffset) config.offset = doc["offset"].as<float>();
    if (hasInterval || hasScale || hasOffset) sensorRequestMeasure();   // show the effect right away

    configDirty = true;   // Flash write in loop(), not in the web task
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// ============================================================
// Persistence (/config.json in LittleFS)
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
    strlcpy(config.ap_ssid,     doc["ap_ssid"]     | defaultApSsid(),       sizeof(config.ap_ssid));
    strlcpy(config.ap_pass,     doc["ap_pass"]     | DEFAULT_AP_PASSWORD,   sizeof(config.ap_pass));
    int tx = doc["wifi_tx_power"] | DEFAULT_TX_POWER;
    config.wifi_tx_power = isValidTxPower(tx) ? (int8_t)tx : DEFAULT_TX_POWER;
    config.ap_always = doc["ap_always"] | DEFAULT_AP_ALWAYS;
    strlcpy(config.mqtt_server, doc["mqtt_server"] | DEFAULT_MQTT_SERVER,   sizeof(config.mqtt_server));
    config.mqtt_port = doc["mqtt_port"] | DEFAULT_MQTT_PORT;
    strlcpy(config.mqtt_user,   doc["mqtt_user"]   | DEFAULT_MQTT_USER,     sizeof(config.mqtt_user));
    strlcpy(config.mqtt_pass,   doc["mqtt_pass"]   | DEFAULT_MQTT_PASSWORD, sizeof(config.mqtt_pass));
    strlcpy(config.mqtt_prefix, doc["mqtt_prefix"] | DEFAULT_MQTT_PREFIX,   sizeof(config.mqtt_prefix));
    strlcpy(config.web_user,    doc["web_user"]    | DEFAULT_WEB_USER,      sizeof(config.web_user));
    strlcpy(config.web_pass,    doc["web_pass"]    | DEFAULT_WEB_PASSWORD,  sizeof(config.web_pass));
    if (config.web_user[0] == '\0') config.web_pass[0] = '\0';

    // TODO: your settings (always with a range check, the file could be old or edited)
    int iv = doc["interval"] | DEFAULT_INTERVAL_S;
    config.interval_s = (iv >= 1 && iv <= 600) ? (uint16_t)iv : DEFAULT_INTERVAL_S;
    float sc = doc["scale"] | DEFAULT_SCALE;
    config.scale = (sc > 0 && sc <= 1000) ? sc : DEFAULT_SCALE;
    config.offset = doc["offset"] | DEFAULT_OFFSET;
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

    // TODO: your settings
    doc["interval"] = config.interval_s;
    doc["scale"] = config.scale;
    doc["offset"] = config.offset;

    // Write to a temp file first so a power loss cannot leave a half-written config
    const char* tmpPath = "/config.tmp";
    File file = LittleFS.open(tmpPath, "w");
    if (!file) { Serial.println("[Config] Cannot open temp file for writing"); return; }
    size_t written = serializeJsonPretty(doc, file);
    file.close();
    if (written == 0) {
        LittleFS.remove(tmpPath);
        Serial.println("[Config] Write failed");
        return;
    }
    LittleFS.remove(CONFIG_FILE);
    if (!LittleFS.rename(tmpPath, CONFIG_FILE)) Serial.println("[Config] Rename failed");
}
