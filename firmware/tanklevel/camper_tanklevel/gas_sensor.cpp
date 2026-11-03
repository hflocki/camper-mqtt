#include "gas_sensor.h"
#include <NimBLEDevice.h>

// ============================================================
// State shared between the NimBLE host task (scan callback) and loop()
// ============================================================
static portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

struct SeenDevice {
    bool used;
    char addr[18];
    char name[24];
    char svc[40];          // UUID of the first service data block ("" = none)
    int32_t company;       // Company ID of the first manufacturer data block (-1 = none)
    uint8_t prio;          // List priority (see devicePriority)
    int rssi;
    unsigned long last;
};
static SeenDevice seen[BLE_MAX_SEEN];

// Reading handed over from the scan callback to loop()
static bool readingPending = false;
static bool pendingHasLevel = false;   // false = Mopeka read quality too low (battery etc. still valid)
static float pendingBattery = NAN;
static float pendingRaw = 0.0f;
static float pendingDist = NAN;
static float pendingTemp = NAN;
static int8_t pendingQuality = -1;
static int pendingRssi = 0;

// Last payload of the configured sensor (shown as hex in the web UI)
static uint8_t rawBuf[BLE_RAW_MAX];
static size_t rawLen = 0;
static bool rawPending = false;

static NimBLEScan* scan = nullptr;
static volatile uint32_t advCount = 0;  // Advertisements received (diagnosis)

// Compile-time sensor definition (see config.h)
static const ValueMap MAP_LEVEL = {
    true, LEVEL_OFFSET, LEVEL_LEN, LEVEL_BIG_ENDIAN, LEVEL_FACTOR, LEVEL_ADD
};
static const ValueMap MAP_BATTERY = {
    BATTERY_ENABLED, BATTERY_OFFSET, BATTERY_LEN, BATTERY_BIG_ENDIAN, BATTERY_FACTOR, BATTERY_ADD
};

// ============================================================
// Helpers
// ============================================================

// NimBLE returns lower case addresses; the config uses upper case
static void toUpperAddr(const std::string& in, char* out, size_t size) {
    size_t i = 0;
    for (; i < in.size() && i < size - 1; i++) out[i] = toupper((unsigned char)in[i]);
    out[i] = '\0';
}

static void bytesToHex(const uint8_t* d, size_t n, char* out, size_t size) {
    size_t pos = 0;
    out[0] = '\0';
    for (size_t i = 0; i < n && pos + 3 < size; i++) {
        pos += snprintf(out + pos, size - pos, i ? " %02X" : "%02X", d[i]);
    }
}

// value = (unsigned integer at offset) * factor + add
static bool decodeValue(const ValueMap& m, const uint8_t* d, size_t n, float& out) {
    if (!m.enabled) return false;
    if (m.len == 0 || m.len > 4 || (size_t)m.offset + m.len > n) return false;
    uint32_t v = 0;
    for (uint8_t i = 0; i < m.len; i++) {
        uint8_t b = d[m.offset + i];
        if (m.big_endian) v = (v << 8) | b;
        else v |= (uint32_t)b << (8 * i);
    }
    out = (float)v * m.factor + m.add;
    return true;
}

// Level must be a plausible percentage; small rounding overshoot is clamped
static bool validLevel(float& v) {
    if (isnan(v) || v < -1.0f || v > 101.0f) return false;
    v = constrain(v, 0.0f, 100.0f);
    return true;
}

static float validBattery(float v) {
    if (isnan(v) || v < 0.0f || v > 100.0f) return NAN;
    return v;
}

static void storeRaw(const uint8_t* d, size_t n) {
    portENTER_CRITICAL(&dataMux);
    rawLen = n > BLE_RAW_MAX ? BLE_RAW_MAX : n;
    memcpy(rawBuf, d, rawLen);
    rawPending = true;
    portEXIT_CRITICAL(&dataMux);
}

#define COMPANY_APPLE 0x004C

// ============================================================
// Mopeka Pro family
// Manufacturer data, company 0x0059, 10 payload bytes (after the company ID):
//   [0] model, [1] battery (/32 = V), [2] bit 0-6 temperature + 40 °C, bit 7 sync button,
//   [3..4] bit 0-13 raw level, bit 14-15 read quality, [5..7] MAC tail, [8..9] accelerometer
// Raw level -> liquid height in mm with the propane coefficients published by Mopeka.
// Decoding as in the MIT licensed mopeka-iot-ble library used by Home Assistant
// (https://github.com/Bluetooth-Devices/mopeka-iot-ble).
// ============================================================
static bool mopekaModelKnown(uint8_t m) {
    // 3 Pro Check, 4 Pro-200, 5 Pro Check H2O, 6 Lippert BottleCheck, 8/9 Pro Plus,
    // 0x0A/0x0B TD40/TD200, 0x0C Pro Check Universal, 0x12 Pro-200B
    return m == 0x03 || m == 0x04 || m == 0x05 || m == 0x06 || m == 0x08 || m == 0x09 ||
           m == 0x0A || m == 0x0B || m == 0x0C || m == 0x12;
}

// ============================================================
// Mopeka Standard (Std Check, CC2540)
// Manufacturer data, company 0x000D, 23 payload bytes (after the company ID):
//   [1] & 0xCF hardware id (0x02 Standard, 0x03 XL, 0x44 Standard, 0x46 eTrailer)
//   [2] battery: V = raw / 256 * 2 + 1.5
//   [3] bit 0-5 temperature, bit 6 slow update rate, bit 7 sync button
//   [4..18] three blocks of 5 bytes = 40 bits little endian, each with four
//           (time, amplitude) pairs of 5 bits: time0, amp0, time1, amp1, ...
// The ultrasonic echo with the strongest amplitude gives the run time (10 us ticks,
// times are summed up until an echo is reported), height = speed of sound in LPG * time.
// Format as documented by the ESPHome mopeka_std_check component; own implementation.
// ============================================================
static bool mopekaStdModelKnown(uint8_t hw) {
    hw &= 0xCF;
    return hw == 0x02 || hw == 0x03 || hw == 0x44 || hw == 0x46;
}

// Speed of sound in liquid LPG in m/s; t = temperature in °C, butane = 0..1
static float lpgSpeedOfSound(float t, float butane) {
    return 1040.71f - 4.87f * t - 137.5f * butane - 0.0107f * t * t - 1.63f * t * butane;
}

// Returns false if the payload is not a Mopeka Standard advertisement.
// quality: 3 = usable echo found, 0 = no usable echo (height not valid)
static bool decodeMopekaStd(const uint8_t* d, size_t n, float& battery, float& temp, int8_t& quality, float& mm) {
    if (n != 23 || !mopekaStdModelKnown(d[1])) return false;

    float volt = d[2] / 256.0f * 2.0f + 1.5f;
    battery = constrain((volt - 2.2f) / 0.65f * 100.0f, 0.0f, 100.0f);   // CR2032
    uint8_t rawTemp = d[3] & 0x3F;
    temp = (rawTemp == 0) ? -40.0f : (float)(int)((rawTemp - 25.0f) * 1.776964f);

    // Unpack the 12 (time, amplitude) pairs
    uint8_t times[12], amps[12];
    for (int b = 0; b < 3; b++) {
        uint64_t bits = 0;
        for (int i = 0; i < 5; i++) bits |= (uint64_t)d[4 + b * 5 + i] << (8 * i);
        for (int k = 0; k < 4; k++) {
            times[b * 4 + k] = (bits >> (10 * k)) & 0x1F;
            amps[b * 4 + k]  = (bits >> (10 * k + 5)) & 0x1F;
        }
    }

    // Strongest echo; the time runs on until an echo is reported
    uint16_t elapsed = 0, bestTime = 0;
    uint8_t bestAmp = 0, echoes = 0;
    for (int i = 0; i < 12; i++) {
        elapsed += times[i] + 1;
        if (amps[i] == 0) continue;
        echoes++;
        if (amps[i] > bestAmp) { bestAmp = amps[i]; bestTime = elapsed; }
        elapsed = 0;
    }
    if (echoes < 1 || bestAmp < 2 || bestTime < 2) {
        quality = 0;
        mm = NAN;
        return true;
    }
    quality = 3;
    float butane = (100 - config.lpg_propane) / 100.0f;
    mm = lpgSpeedOfSound(temp, butane) * bestTime / 100.0f;
    return true;
}

static float mopekaLevelPercent(float mm) {
    float empty = config.mopeka_empty_mm, full = config.mopeka_full_mm;
    if (full <= empty) return 0.0f;
    return constrain((mm - empty) / (full - empty) * 100.0f, 0.0f, 100.0f);
}

// Returns false if the payload is not a Mopeka Pro advertisement
static bool decodeMopeka(const uint8_t* d, size_t n, float& battery, float& temp, int8_t& quality, float& mm) {
    if (n != 10 || !mopekaModelKnown(d[0])) return false;
    float volt = (d[1] & 0x7F) / 32.0f;
    battery = constrain((volt - 2.2f) / 0.65f * 100.0f, 0.0f, 100.0f);   // CR2032
    int t = d[2] & 0x7F;
    temp = t - 40;
    uint16_t raw = ((uint16_t)d[4] << 8 | d[3]) & 0x3FFF;
    quality = d[4] >> 6;
    mm = raw * (0.573045f - 0.002822f * t - 0.00000535f * t * t);        // propane / LPG
    return true;
}

// Higher = more important. Keeps the gas sensor in the list even when many phones are around.
//   3 = configured sensor, 2 = company ID matches the sensor definition,
//   1 = other device with data, 0 = Apple devices (rotating addresses) / no data
static uint8_t devicePriority(const char* addr, const char* svc, int32_t company) {
    if (config.sensor_mac[0] && strcmp(addr, config.sensor_mac) == 0) return 3;
    if (config.gas_type == GAS_TYPE_MOPEKA) {
        if (company == MOPEKA_COMPANY_ID) return 2;
    } else if (config.gas_type == GAS_TYPE_MOPEKA_STD) {
        if (company == MOPEKA_STD_COMPANY_ID) return 2;
    } else if (SENSOR_SOURCE == SRC_MANUFACTURER && SENSOR_COMPANY_ID != COMPANY_ANY &&
               company == SENSOR_COMPANY_ID) {
        return 2;
    }
    if (company == COMPANY_APPLE) return 0;
    if (company >= 0 || svc[0] != '\0') return 1;
    return 0;
}

static void rememberDevice(const char* addr, const char* name, const char* svc, int32_t company, int rssi) {
    unsigned long now = millis();
    uint8_t prio = devicePriority(addr, svc, company);
    int match = -1, freeSlot = -1, victim = -1;

    portENTER_CRITICAL(&dataMux);
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
        portEXIT_CRITICAL(&dataMux);
        return;
    }
    SeenDevice& d = seen[slot];
    if (match < 0) {
        memset(&d, 0, sizeof(d));
        strlcpy(d.addr, addr, sizeof(d.addr));
        d.company = -1;
    }
    d.used = true;
    d.rssi = rssi;
    d.last = now;
    if (company >= 0) d.company = company;
    if (svc[0]) strlcpy(d.svc, svc, sizeof(d.svc));
    if (name && name[0]) strlcpy(d.name, name, sizeof(d.name));
    d.prio = devicePriority(d.addr, d.svc, d.company);
    portEXIT_CRITICAL(&dataMux);
}

// Calculates level and content like the Home Assistant integration (fill stop optional)
static void computeDerived() {
    float fillStop = config.fill_stop_on ? config.fill_stop : 100.0f;
    if (fillStop <= 0.0f) fillStop = 100.0f;

    // Mopeka measures between "empty" and "full" height: that is already the usable range
    float level = (config.gas_type != GAS_TYPE_DEFINITION) ? sensorData.gas.raw_level
                                                       : sensorData.gas.raw_level / fillStop * 100.0f;
    if (level > 100.0f) level = 100.0f;
    if (level < 0.0f) level = 0.0f;

    float usable = config.capacity * fillStop / 100.0f;
    sensorData.gas.level = level;
    sensorData.gas.content = level / 100.0f * usable;
}

static void applyReading(bool hasLevel, float battery, float raw, float dist, float temp, int8_t quality, int rssi) {
    if (!isnan(battery)) sensorData.gas.battery = battery;
    sensorData.gas.temperature = temp;
    sensorData.gas.quality = quality;
    sensorData.gas.ble_rssi = rssi;
    if (!hasLevel) {
        // Mopeka with poor read quality: keep the last level, only show the quality
        sensorData.gas.updates++;
        Serial.printf("[BLE] Mopeka read quality %d too low - level ignored\n", quality);
        return;
    }
    sensorData.gas.raw_level = raw;
    sensorData.gas.distance_mm = dist;
    computeDerived();
    sensorData.gas.last_update = millis();
    sensorData.gas.has_data = true;
    sensorData.gas.updates++;

    Serial.printf("[BLE] Battery: %.0f%% | Raw: %.1f%% | Level: %.1f%% | Content: %.1f L | RSSI: %d\n",
        sensorData.gas.battery, raw, sensorData.gas.level, sensorData.gas.content, rssi);
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
        char svc[40] = "";
        if (dev->getServiceDataCount() > 0) {
            strlcpy(svc, dev->getServiceDataUUID(0).toString().c_str(), sizeof(svc));
        }
        std::string name = dev->haveName() ? dev->getName() : std::string();
        rememberDevice(addr, name.c_str(), svc, company, dev->getRSSI());

        if (config.sensor_mac[0] == '\0' || strcmp(addr, config.sensor_mac) != 0) return;

        // Find the payload: manufacturer data (without the 2 company ID bytes) or service data
        std::string payload;
        bool found = false;

        if (config.gas_type == GAS_TYPE_MOPEKA || config.gas_type == GAS_TYPE_MOPEKA_STD) {
            const bool pro = (config.gas_type == GAS_TYPE_MOPEKA);
            const int32_t wanted = pro ? MOPEKA_COMPANY_ID : MOPEKA_STD_COMPANY_ID;
            for (uint8_t i = 0; i < dev->getManufacturerDataCount(); i++) {
                std::string m = dev->getManufacturerData(i);
                if (m.size() < 2 || ((uint8_t)m[0] | ((uint8_t)m[1] << 8)) != wanted) continue;
                payload = m.substr(2);
                found = true;
                break;
            }
            if (!found) return;
            const uint8_t* d = (const uint8_t*)payload.data();
            storeRaw(d, payload.size());
            float battery, temp, mm;
            int8_t quality;
            bool ok = pro ? decodeMopeka(d, payload.size(), battery, temp, quality, mm)
                          : decodeMopekaStd(d, payload.size(), battery, temp, quality, mm);
            if (!ok) return;
            bool good = quality >= MOPEKA_MIN_QUALITY;
            portENTER_CRITICAL(&dataMux);
            pendingHasLevel = good;
            pendingRaw = good ? mopekaLevelPercent(mm) : 0.0f;
            pendingDist = mm;
            pendingTemp = temp;
            pendingQuality = quality;
            pendingBattery = battery;
            pendingRssi = dev->getRSSI();
            readingPending = true;
            portEXIT_CRITICAL(&dataMux);
            return;
        }

        if (SENSOR_SOURCE == SRC_MANUFACTURER) {
            for (uint8_t i = 0; i < dev->getManufacturerDataCount(); i++) {
                std::string m = dev->getManufacturerData(i);
                if (m.size() < 2) continue;
                int32_t id = (uint8_t)m[0] | ((uint8_t)m[1] << 8);
                if (SENSOR_COMPANY_ID == COMPANY_ANY || id == SENSOR_COMPANY_ID) {
                    payload = m.substr(2);
                    found = true;
                    break;
                }
            }
        } else if (SENSOR_SOURCE == SRC_SERVICE_DATA && SENSOR_UUID[0]) {
            payload = dev->getServiceData(NimBLEUUID(std::string(SENSOR_UUID)));
            found = !payload.empty();
        }
        if (!found) return;

        const uint8_t* d = (const uint8_t*)payload.data();
        storeRaw(d, payload.size());

        float level;
        if (!decodeValue(MAP_LEVEL, d, payload.size(), level) || !validLevel(level)) return;
        float battery = NAN;
        if (decodeValue(MAP_BATTERY, d, payload.size(), battery)) battery = validBattery(battery);

        portENTER_CRITICAL(&dataMux);
        pendingHasLevel = true;
        pendingRaw = level;
        pendingDist = NAN;
        pendingTemp = NAN;
        pendingQuality = -1;
        pendingBattery = battery;
        pendingRssi = dev->getRSSI();
        readingPending = true;
        portEXIT_CRITICAL(&dataMux);
    }
};
static ScanCallbacks scanCallbacks;

static void startScan() {
    if (!scan || scan->isScanning()) return;
    // 0 = scan forever, results are not stored (setMaxResults(0)) to save RAM
    if (!scan->start(0, false, true)) {
        Serial.println("[BLE] Scan start FAILED");
    } else {
        Serial.println("[BLE] Scan started");
    }
}

// ============================================================
// Public API
// ============================================================

bool gasInit() {
    Serial.println("[BLE] Initializing NimBLE...");
    memset(seen, 0, sizeof(seen));
    sensorData.gas.battery = NAN;
    sensorData.gas.distance_mm = NAN;
    sensorData.gas.temperature = NAN;
    sensorData.gas.quality = -1;

    NimBLEDevice::init("");
    scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks, true);  // true = report every advertisement
    scan->setActiveScan(false);                    // Passive: battery friendly for the sensors
    scan->setInterval(BLE_SCAN_INTERVAL);
    scan->setWindow(BLE_SCAN_WINDOW);
    scan->setDuplicateFilter(false);
    scan->setMaxResults(0);
    startScan();

    strlcpy(sensorData.gas.ble_state, "Warte auf Daten", sizeof(sensorData.gas.ble_state));
    Serial.printf("[BLE] Sensor: %s  MAC: %s  Source: %u\n",
        config.gas_type == GAS_TYPE_MOPEKA ? "Mopeka Pro" :
        config.gas_type == GAS_TYPE_MOPEKA_STD ? "Mopeka Standard" : SENSOR_MODEL,
        config.sensor_mac[0] ? config.sensor_mac : "(not set)", (unsigned)SENSOR_SOURCE);
    return true;
}

void gasLoop() {
    // Values and raw bytes from the scan callback
    bool pending = false, hasLevel = false;
    float battery = NAN, raw = 0, dist = NAN, temp = NAN;
    int8_t quality = -1;
    int rssi = 0;
    portENTER_CRITICAL(&dataMux);
    if (readingPending) {
        pending = true;
        hasLevel = pendingHasLevel;
        battery = pendingBattery;
        raw = pendingRaw;
        dist = pendingDist;
        temp = pendingTemp;
        quality = pendingQuality;
        rssi = pendingRssi;
        readingPending = false;
    }
    uint8_t rawCopy[BLE_RAW_MAX];
    size_t rawCopyLen = 0;
    bool newRaw = rawPending;
    if (newRaw) {
        rawCopyLen = rawLen;
        memcpy(rawCopy, rawBuf, rawLen);
        rawPending = false;
    }
    portEXIT_CRITICAL(&dataMux);

    if (newRaw) bytesToHex(rawCopy, rawCopyLen, sensorData.gas.raw_hex, sizeof(sensorData.gas.raw_hex));
    if (pending) applyReading(hasLevel, battery, raw, dist, temp, quality, rssi);

    sensorData.gas.adv_count = advCount;
    sensorData.gas.scanning = scan && scan->isScanning();

    // Restart the scan if the stack stopped it for some reason
    static unsigned long lastScanCheck = 0;
    if (millis() - lastScanCheck > 5000) {
        lastScanCheck = millis();
        startScan();
    }

    // Human readable state
    const unsigned long timeout = DATA_TIMEOUT_MS;
    const char* state;
    if (config.sensor_mac[0] == '\0')                     state = "Kein Sensor";
    else if (!sensorData.gas.has_data)                        state = "Warte auf Daten";
    else if (millis() - sensorData.gas.last_update > timeout) state = "Keine Daten";
    else                                                  state = "OK";
    strlcpy(sensorData.gas.ble_state, state, sizeof(sensorData.gas.ble_state));
}

void gasReset() {
    portENTER_CRITICAL(&dataMux);
    readingPending = false;
    rawPending = false;
    portEXIT_CRITICAL(&dataMux);

    sensorData.gas.has_data = false;
    sensorData.gas.battery = NAN;
    sensorData.gas.raw_level = 0;
    sensorData.gas.level = 0;
    sensorData.gas.content = 0;
    sensorData.gas.ble_rssi = 0;
    sensorData.gas.distance_mm = NAN;
    sensorData.gas.temperature = NAN;
    sensorData.gas.quality = -1;
    sensorData.gas.raw_hex[0] = '\0';
    sensorData.gas.updates++;
    strlcpy(sensorData.gas.ble_state, "Warte auf Daten", sizeof(sensorData.gas.ble_state));
}

void gasRecalculate() {
    // Tank size / fill stop changed: recalculate from the last raw value (age stays unchanged)
    if (sensorData.gas.has_data) {
        // Mopeka: empty/full height may have changed, the level comes from the stored height
        if (config.gas_type != GAS_TYPE_DEFINITION && !isnan(sensorData.gas.distance_mm)) {
            sensorData.gas.raw_level = mopekaLevelPercent(sensorData.gas.distance_mm);
        }
        computeDerived();
        sensorData.gas.updates++;
    }
}

void gasFillSeenList(JsonArray arr) {
    static SeenDevice copy[BLE_MAX_SEEN];   // static: keeps ~2.5 KB off the web server task stack
    portENTER_CRITICAL(&dataMux);
    memcpy(copy, seen, sizeof(copy));
    portEXIT_CRITICAL(&dataMux);

    unsigned long now = millis();
    // Highest priority first (configured sensor, matching company ID, ...)
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
            o["svc"] = (const char*)d.svc;
            o["age"] = (now - d.last) / 1000;
        }
    }
}

void gasPrintSeenList() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    gasFillSeenList(arr);
    Serial.printf("[BLE] %u devices nearby:\n", (unsigned)arr.size());
    for (JsonObject o : arr) {
        Serial.printf("  %s  %4d dBm  mfg:%-4s svc:%s  %s\n",
            o["mac"].as<const char*>(), o["rssi"].as<int>(),
            o["mfg"].as<const char*>(), o["svc"].as<const char*>(), o["name"].as<const char*>());
    }
    Serial.printf("[BLE] Raw data of configured sensor: %s\n", sensorData.gas.raw_hex);
}
