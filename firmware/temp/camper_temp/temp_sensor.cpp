#include "temp_sensor.h"

#include <OneWire.h>
#include <DallasTemperature.h>

static OneWire oneWire(ONEWIRE_PIN);
static DallasTemperature dallas(&oneWire);

static unsigned long lastRequest = 0;
static unsigned long conversionStart = 0;
static unsigned long lastScan = 0;
static bool conversionPending = false;
static volatile bool scanRequested = false;
static volatile bool reconfigureRequested = false;

// ============================================================
// Address helpers
// ============================================================
void addrToHex(const uint8_t* addr, char* out) {
    for (int i = 0; i < 8; i++) sprintf(out + i * 2, "%02X", addr[i]);
    out[16] = '\0';
}

bool hexToAddr(const char* hex, uint8_t* addr) {
    if (!hex || strlen(hex) != 16) return false;
    for (int i = 0; i < 8; i++) {
        char b[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        if (!isxdigit((unsigned char)b[0]) || !isxdigit((unsigned char)b[1])) return false;
        addr[i] = (uint8_t)strtoul(b, nullptr, 16);
    }
    return OneWire::crc8(addr, 7) == addr[7];
}

int findSensorByAddr(const uint8_t* addr) {
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (config.sensors[i].used && memcmp(config.sensors[i].addr, addr, 8) == 0) return i;
    }
    return -1;
}

// ============================================================
// Bus scan
// ============================================================
static void scanBus() {
    uint8_t addr[8];
    uint8_t count = 0;

    oneWire.reset_search();
    while (count < MAX_BUS_DEVICES && oneWire.search(addr)) {
        if (OneWire::crc8(addr, 7) != addr[7]) continue;   // Corrupted address
        // Temperature sensors only: DS18B20 (0x28), DS18S20 (0x10), DS1822 (0x22), DS1825 (0x3B)
        if (addr[0] != 0x28 && addr[0] != 0x10 && addr[0] != 0x22 && addr[0] != 0x3B) continue;
        memcpy(sensorData.bus[count].addr, addr, 8);
        sensorData.bus[count].temp = NAN;
        count++;
    }
    sensorData.busCount = count;
    lastScan = millis();

    // Mark which configured sensors are present
    for (int i = 0; i < MAX_SENSORS; i++) {
        sensorData.temps[i].present = false;
        if (!config.sensors[i].used) continue;
        for (uint8_t b = 0; b < count; b++) {
            if (memcmp(sensorData.bus[b].addr, config.sensors[i].addr, 8) == 0) {
                sensorData.temps[i].present = true;
                break;
            }
        }
    }

    // Parasite power / resolution apply to all devices found
    dallas.begin();
    dallas.setWaitForConversion(false);
    dallas.setResolution(config.resolution);

    Serial.printf("[Temp] Bus scan: %u sensor(s) found\n", count);
}

static void resetData(int i) {
    TempData& t = sensorData.temps[i];
    t.ok = false;
    t.temp = NAN;
    t.min = NAN;
    t.max = NAN;
    t.has_minmax = false;
    t.last_ok = 0;
    t.errors = 0;
    if (t.alarm != ALARM_NONE) sensorData.alarmSeq++;   // Alarm disappears: publish the change
    t.alarm = ALARM_NONE;
    t.pending = ALARM_NONE;
    t.pending_since = 0;
}

// Evaluates the limits of one sensor after a valid reading.
// Raise: limit violated continuously for alarm_delay minutes.
// Clear: value back inside the limit by ALARM_HYSTERESIS (right away).
// A failed reading leaves an active alarm as it is (the sensor shows as unavailable instead).
static void evalAlarm(int i) {
    const TempSensorConfig& s = config.sensors[i];
    TempData& t = sensorData.temps[i];
    uint8_t before = t.alarm;

    bool hasLo = !isnan(s.alarm_lo);
    bool hasHi = !isnan(s.alarm_hi);

    // Clear an active alarm (or one whose limit was removed in the web UI)
    if (t.alarm == ALARM_HIGH && (!hasHi || t.temp <= s.alarm_hi - ALARM_HYSTERESIS)) t.alarm = ALARM_NONE;
    if (t.alarm == ALARM_LOW  && (!hasLo || t.temp >= s.alarm_lo + ALARM_HYSTERESIS)) t.alarm = ALARM_NONE;

    uint8_t want = ALARM_NONE;
    if (hasHi && t.temp > s.alarm_hi) want = ALARM_HIGH;
    else if (hasLo && t.temp < s.alarm_lo) want = ALARM_LOW;

    if (want == ALARM_NONE || want == t.alarm) {
        t.pending = ALARM_NONE;
    } else if (t.pending != want) {
        t.pending = want;
        t.pending_since = millis();
    }
    if (t.pending != ALARM_NONE &&
        millis() - t.pending_since >= (unsigned long)s.alarm_delay * 60000UL) {
        t.alarm = t.pending;
        t.pending = ALARM_NONE;
    }

    if (t.alarm != before) {
        sensorData.alarmSeq++;
        Serial.printf("[Alarm] %s: %s (%.2f C)\n", s.name,
            t.alarm == ALARM_HIGH ? "too high" : t.alarm == ALARM_LOW ? "too low" : "cleared", t.temp);
    }
}

void tempInit() {
    pinMode(ONEWIRE_PIN, INPUT_PULLUP);   // Internal pull-up only helps; the external 4.7 kOhm is still required
    for (int i = 0; i < MAX_SENSORS; i++) resetData(i);
    scanBus();
    tempPrintBus();
    lastRequest = 0;   // First measurement right away
}

// A reading is valid if it is in the sensor range and not an error code.
// 85.0 °C is the power-on value of the DS18B20 (no conversion done yet); it is accepted
// only if the previous value of that sensor was already close to it.
static bool validReading(float raw, float previous) {
    if (raw == DEVICE_DISCONNECTED_C) return false;
    if (raw < -55.0f || raw > 125.0f) return false;
    if (raw == 85.0f && (isnan(previous) || fabsf(previous - 85.0f) > 5.0f)) return false;
    return true;
}

static void readAll() {
    for (uint8_t b = 0; b < sensorData.busCount; b++) {
        BusDevice& dev = sensorData.bus[b];
        float previous = dev.temp;
        float raw = dallas.getTempC(dev.addr);
        dev.temp = validReading(raw, previous) ? raw : NAN;
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!config.sensors[i].used) continue;
        TempData& t = sensorData.temps[i];

        float raw = NAN;
        for (uint8_t b = 0; b < sensorData.busCount; b++) {
            if (memcmp(sensorData.bus[b].addr, config.sensors[i].addr, 8) == 0) {
                raw = sensorData.bus[b].temp;
                break;
            }
        }

        if (isnan(raw)) {
            t.ok = false;
            t.errors++;
            continue;
        }

        float v = raw + config.sensors[i].offset;
        t.ok = true;
        t.temp = v;
        t.last_ok = millis();
        if (!t.has_minmax) {
            t.min = t.max = v;
            t.has_minmax = true;
        } else {
            if (v < t.min) t.min = v;
            if (v > t.max) t.max = v;
        }
        evalAlarm(i);
    }
    sensorData.readCount++;
}

void tempLoop() {
    if (reconfigureRequested) {
        reconfigureRequested = false;
        // Sensors may have been added, removed or re-assigned: start over
        for (int i = 0; i < MAX_SENSORS; i++) resetData(i);
        conversionPending = false;
        scanRequested = true;
    }

    // Re-scan on request or periodically (finds sensors that were plugged in later)
    bool anyMissing = false;
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (config.sensors[i].used && !sensorData.temps[i].present) anyMissing = true;
    }
    if (!conversionPending &&
        (scanRequested || millis() - lastScan > BUS_RESCAN_MS || (anyMissing && millis() - lastScan > 30000))) {
        scanRequested = false;
        scanBus();
        lastRequest = 0;
    }

    unsigned long interval = (unsigned long)config.interval_s * 1000UL;

    if (!conversionPending && (lastRequest == 0 || millis() - lastRequest >= interval)) {
        lastRequest = millis();
        if (sensorData.busCount == 0) {
            // Nothing on the bus: count the configured sensors as failed
            for (int i = 0; i < MAX_SENSORS; i++) {
                if (config.sensors[i].used) { sensorData.temps[i].ok = false; sensorData.temps[i].errors++; }
            }
            sensorData.readCount++;
            return;
        }
        dallas.requestTemperatures();   // Non-blocking (setWaitForConversion(false))
        conversionStart = millis();
        conversionPending = true;
    }

    if (conversionPending &&
        millis() - conversionStart >= dallas.millisToWaitForConversion(config.resolution) + 20) {
        conversionPending = false;
        readAll();
    }
}

void tempRequestScan() {
    scanRequested = true;
}

// Called from loop() (webserverLoop), so the live data can be cleared right away:
// slots may now belong to other sensors, old values must not be published under new ids
void tempRequestReconfigure() {
    for (int i = 0; i < MAX_SENSORS; i++) resetData(i);
    reconfigureRequested = true;
}

void tempResetMinMax() {
    for (int i = 0; i < MAX_SENSORS; i++) {
        TempData& t = sensorData.temps[i];
        t.has_minmax = t.ok;
        t.min = t.max = t.ok ? t.temp : NAN;
    }
}

void tempPrintBus() {
    char hex[17];
    Serial.printf("[Temp] %u device(s) on GPIO%d:\n", sensorData.busCount, ONEWIRE_PIN);
    for (uint8_t b = 0; b < sensorData.busCount; b++) {
        addrToHex(sensorData.bus[b].addr, hex);
        int idx = findSensorByAddr(sensorData.bus[b].addr);
        if (isnan(sensorData.bus[b].temp)) {
            Serial.printf("  %s  --.- C  %s\n", hex, idx >= 0 ? config.sensors[idx].name : "(neu)");
        } else {
            Serial.printf("  %s  %5.2f C  %s\n", hex, sensorData.bus[b].temp, idx >= 0 ? config.sensors[idx].name : "(neu)");
        }
    }
}
