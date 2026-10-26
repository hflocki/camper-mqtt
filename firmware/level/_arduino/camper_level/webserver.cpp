#include "webserver.h"
#include "wifi_manager.h"
#include "sensor.h"
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

// Work requested by the async web task, done in loop() (flash writes are not safe in the web task)
static volatile bool configDirty = false;
static volatile bool tareSetPending = false;
static volatile bool tareResetPending = false;
static volatile bool sensorRestartPending = false;
static volatile bool restartPending = false;
static unsigned long restartAt = 0;

// ---- Upload (web UI): firmware .bin or the web page index.html.gz --------------
// A complete LittleFS image is NOT accepted here: it would also replace /config.json.
static bool upAuthorized = false;
static bool upWebPage = false;
static bool upFailed = false;
static File upFile;
static const char* WEB_FILE = "/index.html.gz";
static const char* WEB_TMP = "/index.html.gz.tmp";

static const size_t MAX_BODY_SIZE = 4096;

// ============================================================
// Helpers
// ============================================================

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

static void sendError(AsyncWebServerRequest* request, const char* msg) {
    request->send(400, "application/json", String("{\"error\":\"") + msg + "\"}");
}

static void sendOk(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

static void copyIfPresent(JsonDocument& doc, const char* key, char* dest, size_t size, bool skipEmpty) {
    if (!doc[key].is<const char*>()) return;
    const char* v = doc[key].as<const char*>();
    if (skipEmpty && v[0] == '\0') return;
    strlcpy(dest, v, size);
}

static bool numberIn(JsonDocument& doc, const char* key, float minV, float maxV, float& out) {
    if (!(doc[key].is<float>() || doc[key].is<int>())) return false;
    float v = doc[key].as<float>();
    if (v < minV || v > maxV) return false;
    out = v;
    return true;
}

static float r2(float v) { return roundf(v * 100.0f) / 100.0f; }
static float r1(float v) { return roundf(v * 10.0f) / 10.0f; }

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

    config.mount = MOUNT_USB_FRONT;
    config.track_width = DEFAULT_TRACK_WIDTH;
    config.wheelbase = DEFAULT_WHEELBASE;
    config.tolerance = DEFAULT_TOLERANCE;
    config.tare_pitch = 0.0f;
    config.tare_roll = 0.0f;
    config.tare_active = false;
    config.filter_active = false;
    config.mqtt_fast = false;
}

// ============================================================
// Live data (WebSocket)
// ============================================================
static void wsSendLive() {
    if (!ws.availableForWriteAll()) return;   // slow client: skip this update instead of queueing
    JsonDocument doc;
    bool valid = sensorData.sensor_ok && sensorData.updates > 0;
    doc["ok"] = valid;
    doc["pitch"] = r1(sensorData.pitch);
    doc["roll"] = r1(sensorData.roll);
    doc["raw_pitch"] = r2(sensorData.raw_pitch);
    doc["raw_roll"] = r2(sensorData.raw_roll);
    doc["fl"] = r1(sensorData.wedge_fl);
    doc["fr"] = r1(sensorData.wedge_fr);
    doc["rl"] = r1(sensorData.wedge_rl);
    doc["rr"] = r1(sensorData.wedge_rr);
    doc["level"] = sensorData.is_level;
    doc["tol"] = config.tolerance;
    doc["tare"] = config.tare_active;
    doc["tare_pitch"] = r2(config.tare_pitch);
    doc["tare_roll"] = r2(config.tare_roll);
    doc["filter"] = config.filter_active;
    doc["fast"] = config.mqtt_fast;
    doc["rssi"] = sensorData.rssi;
    doc["uptime"] = millis() / 1000;
    doc["heap"] = (uint32_t)ESP.getFreeHeap();
    doc["wifi_drops"] = sensorData.wifi_drops;
    doc["wifi_reason"] = (const char*)sensorData.wifi_reason;
    doc["wifi"] = getWiFiStateString();
    doc["mqtt"] = mqttIsConnected();

    char buffer[768];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) return;
    ws.textAll(buffer, len);
}

static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) lastWsUpdate = 0;   // send the current values with the next loop
}

// ============================================================
// REST API
// ============================================================
static void handleGetConfig(AsyncWebServerRequest* request) {
    if (!auth(request)) return;
    JsonDocument doc;
    // Passwords are never sent back to the browser
    doc["wifi_ssid"] = (const char*)config.wifi_ssid;
    doc["wifi_pass_set"] = strlen(config.wifi_pass) > 0;
    doc["ap_ssid"] = (const char*)config.ap_ssid;
    doc["wifi_tx_power"] = config.wifi_tx_power;
    doc["ap_always"] = config.ap_always;
    doc["reset_reason"] = resetReasonString();
    doc["mqtt_server"] = (const char*)config.mqtt_server;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = (const char*)config.mqtt_user;
    doc["mqtt_pass_set"] = strlen(config.mqtt_pass) > 0;
    doc["mqtt_prefix"] = (const char*)config.mqtt_prefix;
    doc["web_user"] = (const char*)config.web_user;
    doc["version"] = FW_VERSION;
    doc["pins"] = String("SDA ") + PIN_I2C_SDA + " / SCL " + PIN_I2C_SCL;

    doc["mount"] = (int)config.mount;
    doc["track_width"] = config.track_width;
    doc["wheelbase"] = config.wheelbase;
    doc["tolerance"] = config.tolerance;
    doc["tare_pitch"] = r2(config.tare_pitch);
    doc["tare_roll"] = r2(config.tare_roll);
    doc["tare_active"] = config.tare_active;
    doc["filter_active"] = config.filter_active;
    doc["mqtt_fast"] = config.mqtt_fast;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

static void handleSaveConfig(AsyncWebServerRequest* request, JsonDocument& doc) {
    // ---- Validate everything first, before anything is changed ----
    if (doc["ap_pass"].is<const char*>()) {
        size_t n = strlen(doc["ap_pass"].as<const char*>());
        if (n > 0 && n < 8) { sendError(request, "AP password needs at least 8 characters"); return; }
    }
    bool hasTx = doc["wifi_tx_power"].is<int>();
    if (hasTx && !isValidTxPower(doc["wifi_tx_power"].as<int>())) { sendError(request, "Invalid TX power"); return; }
    if (doc["web_user"].is<const char*>() && doc["web_user"].as<const char*>()[0]) {
        const char* wp = doc["web_pass"] | "";
        if (wp[0] == '\0' && config.web_pass[0] == '\0') { sendError(request, "Login needs a password"); return; }
    }
    if (doc["mqtt_prefix"].is<const char*>()) {
        const char* p = doc["mqtt_prefix"].as<const char*>();
        if (p[0] && (strchr(p, '#') || strchr(p, '+') || p[strlen(p) - 1] == '/')) { sendError(request, "Invalid topic prefix"); return; }
    }
    float track = config.track_width, wb = config.wheelbase, tol = config.tolerance;
    float tp = config.tare_pitch, tr = config.tare_roll;
    if (!doc["track_width"].isNull() && !numberIn(doc, "track_width", 100, 300, track)) { sendError(request, "Track width 100-300 cm"); return; }
    if (!doc["wheelbase"].isNull() && !numberIn(doc, "wheelbase", 150, 800, wb)) { sendError(request, "Wheelbase 150-800 cm"); return; }
    if (!doc["tolerance"].isNull() && !numberIn(doc, "tolerance", 0.1f, 5, tol)) { sendError(request, "Tolerance 0.1-5"); return; }
    if (!doc["tare_pitch"].isNull() && !numberIn(doc, "tare_pitch", -MAX_TARE_DEG, MAX_TARE_DEG, tp)) { sendError(request, "Tare -10 ... 10"); return; }
    if (!doc["tare_roll"].isNull() && !numberIn(doc, "tare_roll", -MAX_TARE_DEG, MAX_TARE_DEG, tr)) { sendError(request, "Tare -10 ... 10"); return; }
    int mount = doc["mount"] | (int)config.mount;
    if (mount < 0 || mount >= MOUNT_COUNT) { sendError(request, "Invalid mounting"); return; }

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
    if (config.web_user[0] == '\0') config.web_pass[0] = '\0';   // no user = login off
    if (hasTx) config.wifi_tx_power = (int8_t)doc["wifi_tx_power"].as<int>();
    if (doc["ap_always"].is<bool>()) config.ap_always = doc["ap_always"].as<bool>();
    if (doc["mqtt_port"].is<int>()) {
        int port = doc["mqtt_port"].as<int>();
        config.mqtt_port = (port > 0 && port <= 65535) ? (uint16_t)port : DEFAULT_MQTT_PORT;
    }

    config.mount = (MountOrientation)mount;
    config.track_width = track;
    config.wheelbase = wb;
    config.tolerance = tol;
    if (!doc["tare_pitch"].isNull() || !doc["tare_roll"].isNull()) {
        config.tare_pitch = tp;
        config.tare_roll = tr;
        config.tare_active = true;
    }
    if (doc["filter_active"].is<bool>()) config.filter_active = doc["filter_active"].as<bool>();
    if (doc["mqtt_fast"].is<bool>()) config.mqtt_fast = doc["mqtt_fast"].as<bool>();

    configDirty = true;   // flash write + recalculation in loop()
    sendOk(request);
}

// Collects a JSON body (also split into several chunks) and hands it over when complete
static void jsonBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total,
                     void (*handler)(AsyncWebServerRequest*, JsonDocument&)) {
    static String body;
    static bool rejected = false;
    if (index == 0) { body = ""; rejected = (total > MAX_BODY_SIZE); }
    if (!authOk(request)) {
        if (index + len == total) request->requestAuthentication();
        return;
    }
    if (rejected) {
        if (index + len == total) request->send(413, "application/json", "{\"error\":\"Body too large\"}");
        return;
    }
    body.concat((const char*)data, len);   // the body is not 0-terminated
    if (index + len != total) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    body = "";
    if (err) { sendError(request, "Invalid JSON"); return; }
    handler(request, doc);
}

// ============================================================
// Setup / loop
// ============================================================
void webserverInit() {
    fsReady = LittleFS.begin(true);
    if (!fsReady) Serial.println("[Web] LittleFS mount failed - running with defaults");
    loadConfig();

    ws.onEvent(onWsEvent);
    // Browsers send the basic auth credentials of the page along with the WebSocket upgrade
    ws.handleHandshake([](AsyncWebServerRequest* request) { return authOk(request); });
    server.addHandler(&ws);

    // Serve ONLY index.html. Serving the whole filesystem would expose /config.json
    // (WiFi and MQTT credentials) to everybody in the network - the original did that.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        if (fsReady && LittleFS.exists("/index.html.gz")) {
            AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/index.html.gz", "text/html");
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
        } else {
            request->send(200, "text/html",
                "<h1>Camper Level</h1><p>Web page missing. Upload <code>data/index.html.gz</code> "
                "or a firmware (.bin):</p><form method=POST action=/api/update enctype=multipart/form-data>"
                "<input type=file name=f accept='.bin,.gz'> <button>Upload</button></form>");
        }
    });

    server.on("/api/config", HTTP_GET, handleGetConfig);
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest*) {}, NULL,
              [](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) { jsonBody(r, d, l, i, t, handleSaveConfig); });

    server.on("/api/tare", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        String action = request->hasParam("action") ? request->getParam("action")->value() : "set";
        if (action == "reset") tareResetPending = true; else tareSetPending = true;
        sendOk(request);
    });
    server.on("/api/sensor_restart", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        sensorRestartPending = true;
        sendOk(request);
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
                } else {
#if defined(ESP8266)
                    Update.runAsync(true);   // called from the async web server
                    uint32_t maxSize = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                    if (!Update.begin(maxSize, U_FLASH)) Update.printError(Serial);
#else
                    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
#endif
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
        sensorRecalc();
        saveConfig();
        mqttRequestSettings();
    }
    if (tareSetPending) { tareSetPending = false; sensorSetTare(); mqttRequestSettings(); }
    if (tareResetPending) { tareResetPending = false; sensorResetTare(); mqttRequestSettings(); }
    if (sensorRestartPending) { sensorRestartPending = false; sensorRestart(); }
    if (restartPending && millis() >= restartAt) ESP.restart();

    if (millis() - lastWsUpdate > WS_UPDATE_MS) {
        lastWsUpdate = millis();
        if (ws.count() > 0) wsSendLive();
    }
}

// ============================================================
// Persistence
// ============================================================
void loadConfig() {
    setDefaults();
    if (!fsReady) return;
    if (!LittleFS.exists(CONFIG_FILE)) { saveConfig(); return; }

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) { Serial.printf("[Config] Parse error (%s) - using defaults\n", err.c_str()); return; }

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

    // Missing or invalid values keep the defaults (the original set the track width to 0 then)
    int m = doc["mount"] | 0;
    config.mount = (m >= 0 && m < MOUNT_COUNT) ? (MountOrientation)m : MOUNT_USB_FRONT;
    float v;
    if (numberIn(doc, "track_width", 100, 300, v)) config.track_width = v;
    if (numberIn(doc, "wheelbase", 150, 800, v)) config.wheelbase = v;
    if (numberIn(doc, "tolerance", 0.1f, 5, v)) config.tolerance = v;
    if (numberIn(doc, "tare_pitch", -MAX_TARE_DEG, MAX_TARE_DEG, v)) config.tare_pitch = v;
    if (numberIn(doc, "tare_roll", -MAX_TARE_DEG, MAX_TARE_DEG, v)) config.tare_roll = v;
    config.tare_active = doc["tare_active"] | false;
    config.filter_active = doc["filter_active"] | false;
    config.mqtt_fast = doc["mqtt_fast"] | false;
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
    doc["mount"] = (int)config.mount;
    doc["track_width"] = config.track_width;
    doc["wheelbase"] = config.wheelbase;
    doc["tolerance"] = config.tolerance;
    doc["tare_pitch"] = config.tare_pitch;
    doc["tare_roll"] = config.tare_roll;
    doc["tare_active"] = config.tare_active;
    doc["filter_active"] = config.filter_active;
    doc["mqtt_fast"] = config.mqtt_fast;

    // Write to a temp file first so a power loss cannot leave a half-written config
    const char* tmpPath = "/config.tmp";
    File file = LittleFS.open(tmpPath, "w");
    if (!file) { Serial.println("[Config] Cannot write"); return; }
    size_t written = serializeJsonPretty(doc, file);
    file.close();
    if (written == 0) { LittleFS.remove(tmpPath); return; }
    LittleFS.remove(CONFIG_FILE);
    LittleFS.rename(tmpPath, CONFIG_FILE);
}
