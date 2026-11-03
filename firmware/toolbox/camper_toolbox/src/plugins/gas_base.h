#ifndef CT_GAS_BASE_H
#define CT_GAS_BASE_H

#include "../core/plugin.h"

#if CT_BLE_PASSIVE

// ============================================================
// Common part of all passive BLE gas level plugins (DIMES, Mopeka Pro, Mopeka Standard)
// ------------------------------------------------------------
// The subclass decodes one advertisement (decode(), NimBLE host task), the base class
// hands the reading over to loop(), computes level and content with fill stop and
// bottle size, and provides web UI, MQTT and Home Assistant entities.
// ============================================================

// Settings shared by all gas plugins; first member of every gas config struct
struct GasCommon {
    char mac[18];          // "AA:BB:CC:DD:EE:FF" (upper case), empty = no sensor
    float capacity;        // Gross bottle capacity in liters
    bool fill_stop_on;     // Bottle has a mechanical fill stop
    float fill_stop;       // Fill stop in %
};

#define GAS_COMMON_FIELDS(S) \
    FLD_MAC(S, g.mac, "mac", "Sensor-MAC", "Über \"Suchen\" aus der Liste wählen"), \
    FLD_FLOAT(S, g.capacity, "capacity", "Flaschengröße", "L", 1, 200, 0.1f, "Bruttovolumen, z. B. 11 kg = 27,2 L"), \
    FLD_BOOL(S, g.fill_stop_on, "fill_stop_on", "Füllstopp vorhanden", "Tankflaschen mit 80-%-Füllstopp"), \
    FLD_FLOAT(S, g.fill_stop, "fill_stop", "Füllstopp", "%", 10, 100, 1, nullptr)

// One decoded advertisement
struct GasReading {
    bool hasLevel;         // false = read quality too low (battery etc. still valid)
    float battery;         // % or NAN
    float raw;             // Raw level in % (Mopeka: % between empty and full height)
    float distance_mm;     // Mopeka: liquid height (NAN = none)
    float temperature;     // Mopeka: sensor temperature (NAN = none)
    int8_t quality;        // Mopeka: 0-3 (-1 = none)
    int rssi;
};

class GasPlugin : public Plugin {
public:
    GasPlugin(const char* type, const char* typeName, uint8_t index);

    uint8_t bleUse() const override { return BLE_PASSIVE; }
    void begin() override;
    void loop() override;
    void onConfigChanged() override;
    void onAdvertisement(const NimBLEAdvertisedDevice* dev, const char* addr) override;
    const char* bleAddress() const override { return common().mac; }

    void live(JsonObject o) override;
    size_t entities(const Entity*& out) override;
    bool entityActive(const Entity& e) override;
    void publish(MqttOut& out) override;
    uint32_t publishIntervalMs() const override { return MQTT_GAS_MS; }
    bool available() override;

protected:
    virtual GasCommon& common() = 0;
    virtual const GasCommon& common() const = 0;

    // Decodes the payload of this sensor type. Returns false if the advertisement is not
    // a valid reading of this type. raw/rawLen: payload for the hex view (max. BLE_RAW_MAX).
    virtual bool decode(const NimBLEAdvertisedDevice* dev, GasReading& r, uint8_t* raw, size_t& rawLen) = 0;

    // Raw level from a stored height (Mopeka: empty/full height changed). NAN = not supported.
    virtual float levelFromDistance(float mm) { return NAN; }
    // true = raw level already is the usable range (Mopeka), false = scale to the fill stop
    virtual bool levelIsUsable() const { return false; }
    // Mopeka entities (distance, temperature, quality) are shown
    virtual bool hasMopekaValues() const { return false; }

private:
    // Handed over from the NimBLE task
    portMUX_TYPE mux;
    bool pending = false;
    GasReading pendingReading;
    uint8_t rawBuf[BLE_RAW_MAX];
    size_t rawLen = 0;
    bool rawPending = false;

    // Values (loop task)
    bool hasData = false;
    float battery = NAN;
    float rawLevel = 0;
    float distance = NAN;
    float temperature = NAN;
    int8_t quality = -1;
    float level = 0;
    float content = 0;
    int bleRssi = 0;
    unsigned long lastUpdate = 0;
    char rawHex[BLE_RAW_MAX * 3 + 1] = "";
    char lastMac[18] = "";
    const char* state = "Kein Sensor";

    void computeDerived();
    void apply(const GasReading& r);
    void resetValues();
};

#endif // CT_BLE_PASSIVE
#endif // CT_GAS_BASE_H
