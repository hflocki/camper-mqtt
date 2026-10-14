#include "water_sensor.h"

// State of one analog tank input (moving-average buffer)
struct TankChannel {
    const char* name;
    int pin;
    const TankConfig* cfg;
    WaterData* out;
    float readings[FILTER_WINDOW_SIZE];
    int readIndex;
    bool primed;
};

static TankChannel fresh = {"Fresh", ADC_PIN, &config.fresh, &sensorData.water, {0}, 0, false};
static TankChannel grey  = {"Grey",  GREY_ADC_PIN, &config.grey, &sensorData.grey, {0}, 0, false};

static unsigned long lastSensorRead = 0;

// Linear interpolation over the calibration table (sorted by voltage); returns the fill level in %
static float interpolatePercent(const TankConfig& cfg, float volt) {
    uint8_t n = cfg.calibCount;
    if (n == 0) return 0.0f;

    // Clamp below the first and above the last point
    if (volt <= cfg.calibTable[0].volt) return cfg.calibTable[0].percent;
    if (volt >= cfg.calibTable[n - 1].volt) return cfg.calibTable[n - 1].percent;

    for (int i = 0; i < n - 1; i++) {
        const CalibPoint& a = cfg.calibTable[i];
        const CalibPoint& b = cfg.calibTable[i + 1];
        if (volt >= a.volt && volt <= b.volt) {
            float vDiff = b.volt - a.volt;
            if (vDiff <= 0.0001f) return a.percent;  // Guard against division by zero
            float t = (volt - a.volt) / vDiff;
            return a.percent + t * (b.percent - a.percent);
        }
    }
    return 0.0f;
}

// Reads the pin voltage in volts using the chip's factory ADC calibration
static float readVoltage(int pin) {
    uint32_t sumMv = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sumMv += analogReadMilliVolts(pin);
    }
    return (sumMv / (float)ADC_SAMPLES) / 1000.0f;
}

static void initChannel(TankChannel& ch) {
    Serial.printf("[Water] %s tank on GPIO%d\n", ch.name, ch.pin);
    analogSetPinAttenuation(ch.pin, ADC_11db);   // Widest input range (approx. 0-2.5 V on the ESP32-C3)
    for (int i = 0; i < FILTER_WINDOW_SIZE; i++) ch.readings[i] = 0.0f;
    ch.readIndex = 0;
    ch.primed = false;
}

static void readChannel(TankChannel& ch) {
    WaterData& w = *ch.out;
    const TankConfig& cfg = *ch.cfg;

    float voltage = readVoltage(ch.pin);
    w.raw_voltage = voltage;

    // Prefill the filter with the first reading so the average does not start at 0 V
    if (!ch.primed) {
        for (int i = 0; i < FILTER_WINDOW_SIZE; i++) ch.readings[i] = voltage;
        ch.primed = true;
    }

    // Moving average (buffer is always updated, even if the filter is switched off)
    ch.readings[ch.readIndex] = voltage;
    ch.readIndex = (ch.readIndex + 1) % FILTER_WINDOW_SIZE;

    float sum = 0.0f;
    for (int i = 0; i < FILTER_WINDOW_SIZE; i++) sum += ch.readings[i];

    w.avg_voltage = cfg.filter_active ? (sum / FILTER_WINDOW_SIZE) : voltage;

    // Convert voltage to fill level in % using the table, then to liters using the tank size
    float percent = constrain(interpolatePercent(cfg, w.avg_voltage), 0.0f, 100.0f);
    w.percent = percent;
    w.liters = percent / 100.0f * cfg.tank_size;
}

bool waterInit() {
    analogReadResolution(12);   // 12 bit (0-4095)
    if (config.water_enabled) initChannel(fresh);
    if (config.grey_enabled) initChannel(grey);
    return true;
}

void waterLoop() {
    if (millis() - lastSensorRead < SENSOR_UPDATE_MS) return;
    lastSensorRead = millis();

    if (config.water_enabled) readChannel(fresh);
    if (config.grey_enabled) readChannel(grey);
}
