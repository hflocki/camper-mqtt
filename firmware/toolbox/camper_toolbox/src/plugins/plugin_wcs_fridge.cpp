// Plugin "fridge": WCS compressor fridge via BLE connection.
// The fridge sends ASCII status messages (comma separated, possibly split over several
// notifications):
//   /SC0/t0,t1,t2,<temp zone 1>,<temp zone 2>,<set zone 1>,<set zone 2>,t7,<voltage x10>,t9,<battery %>,...
// Field positions as in the ESPHome example of the fridge. GATT: BLE UART bridge,
// service FFE0 with characteristic FFE1 (notify and write), unless set in the web UI.

#include "../core/plugin.h"

#if PLUGIN_WCS_FRIDGE

#include "../core/ble_link.h"
#include "../core/util.h"

#define WCS_LINE_MAX    160
#define WCS_IDLE_FLUSH  300    // ms without new data: the collected message is complete
#define WCS_MIN_TOKENS  11

struct WcsCfg {
    char mac[18];
    char pin[8];           // Bluetooth PIN (digits), empty = no pairing
    bool zone2;            // Fridge has a second zone (freezer)
    char service[40];      // GATT service UUID, empty = all services
    char notify[40];       // Notify characteristic, empty = every one that can notify
    char write[40];        // Write characteristic for the poll command, empty = none
    char poll[24];         // Poll command (ASCII), empty = fridge sends on its own
    int32_t poll_s;        // Poll interval in s
};

static const Field WCS_FIELDS[] = {
    FLD_MAC(WcsCfg, mac, "mac", "Kühlschrank-MAC", "Über \"Suchen\" aus der Liste wählen"),
    FLD_TEXT(WcsCfg, pin, "pin", "Bluetooth-PIN", "Nur Ziffern (1-6, auch 000000), wie in der Kühlschrank-App. "
                                             "Leer lassen, wenn keine nötig ist. Handy-App dabei trennen."),
    FLD_BOOL(WcsCfg, zone2, "zone2", "Zweite Zone", "Kühlschrank mit zweitem Fach / Gefrierfach"),
    FLD_TEXT(WcsCfg, service, "service", "Service-UUID", "Leer = FFE0 (Standard)"),
    FLD_TEXT(WcsCfg, notify, "notify", "Notify-UUID", "Leer = FFE1 (Standard)"),
    FLD_TEXT(WcsCfg, write, "write", "Write-UUID", "Leer = FFE1 (Standard), nur für einen Abfragebefehl nötig"),
    FLD_TEXT(WcsCfg, poll, "poll", "Abfragebefehl", "Leer = der Kühlschrank sendet von selbst. \\r und \\n erlaubt"),
    FLD_INT(WcsCfg, poll_s, "poll_s", "Abfrage alle", "s", 1, 3600, nullptr),
};

static const Entity WCS_ENTITIES[] = {
    {E_SENSOR, "temp1",     "Ist-Temperatur Zone 1",  "°C", "temperature",     "measurement", nullptr, 0, nullptr},
    {E_SENSOR, "temp2",     "Ist-Temperatur Zone 2",  "°C", "temperature",     "measurement", nullptr, 0, nullptr},
    {E_SENSOR, "set1",      "Soll-Temperatur Zone 1", "°C", "temperature",     nullptr,       "mdi:thermometer-check", 0, nullptr},
    {E_SENSOR, "set2",      "Soll-Temperatur Zone 2", "°C", "temperature",     nullptr,       "mdi:thermometer-check", 0, nullptr},
    {E_SENSOR, "voltage",   "Spannung",               "V",  "voltage",         "measurement", nullptr, 0, nullptr},
    {E_SENSOR, "battery",   "Batterie",               "%",  "battery",         "measurement", nullptr, 0, nullptr},
    {E_BINARY, "connected", "Verbindung",             nullptr, "connectivity", nullptr,       nullptr, EF_DIAG | EF_ALWAYS, nullptr},
    {E_SENSOR, "state",     "Status",                 nullptr, nullptr,        nullptr,       "mdi:bluetooth-connect", EF_DIAG | EF_ALWAYS, nullptr},
    {E_SENSOR, "ble_rssi",  "Bluetooth Signal",       "dBm", "signal_strength","measurement", "mdi:bluetooth", EF_DIAG | EF_HIDDEN, nullptr},
    {E_SENSOR, "message",   "Letzte Nachricht",       nullptr, nullptr,        nullptr,       "mdi:message-text", EF_DIAG | EF_HIDDEN | EF_ALWAYS, nullptr},
    {E_SWITCH, "release",   "Bluetooth für App freigeben", nullptr, nullptr,   nullptr,       "mdi:bluetooth-off", EF_CONFIG | EF_ALWAYS, nullptr},
};

class WcsFridgePlugin : public Plugin {
public:
    explicit WcsFridgePlugin(uint8_t index) : Plugin("fridge", "WCS Kühlschrank", "Kühlschrank", index) {}

    size_t fields(const Field*& out) override {
        out = WCS_FIELDS;
        return sizeof(WCS_FIELDS) / sizeof(WCS_FIELDS[0]);
    }
    void* cfg() override { return &c; }
    void setDefaults() override {
        memset(&c, 0, sizeof(c));
        c.zone2 = true;
        c.poll_s = 30;
    }
    void onConfigChanged() override {
        if (strcmp(c.mac, lastMac) != 0) {
            strlcpy(lastMac, c.mac, sizeof(lastMac));
            hasData = false;
            updates++;
        }
        unescape(c.poll, pollCmd, sizeof(pollCmd));
        pinValue = blePinParse(c.pin);
        if (begun) {
            link.setUuids(svcUuid(), notifyUuid(), writeUuid());
            link.setTarget(c.mac, pinValue);
        }
    }

    uint8_t bleUse() const override { return BLE_LINK; }
    const char* bleAddress() const override { return c.mac; }
    bool bleCandidate(int32_t company, const char* uuid, const char* name) const override {
        return containsIgnoreCase(name, "WCS");
    }
    const char* manufacturer() const override { return "WCS"; }

    void begin() override {
        link.begin(id, svcUuid(), notifyUuid(), writeUuid());
        link.setSilenceTimeout(WCS_OFFLINE_MS);   // Connected but silent: reconnect
        begun = true;
        link.setTarget(c.mac, pinValue);
    }

    void loop() override {
        link.loop();
        if (link.changes != lastLinkChanges) {
            lastLinkChanges = link.changes;
            lineLen = 0;
            lastPoll = 0;   // Poll right after (re)connecting
            updates++;
        }

        BlePacket pkt;
        while (link.receive(pkt)) feed(pkt.data, pkt.len);
        if (lineLen && millis() - lastByte > WCS_IDLE_FLUSH) flushLine();

        if (pollCmd[0] && link.canWrite() && (lastPoll == 0 || millis() - lastPoll > (uint32_t)c.poll_s * 1000UL)) {
            lastPoll = millis();
            bool ok = link.write(pollCmd, true);
            if (!ok) Serial.printf("[%s] Poll command failed\n", id);
        }

        bool avail = available();
        if (avail != lastAvailable) {
            lastAvailable = avail;
            updates++;
        }
    }

    bool available() override {
        return link.connected() && hasData && millis() - lastMsg <= WCS_OFFLINE_MS;
    }

    void live(JsonObject o) override {
        o["state"] = stateText();
        o["ok"] = available();
        JsonObject g = o["gauge"].to<JsonObject>();
        if (hasData) g["v"] = roundf(temp1 * 10.0f) / 10.0f;
        else g["v"] = nullptr;
        g["unit"] = "°C";
        g["label"] = "Zone 1";
        g["min"] = -25;
        g["max"] = 15;
        liveRowNum(o, "Soll Zone 1", hasData ? set1 : NAN, 1, "°C");
        if (c.zone2) {
            liveRowNum(o, "Ist Zone 2", hasData ? temp2 : NAN, 1, "°C");
            liveRowNum(o, "Soll Zone 2", hasData ? set2 : NAN, 1, "°C");
        }
        liveRowNum(o, "Spannung", hasData ? voltage : NAN, 1, "V");
        liveRowNum(o, "Batterie", hasData ? battery : NAN, 0, "%");
        liveRowNum(o, "Bluetooth", link.connected() ? (float)link.rssi() : NAN, 0, "dBm");
        liveRowNum(o, "Nachrichten", (float)messages, 0, "");
        if (link.connected()) liveRowNum(o, "Abonniert", (float)link.subscriptions(), 0, "Char.");
        if (link.lastError()) liveRowText(o, "Letzter BT-Fehler", link.lastErrorText());
        liveCtrl(o, "switch", "release", "Bluetooth für App freigeben")["v"] = link.released();
        o["raw"] = (const char*)lastLine;
    }

    size_t entities(const Entity*& out) override {
        out = WCS_ENTITIES;
        return sizeof(WCS_ENTITIES) / sizeof(WCS_ENTITIES[0]);
    }
    bool entityActive(const Entity& e) override {
        if (!strcmp(e.key, "temp2") || !strcmp(e.key, "set2")) return c.zone2;
        return true;
    }
    uint32_t publishIntervalMs() const override { return MQTT_WCS_MS; }

    void publish(MqttOut& out) override {
        out.text("state", stateText());
        out.onoff("connected", link.connected());
        out.onoff("release", link.released());
        out.text("message", lastLine);
        if (link.connected()) out.integer("ble_rssi", link.rssi());
        if (!hasData) return;
        out.num("temp1", temp1, 1);
        out.num("set1", set1, 1);
        if (c.zone2) {
            out.num("temp2", temp2, 1);
            out.num("set2", set2, 1);
        }
        out.num("voltage", voltage, 1);
        out.num("battery", battery, 0);
    }

    void command(const char* key, const char* value) override {
        if (!strcmp(key, "release")) {
            link.setReleased(payloadOn(value));
            updates++;
        }
    }

private:
    WcsCfg c;
    BleLink link;
    bool begun = false;
    char lastMac[18] = "";
    char pollCmd[24] = "";
    int32_t pinValue = BLE_NO_PIN;
    uint32_t lastLinkChanges = 0;
    unsigned long lastPoll = 0;
    bool lastAvailable = false;

    char line[WCS_LINE_MAX + 1];
    size_t lineLen = 0;
    unsigned long lastByte = 0;
    char lastLine[WCS_LINE_MAX + 1] = "";

    bool hasData = false;
    float temp1 = NAN, temp2 = NAN, set1 = NAN, set2 = NAN, voltage = NAN, battery = NAN;
    unsigned long lastMsg = 0;
    uint32_t messages = 0;

    // Empty UUID fields: the BLE UART bridge of the fridge (FFE0 / FFE1)
    const char* svcUuid() const { return c.service[0] ? c.service : WCS_SERVICE_UUID; }
    const char* notifyUuid() const { return c.notify[0] ? c.notify : WCS_CHAR_UUID; }
    const char* writeUuid() const { return c.write[0] ? c.write : WCS_CHAR_UUID; }

    const char* stateText() {
        if (link.connected() && !hasData) return "Warte auf Daten";
        if (link.connected() && millis() - lastMsg > WCS_OFFLINE_MS) return "Keine Daten";
        return link.state();
    }

    // "\r" and "\n" in the web UI field become real control characters
    static void unescape(const char* in, char* out, size_t size) {
        size_t o = 0;
        for (size_t i = 0; in[i] && o < size - 1; i++) {
            if (in[i] == '\\' && in[i + 1] == 'r') { out[o++] = '\r'; i++; }
            else if (in[i] == '\\' && in[i + 1] == 'n') { out[o++] = '\n'; i++; }
            else out[o++] = in[i];
        }
        out[o] = '\0';
    }

    void feed(const uint8_t* d, size_t n) {
        lastByte = millis();
        for (size_t i = 0; i < n; i++) {
            char ch = (char)d[i];
            if (ch == '\r' || ch == '\n' || ch == '\0') {
                if (lineLen) flushLine();
                continue;
            }
            if (lineLen < WCS_LINE_MAX) line[lineLen++] = ch;
        }
    }

    void flushLine() {
        line[lineLen] = '\0';
        printableCopy((const uint8_t*)line, lineLen, lastLine, sizeof(lastLine));
        lineLen = 0;
        parseMessage(lastLine);
        updates++;
    }

    static bool tokenFloat(const char* s, float& out) {
        char* end;
        float v = strtof(s, &end);
        if (end == s || isnan(v)) return false;
        out = v;
        return true;
    }

    void parseMessage(const char* msg) {
        const char* p = strstr(msg, "/SC0/");
        if (!p) {
            Serial.printf("[%s] Message: %s\n", id, msg);
            return;
        }
        char buf[WCS_LINE_MAX + 1];
        strlcpy(buf, p + 5, sizeof(buf));

        // Split by hand: strtok() would skip empty fields and shift the positions
        const char* tok[16];
        int n = 0;
        char* s = buf;
        while (n < 16) {
            tok[n++] = s;
            char* comma = strchr(s, ',');
            if (!comma) break;
            *comma = '\0';
            s = comma + 1;
        }
        if (n < WCS_MIN_TOKENS) {
            Serial.printf("[%s] Status with %d fields ignored: %s\n", id, n, msg);
            return;
        }

        float v;
        bool any = false;
        if (tokenFloat(tok[3], v))  { temp1 = v; any = true; }
        if (tokenFloat(tok[4], v))  { temp2 = v; any = true; }
        if (tokenFloat(tok[5], v))  { set1 = v; any = true; }
        if (tokenFloat(tok[6], v))  { set2 = v; any = true; }
        if (tokenFloat(tok[8], v))  { voltage = v / 10.0f; any = true; }
        if (tokenFloat(tok[10], v)) { battery = v; any = true; }
        if (!any) return;
        hasData = true;
        lastMsg = millis();
        messages++;
        Serial.printf("[%s] Zone 1: %.1f (set %.1f) | Zone 2: %.1f (set %.1f) | %.1f V | %.0f%%\n",
            id, temp1, set1, temp2, set2, voltage, battery);
    }
};

Plugin* createWcsFridge(uint8_t index) {
    return new WcsFridgePlugin(index);
}

#endif // PLUGIN_WCS_FRIDGE
