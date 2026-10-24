// BLE client for the battery management systems.
//
// A dedicated FreeRTOS task owns all NimBLE client calls (connect, discover, write).
// They block for seconds at a time, so loop() (web server, MQTT) keeps running meanwhile.
// Notifications arrive in the NimBLE host task and are only collected into a buffer there.
#include "bms_client.h"
#include <NimBLEDevice.h>

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;   // Guards live[], found[], flags

// ---- Shared with loop() / web task (always under mux) ---------------
static BmsLive live[MAX_BMS];
static FoundDevice found[MAX_FOUND];
static uint8_t foundCount = 0;
static volatile bool reconfigure = true;   // First run of the task takes the config
static volatile bool scanRequested = false;
static volatile uint32_t readSeq = 0;
static volatile bool scanning = false;

// ---- Owned by the BLE task --------------------------------------------
struct Link {
    BmsConfig cfg;                 // Snapshot of the slot configuration
    NimBLEClient* client;
    NimBLERemoteCharacteristic* rx;
    NimBLERemoteCharacteristic* tx;
    bool txResponse;               // Write with response (no "write without response" support)
    unsigned long nextTry;
    unsigned long backoff;
    unsigned long lastPoll;
    uint8_t dalyProto;             // 0 = not detected yet
    int16_t ectiveId;              // -1 = not detected yet
    // Receive buffer, filled by the notify callback (host task)
    uint8_t buf[200];
    volatile size_t len;
    volatile bool done;
    volatile uint8_t head;         // Expected first byte of the response
};
static Link links[MAX_BMS];
static NimBLEScan* scan = nullptr;
static unsigned long lastScan = 0;

// ============================================================
// Helpers
// ============================================================

bool isValidMac(const char* mac) {
    if (strlen(mac) != 17) return false;
    for (int i = 0; i < 17; i++) {
        char c = mac[i];
        if (i % 3 == 2) { if (c != ':') return false; }
        else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static void upperMac(const std::string& in, char* out) {
    strlcpy(out, in.c_str(), 18);
    for (char* c = out; *c; c++) *c = toupper(*c);
}

static void setState(int i, const char* txt, bool connected) {
    portENTER_CRITICAL(&mux);
    strlcpy(live[i].state, txt, sizeof(live[i].state));
    live[i].connected = connected;
    portEXIT_CRITICAL(&mux);
}

static void countError(int i) {
    portENTER_CRITICAL(&mux);
    live[i].errors++;
    portEXIT_CRITICAL(&mux);
}

// Detects the BMS family from an advertisement
static uint8_t detectType(const NimBLEAdvertisedDevice* dev) {
    if (dev->isAdvertisingService(NimBLEUUID((uint16_t)0xAF30))) return BMS_ECTIVE;
    std::string name = dev->haveName() ? dev->getName() : std::string();
    if (name.rfind("DL-", 0) == 0) return BMS_DALY;
    if (dev->isAdvertisingService(NimBLEUUID((uint16_t)0xFFF0)) && dev->getManufacturerDataCount() > 0) {
        std::string m = dev->getManufacturerData(0);
        if (m.size() >= 2) {
            uint16_t id = (uint8_t)m[0] | ((uint8_t)m[1] << 8);
            if (id == 0x102 || id == 0x104 || id == 0x302 || id == 0x303 || id == 0x402) return BMS_DALY;
        }
    }
    return BMS_NONE;
}

// ============================================================
// Scan (callback runs in the NimBLE host task - keep it short)
// ============================================================
class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        uint8_t type = detectType(dev);
        char mac[18];
        upperMac(dev->getAddress().toString(), mac);

        // Keep BMS and every configured address (whatever it advertises)
        bool configured = false;
        for (int i = 0; i < MAX_BMS; i++) {
            if (links[i].cfg.used && strcmp(links[i].cfg.mac, mac) == 0) configured = true;
        }
        if (type == BMS_NONE && !configured) return;

        std::string name = dev->haveName() ? dev->getName() : std::string();
        portENTER_CRITICAL(&mux);
        int slot = -1, oldest = 0;
        for (int i = 0; i < foundCount; i++) {
            if (strcmp(found[i].mac, mac) == 0) { slot = i; break; }
            if (found[i].seen < found[oldest].seen) oldest = i;
        }
        if (slot < 0) slot = (foundCount < MAX_FOUND) ? foundCount++ : oldest;
        FoundDevice& f = found[slot];
        if (strcmp(f.mac, mac) != 0) memset(&f, 0, sizeof(f));   // new or reused entry
        strlcpy(f.mac, mac, sizeof(f.mac));
        if (!name.empty()) strlcpy(f.name, name.c_str(), sizeof(f.name));
        if (type != BMS_NONE) f.type = type;
        f.addr_type = dev->getAddress().getType();
        f.rssi = dev->getRSSI();
        f.seen = millis();
        if (f.seen == 0) f.seen = 1;
        portEXIT_CRITICAL(&mux);
    }
    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        scanning = false;
    }
};
static ScanCallbacks scanCallbacks;

static int findFound(const char* mac) {
    for (int i = 0; i < foundCount; i++) if (strcmp(found[i].mac, mac) == 0) return i;
    return -1;
}

static void runScan(uint32_t ms) {
    if (!scan || scan->isScanning()) return;
    scanning = true;
    if (!scan->start(ms, false, true)) {
        scanning = false;
        Serial.println("[BLE] Scan start failed");
        return;
    }
    Serial.printf("[BLE] Scanning %u s\n", (unsigned)(ms / 1000));
    unsigned long t0 = millis();
    while (scan->isScanning() && millis() - t0 < ms + 1000) vTaskDelay(pdMS_TO_TICKS(100));
    scanning = false;
    lastScan = millis();
}

// ============================================================
// Connection
// ============================================================

static void dropLink(int i, const char* why) {
    Link& l = links[i];
    if (l.client) {
        if (l.client->isConnected()) l.client->disconnect();
        NimBLEDevice::deleteClient(l.client);
        l.client = nullptr;
    }
    l.rx = l.tx = nullptr;
    setState(i, why, false);
}

static void onNotify(int i, const uint8_t* data, size_t n) {
    Link& l = links[i];
    if (l.done || n == 0) return;
    size_t len = l.len;
    // A frame starts with the expected head byte; responses longer than the MTU come in pieces
    if (len == 0 && data[0] != l.head) return;
    if (len + n > sizeof(l.buf)) len = 0;
    memcpy(l.buf + len, data, n);
    len += n;
    int need = bmsModbusExpectedLen(l.buf, len, l.head);
    if (need < 0) {
        // Garbage in the buffer: start over if this piece begins a new frame
        if (data[0] != l.head) { l.len = 0; return; }
        memmove(l.buf, data, n);
        len = n;
        need = bmsModbusExpectedLen(l.buf, len, l.head);
        if (need < 0) { l.len = 0; return; }
    }
    if (need > 0 && len >= (size_t)need) {
        l.len = need;
        l.done = true;
        return;
    }
    l.len = len;
}

// Pick the characteristics: known UUIDs first, else the first notify / write pair of the service
static bool findChars(Link& l, uint16_t svcId, uint16_t rxId, uint16_t txId) {
    NimBLERemoteService* svc = l.client->getService(NimBLEUUID(svcId));
    if (!svc) return false;
    l.rx = svc->getCharacteristic(NimBLEUUID(rxId));
    l.tx = svc->getCharacteristic(NimBLEUUID(txId));
    if (!l.rx || !l.tx) {
        l.rx = l.tx = nullptr;
        for (auto* c : svc->getCharacteristics(true)) {
            if (!l.rx && (c->canNotify())) l.rx = c;
            if (!l.tx && (c->canWriteNoResponse() || c->canWrite())) l.tx = c;
        }
    }
    return l.rx && l.tx && l.rx->canNotify();
}

static bool connectLink(int i) {
    Link& l = links[i];
    char st[40];
    snprintf(st, sizeof(st), "Verbinde ...");
    setState(i, st, false);

    portENTER_CRITICAL(&mux);
    int f = findFound(l.cfg.mac);
    int addrType = f >= 0 ? found[f].addr_type : -1;
    portEXIT_CRITICAL(&mux);

    if (!l.client) {
        l.client = NimBLEDevice::createClient();
        l.client->setConnectTimeout(BLE_CONNECT_TIMEOUT_MS);
    }

    // Address type from the scan; if the device was not seen yet try both
    bool ok = false;
    for (int t = 0; t < 2 && !ok; t++) {
        uint8_t type = addrType >= 0 ? addrType : (t == 0 ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM);
        ok = l.client->connect(NimBLEAddress(std::string(l.cfg.mac), type), true);
        if (addrType >= 0) break;
    }
    if (!ok) {
        dropLink(i, "Nicht erreichbar");
        return false;
    }

    bool chars = false;
    if (l.cfg.type == BMS_DALY) {
        chars = findChars(l, 0xFFF0, 0xFFF1, 0xFFF2);
    } else if (l.cfg.type == BMS_ECTIVE) {
        chars = findChars(l, 0xFFE0, 0xFFE1, 0xFFE1) || findChars(l, 0xAF30, 0xFFE1, 0xFFE1);
    }
    if (!chars) {
        Serial.printf("[BLE] %s: characteristics not found\n", l.cfg.mac);
        dropLink(i, "Dienst nicht gefunden");
        return false;
    }
    l.txResponse = !l.tx->canWriteNoResponse();
    if (!l.rx->subscribe(true, [i](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) { onNotify(i, d, n); })) {
        dropLink(i, "Notify fehlgeschlagen");
        return false;
    }
    Serial.printf("[BLE] %s connected (%s), MTU %u\n", l.cfg.mac, bmsTypeName(l.cfg.type), l.client->getMTU());
    setState(i, "Verbunden", true);
    return true;
}

// Sends one MODBUS read and waits for the response; returns the frame length, 0 on timeout / bad CRC
static size_t request(int i, uint8_t dev, uint8_t head, uint16_t addr, uint16_t count) {
    Link& l = links[i];
    uint8_t cmd[8];
    bmsModbusRead(dev, addr, count, cmd);
    l.head = head;
    l.len = 0;
    l.done = false;
    if (!l.tx->writeValue(cmd, sizeof(cmd), l.txResponse)) return 0;
    unsigned long t0 = millis();
    while (!l.done && millis() - t0 < BLE_RESPONSE_TIMEOUT_MS) {
        if (!l.client->isConnected()) return 0;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!l.done) return 0;
    size_t n = l.len;
    return bmsModbusCrcOk(l.buf, n) ? n : 0;
}

static bool pollDaly(int i, BmsSample& s) {
    Link& l = links[i];
    static uint8_t f1[200];
    const DalyProto protos[2] = {DALY_D2, DALY_X81};
    for (int p = 0; p < 2; p++) {
        DalyProto proto = protos[p];
        if (l.dalyProto && l.dalyProto != proto) continue;
        uint16_t addr, count;
        dalyRequest(proto, 0, addr, count);
        size_t n1 = request(i, proto, dalyResponseHead(proto), addr, count);
        if (!n1) continue;
        memcpy(f1, l.buf, n1);
        size_t n2 = 0;
        if (dalyRequestCount(proto) > 1) {
            dalyRequest(proto, 1, addr, count);
            n2 = request(i, proto, dalyResponseHead(proto), addr, count);
            if (!n2) return false;
        }
        if (!dalyDecode(proto, f1, n1, n2 ? l.buf : nullptr, n2, s)) return false;
        if (!l.dalyProto) Serial.printf("[BLE] %s: Daly protocol 0x%02X\n", l.cfg.mac, proto);
        l.dalyProto = proto;
        return true;
    }
    return false;
}

static bool pollEctive(int i, BmsSample& s) {
    Link& l = links[i];
    for (uint8_t id : ECTIVE_DEV_IDS) {
        if (l.ectiveId >= 0 && l.ectiveId != id) continue;
        size_t n = request(i, id, id, ECTIVE_REG_ADDR, ECTIVE_REG_COUNT);
        if (!n || !ectiveDecode(l.buf, n, s)) continue;
        if (l.ectiveId < 0) Serial.printf("[BLE] %s: Ective device id 0x%02X\n", l.cfg.mac, id);
        l.ectiveId = id;
        return true;
    }
    return false;
}

static void pollLink(int i) {
    Link& l = links[i];
    static BmsSample s;
    bool ok = (l.cfg.type == BMS_DALY) ? pollDaly(i, s) : pollEctive(i, s);
    int rssi = l.client ? l.client->getRssi() : 0;

    portENTER_CRITICAL(&mux);
    if (ok) {
        live[i].s = s;
        live[i].valid = true;
        live[i].last_ok = millis();
        live[i].reads++;
        live[i].rssi = rssi;
        strlcpy(live[i].state, "OK", sizeof(live[i].state));
        readSeq = readSeq + 1;
    } else {
        live[i].errors++;
        strlcpy(live[i].state, "Keine Antwort", sizeof(live[i].state));
    }
    portEXIT_CRITICAL(&mux);
}

// ============================================================
// Task
// ============================================================

static void takeConfig() {
    for (int i = 0; i < MAX_BMS; i++) {
        BmsConfig next;
        portENTER_CRITICAL(&mux);
        next = config.bms[i];
        portEXIT_CRITICAL(&mux);
        bool changed = !next.used || !links[i].cfg.used ||
                       strcmp(next.mac, links[i].cfg.mac) != 0 || next.type != links[i].cfg.type;
        if (changed) {
            dropLink(i, next.used ? "Warte" : "-");
            links[i].dalyProto = 0;
            links[i].ectiveId = -1;
            links[i].backoff = BLE_RETRY_MS;
            links[i].nextTry = 0;
            portENTER_CRITICAL(&mux);
            BmsLive fresh = {};
            bmsClear(fresh.s);
            strlcpy(fresh.state, next.used ? "Warte" : "-", sizeof(fresh.state));
            live[i] = fresh;
            portEXIT_CRITICAL(&mux);
        }
        links[i].cfg = next;
        links[i].lastPoll = 0;   // poll right away with the new interval
    }
}

static void bleTask(void*) {
    for (;;) {
        if (reconfigure) {
            reconfigure = false;
            takeConfig();
        }

        bool allConnected = true;
        unsigned long now = millis();
        for (int i = 0; i < MAX_BMS; i++) {
            Link& l = links[i];
            if (!l.cfg.used) continue;
            bool up = l.client && l.client->isConnected();
            if (!up) {
                allConnected = false;
                if (l.client) dropLink(i, "Getrennt");
                if ((long)(millis() - l.nextTry) >= 0) {
                    if (connectLink(i)) {
                        l.backoff = BLE_RETRY_MS;
                        l.lastPoll = 0;
                    } else {
                        countError(i);
                        l.nextTry = millis() + l.backoff;
                        l.backoff = (l.backoff * 2 > 120000UL) ? 120000UL : l.backoff * 2;
                    }
                }
                continue;
            }
            if (l.lastPoll == 0 || now - l.lastPoll >= (unsigned long)config.interval_s * 1000UL) {
                l.lastPoll = now;
                pollLink(i);
            }
        }

        // Scan: on request from the web UI, and now and then while a BMS is missing
        if (scanRequested) {
            scanRequested = false;
            runScan(BLE_SCAN_MS * 2);
        } else if (!allConnected && (lastScan == 0 || millis() - lastScan > BLE_SCAN_EVERY_MS)) {
            runScan(BLE_SCAN_MS);
        }

        // Mark stale readings
        portENTER_CRITICAL(&mux);
        for (int i = 0; i < MAX_BMS; i++) {
            if (live[i].valid && millis() - live[i].last_ok > BMS_STALE_MS) live[i].valid = false;
        }
        portEXIT_CRITICAL(&mux);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============================================================
// Public API
// ============================================================

void bmsInit() {
    for (int i = 0; i < MAX_BMS; i++) {
        memset(&links[i].cfg, 0, sizeof(BmsConfig));
        links[i].client = nullptr;
        links[i].ectiveId = -1;
        bmsClear(live[i].s);
        strlcpy(live[i].state, "-", sizeof(live[i].state));
    }
    NimBLEDevice::init("");
    NimBLEDevice::setMTU(247);
    scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks, false);
    scan->setActiveScan(true);        // Scan responses carry the device name
    scan->setInterval(100);
    scan->setWindow(60);
    scan->setDuplicateFilter(false);
    scan->setMaxResults(0);

    reconfigure = true;
    xTaskCreate(bleTask, "bms", 8192, nullptr, 1, nullptr);
    Serial.println("[BLE] BMS task started");
}

void bmsRequestReconfigure() { reconfigure = true; }
void bmsRequestScan() { scanRequested = true; }
bool bmsScanning() { return scanning; }
uint32_t bmsReadSeq() { return readSeq; }

void bmsGetLive(int i, BmsLive& out) {
    portENTER_CRITICAL(&mux);
    out = live[i];
    portEXIT_CRITICAL(&mux);
}

uint8_t bmsGetFound(FoundDevice* out, uint8_t max) {
    portENTER_CRITICAL(&mux);
    uint8_t n = foundCount < max ? foundCount : max;
    memcpy(out, found, n * sizeof(FoundDevice));
    portEXIT_CRITICAL(&mux);
    return n;
}

void bmsPrint() {
    BmsLive l;
    for (int i = 0; i < MAX_BMS; i++) {
        if (!config.bms[i].used) continue;
        bmsGetLive(i, l);
        Serial.printf("[BMS] %-16s %s %-12s %s", config.bms[i].name, config.bms[i].mac,
                      bmsTypeName(config.bms[i].type), l.state);
        if (l.valid) {
            Serial.printf("  %.2f V %.2f A %.0f %% %.1f Ah", l.s.voltage, l.s.current, l.s.soc, l.s.charge_ah);
        }
        Serial.println();
    }
    FoundDevice f[MAX_FOUND];
    uint8_t n = bmsGetFound(f, MAX_FOUND);
    for (uint8_t i = 0; i < n; i++) {
        Serial.printf("[SCAN] %s %-20s %-12s %d dBm\n", f[i].mac, f[i].name, bmsTypeName(f[i].type), f[i].rssi);
    }
}
