// Plugins "mopeka" (Mopeka Pro family) and "mopeka_std" (Mopeka Standard / Std Check):
// ultrasonic gas level sensors, passive BLE advertising.

#include "gas_base.h"

#if PLUGIN_MOPEKA_PRO || PLUGIN_MOPEKA_STD

#include <NimBLEDevice.h>

struct MopekaCfg {
    GasCommon g;
    int32_t empty_mm;      // Liquid height of an empty bottle
    int32_t full_mm;       // Liquid height of a full bottle
    int32_t lpg_propane;   // Standard only: propane share in % (rest butane)
};

#define MOPEKA_HEIGHT_FIELDS \
    FLD_INT(MopekaCfg, empty_mm, "empty_mm", "Höhe leer", "mm", 0, 2000, \
            "Europa 6 kg 38/336, 11 kg 38/366, 14 kg 38/467, US 20 lb 38/254 mm"), \
    FLD_INT(MopekaCfg, full_mm, "full_mm", "Höhe voll", "mm", 1, 2000, nullptr)

static const Field MOPEKA_PRO_FIELDS[] = {
    GAS_COMMON_FIELDS(MopekaCfg),
    MOPEKA_HEIGHT_FIELDS,
};

static const Field MOPEKA_STD_FIELDS[] = {
    GAS_COMMON_FIELDS(MopekaCfg),
    MOPEKA_HEIGHT_FIELDS,
    FLD_INT(MopekaCfg, lpg_propane, "lpg_propane", "Propananteil", "%", 0, 100,
            "Rest Butan; bestimmt die Schallgeschwindigkeit im Flüssiggas"),
};

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

static bool decodeMopekaPro(const uint8_t* d, size_t n, GasReading& r, float& mm) {
    if (n != 10 || !mopekaModelKnown(d[0])) return false;
    float volt = (d[1] & 0x7F) / 32.0f;
    r.battery = constrain((volt - 2.2f) / 0.65f * 100.0f, 0.0f, 100.0f);   // CR2032
    int t = d[2] & 0x7F;
    r.temperature = t - 40;
    uint16_t raw = ((uint16_t)d[4] << 8 | d[3]) & 0x3FFF;
    r.quality = d[4] >> 6;
    mm = raw * (0.573045f - 0.002822f * t - 0.00000535f * t * t);        // propane / LPG
    return true;
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

// quality: 3 = usable echo found, 0 = no usable echo (height not valid)
static bool decodeMopekaStd(const uint8_t* d, size_t n, int propane, GasReading& r, float& mm) {
    if (n != 23 || !mopekaStdModelKnown(d[1])) return false;

    float volt = d[2] / 256.0f * 2.0f + 1.5f;
    r.battery = constrain((volt - 2.2f) / 0.65f * 100.0f, 0.0f, 100.0f);   // CR2032
    uint8_t rawTemp = d[3] & 0x3F;
    r.temperature = (rawTemp == 0) ? -40.0f : (float)(int)((rawTemp - 25.0f) * 1.776964f);

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
        r.quality = 0;
        mm = NAN;
        return true;
    }
    r.quality = 3;
    float butane = (100 - propane) / 100.0f;
    mm = lpgSpeedOfSound(r.temperature, butane) * bestTime / 100.0f;
    return true;
}

class MopekaPlugin : public GasPlugin {
public:
    MopekaPlugin(bool standard, uint8_t index)
        : GasPlugin(standard ? "mopeka_std" : "mopeka", standard ? "Mopeka Standard" : "Mopeka Pro", index),
          isStd(standard) {}

    size_t fields(const Field*& out) override {
        if (isStd) {
            out = MOPEKA_STD_FIELDS;
            return sizeof(MOPEKA_STD_FIELDS) / sizeof(MOPEKA_STD_FIELDS[0]);
        }
        out = MOPEKA_PRO_FIELDS;
        return sizeof(MOPEKA_PRO_FIELDS) / sizeof(MOPEKA_PRO_FIELDS[0]);
    }
    void* cfg() override { return &c; }
    void setDefaults() override {
        memset(&c, 0, sizeof(c));
        c.g.capacity = DEFAULT_CAPACITY;
        c.g.fill_stop_on = DEFAULT_FILL_STOP_ON;
        c.g.fill_stop = DEFAULT_FILL_STOP;
        c.empty_mm = DEFAULT_MOPEKA_EMPTY_MM;
        c.full_mm = DEFAULT_MOPEKA_FULL_MM;
        c.lpg_propane = DEFAULT_LPG_PROPANE;
    }
    const char* manufacturer() const override { return "Mopeka"; }

    bool bleCandidate(int32_t company, const char* uuid, const char* name) const override {
        return company == (isStd ? MOPEKA_STD_COMPANY_ID : MOPEKA_COMPANY_ID);
    }

protected:
    GasCommon& common() override { return c.g; }
    const GasCommon& common() const override { return c.g; }
    bool levelIsUsable() const override { return true; }   // Between "empty" and "full" height
    bool hasMopekaValues() const override { return true; }

    float levelFromDistance(float mm) override {
        if (isnan(mm)) return NAN;
        float empty = c.empty_mm, full = c.full_mm;
        if (full <= empty) return 0.0f;
        return constrain((mm - empty) / (full - empty) * 100.0f, 0.0f, 100.0f);
    }

    bool decode(const NimBLEAdvertisedDevice* dev, GasReading& r, uint8_t* raw, size_t& rawLen) override {
        const int32_t wanted = isStd ? MOPEKA_STD_COMPANY_ID : MOPEKA_COMPANY_ID;
        for (uint8_t i = 0; i < dev->getManufacturerDataCount(); i++) {
            std::string m = dev->getManufacturerData(i);
            if (m.size() < 2 || ((uint8_t)m[0] | ((uint8_t)m[1] << 8)) != wanted) continue;
            const uint8_t* d = (const uint8_t*)m.data() + 2;
            size_t n = m.size() - 2;
            rawLen = n > BLE_RAW_MAX ? BLE_RAW_MAX : n;
            memcpy(raw, d, rawLen);

            float mm;
            bool ok = isStd ? decodeMopekaStd(d, n, c.lpg_propane, r, mm) : decodeMopekaPro(d, n, r, mm);
            if (!ok) return false;
            r.hasLevel = r.quality >= MOPEKA_MIN_QUALITY && !isnan(mm);
            r.distance_mm = mm;
            r.raw = r.hasLevel ? levelFromDistance(mm) : 0.0f;
            return true;
        }
        return false;
    }

private:
    bool isStd;
    MopekaCfg c;
};

#if PLUGIN_MOPEKA_PRO
Plugin* createMopekaPro(uint8_t index) {
    return new MopekaPlugin(false, index);
}
#endif
#if PLUGIN_MOPEKA_STD
Plugin* createMopekaStd(uint8_t index) {
    return new MopekaPlugin(true, index);
}
#endif

#endif // PLUGIN_MOPEKA_PRO || PLUGIN_MOPEKA_STD
