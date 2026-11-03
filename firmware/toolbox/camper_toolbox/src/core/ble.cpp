#include "ble.h"

#if CT_USE_BLE

#include "registry.h"
#include <NimBLEDevice.h>

// ============================================================
// State shared between the NimBLE host task (scan callback) and loop()
// ============================================================
static portMUX_TYPE seenMux = portMUX_INITIALIZER_UNLOCKED;

struct SeenDevice {
    bool used;
    char addr[18];
    uint8_t addrType;
    char name[24];
    char uuid[40];         // First service data UUID, else first advertised service UUID ("" = none)
    int32_t company;       // Company ID of the first manufacturer data block (-1 = none)
    uint8_t prio;          // List priority (see devicePriority)
    int rssi;
    unsigned long last;
};
static SeenDevice seen[BLE_MAX_SEEN];

static NimBLEScan* scan = nullptr;
static bool active = false;
static volatile uint32_t advCount = 0;
static int pauseCount = 0;
static const void* connectOwner = nullptr;

#define COMPANY_APPLE 0x004C

// NimBLE returns lower case addresses; the config uses upper case
static void toUpperAddr(const std::string& in, char* out, size_t size) {
    size_t i = 0;
    for (; i < in.size() && i < size - 1; i++) out[i] = toupper((unsigned char)in[i]);
    out[i] = '\0';
}

// Higher = more important. Keeps configured devices in the list even when many phones are around.
//   3 = configured in a plugin, 2 = a plugin recognizes it, 1 = other device with data,
//   0 = Apple devices (rotating addresses) / no data
static uint8_t devicePriority(const char* addr, const char* uuid, int32_t company, const char* name) {
    bool candidate = false;
    FOR_EACH_ENABLED_PLUGIN(p) {
        if (p->bleUse() == BLE_NONE) continue;
        const char* a = p->bleAddress();
        if (a[0] && strcmp(addr, a) == 0) return 3;
        if (p->bleCandidate(company, uuid, name)) candidate = true;
    }
    if (candidate) return 2;
    if (company == COMPANY_APPLE) return 0;
    if (company >= 0 || uuid[0] != '\0') return 1;
    return 0;
}

static void rememberDevice(const char* addr, uint8_t type, const char* name, const char* uuid,
                           int32_t company, int rssi) {
    unsigned long now = millis();
    uint8_t prio = devicePriority(addr, uuid, company, name);
    int match = -1, freeSlot = -1, victim = -1;

    portENTER_CRITICAL(&seenMux);
    for (int i = 0; i < BLE_MAX_SEEN; i++) {
        SeenDevice& s = seen[i];
        if (s.used && strcmp(s.addr, addr) == 0) { match = i; break; }
        bool expired = s.used && (now - s.last > BLE_SEEN_TIMEOUT_MS);
        if ((!s.used || expired) && freeSlot < 0) freeSlot = i;
        // Eviction candidate: lowest priority first, then the oldest entry
        if (s.used && (victim < 0 || s.prio < seen[victim].prio ||
                       (s.prio == seen[victim].prio && s.last < seen[victim].last))) {
            victim = i;
        }
    }
    int slot = match;
    if (slot < 0) slot = freeSlot;
    // List full: only replace an entry with the same or lower priority
    if (slot < 0 && victim >= 0 && seen[victim].prio <= prio) slot = victim;
    if (slot < 0) {
        portEXIT_CRITICAL(&seenMux);
        return;
    }
    SeenDevice& d = seen[slot];
    if (match < 0) {
        memset(&d, 0, sizeof(d));
        strlcpy(d.addr, addr, sizeof(d.addr));
        d.company = -1;
    }
    d.used = true;
    d.addrType = type;
    d.rssi = rssi;
    d.last = now;
    if (company >= 0) d.company = company;
    if (uuid[0]) strlcpy(d.uuid, uuid, sizeof(d.uuid));
    if (name && name[0]) strlcpy(d.name, name, sizeof(d.name));
    if (prio > d.prio || match < 0) d.prio = prio;
    portEXIT_CRITICAL(&seenMux);
}

// ============================================================
// Scan callback (runs in the NimBLE host task - keep it short)
// ============================================================
class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        advCount = advCount + 1;
        char addr[18];
        toUpperAddr(dev->getAddress().toString(), addr, sizeof(addr));

        int32_t company = -1;
        if (dev->getManufacturerDataCount() > 0) {
            std::string m = dev->getManufacturerData(0);
            if (m.size() >= 2) company = (uint8_t)m[0] | ((uint8_t)m[1] << 8);
        }
        char uuid[40] = "";
        if (dev->getServiceDataCount() > 0) {
            strlcpy(uuid, dev->getServiceDataUUID(0).toString().c_str(), sizeof(uuid));
        } else if (dev->getServiceUUIDCount() > 0) {
            strlcpy(uuid, dev->getServiceUUID(0).toString().c_str(), sizeof(uuid));
        }
        std::string name = dev->haveName() ? dev->getName() : std::string();
        rememberDevice(addr, dev->getAddressType(), name.c_str(), uuid, company, dev->getRSSI());

        FOR_EACH_ENABLED_PLUGIN(p) {
            if (p->bleUse() != BLE_NONE) p->onAdvertisement(dev, addr);
        }
    }
};
static ScanCallbacks scanCallbacks;

static void startScan() {
    if (!scan || pauseCount > 0 || scan->isScanning()) return;
    // 0 = scan forever, results are not stored (setMaxResults(0)) to save RAM
    if (!scan->start(0, false, true)) Serial.println("[BLE] Scan start FAILED");
    else Serial.println("[BLE] Scan started");
}

// ============================================================
// Public API
// ============================================================

void bleInit() {
    if (active) return;
    Serial.println("[BLE] Initializing NimBLE...");
    memset(seen, 0, sizeof(seen));

    NimBLEDevice::init("");
    NimBLEDevice::setMTU(247);                          // Larger frames for connecting plugins
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);   // PIN entry (roof hatch, fridge)
    // Bonding + secure connections, set once here for all links (never changed later).
    // MITM is not forced: a device that wants a PIN requests MITM itself and the
    // keyboard IO capability then leads to passkey entry; devices without a PIN
    // (pump) keep pairing "just works". Legacy pairing falls back automatically.
    NimBLEDevice::setSecurityAuth(BLE_SEC_BOND, BLE_SEC_MITM, BLE_SEC_SC);

    scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks, true);  // true = report every advertisement
    scan->setActiveScan(false);                    // Passive: battery friendly for the sensors
    scan->setInterval(BLE_SCAN_INTERVAL);
    scan->setWindow(BLE_SCAN_WINDOW);
    scan->setDuplicateFilter(false);
    scan->setMaxResults(0);
    active = true;
    startScan();
}

void bleLoop() {
    if (!active) return;
    // Restart the scan if the stack stopped it for some reason
    static unsigned long lastScanCheck = 0;
    if (millis() - lastScanCheck > 5000) {
        lastScanCheck = millis();
        startScan();
    }
}

bool bleActive() {
    return active;
}

void bleScanPause() {
    pauseCount++;
    if (scan && scan->isScanning()) scan->stop();
}

void bleScanResume() {
    if (pauseCount > 0) pauseCount--;
    if (pauseCount == 0) startScan();
}

bool bleConnectLock(const void* owner) {
    if (connectOwner && connectOwner != owner) return false;
    connectOwner = owner;
    return true;
}

void bleConnectUnlock(const void* owner) {
    if (connectOwner == owner) connectOwner = nullptr;
}

bool bleFindAddrType(const char* mac, uint8_t* type) {
    bool found = false;
    portENTER_CRITICAL(&seenMux);
    for (int i = 0; i < BLE_MAX_SEEN; i++) {
        if (seen[i].used && strcmp(seen[i].addr, mac) == 0) {
            *type = seen[i].addrType;
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&seenMux);
    return found;
}

int bleLastRssi(const char* mac) {
    int rssi = 0;
    unsigned long now = millis();
    portENTER_CRITICAL(&seenMux);
    for (int i = 0; i < BLE_MAX_SEEN; i++) {
        if (seen[i].used && strcmp(seen[i].addr, mac) == 0 && now - seen[i].last < BLE_SEEN_TIMEOUT_MS) {
            rssi = seen[i].rssi;
            break;
        }
    }
    portEXIT_CRITICAL(&seenMux);
    return rssi;
}

uint32_t bleAdvCount() {
    return advCount;
}

bool bleScanning() {
    return scan && scan->isScanning();
}

void bleFillSeenList(JsonArray arr) {
    static SeenDevice copy[BLE_MAX_SEEN];   // static: keeps ~3 KB off the web server task stack
    portENTER_CRITICAL(&seenMux);
    memcpy(copy, seen, sizeof(copy));
    portEXIT_CRITICAL(&seenMux);

    unsigned long now = millis();
    // Highest priority first (configured device, recognized by a plugin, ...)
    for (int prio = 3; prio >= 0; prio--) {
        for (int i = 0; i < BLE_MAX_SEEN; i++) {
            const SeenDevice& d = copy[i];
            if (!d.used || d.prio != prio || now - d.last > BLE_SEEN_TIMEOUT_MS) continue;
            JsonObject o = arr.add<JsonObject>();
            // Cast to const char*: ArduinoJson 7.3/7.4 stores char arrays by pointer (would dangle)
            o["mac"] = (const char*)d.addr;
            o["name"] = (const char*)d.name;
            o["rssi"] = d.rssi;
            if (d.company >= 0) {
                char buf[7];
                snprintf(buf, sizeof(buf), "%04X", (unsigned)(d.company & 0xFFFF));
                o["mfg"] = (const char*)buf;
            } else {
                o["mfg"] = "";
            }
            o["uuid"] = (const char*)d.uuid;
            o["age"] = (now - d.last) / 1000;
            // Plugin types that recognize this device (web UI: "passt zu")
            JsonArray fits = o["fits"].to<JsonArray>();
            FOR_EACH_PLUGIN(p) {
                if (p->bleUse() == BLE_NONE) continue;
                if (p->bleCandidate(d.company, d.uuid, d.name)) {
                    bool dup = false;
                    for (JsonVariant v : fits) if (strcmp(v.as<const char*>(), p->type) == 0) dup = true;
                    if (!dup) fits.add(p->type);
                }
            }
        }
    }
}

void blePrintSeenList() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    bleFillSeenList(arr);
    Serial.printf("[BLE] %u devices nearby:\n", (unsigned)arr.size());
    for (JsonObject o : arr) {
        Serial.printf("  %s  %4d dBm  mfg:%-4s uuid:%s  %s\n",
            o["mac"].as<const char*>(), o["rssi"].as<int>(),
            o["mfg"].as<const char*>(), o["uuid"].as<const char*>(), o["name"].as<const char*>());
    }
}

#else  // !CT_USE_BLE: no Bluetooth plugin compiled in

void bleInit() {}
void bleLoop() {}
bool bleActive() { return false; }
void bleScanPause() {}
void bleScanResume() {}
bool bleConnectLock(const void*) { return false; }
void bleConnectUnlock(const void*) {}
bool bleFindAddrType(const char*, uint8_t*) { return false; }
int bleLastRssi(const char*) { return 0; }
uint32_t bleAdvCount() { return 0; }
bool bleScanning() { return false; }
void bleFillSeenList(JsonArray) {}
void blePrintSeenList() { Serial.println("[BLE] No Bluetooth plugin compiled in"); }

#endif
