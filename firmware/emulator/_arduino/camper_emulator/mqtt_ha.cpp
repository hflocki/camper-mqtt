#include "mqtt_ha.h"
#include "wifi_manager.h"
#include "emu.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <map>

// Broker connections of the emulator:
//   conn[D_TANK .. D_BATTERY]  one per emulated device, client id and last will of its own,
//                              so the display sees each device go online / offline separately
//   conn[SELF]                 the emulator itself (<prefix>/status, <prefix>/info), also sends
//                              the router and Zigbee topics and receives the Zigbee commands
//
// Every second all enabled devices are built (emu.cpp) and only changed payloads are sent;
// everything again every MQTT_REPUBLISH_MS. Topics a device no longer sends (sensor switched
// off, id or base topic changed, device "off") are deleted on the broker with an empty
// retained message. Home Assistant discovery is left out on purpose: emulated devices must
// not create entities next to the real ones.

static const uint8_t SELF = D_COUNT;
static const uint8_t CONN_COUNT = D_COUNT + 1;

struct Conn {
    WiFiClient net;
    PubSubClient mqtt;
    uint32_t lastTry = 0;
    uint32_t lastInfo = 0;
    bool force = true;                     // send everything on the next round
    bool active = false;                   // wanted connection is up (for the web UI)
    String prefix;                         // base topic the connection was opened with
    std::map<String, String> sent;         // retained topic -> last payload
};

static Conn conns[CONN_COUNT];
static uint8_t applied[D_COUNT];           // mode the connection layer has carried out
static uint32_t lastRound = 0, lastFull = 0;
static uint8_t nextTry = 0;
static bool started = false;

static uint8_t connOf(uint8_t dev) { return EMU_DEVS[dev].own ? dev : SELF; }

static String selfPrefix() { return String(config.mqtt_prefix); }

static String connPrefix(uint8_t c) { return c == SELF ? selfPrefix() : emuPrefix(c); }

// ---- publishing with change tracking ----

struct RoundCtx {
    Conn* c;
    bool force;
    std::map<String, bool>* seen;
};

static void emitTracked(const String& topic, const String& payload, void* vctx) {
    RoundCtx* r = (RoundCtx*)vctx;
    if (topic.length() == 0) return;
    (*r->seen)[topic] = true;
    auto it = r->c->sent.find(topic);
    if (!r->force && it != r->c->sent.end() && it->second == payload) return;
    if (r->c->mqtt.publish(topic.c_str(), payload.c_str(), true)) r->c->sent[topic] = payload;
}

// Empty retained message = delete the topic on the broker. Sent over any live connection.
static Conn* anyLive() {
    if (conns[SELF].mqtt.connected()) return &conns[SELF];
    for (auto& c : conns) if (c.mqtt.connected()) return &c;
    return nullptr;
}

static void dropTopics(Conn& c, std::map<String, bool>* keep) {
    Conn* via = anyLive();
    for (auto it = c.sent.begin(); it != c.sent.end();) {
        if (keep && keep->count(it->first)) { ++it; continue; }
        if (via) via->mqtt.publish(it->first.c_str(), "", true);
        it = c.sent.erase(it);
    }
}

static void collectTopic(const String& topic, const String&, void* ctx) {
    (*(std::map<String, bool>*)ctx)[topic] = true;
}

static String infoJson(uint8_t c) {
    // same keys as devinfo.* of the real devices; the MAC is made up per emulated device
    char mac[18];
    uint8_t m[6];
    WiFi.macAddress(m);
    snprintf(mac, sizeof(mac), "02:%02X:%02X:%02X:EE:%02X", m[3], m[4], m[5], c);
    String s = "{\"version\":\"emu-" FW_VERSION "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"mac\":\"" + mac +
               "\",\"rssi\":" + String(WiFi.RSSI()) + ",\"uptime\":" + String(millis() / 1000) +
               ",\"reset\":\"" + resetReasonString() + "\",\"heap\":" + String(ESP.getFreeHeap()) + "}";
    return s;
}

static void onMessage(char* topic, byte* payload, unsigned int len) {
    Serial.printf("[MQTT] < %s = %.*s\n", topic, (int)min(len, 80u), (const char*)payload);
    emuCommand(topic, (const char*)payload, len);
}

static void sendOnce(const String& topic, const String& payload) {
    if (conns[SELF].mqtt.connected()) conns[SELF].mqtt.publish(topic.c_str(), payload.c_str(), false);
    Serial.printf("[MQTT] > %s = %s\n", topic.c_str(), payload.c_str());
}

// ---- connections ----

static bool wanted(uint8_t c) {
    if (c == SELF) return true;
    return EMU_DEVS[c].own && emuMode(c) == M_ON;   // router / Zigbee have no connection of their own
}

static bool tryConnect(uint8_t c) {
    Conn& k = conns[c];
    k.prefix = connPrefix(c);
    String id = String("camper-emu-") + (c == SELF ? "self" : EMU_DEVS[c].id) + "-" + WiFi.macAddress().substring(12);
    id.replace(":", "");
    String will = k.prefix + "/status";
    const char* user = strlen(config.mqtt_user) ? config.mqtt_user : nullptr;
    const char* pass = user ? config.mqtt_pass : nullptr;
    if (!k.mqtt.connect(id.c_str(), user, pass, will.c_str(), 1, true, "offline")) {
        Serial.printf("[MQTT] %s: connect failed, rc=%d\n", id.c_str(), k.mqtt.state());
        return false;
    }
    Serial.printf("[MQTT] %s connected\n", id.c_str());
    if (c == SELF) {
        k.mqtt.subscribe(emuZigbeeCmdFilter().c_str());
    } else {
        k.mqtt.subscribe((k.prefix + "/cmd/#").c_str());
        k.mqtt.subscribe((k.prefix + "/set/#").c_str());
    }
    k.force = true;
    k.lastInfo = 0;
    return true;
}

void mqttInit() {
    for (auto& c : conns) {
        c.mqtt.setClient(c.net);
        c.mqtt.setBufferSize(1280);
        c.mqtt.setKeepAlive(30);
        c.mqtt.setSocketTimeout(3);
        c.mqtt.setCallback(onMessage);
    }
    for (int d = 0; d < D_COUNT; d++) applied[d] = M_ON;
    emuSetSender(sendOnce);
    if (strlen(config.mqtt_server) == 0) {
        Serial.println("[MQTT] No server configured");
        return;
    }
    for (auto& c : conns) c.mqtt.setServer(config.mqtt_server, config.mqtt_port);
    started = true;
    Serial.printf("[MQTT] Broker %s:%d, %d connections\n", config.mqtt_server, config.mqtt_port, CONN_COUNT);
}

// Carries out mode changes from the web UI or a scenario
static void applyModes() {
    if (!conns[SELF].mqtt.connected()) return;   // deleting topics needs the broker
    for (uint8_t d = 0; d < D_COUNT; d++) {
        uint8_t m = emuMode(d);
        if (m == applied[d]) continue;
        uint8_t c = connOf(d);
        Conn& k = conns[c];
        if (m == M_OFF) {
            if (c != SELF) {
                dropTopics(k, nullptr);   // values, status and info of this device
                k.mqtt.disconnect();      // clean: the broker does not send the will
            } else {
                // router / Zigbee share the own connection: delete only their topics
                std::map<String, bool> mine;
                emuBuild(d, collectTopic, &mine);
                Conn* via = anyLive();
                for (auto& kv : mine) {
                    if (via) via->mqtt.publish(kv.first.c_str(), "", true);
                    k.sent.erase(kv.first);
                }
            }
            Serial.printf("[EMU] %s off, topics deleted\n", EMU_DEVS[d].id);
        } else if (m == M_OFFLINE) {
            if (c != SELF) {
                // drop the TCP connection without DISCONNECT: the broker publishes the last will
                k.net.stop();
                Serial.printf("[EMU] %s offline (connection dropped, last will)\n", EMU_DEVS[d].id);
            }
        } else if (m == M_ON) {
            k.force = true;
            Serial.printf("[EMU] %s online\n", EMU_DEVS[d].id);
        }
        applied[d] = m;
    }
}

static void publishRound(bool full) {
    for (uint8_t c = 0; c < CONN_COUNT; c++) {
        Conn& k = conns[c];
        if (!k.mqtt.connected()) continue;
        bool force = full || k.force;
        k.force = false;
        std::map<String, bool> seen;
        RoundCtx r{&k, force, &seen};
        String p = k.prefix;
        if (c == SELF) {
            for (uint8_t d = D_ROUTER; d < D_COUNT; d++)
                if (emuMode(d) == M_ON) emuBuild(d, emitTracked, &r);
                else if (emuMode(d) == M_OFFLINE)   // paused: keep its topics as they are
                    emuBuild(d, collectTopic, &seen);
        } else {
            emuBuild(c, emitTracked, &r);
        }
        emitTracked(p + "/status", "online", &r);
        // signal and diagnostics once a minute (the WiFi RSSI changes all the time)
        if (k.lastInfo == 0 || millis() - k.lastInfo >= 60000) {
            k.lastInfo = millis();
            emitTracked(p + "/rssi", String(WiFi.RSSI()), &r);
            emitTracked(p + "/info", infoJson(c), &r);
        } else {
            seen[p + "/rssi"] = true;
            seen[p + "/info"] = true;
        }
        dropTopics(k, &seen);   // what this device did not send this round is gone
    }
}

void mqttLoop() {
    if (!started || !isConnected()) return;

    if (emuTakeClearAll()) {
        // delete everything the emulator knows it sent, then start over
        for (auto& k : conns) dropTopics(k, nullptr);
        for (auto& k : conns) k.force = true;
        Serial.println("[EMU] All retained topics deleted");
    }

    // base topic / id changed: the stale topics go in the next round; a device connection
    // reconnects so its last will and subscriptions use the new prefix
    for (uint8_t d = 0; d < D_COUNT; d++) {
        if (!emuTakeTopicChange(d)) continue;
        uint8_t c = connOf(d);
        if (c == SELF) {
            conns[SELF].mqtt.disconnect();   // resubscribe the Zigbee commands
            continue;
        }
        if (conns[c].mqtt.connected() && connPrefix(c) != conns[c].prefix) {
            Conn& k = conns[c];
            Conn* via = anyLive();
            if (via) {
                // old topics of this device incl. status and info
                for (auto& kv : k.sent) via->mqtt.publish(kv.first.c_str(), "", true);
            }
            k.sent.clear();
            k.mqtt.disconnect();
        }
    }

    applyModes();

    for (auto& k : conns) if (k.mqtt.connected()) k.mqtt.loop();

    // reconnect: one connection per pass, the own one first (broker reachable at all?)
    uint32_t now = millis();
    for (uint8_t n = 0; n < CONN_COUNT; n++) {
        uint8_t c = (n == 0) ? SELF : (uint8_t)((nextTry + n - 1) % D_COUNT);
        Conn& k = conns[c];
        k.active = k.mqtt.connected();
        if (k.active || !wanted(c)) continue;
        if (c != SELF && !conns[SELF].mqtt.connected()) continue;
        if (now - k.lastTry < 3000) continue;
        k.lastTry = now;
        nextTry = (c + 1) % D_COUNT;
        tryConnect(c);
        break;
    }

    if (now - lastRound >= 1000) {
        lastRound = now;
        bool full = now - lastFull >= MQTT_REPUBLISH_MS;
        if (full) lastFull = now;
        publishRound(full);
    }
}

bool mqttIsConnected() { return conns[SELF].mqtt.connected(); }

uint8_t mqttConnState(uint8_t dev) {
    if (dev >= D_COUNT) return 0;
    return conns[connOf(dev)].mqtt.connected() ? 1 : 0;
}

void mqttRequestDiscovery() {}
void mqttPublishDiscovery() {}
