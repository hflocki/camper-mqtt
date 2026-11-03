// Plugin "dimes": Rotarex DIMES / SRG WAVE gas level sensor (passive BLE advertising)
// Value mapping in config.h (DIMES_*), so other simple advertising sensors fit as well.

#include "gas_base.h"

#if PLUGIN_DIMES

#include "../core/util.h"
#include <NimBLEDevice.h>

struct DimesCfg {
    GasCommon g;
};

static const Field DIMES_FIELDS[] = {
    GAS_COMMON_FIELDS(DimesCfg),
};

// value = (unsigned integer at offset) * factor + add
static bool decodeValue(const uint8_t* d, size_t n, uint8_t offset, uint8_t len, bool bigEndian,
                        float factor, float add, float& out) {
    if (len == 0 || len > 4 || (size_t)offset + len > n) return false;
    uint32_t v = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t b = d[offset + i];
        if (bigEndian) v = (v << 8) | b;
        else v |= (uint32_t)b << (8 * i);
    }
    out = (float)v * factor + add;
    return true;
}

class DimesPlugin : public GasPlugin {
public:
    explicit DimesPlugin(uint8_t index) : GasPlugin("dimes", DIMES_MODEL, index) {}

    size_t fields(const Field*& out) override {
        out = DIMES_FIELDS;
        return sizeof(DIMES_FIELDS) / sizeof(DIMES_FIELDS[0]);
    }
    void* cfg() override { return &c; }
    void setDefaults() override {
        memset(&c, 0, sizeof(c));
        c.g.capacity = DEFAULT_CAPACITY;
        c.g.fill_stop_on = DEFAULT_FILL_STOP_ON;
        c.g.fill_stop = DEFAULT_FILL_STOP;
    }
    const char* manufacturer() const override { return "Rotarex"; }

    bool bleCandidate(int32_t company, const char* uuid, const char* name) const override {
        if (DIMES_SOURCE == SRC_MANUFACTURER) return DIMES_COMPANY_ID != COMPANY_ANY && company == DIMES_COMPANY_ID;
        return DIMES_UUID[0] && containsIgnoreCase(uuid, DIMES_UUID);
    }

protected:
    GasCommon& common() override { return c.g; }
    const GasCommon& common() const override { return c.g; }

    bool decode(const NimBLEAdvertisedDevice* dev, GasReading& r, uint8_t* raw, size_t& rawLen) override {
        // Payload: manufacturer data (without the 2 company ID bytes) or service data
        std::string payload;
        bool found = false;
        if (DIMES_SOURCE == SRC_MANUFACTURER) {
            for (uint8_t i = 0; i < dev->getManufacturerDataCount(); i++) {
                std::string m = dev->getManufacturerData(i);
                if (m.size() < 2) continue;
                int32_t cid = (uint8_t)m[0] | ((uint8_t)m[1] << 8);
                if (DIMES_COMPANY_ID == COMPANY_ANY || cid == DIMES_COMPANY_ID) {
                    payload = m.substr(2);
                    found = true;
                    break;
                }
            }
        } else if (DIMES_UUID[0]) {
            payload = dev->getServiceData(NimBLEUUID(std::string(DIMES_UUID)));
            found = !payload.empty();
        }
        if (!found) return false;

        const uint8_t* d = (const uint8_t*)payload.data();
        size_t n = payload.size();
        rawLen = n > BLE_RAW_MAX ? BLE_RAW_MAX : n;
        memcpy(raw, d, rawLen);

        float level;
        if (!decodeValue(d, n, DIMES_LEVEL_OFFSET, DIMES_LEVEL_LEN, DIMES_LEVEL_BIG_ENDIAN,
                         DIMES_LEVEL_FACTOR, DIMES_LEVEL_ADD, level)) return false;
        // Level must be a plausible percentage; small rounding overshoot is clamped
        if (isnan(level) || level < -1.0f || level > 101.0f) return false;
        r.raw = constrain(level, 0.0f, 100.0f);
        r.hasLevel = true;

        float bat;
        if (DIMES_BATTERY_ENABLED &&
            decodeValue(d, n, DIMES_BATTERY_OFFSET, DIMES_BATTERY_LEN, DIMES_BATTERY_BIG_ENDIAN,
                        DIMES_BATTERY_FACTOR, DIMES_BATTERY_ADD, bat) &&
            bat >= 0.0f && bat <= 100.0f) {
            r.battery = bat;
        }
        return true;
    }

private:
    DimesCfg c;
};

Plugin* createDimes(uint8_t index) {
    return new DimesPlugin(index);
}

#endif // PLUGIN_DIMES
