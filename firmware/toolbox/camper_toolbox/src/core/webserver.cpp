#include "webserver.h"
#include "wifi_manager.h"
#include "mqtt_ha.h"
#include "settings.h"
#include "registry.h"
#include "ble.h"
#include "../../fwcheck.h"
#include "../../evlog.h"
#include "../../safeguard.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// All web handlers run in the async web server task. They never change plugin state
// directly: settings and commands are validated there and queued, loop() applies them.

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static unsigned long lastWsUpdate = 0;
static volatile bool wsSendNow = false;

static bool restartPending = false;
static unsigned long restartAt = 0;
static volatile bool restartRequested = false;
static volatile bool factoryRequested = false;

// ---- Upload (web UI): firmware .bin or the web page index.html.gz --------------
// A complete LittleFS image is NOT accepted here: it would also replace /config.json.
static bool upAuthorized = false;
static bool upWebPage = false;
static bool upFailed = false;
static File upFile;
static const char* WEB_FILE = "/index.html.gz";
static const char* WEB_TMP = "/index.html.gz.tmp";

static const size_t MAX_BODY_SIZE = 4096;

// Queued settings (one at a time) and commands
static SemaphoreHandle_t pendingLock = nullptr;
static String pendingConfig;
static bool pendingRestart = false;

struct WebCommand {
    char id[16];
    char key[24];
    char value[48];
};
static QueueHandle_t cmdQueue = nullptr;

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

static void sendError(AsyncWebServerRequest* request, int code, const char* msg) {
    JsonDocument doc;
    doc["error"] = msg;
    String s;
    serializeJson(doc, s);
    request->send(code, "application/json", s);
}

// Collects a request body; returns true once it is complete (body in out)
static bool collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total,
                        String& body, bool& rejected) {
    if (index == 0) {
        body = "";
        rejected = (total > MAX_BODY_SIZE);
    }
    if (rejected) {
        if (index + len == total) sendError(request, 413, "Body too large");
        return false;
    }
    body += String((const char*)data, len);
    return index + len == total;
}

// ============================================================
// REST API
// ============================================================

static void handleGetConfig(AsyncWebServerRequest* request) {
    if (!auth(request)) return;
    JsonDocument doc;
    doc["version"] = FW_VERSION;
    doc["device"] = DEVICE_NAME;
    doc["reset_reason"] = resetReasonString();
    doc["ha_node"] = mqttNodeId();
    coreConfigToJson(doc["core"].to<JsonObject>());

    JsonArray arr = doc["plugins"].to<JsonArray>();
    FOR_EACH_PLUGIN(p) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = (const char*)p->id;
        o["type"] = p->type;
        o["type_name"] = p->typeName;
        o["ble"] = p->bleUse();
        pluginSchemaToJson(p, o["fields"].to<JsonArray>());
        pluginConfigToJson(p, o["cfg"].to<JsonObject>());
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

// Body: {"core": {...}} or {"plugin": "<id>", "cfg": {...}}
static void handleSaveConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    static String body;
    static bool rejected = false;
    if (!collectBody(request, data, len, index, total, body, rejected)) return;

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        sendError(request, 400, "Invalid JSON");
        return;
    }
    bool restart = false;
    const char* err = nullptr;
    if (doc["core"].is<JsonObject>()) {
        err = coreConfigCheck(doc["core"].as<JsonObjectConst>(), restart);
    } else if (doc["enabled"].is<JsonObject>()) {
        // System tab, "Plugins": {"enabled": {"<plugin id>": true / false, ...}}
        for (JsonPairConst kv : doc["enabled"].as<JsonObjectConst>()) {
            Plugin* p = pluginById(kv.key().c_str());
            if (!p) { err = "Unbekanntes Plugin"; break; }
            if (!kv.value().is<bool>()) { err = "Ungültiger Wert"; break; }
            if (kv.value().as<bool>() != p->enabled) restart = true;
        }
    } else if (doc["plugin"].is<const char*>()) {
        Plugin* p = pluginById(doc["plugin"].as<const char*>());
        if (!p) err = "Unbekanntes Plugin";
        else err = pluginConfigFromJson(p, doc["cfg"].as<JsonObjectConst>(), false, restart);
    } else {
        err = "Nothing to save";
    }
    if (err) {
        sendError(request, 400, err);
        return;
    }

    bool busy = false;
    xSemaphoreTake(pendingLock, portMAX_DELAY);
    if (pendingConfig.length()) busy = true;
    else { pendingConfig = body; pendingRestart = restart; }
    xSemaphoreGive(pendingLock);
    if (busy) {
        sendError(request, 503, "Busy, try again");
        return;
    }
    request->send(200, "application/json", restart ? "{\"status\":\"ok\",\"restart\":true}" : "{\"status\":\"ok\"}");
}

// Body: {"p": "<plugin id>", "k": "<key>", "v": "<value>"} - same keys / values as MQTT
static void handleCommand(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    static String body;
    static bool rejected = false;
    if (!collectBody(request, data, len, index, total, body, rejected)) return;

    JsonDocument doc;
    if (deserializeJson(doc, body) || !doc["p"].is<const char*>() || !doc["k"].is<const char*>()) {
        sendError(request, 400, "Invalid command");
        return;
    }
    WebCommand c;
    strlcpy(c.id, doc["p"].as<const char*>(), sizeof(c.id));
    strlcpy(c.key, doc["k"].as<const char*>(), sizeof(c.key));
    strlcpy(c.value, doc["v"] | "", sizeof(c.value));   // The web UI always sends strings
    if (xQueueSend(cmdQueue, &c, 0) != pdTRUE) {
        sendError(request, 503, "Busy, try again");
        return;
    }
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// ============================================================
// Live data
// ============================================================

void buildLiveJson(JsonDocument& doc) {
    doc["rssi"] = coreData.rssi;
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    doc["wifi_drops"] = coreData.wifi_drops;
    doc["wifi_reason"] = (const char*)coreData.wifi_reason;
    doc["wifi"] = getWiFiStateString();
    doc["mqtt"] = mqttIsConnected();
    if (bleActive()) {
        JsonObject b = doc["ble"].to<JsonObject>();
        b["adv"] = bleAdvCount();
        b["scanning"] = bleScanning();
    }
    JsonArray arr = doc["plugins"].to<JsonArray>();
    FOR_EACH_PLUGIN(p) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = (const char*)p->id;
        o["name"] = (const char*)p->name;
        o["type"] = p->type;
        o["enabled"] = p->enabled;
        if (p->enabled) p->live(o);
    }
}

static void wsSendLive() {
    JsonDocument doc;
    buildLiveJson(doc);
    String s;
    serializeJson(doc, s);
    ws.textAll(s);
}

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) wsSendNow = true;   // Built in loop(): plugins are not thread safe
}

// ============================================================
// Setup / loop
// ============================================================

void webserverInit() {
    pendingLock = xSemaphoreCreateMutex();
    cmdQueue = xQueueCreate(8, sizeof(WebCommand));

    ws.onEvent(onWsEvent);
    // Browsers send the basic auth credentials of the page along with the WebSocket upgrade
    ws.handleHandshake([](AsyncWebServerRequest* request) { return authOk(request); });
    server.addHandler(&ws);

    // Serve ONLY index.html. Serving the whole filesystem would expose /config.json
    // (WiFi and MQTT credentials) to everybody in the network.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        if (settingsFsReady() && LittleFS.exists(WEB_FILE)) {
            AsyncWebServerResponse* response = request->beginResponse(LittleFS, WEB_FILE, "text/html");
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
        } else {
            request->send(200, "text/html",
                "<h1>Camper Toolbox</h1><p>Web page missing. Upload <code>data/index.html.gz</code> "
                "or a firmware (.bin):</p><form method=POST action=/api/update enctype=multipart/form-data>"
                "<input type=file name=f accept='.bin,.gz'> <button>Upload</button></form>");
        }
    });

    server.on("/api/config", HTTP_GET, handleGetConfig);
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL,
              [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                  // Checked per chunk: the body handler runs before the request handler
                  if (!authOk(request)) {
                      if (index + len == total) request->requestAuthentication();
                      return;
                  }
                  handleSaveConfig(request, data, len, index, total);
              });
    server.on("/api/cmd", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL,
              [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                  if (!authOk(request)) {
                      if (index + len == total) request->requestAuthentication();
                      return;
                  }
                  handleCommand(request, data, len, index, total);
              });

    // Firmware info for the update check in the browser, and the event log
    server.on("/api/system", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        JsonDocument d;
        fwcheck::info(d["fw"].to<JsonObject>());
        evlog::json(d["log"].to<JsonArray>());
        d["up"] = millis() / 1000;
        String out;
        serializeJson(d, out);
        request->send(200, "application/json", out);
    });

    // Firmware (.bin) or web page (.gz) upload; the file name decides
    server.on("/api/update", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (!auth(request)) return;
            if (!upAuthorized) { request->send(401, "text/plain", "Login fehlt"); return; }
            if (upWebPage) {
                if (upFailed) request->send(500, "text/plain", "Web-Oberfläche nicht gespeichert (LittleFS voll oder nicht bereit)");
                else request->send(200, "text/plain", "OK web");
                return;
            }
            // firmware: checked and written by fwcheck
            if (!fwcheck::ok()) { request->send(400, "text/plain", "Update abgelehnt: " + fwcheck::error()); return; }
            guard::otaDone();
            request->send(200, "text/plain", "OK");
            restartRequested = true;
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
                    upFile = settingsFsReady() ? LittleFS.open(WEB_TMP, "w") : File();
                    if (!upFile) upFailed = true;
                } else {
                    fwcheck::begin(request->contentLength(), request->hasParam("force"));
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
                    if (upFailed) evlog::add("Web-Oberfläche nicht gespeichert");
                    else evlog::add("Web-Oberfläche ersetzt (%u kB)", (unsigned)((index + len) / 1024));
                }
                return;
            }
            fwcheck::chunk(data, len, final);
        });

    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        evlog::add("Neustart über die Web-Oberfläche");
        request->send(200, "application/json", "{\"status\":\"restarting\"}");
        restartRequested = true;   // Do not block the async web task
    });

    server.on("/api/factory", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        evlog::add("Werkseinstellungen über die Web-Oberfläche");
        request->send(200, "application/json", "{\"status\":\"restarting\"}");
        factoryRequested = true;
    });

    // Nearby BLE devices (for picking device addresses in the web UI)
    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!auth(request)) return;
        JsonDocument doc;
        doc["active"] = bleActive();
        bleFillSeenList(doc["devices"].to<JsonArray>());
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
}

void webserverStart() {
    server.begin();
    Serial.println("[Web] Server started on port 80");
}

static void applyPendingConfig() {
    String body;
    bool restart = false;
    xSemaphoreTake(pendingLock, portMAX_DELAY);
    body = pendingConfig;
    restart = pendingRestart;
    pendingConfig = "";
    xSemaphoreGive(pendingLock);
    if (!body.length()) return;

    JsonDocument doc;
    if (deserializeJson(doc, body)) return;
    if (doc["core"].is<JsonObject>()) {
        JsonObjectConst core = doc["core"].as<JsonObjectConst>();
        // WiFi change while connected: keep the working values as fallback (safeguard.h)
        if (isConnected()) {
            const char* newSsid = core["wifi_ssid"].is<const char*>() ? core["wifi_ssid"].as<const char*>() : config.wifi_ssid;
            const char* newPass = config.wifi_pass;
            if (core["wifi_pass"].is<const char*>() && core["wifi_pass"].as<const char*>()[0]) newPass = core["wifi_pass"].as<const char*>();
            if (strcmp(newSsid, config.wifi_ssid) != 0 || strcmp(newPass, config.wifi_pass) != 0) {
                guard::wifiTrialStart(config.wifi_ssid, config.wifi_pass);
            }
        }
        coreConfigApply(core);
        evlog::add("Einstellungen gespeichert");
        Serial.println("[Web] Core settings changed");
    } else if (doc["enabled"].is<JsonObject>()) {
        // Takes effect after the restart (plugins are only started at boot)
        for (JsonPairConst kv : doc["enabled"].as<JsonObjectConst>()) {
            Plugin* p = pluginById(kv.key().c_str());
            if (p && kv.value().is<bool>()) p->enabled = kv.value().as<bool>();
        }
        evlog::add("Plugin-Auswahl geändert");
        Serial.println("[Web] Plugin selection changed");
    } else {
        Plugin* p = pluginById(doc["plugin"] | "");
        if (!p) return;
        bool r;
        pluginConfigFromJson(p, doc["cfg"].as<JsonObjectConst>(), true, r);
        if (p->enabled && !restart) p->onConfigChanged();
        evlog::add("Einstellungen von %s gespeichert", p->id);
        Serial.printf("[Web] Settings of %s changed\n", p->id);
        mqttRequestDiscovery();   // Name / entities may have changed
    }
    saveConfig();
    if (restart) {
        Serial.println("[Web] Restart for the new settings");
        restartPending = true;
        restartAt = millis() + 1000;
    }
}

void webserverLoop() {
    ws.cleanupClients(2);

    applyPendingConfig();

    WebCommand c;
    while (cmdQueue && xQueueReceive(cmdQueue, &c, 0) == pdTRUE) {
        Plugin* p = pluginById(c.id);
        if (p && p->enabled) {
            Serial.printf("[Web] Command %s/%s = %s\n", c.id, c.key, c.value);
            p->command(c.key, c.value);
            wsSendNow = true;
        }
    }

    if (factoryRequested) {
        factoryRequested = false;
        settingsFactoryReset();
        restartPending = true;
        restartAt = millis() + 500;
    }
    if (restartRequested) {
        restartRequested = false;
        restartPending = true;
        restartAt = millis() + 1000;
    }
    // Deferred restart: the HTTP response can be sent before the chip resets
    if (restartPending && (long)(millis() - restartAt) >= 0) ESP.restart();

    if (wsSendNow || millis() - lastWsUpdate > WS_UPDATE_MS) {
        wsSendNow = false;
        lastWsUpdate = millis();
        if (ws.count() > 0) wsSendLive();
    }
}
