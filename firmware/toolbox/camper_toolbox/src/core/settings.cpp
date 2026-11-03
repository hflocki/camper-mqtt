#include "settings.h"
#include "registry.h"
#include "util.h"
#include "wifi_manager.h"

#include <LittleFS.h>

static bool fsReady = false;

// ============================================================
// Core defaults / JSON
// ============================================================

static void setCoreDefaults() {
    memset(&config, 0, sizeof(config));
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
}

static void coreFromFile(JsonDocument& doc) {
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
    if (config.mqtt_prefix[0] == '\0') strlcpy(config.mqtt_prefix, DEFAULT_MQTT_PREFIX, sizeof(config.mqtt_prefix));
}

static void coreToFile(JsonDocument& doc) {
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
}

void coreConfigToJson(JsonObject o) {
    // Passwords are never sent back to the browser
    o["wifi_ssid"] = (const char*)config.wifi_ssid;
    o["wifi_pass_set"] = config.wifi_pass[0] != '\0';
    o["ap_ssid"] = (const char*)config.ap_ssid;
    o["wifi_tx_power"] = config.wifi_tx_power;
    o["ap_always"] = config.ap_always;
    o["mqtt_server"] = (const char*)config.mqtt_server;
    o["mqtt_port"] = config.mqtt_port;
    o["mqtt_user"] = (const char*)config.mqtt_user;
    o["mqtt_pass_set"] = config.mqtt_pass[0] != '\0';
    o["mqtt_prefix"] = (const char*)config.mqtt_prefix;
    o["web_user"] = (const char*)config.web_user;
    o["web_pass_set"] = config.web_pass[0] != '\0';
}

// Strings: max length check. Passwords: empty input keeps the stored one.
static const char* checkStr(JsonObjectConst in, const char* key, size_t size, const char* cur, bool& changed) {
    JsonVariantConst v = in[key];
    if (v.isNull()) return nullptr;
    if (!v.is<const char*>()) return "Ungültiger Text";
    const char* s = v.as<const char*>();
    if (strlen(s) >= size) return "Text zu lang";
    if (strcmp(s, cur) != 0) changed = true;
    return nullptr;
}

const char* coreConfigCheck(JsonObjectConst in, bool& restart) {
    bool ch = false;
    const char* e = nullptr;
    if ((e = checkStr(in, "wifi_ssid", sizeof(config.wifi_ssid), config.wifi_ssid, ch))) return e;
    if ((e = checkStr(in, "ap_ssid", sizeof(config.ap_ssid), config.ap_ssid, ch))) return e;
    if ((e = checkStr(in, "mqtt_server", sizeof(config.mqtt_server), config.mqtt_server, ch))) return e;
    if ((e = checkStr(in, "mqtt_user", sizeof(config.mqtt_user), config.mqtt_user, ch))) return e;
    if ((e = checkStr(in, "mqtt_prefix", sizeof(config.mqtt_prefix), config.mqtt_prefix, ch))) return e;
    bool dummy = false;
    if ((e = checkStr(in, "web_user", sizeof(config.web_user), config.web_user, dummy))) return e;
    if ((e = checkStr(in, "web_pass", sizeof(config.web_pass), "", dummy))) return e;
    if ((e = checkStr(in, "wifi_pass", sizeof(config.wifi_pass), "", ch))) return e;
    if ((e = checkStr(in, "mqtt_pass", sizeof(config.mqtt_pass), "", ch))) return e;
    JsonVariantConst ap = in["ap_pass"];
    if (ap.is<const char*>()) {
        size_t n = strlen(ap.as<const char*>());
        if (n > 0 && (n < 8 || n >= sizeof(config.ap_pass))) return "AP-Passwort: 8 bis 31 Zeichen";
        if (n > 0) ch = true;
    }
    if (in["mqtt_prefix"].is<const char*>() && in["mqtt_prefix"].as<const char*>()[0] == '\0') return "MQTT-Präfix fehlt";
    if (!in["mqtt_port"].isNull()) {
        int port = in["mqtt_port"] | -1;
        if (port < 1 || port > 65535) return "MQTT-Port 1-65535";
        if (port != config.mqtt_port) ch = true;
    }
    if (!in["wifi_tx_power"].isNull()) {
        int tx = in["wifi_tx_power"] | -1;
        if (!isValidTxPower(tx)) return "Ungültige Sendeleistung";
        if (tx != config.wifi_tx_power) ch = true;
    }
    if (in["ap_always"].is<bool>() && in["ap_always"].as<bool>() != config.ap_always) ch = true;
    restart = ch;
    return nullptr;
}

static void applyStr(JsonObjectConst in, const char* key, char* dst, size_t size, bool keepIfEmpty) {
    JsonVariantConst v = in[key];
    if (!v.is<const char*>()) return;
    const char* s = v.as<const char*>();
    if (keepIfEmpty && s[0] == '\0') return;
    strlcpy(dst, s, size);
}

void coreConfigApply(JsonObjectConst in) {
    applyStr(in, "wifi_ssid", config.wifi_ssid, sizeof(config.wifi_ssid), false);
    applyStr(in, "wifi_pass", config.wifi_pass, sizeof(config.wifi_pass), true);
    applyStr(in, "ap_ssid", config.ap_ssid, sizeof(config.ap_ssid), true);
    applyStr(in, "ap_pass", config.ap_pass, sizeof(config.ap_pass), true);
    applyStr(in, "mqtt_server", config.mqtt_server, sizeof(config.mqtt_server), false);
    applyStr(in, "mqtt_user", config.mqtt_user, sizeof(config.mqtt_user), false);
    applyStr(in, "mqtt_pass", config.mqtt_pass, sizeof(config.mqtt_pass), true);
    applyStr(in, "mqtt_prefix", config.mqtt_prefix, sizeof(config.mqtt_prefix), true);
    if (!in["mqtt_port"].isNull()) config.mqtt_port = in["mqtt_port"] | DEFAULT_MQTT_PORT;
    if (!in["wifi_tx_power"].isNull()) config.wifi_tx_power = (int8_t)(in["wifi_tx_power"] | DEFAULT_TX_POWER);
    if (in["ap_always"].is<bool>()) config.ap_always = in["ap_always"].as<bool>();
    // Web login: empty user removes the login; empty password keeps the old one
    if (in["web_user"].is<const char*>()) {
        applyStr(in, "web_user", config.web_user, sizeof(config.web_user), false);
        applyStr(in, "web_pass", config.web_pass, sizeof(config.web_pass), true);
        if (config.web_user[0] == '\0') config.web_pass[0] = '\0';
    }
    if (in["wifi_ssid"].is<const char*>() && config.wifi_ssid[0] == '\0') config.wifi_pass[0] = '\0';
}

// ============================================================
// Plugin fields
// ============================================================

static const char* const FIELD_TYPES[] = {"text", "mac", "float", "int", "bool", "select"};

void pluginSchemaToJson(Plugin* p, JsonArray arr) {
    const Field* f = nullptr;
    size_t n = p->fields(f);
    for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["k"] = f[i].key;
        o["l"] = f[i].label;
        o["t"] = FIELD_TYPES[f[i].type];
        if (f[i].type == F_FLOAT || f[i].type == F_INT) {
            o["min"] = f[i].min;
            o["max"] = f[i].max;
            o["step"] = f[i].step;
        }
        if (f[i].type == F_TEXT) o["max"] = f[i].max > 0 ? (int)f[i].max : (int)f[i].size - 1;
        if (f[i].unit) o["u"] = f[i].unit;
        if (f[i].options) o["o"] = f[i].options;
        if (f[i].help) o["h"] = f[i].help;
    }
}

void pluginConfigToJson(Plugin* p, JsonObject o) {
    o["enabled"] = p->enabled;
    o["name"] = (const char*)p->name;
    const Field* f = nullptr;
    size_t n = p->fields(f);
    uint8_t* base = (uint8_t*)p->cfg();
    if (!base) return;
    for (size_t i = 0; i < n; i++) {
        void* m = base + f[i].offset;
        switch (f[i].type) {
            case F_TEXT:
            case F_MAC:    o[f[i].key] = (const char*)m; break;
            case F_FLOAT:  o[f[i].key] = *(float*)m; break;
            case F_INT:    o[f[i].key] = *(int32_t*)m; break;
            case F_BOOL:   o[f[i].key] = *(bool*)m; break;
            case F_SELECT: o[f[i].key] = *(uint8_t*)m; break;
        }
    }
}

static uint8_t optionCount(const char* opts) {
    if (!opts || !opts[0]) return 0;
    uint8_t n = 1;
    for (const char* c = opts; *c; c++) if (*c == '|') n++;
    return n;
}

const char* pluginConfigFromJson(Plugin* p, JsonObjectConst in, bool write, bool& restart) {
    static char err[64];
    restart = false;

    JsonVariantConst en = in["enabled"];
    if (en.is<bool>()) {
        if (en.as<bool>() != p->enabled) restart = true;
        if (write) p->enabled = en.as<bool>();
    }
    JsonVariantConst nm = in["name"];
    if (nm.is<const char*>()) {
        const char* s = nm.as<const char*>();
        if (strlen(s) >= sizeof(p->name)) return "Name zu lang";
        if (write && s[0]) strlcpy(p->name, s, sizeof(p->name));
    }

    const Field* f = nullptr;
    size_t n = p->fields(f);
    uint8_t* base = (uint8_t*)p->cfg();
    if (!base) return nullptr;

    for (size_t i = 0; i < n; i++) {
        JsonVariantConst v = in[f[i].key];
        if (v.isNull()) continue;
        void* m = base + f[i].offset;
        switch (f[i].type) {
            case F_TEXT: {
                if (!v.is<const char*>()) goto bad;
                const char* s = v.as<const char*>();
                size_t maxLen = f[i].max > 0 ? (size_t)f[i].max : (size_t)f[i].size - 1;
                if (strlen(s) > maxLen) goto bad;
                if (write) strlcpy((char*)m, s, f[i].size);
                break;
            }
            case F_MAC: {
                char mac[18];
                if (!v.is<const char*>() || !normalizeMac(v.as<const char*>(), mac, sizeof(mac))) goto bad;
                if (write) strlcpy((char*)m, mac, f[i].size);
                break;
            }
            case F_FLOAT: {
                if (!v.is<float>() && !v.is<int>()) goto bad;
                float x = v.as<float>();
                if (isnan(x) || x < f[i].min || x > f[i].max) goto bad;
                if (write) *(float*)m = x;
                break;
            }
            case F_INT: {
                if (!v.is<int>() && !v.is<float>()) goto bad;
                long x = lroundf(v.as<float>());
                if (x < f[i].min || x > f[i].max) goto bad;
                if (write) *(int32_t*)m = (int32_t)x;
                break;
            }
            case F_BOOL:
                if (!v.is<bool>()) goto bad;
                if (write) *(bool*)m = v.as<bool>();
                break;
            case F_SELECT: {
                if (!v.is<int>()) goto bad;
                int x = v.as<int>();
                if (x < 0 || x >= optionCount(f[i].options)) goto bad;
                if (write) *(uint8_t*)m = (uint8_t)x;
                break;
            }
        }
        continue;
    bad:
        snprintf(err, sizeof(err), "Ungültiger Wert: %s", f[i].label);
        return err;
    }
    return nullptr;
}

// ============================================================
// Persistence
// ============================================================

static void loadConfig() {
    setCoreDefaults();
    if (!fsReady || !LittleFS.exists(CONFIG_FILE)) return;

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.printf("[Config] Parse error (%s) - using defaults\n", err.c_str());
        return;
    }
    coreFromFile(doc);

    // Plugins: anything missing or invalid keeps its factory value
    JsonObjectConst all = doc["plugins"].as<JsonObjectConst>();
    FOR_EACH_PLUGIN(p) {
        JsonObjectConst pc = all[p->id].as<JsonObjectConst>();
        if (pc.isNull()) continue;
        bool restart;
        const Field* f = nullptr;
        size_t n = p->fields(f);
        // Apply field by field so one invalid stored value does not discard the others
        if (pluginConfigFromJson(p, pc, false, restart) == nullptr) {
            pluginConfigFromJson(p, pc, true, restart);
        } else {
            JsonDocument one;
            for (size_t i = 0; i < n; i++) {
                one.clear();
                one[f[i].key] = pc[f[i].key];
                pluginConfigFromJson(p, one.as<JsonObjectConst>(), true, restart);
            }
            one.clear();
            one["enabled"] = pc["enabled"];
            one["name"] = pc["name"];
            pluginConfigFromJson(p, one.as<JsonObjectConst>(), true, restart);
        }
    }
}

void saveConfig() {
    if (!fsReady) return;
    JsonDocument doc;
    coreToFile(doc);
    JsonObject all = doc["plugins"].to<JsonObject>();
    FOR_EACH_PLUGIN(p) {
        pluginConfigToJson(p, all[p->id].to<JsonObject>());
    }
    File file = LittleFS.open(CONFIG_FILE ".tmp", "w");
    if (!file) {
        Serial.println("[Config] Save failed");
        return;
    }
    serializeJson(doc, file);
    file.close();
    LittleFS.remove(CONFIG_FILE);
    LittleFS.rename(CONFIG_FILE ".tmp", CONFIG_FILE);
    Serial.println("[Config] Saved");
}

bool settingsInit() {
    fsReady = LittleFS.begin(true);
    if (!fsReady) Serial.println("[Config] LittleFS mount failed - running with defaults");
    registryCreate();
    loadConfig();
    if (fsReady && !LittleFS.exists(CONFIG_FILE)) saveConfig();
    FOR_EACH_PLUGIN(p) p->onConfigChanged();
    return fsReady;
}

bool settingsFsReady() {
    return fsReady;
}

void settingsFactoryReset() {
    if (fsReady) LittleFS.remove(CONFIG_FILE);
}
