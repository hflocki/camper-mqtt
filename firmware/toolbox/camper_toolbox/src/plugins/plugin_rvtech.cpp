// Plugin "rvtech": RV Tech roof hatch / skylight motor control via BLE connection.
// Ported from the Home Assistant integration rvtech-hacs (https://github.com/C64Axel/rvtech-hacs):
//   ASCII over a BLE UART bridge, characteristic FFE1 (notify + write without response)
//   send    "P_d=NNN\r\n"  target position (3 digits)
//   receive "P_a=NNN\r\n"  reported position (3 digits)
// Valid values 0-102; 102 is the code of the preset "Lüften" (vent).

#include "../core/plugin.h"

#if PLUGIN_RVTECH

#include "../core/ble_link.h"
#include <NimBLEDevice.h>

#define RV_MAX_POSITION 102
#define RV_SEND_RETRY_MS 1000

struct RvCfg {
    char mac[18];
    char pin[8];           // Optional pairing PIN (digits), empty = no pairing
};

static const Field RV_FIELDS[] = {
    FLD_MAC(RvCfg, mac, "mac", "Hauben-MAC", "Über \"Suchen\" aus der Liste wählen"),
    FLD_TEXT(RvCfg, pin, "pin", "PIN", "Nur Ziffern (1-6, auch 000000). Leer lassen, wenn keine nötig ist. "
                                       "Kein anderes Gerät (Handy) darf gleichzeitig verbunden sein."),
};

// Presets: key, name, value (same telegrams as the HA integration)
struct RvPreset {
    const char* key;
    const char* name;
    int value;
};
static const RvPreset RV_PRESETS[] = {
    {"auf", "Auf", 100},
    {"zu", "Zu", 0},
    {"regen", "Regen", 12},
    {"lueften", "Lüften", 102},
};

static const Entity RV_ENTITIES[] = {
    {E_COVER,  "cover",     nullptr,         nullptr, "window",          nullptr,       nullptr, 0, nullptr},
    {E_BUTTON, "auf",       "Auf",           nullptr, nullptr,           nullptr,       "mdi:window-open", 0, nullptr},
    {E_BUTTON, "zu",        "Zu",            nullptr, nullptr,           nullptr,       "mdi:window-closed", 0, nullptr},
    {E_BUTTON, "regen",     "Regen",         nullptr, nullptr,           nullptr,       "mdi:weather-rainy", 0, nullptr},
    {E_BUTTON, "lueften",   "Lüften",        nullptr, nullptr,           nullptr,       "mdi:fan", 0, nullptr},
    {E_SENSOR, "reported",  "Position (gemeldet)", nullptr, nullptr,     nullptr,       "mdi:numeric", EF_DIAG | EF_HIDDEN, nullptr},
    {E_BINARY, "connected", "Verbindung",    nullptr, "connectivity",    nullptr,       nullptr, EF_DIAG | EF_ALWAYS, nullptr},
    {E_SENSOR, "rssi",      "Signalstärke",  "dBm",   "signal_strength", "measurement", nullptr, EF_DIAG | EF_HIDDEN | EF_ALWAYS, nullptr},
    {E_SENSOR, "state",     "Status",        nullptr, nullptr,           nullptr,       "mdi:bluetooth-connect", EF_DIAG | EF_ALWAYS, nullptr},
    {E_SWITCH, "release",   "Bluetooth für App freigeben", nullptr, nullptr, nullptr,   "mdi:bluetooth-off", EF_CONFIG | EF_ALWAYS, nullptr},
};

class RvTechPlugin : public Plugin {
public:
    explicit RvTechPlugin(uint8_t index) : Plugin("rvtech", "RV Tech Dachhaube", "Dachhaube", index) {}

    size_t fields(const Field*& out) override {
        out = RV_FIELDS;
        return sizeof(RV_FIELDS) / sizeof(RV_FIELDS[0]);
    }
    void* cfg() override { return &c; }
    void setDefaults() override { memset(&c, 0, sizeof(c)); }
    void onConfigChanged() override {
        pinValue = blePinParse(c.pin);
        if (strcmp(c.mac, lastMac) != 0) {
            strlcpy(lastMac, c.mac, sizeof(lastMac));
            position = -1;
            target = -1;
            updates++;
        }
        if (begun) link.setTarget(c.mac, pinValue);
    }

    uint8_t bleUse() const override { return BLE_LINK; }
    const char* bleAddress() const override { return c.mac; }

    // Check BLE UUID advertising candidate (looking for UART service FFE0)
    bool bleCandidate(int32_t company, const char* uuid, const char* name) const override {
        return strstr(uuid, "ffe0") != nullptr;
    }

    // Update RSSI from advertising packets even when disconnected
    void onAdvertisement(const NimBLEAdvertisedDevice* dev, const char* addr) override {
        if (c.mac[0] && strcmp(addr, c.mac) == 0) advRssi = dev->getRSSI();
    }
    const char* manufacturer() const override { return "RV Tech"; }

    void begin() override {
        // Initialize BLE link using Service FFE0 and Characteristic FFE1
        link.begin(id, RVTECH_SERVICE_UUID, RVTECH_CHAR_UUID, RVTECH_CHAR_UUID);
        begun = true;
        link.setTarget(c.mac, pinValue);
    }

    void loop() override {
        link.loop();

        // New connection or disconnect: empty telegram buffer, publish the state right away
        if (link.changes != lastLinkChanges) {
            lastLinkChanges = link.changes;
            rxLen = 0;
            updates++;
        }

        // Process incoming BLE UART packets from ESP32 BLE task
        BlePacket pkt;
        while (link.receive(pkt)) feed(pkt.data, pkt.len);

        // Reset target position state if timeout reached
        if (target >= 0 && millis() - targetSince > RVTECH_TARGET_TIMEOUT_MS) {
            target = -1;
            updates++;
        }

        // Retry or queue outgoing write commands over BLE
        if (sendValue >= 0) {
            if (link.canWrite() && millis() >= sendNotBefore) {
                if (writeValue(sendValue)) {
                    setTarget(sendValue);
                    sendValue = -1;
                } else if (++sendAttempts >= 2) {
                    Serial.printf("[%s] Sending %d failed\n", id, sendValue);
                    sendValue = -1;
                } else {
                    sendNotBefore = millis() + RV_SEND_RETRY_MS;
                }
            } else if (millis() - sendQueued > RVTECH_SEND_WAIT_MS) {
                Serial.printf("[%s] Not connected - command %d dropped\n", id, sendValue);
                sendValue = -1;
                updates++;
            }
        }

        // Periodic RSSI publish rate limiter
        int rssi = link.connected() ? link.rssi() : advRssi;
        if (rssi != lastRssi && millis() - lastRssiPublish > 10000) {
            lastRssi = rssi;
            lastRssiPublish = millis();
            updates++;
        }
    }

    bool available() override {
        return link.connected();
    }

    void live(JsonObject o) override {
        o["state"] = link.state();
        o["ok"] = link.connected();
        JsonObject g = o["gauge"].to<JsonObject>();
        if (position >= 0) g["v"] = displayPosition();
        else g["v"] = nullptr;
        g["unit"] = "%";
        static const char* const LABELS[][2] = {
            {"open", "Offen"}, {"closed", "Geschlossen"}, {"opening", "Öffnet..."}, {"closing", "Schließt..."},
        };
        const char* st = coverState();
        g["label"] = position == RV_MAX_POSITION ? "Lüften" : "Unbekannt";
        for (const auto& l : LABELS) if (!strcmp(st, l[0]) && position != RV_MAX_POSITION) g["label"] = l[1];
        liveRowNum(o, "Gemeldet", position >= 0 ? (float)position : NAN, 0, "");
        liveRowNum(o, "Ziel", target >= 0 ? (float)target : NAN, 0, "");
        int rssi = link.connected() ? link.rssi() : advRssi;
        liveRowNum(o, "Bluetooth", rssi ? (float)rssi : NAN, 0, "dBm");

        JsonObject b = liveCtrl(o, "buttons", "preset", nullptr);
        JsonArray arr = b["b"].to<JsonArray>();
        for (const RvPreset& p : RV_PRESETS) {
            JsonArray x = arr.add<JsonArray>();
            x.add(p.key);
            x.add(p.name);
        }
        JsonObject s = liveCtrl(o, "slider", "cover_position", "Position");
        s["min"] = 0;
        s["max"] = 100;
        s["v"] = position >= 0 ? displayPosition() : 0;
        if (link.lastError()) liveRowText(o, "Letzter BT-Fehler", link.lastErrorText());
        liveCtrl(o, "switch", "release", "Bluetooth für App freigeben")["v"] = link.released();
    }

    size_t entities(const Entity*& out) override {
        out = RV_ENTITIES;
        return sizeof(RV_ENTITIES) / sizeof(RV_ENTITIES[0]);
    }
    uint32_t publishIntervalMs() const override { return MQTT_RVTECH_MS; }

    void publish(MqttOut& out) override {
        out.text("state", link.state());
        out.onoff("connected", link.connected());
        out.onoff("release", link.released());
        int rssi = link.connected() ? link.rssi() : advRssi;
        if (rssi) out.integer("rssi", rssi);
        if (position < 0) return;
        out.text("cover", coverState());
        out.integer("cover_position", displayPosition());
        out.integer("reported", position);
    }

    void command(const char* key, const char* value) override {
        int v = -1;
        if (!strcmp(key, "release")) {
            link.setReleased(payloadOn(value));
            updates++;
            return;
        } else if (!strcmp(key, "cover")) {
            if (!strcasecmp(value, "OPEN")) v = 100;
            else if (!strcasecmp(value, "CLOSE")) v = 0;
        } else if (!strcmp(key, "cover_position")) {
            char* end;
            long x = strtol(value, &end, 10);
            if (end != value && x >= 0 && x <= 100) v = (int)x;
        } else if (!strcmp(key, "preset")) {
            key = value;   // Web UI: preset key as value
        }
        for (const RvPreset& p : RV_PRESETS) {
            if (!strcmp(key, p.key)) v = p.value;
        }
        if (v < 0 || v > RV_MAX_POSITION) return;
        // Queue target value to be transmitted inside loop()
        sendValue = v;
        sendAttempts = 0;
        sendQueued = millis();
        sendNotBefore = 0;
    }

private:
    RvCfg c;
    BleLink link;
    bool begun = false;
    char lastMac[18] = "";
    int32_t pinValue = BLE_NO_PIN;
    uint32_t lastLinkChanges = 0;

    int position = -1;               // Reported position (0-102), -1 = unknown
    int target = -1;                 // Sent target while moving, -1 = none
    unsigned long targetSince = 0;
    volatile int advRssi = 0;
    int lastRssi = 0;
    unsigned long lastRssiPublish = 0;

    int sendValue = -1;
    uint8_t sendAttempts = 0;
    unsigned long sendQueued = 0;
    unsigned long sendNotBefore = 0;

    // Buffer for assembling fragmented UART ASCII telegrams from ESP32 BLE link
    char rx[32];
    size_t rxLen = 0;

    // Cap position display at 100% (preset 102 represents "vent" state)
    int displayPosition() const { return position > 100 ? 100 : position; }

    const char* coverState() const {
        if (position < 0) return "unbekannt";
        if (target >= 0 && position < target) return "opening";
        if (target >= 0 && position > target) return "closing";
        return position == 0 ? "closed" : "open";
    }

    void setTarget(int t) {
        target = t;
        targetSince = millis();
        updates++;
    }

    // Format target command into ASCII payload "P_d=NNN\r\n" and send via BLE UART
    bool writeValue(int v) {
        char buf[12];
        snprintf(buf, sizeof(buf), "P_d=%03d\r\n", v);
        bool ok = link.write(buf, false);
        Serial.printf("[%s] Send %d: %s\n", id, v, ok ? "OK" : "FAILED");
        return ok;
    }

    void onPosition(int pos) {
        position = pos;
        if (target >= 0 && pos == target) target = -1;
        updates++;
    }

    // Reassemble incoming BLE byte stream and parse "P_a=NNN\r\n" status telegrams
    void feed(const uint8_t* d, size_t n) {
        for (size_t i = 0; i < n; i++) {
            // Prevent buffer overflow while preserving room for null terminator
            if (rxLen >= sizeof(rx) - 1) {
                memmove(rx, rx + 1, sizeof(rx) - 2);
                rxLen = sizeof(rx) - 2;
            }
            rx[rxLen++] = (char)d[i];
            rx[rxLen] = '\0';

            // Scan buffer for line end indicator "\r\n"
            char* lineEnd = strstr(rx, "\r\n");
            if (lineEnd) {
                char* match = strstr(rx, "P_a=");
                if (match && match < lineEnd && (lineEnd - match) >= 7) {
                    if (isdigit((unsigned char)match[4]) &&
                        isdigit((unsigned char)match[5]) &&
                        isdigit((unsigned char)match[6])) {
                        int v = (match[4] - '0') * 100 + (match[5] - '0') * 10 + (match[6] - '0');
                        if (v <= RV_MAX_POSITION) {
                            onPosition(v);
                        } else {
                            Serial.printf("[%s] Invalid position %d ignored\n", id, v);
                        }
                    }
                }
                // Shift processed line out of buffer
                size_t processed = (lineEnd + 2) - rx;
                memmove(rx, rx + processed, rxLen - processed);
                rxLen -= processed;
            }
        }
    }
};

Plugin* createRvTech(uint8_t index) {
    return new RvTechPlugin(index);
}

#endif // PLUGIN_RVTECH