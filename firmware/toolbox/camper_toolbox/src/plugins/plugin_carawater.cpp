// Plugin "pump": CaraWater water pump (tested protocol: ISP 15) via BLE connection.
// Protocol ported from the Home Assistant integration "carawater_isp15":
//   notify FF01: status frame  AA 93 <status> .. <U16LE values at 6..19>
//   write  FF02: 60 byte frame AA 39 <status> .. 22 12 07 .. <checksum>
// Some pumps use FFF0/FFF1/FFF2 or FFE0/FFE1 instead; all layouts are tried automatically.

#include "../core/plugin.h"

#if PLUGIN_CARAWATER

#include "../core/ble_link.h"

// Known GATT layouts: FF00/FF01/FF02 (ISP 15, as in the HA integration),
// FFF0/FFF1/FFF2 (other Seaflo based pumps), and FFE0/FFE1/FFE1.
// The plugin starts with the first one and switches to the next one if the service is not found ("Falsches Gerät?").
struct PumpUuids {
    const char* service;
    const char* notify;
    const char* write;
};

static const PumpUuids PUMP_LAYOUTS[] = {
    {PUMP_SERVICE_UUID, PUMP_NOTIFY_UUID, PUMP_WRITE_UUID},             // FF00 / FF01 / FF02
    {PUMP_ALT_SERVICE_UUID, PUMP_ALT_NOTIFY_UUID, PUMP_ALT_WRITE_UUID}, // FFF0 / FFF1 / FFF2
    {PUMP_UART_SERVICE_UUID, PUMP_UART_CHAR_UUID, PUMP_UART_CHAR_UUID},    // FFE0 / FFE1
};

struct PumpCfg {
    char mac[18];
    char model[24];        // Model label for Home Assistant only (empty = "Pumpe")
};

static const Field PUMP_FIELDS[] = {
    FLD_MAC(PumpCfg, mac, "mac", "Pumpen-MAC", "Über \"Suchen\" aus der Liste wählen"),
    FLD_TEXT(PumpCfg, model, "model", "Modell", "Nur Anzeige in Home Assistant, z. B. ISP 15"),
};

static const Entity PUMP_ENTITIES[] = {
    {E_SENSOR, "voltage",      "Spannung",         "V",     "voltage",          "measurement",      nullptr, 0, nullptr},
    {E_SENSOR, "current",      "Strom",            "A",     "current",          "measurement",      nullptr, 0, nullptr},
    {E_SENSOR, "temperature",  "Wassertemperatur", "°C",    "temperature",      "measurement",      nullptr, 0, nullptr},
    {E_SENSOR, "pressure",     "Wasserdruck",      "bar",   "pressure",         "measurement",      nullptr, 0, nullptr},
    {E_SENSOR, "flow",         "Durchfluss",       "L/min", "volume_flow_rate", "measurement",      nullptr, 0, nullptr},
    {E_SENSOR, "daily_liters", "Tagesverbrauch",   "L",     "water",            "total_increasing", nullptr, 0, nullptr},
    {E_SENSOR, "total_liters", "Gesamtverbrauch",  "L",     "water",            "total_increasing", nullptr, 0, nullptr},
    {E_SENSOR, "ble_rssi",     "Bluetooth Signal", "dBm",   "signal_strength",  "measurement",      "mdi:bluetooth", EF_DIAG | EF_HIDDEN, nullptr},
    {E_SENSOR, "state",        "Status",           nullptr, nullptr,            nullptr,            "mdi:bluetooth-connect", EF_DIAG | EF_ALWAYS, nullptr},
    {E_BINARY, "running",      "Pumpe läuft",      nullptr, "running",          nullptr,            "mdi:water-pump", 0, nullptr},
    {E_BINARY, "connected",    "Verbindung",       nullptr, "connectivity",     nullptr,            "mdi:bluetooth-connect", EF_DIAG | EF_ALWAYS, nullptr},
    {E_SWITCH, "enable",       "Pumpenfreigabe",   nullptr, nullptr,            nullptr,            "mdi:water-pump", 0, nullptr},
    {E_SWITCH, "leak",         "Leckage-Erkennung",nullptr, nullptr,            nullptr,            "mdi:pipe-leak", EF_CONFIG, nullptr},
    {E_SWITCH, "release",      "Bluetooth für App freigeben", nullptr, nullptr, nullptr,            "mdi:bluetooth-off", EF_CONFIG | EF_ALWAYS, nullptr},
    {E_SELECT, "pressure_level","Konstantdruck",   nullptr, nullptr,            nullptr,            "mdi:gauge", EF_CONFIG, "Stufe 1|Stufe 2|Stufe 3"},
};

class CaraWaterPlugin : public Plugin {
public:
    explicit CaraWaterPlugin(uint8_t index) : Plugin("pump", "CaraWater Pumpe", "Wasserpumpe", index) {}

    size_t fields(const Field*& out) override {
        out = PUMP_FIELDS;
        return sizeof(PUMP_FIELDS) / sizeof(PUMP_FIELDS[0]);
    }
    void* cfg() override { return &c; }
    void setDefaults() override { memset(&c, 0, sizeof(c)); }
    void onConfigChanged() override {
        // Other pump: forget the old status
        if (strcmp(c.mac, lastMac) != 0) {
            strlcpy(lastMac, c.mac, sizeof(lastMac));
            hasData = false;
            updates++;
        }
        if (begun) link.setTarget(c.mac);
    }

    uint8_t bleUse() const override { return BLE_LINK; }
    const char* bleAddress() const override { return c.mac; }
    bool bleCandidate(int32_t company, const char* uuid, const char* name) const override {
        return strstr(uuid, "ff00") != nullptr || strstr(uuid, "fff0") != nullptr || strstr(uuid, "ffe0") != nullptr;
    }
    const char* manufacturer() const override { return "CaraWater"; }
    const char* model() const override { return c.model[0] ? c.model : "Pumpe"; }

    void begin() override {
        const PumpUuids& u = PUMP_LAYOUTS[layout];
        link.begin(id, u.service, u.notify, u.write);
        link.setSilenceTimeout(PUMP_OFFLINE_MS * 4);   // Connected but silent: reconnect
        begun = true;
        link.setTarget(c.mac);
    }

    void loop() override {
        link.loop();
        BlePacket pkt;
        while (link.receive(pkt)) parseFrame(pkt.data, pkt.len);

        // No "handshake" write here: before the first status frame statusRaw is 0, and
        // writing it would switch the pump OFF (enable bit cleared, invalid pressure level).
        // The pump sends its status frames on its own once notifications are subscribed.

        bool avail = available();
        if (avail != lastAvailable || link.changes != lastLinkChanges) {
            lastAvailable = avail;
            lastLinkChanges = link.changes;
            // Service not found: try the other known GATT layout on the next attempt
            if (!strcmp(link.state(), "Falsches Gerät?")) {
                layout = (layout + 1) % (sizeof(PUMP_LAYOUTS) / sizeof(PUMP_LAYOUTS[0]));
                const PumpUuids& u = PUMP_LAYOUTS[layout];
                Serial.printf("[%s] Trying GATT layout %s\n", id, u.service);
                link.setUuids(u.service, u.notify, u.write);
            }
            updates++;
        }
    }

    bool available() override {
        return link.connected() && hasData && millis() - lastFrame <= (PUMP_OFFLINE_MS * 2);
    }

    void live(JsonObject o) override {
        o["state"] = stateText();
        o["ok"] = available();
        if (hasData) {
            JsonObject g = o["gauge"].to<JsonObject>();
            g["v"] = roundf(pressure * 100.0f) / 100.0f;
            g["unit"] = "bar";
            g["label"] = running ? "Druck - läuft" : "Druck";
            g["max"] = 4;
            liveRowNum(o, "Durchfluss", flow, 1, "L/min");
            liveRowNum(o, "Spannung", voltage, 1, "V");
            liveRowNum(o, "Strom", current, 1, "A");
            liveRowNum(o, "Wassertemperatur", temperature, 1, "°C");
            liveRowNum(o, "Heute", dailyLiters, 0, "L");
            liveRowNum(o, "Gesamt", totalLiters, 0, "L");
        }
        liveRowNum(o, "Bluetooth", link.connected() ? (float)link.rssi() : NAN, 0, "dBm");
        if (hasData) {
            liveCtrl(o, "switch", "enable", "Pumpenfreigabe")["v"] = pumpEnabled;
            liveCtrl(o, "switch", "leak", "Leckage-Erkennung")["v"] = leakDetection;
            JsonObject s = liveCtrl(o, "select", "pressure_level", "Konstantdruck");
            s["o"] = "Stufe 1|Stufe 2|Stufe 3";
            s["v"] = pressureLevel >= 1 && pressureLevel <= 3 ? pressureLevel - 1 : 0;
        }
        if (link.lastError()) liveRowText(o, "Letzter BT-Fehler", link.lastErrorText());
        liveCtrl(o, "switch", "release", "Bluetooth für App freigeben")["v"] = link.released();
    }

    size_t entities(const Entity*& out) override {
        out = PUMP_ENTITIES;
        return sizeof(PUMP_ENTITIES) / sizeof(PUMP_ENTITIES[0]);
    }
    uint32_t publishIntervalMs() const override { return MQTT_PUMP_MS; }

    void publish(MqttOut& out) override {
        out.text("state", stateText());
        out.onoff("connected", link.connected());
        out.onoff("release", link.released());
        // Values only once a real status frame exists
        if (!hasData) return;
        out.onoff("enable", pumpEnabled);
        out.onoff("leak", leakDetection);
        out.onoff("running", running);
        char buf[12];
        snprintf(buf, sizeof(buf), "Stufe %u", (unsigned)pressureLevel);
        out.text("pressure_level", buf);
        out.num("voltage", voltage, 1);
        out.num("current", current, 1);
        out.num("temperature", temperature, 1);
        out.num("pressure", pressure, 2);
        out.num("flow", flow, 1);
        out.integer("daily_liters", dailyLiters);
        out.integer("total_liters", totalLiters);
        if (link.connected()) out.integer("ble_rssi", link.rssi());
    }

    void command(const char* key, const char* value) override {
        if (!strcmp(key, "release")) {
            link.setReleased(payloadOn(value));
            updates++;
            return;
        }
        uint8_t status = statusRaw;
        if (!strcmp(key, "enable")) {
            status = payloadOn(value) ? (status | 0x01) : (status & ~0x01);
        } else if (!strcmp(key, "leak")) {
            status = payloadOn(value) ? (status | 0x04) : (status & ~0x04);
        } else if (!strcmp(key, "pressure_level")) {
            // Accepts "2" as well as "Stufe 2" (select option)
            size_t n = strlen(value);
            int level = n ? value[n - 1] - '0' : 0;
            if (level < 1 || level > 3) return;
            status = (status & ~0x60) | ((level & 0x03) << 5);
        } else {
            return;
        }
        if (!link.connected()) {
            Serial.printf("[%s] Command ignored - pump not connected\n", id);
            updates++;   // Re-publish the real state so HA switches snap back
            return;
        }
        if (status != statusRaw && writeStatus(status)) applyStatus(status);   // Optimistic
        updates++;
    }

private:
    PumpCfg c;
    BleLink link;
    uint8_t layout = 0;    // Index into PUMP_LAYOUTS
    bool begun = false;
    char lastMac[18] = "";

    bool hasData = false;
    uint8_t statusRaw = 0;
    uint8_t pressureLevel = 0;
    bool pumpEnabled = false;
    bool leakDetection = false;
    bool running = false;
    float voltage = 0, current = 0, temperature = 0, pressure = 0, flow = 0;
    uint16_t dailyLiters = 0, totalLiters = 0;
    unsigned long lastFrame = 0;
    bool lastAvailable = false;
    bool lastRunning = false;
    uint32_t lastLinkChanges = 0;

    const char* stateText() {
        if (link.connected() && hasData && millis() - lastFrame > PUMP_OFFLINE_MS) return "Keine Daten";
        return link.state();
    }

    static uint16_t u16le(const uint8_t* d, size_t off) {
        return (uint16_t)d[off] | ((uint16_t)d[off + 1] << 8);
    }

    // Derived fields from the status byte (also used after an optimistic write)
    void applyStatus(uint8_t status) {
        statusRaw = status;
        pressureLevel = (status & 0x60) >> 5;
        pumpEnabled = (status & 0x01) != 0;
        leakDetection = (status & 0x04) != 0;
    }

    // Status frame: AA 93 <status> ... U16LE at 6 voltage, 8 current, 10 pressure raw (psi*10),
    // 12 temperature, 14 flow, 16 daily liters, 18 total liters
    // The frame type in byte 1 is not checked (it differs between pumps); only the echo
    // of an own command frame (AA 39) is skipped.
    void parseFrame(const uint8_t* d, size_t n) {
        if (n < 20 || d[0] != 0xAA || d[1] == 0x39) return;
        uint8_t oldStatus = statusRaw;
        bool hadData = hasData;

        applyStatus(d[2]);
        running = (d[2] & 0x10) != 0;          // status bit 4: motor runs

        voltage = u16le(d, 6) / 10.0f;
        current = u16le(d, 8) / 10.0f;
        pressure = u16le(d, 10) / 10.0f * 0.0689476f;   // psi x 10 -> bar
        temperature = u16le(d, 12) / 10.0f;
        flow = u16le(d, 14) / 10.0f;
        dailyLiters = u16le(d, 16);
        totalLiters = u16le(d, 18);

        lastFrame = millis();
        hasData = true;

        // Publish immediately on switch/level changes or pump start/stop; values follow periodically
        if (!hadData || oldStatus != statusRaw || lastRunning != running) {
            lastRunning = running;
            updates++;
        }
    }

    // 60 byte command frame, same layout as the Home Assistant integration
    bool writeStatus(uint8_t status) {
        uint8_t cmd[60] = {0};
        cmd[0] = 0xAA;
        cmd[1] = 0x39;
        cmd[2] = status;
        cmd[6] = 0x22;
        cmd[7] = 0x12;
        cmd[8] = 0x07;
        uint8_t sum = 0;
        for (int i = 0; i < 59; i++) sum += cmd[i];
        cmd[59] = sum;
        bool ok = link.write(cmd, sizeof(cmd), true);
        Serial.printf("[%s] Write status 0x%02X: %s\n", id, status, ok ? "OK" : "FAILED");
        return ok;
    }
};

Plugin* createCaraWater(uint8_t index) {
    return new CaraWaterPlugin(index);
}

#endif // PLUGIN_CARAWATER