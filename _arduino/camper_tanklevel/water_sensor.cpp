#include "water_sensor.h"

static unsigned long lastSensorRead = 0;

// Moving-average filter buffer
static const int WINDOW_SIZE = FILTER_WINDOW_SIZE;
static float readings[WINDOW_SIZE];
static int readIndex = 0;
static bool bufferPrimed = false;

// Linear interpolation over config.calibTable (sorted by voltage); returns the fill level in %
static float interpolatePercent(float volt) {
    uint8_t n = config.calibCount;
    if (n == 0) return 0.0f;

    // Clamp below the first and above the last point
    if (volt <= config.calibTable[0].volt) return config.calibTable[0].percent;
    if (volt >= config.calibTable[n - 1].volt) return config.calibTable[n - 1].percent;

    for (int i = 0; i < n - 1; i++) {
        const CalibPoint& a = config.calibTable[i];
        const CalibPoint& b = config.calibTable[i + 1];
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
static float readVoltage() {
    uint32_t sumMv = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sumMv += analogReadMilliVolts(ADC_PIN);
    }
    return (sumMv / (float)ADC_SAMPLES) / 1000.0f;
}

bool waterInit() {
    Serial.printf("[Water] Initializing analog input (GPIO%d)...\n", ADC_PIN);

    analogReadResolution(12);                        // 12 bit (0-4095)
    analogSetPinAttenuation(ADC_PIN, ADC_11db);      // Widest input range (approx. 0-2.5 V on the ESP32-C3)

    for (int i = 0; i < WINDOW_SIZE; i++) readings[i] = 0.0f;
    bufferPrimed = false;
    return true;
}

void waterLoop() {
    if (millis() - lastSensorRead < SENSOR_UPDATE_MS) return;
    lastSensorRead = millis();

    WaterData& w = sensorData.water;
    float voltage = readVoltage();
    w.raw_voltage = voltage;

    // Prefill the filter with the first reading so the average does not start at 0 V
    if (!bufferPrimed) {
        for (int i = 0; i < WINDOW_SIZE; i++) readings[i] = voltage;
        bufferPrimed = true;
    }

    // Moving average (buffer is always updated, even if the filter is switched off)
    readings[readIndex] = voltage;
    readIndex = (readIndex + 1) % WINDOW_SIZE;

    float sum = 0.0f;
    for (int i = 0; i < WINDOW_SIZE; i++) sum += readings[i];

    w.avg_voltage = config.filter_active ? (sum / WINDOW_SIZE) : voltage;

    // Convert voltage to fill level in % using the table, then to liters using the tank size
    float percent = constrain(interpolatePercent(w.avg_voltage), 0.0f, 100.0f);
    w.percent = percent;
    w.liters = percent / 100.0f * config.tank_size;
}
