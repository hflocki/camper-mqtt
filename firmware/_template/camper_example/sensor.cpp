// Sensor part of the device. This is the file you fill with life.
//
// The example reads an analog input (SENSOR_PIN), averages a few samples and
// publishes three values: the raw millivolts, a scaled voltage (scale / offset
// from the web UI) and the number of readings since boot.
//
// To build your own device:
//   1. Change VALUES[] below: one line per value you want to publish.
//   2. Put your hardware setup into sensorInit().
//   3. Fill measure(): write sensorData.values[i] in the same order as VALUES[].
//   Everything else (MQTT, Home Assistant, web UI tiles) follows the list automatically.
#include "sensor.h"

// TODO: your values. Order = index into sensorData.values[]
const ValueDef VALUES[] = {
//   key        name              unit   device_class  icon                 decimals
    {"voltage", "Spannung",       "V",   "voltage",    "mdi:flash",         2},
    {"raw_mv",  "Rohwert",        "mV",  "voltage",    "mdi:sine-wave",     0},
    {"reads",   "Messungen",      "",    nullptr,      "mdi:counter",       0},
};
const uint8_t VALUE_COUNT = sizeof(VALUES) / sizeof(VALUES[0]);
static_assert(sizeof(VALUES) / sizeof(VALUES[0]) <= MAX_VALUES, "Too many values, raise MAX_VALUES");

static unsigned long lastMeasure = 0;
static volatile bool measureRequested = true;   // first reading right after boot

void sensorInit() {
    for (uint8_t i = 0; i < MAX_VALUES; i++) sensorData.values[i] = NAN;

    // TODO: hardware setup (pins, I2C, UART, BLE ...)
    analogReadResolution(12);
    pinMode(SENSOR_PIN, INPUT);
    Serial.printf("[Sensor] Example sensor on GPIO%d\n", SENSOR_PIN);
}

// One reading. Returns false if the sensor did not answer (values stay as they are).
static bool measure() {
    // TODO: replace with your sensor
    uint32_t sum = 0;
    const int samples = 16;
    for (int i = 0; i < samples; i++) sum += analogReadMilliVolts(SENSOR_PIN);
    float mv = sum / (float)samples;

    sensorData.values[0] = mv / 1000.0f * config.scale + config.offset;   // voltage
    sensorData.values[1] = mv;                                            // raw_mv
    sensorData.values[2] = sensorData.readCount + 1;                      // reads
    return true;
}

void sensorLoop() {
    unsigned long now = millis();
    if (!measureRequested && now - lastMeasure < (unsigned long)config.interval_s * 1000UL) return;
    measureRequested = false;
    lastMeasure = now;

    if (measure()) {
        sensorData.ok = true;
        sensorData.last_ok = now;
    } else {
        sensorData.ok = false;
        sensorData.errors++;
    }
    sensorData.readCount++;   // MQTT publishes after every cycle
}

void sensorRequestMeasure() {
    measureRequested = true;
}

void sensorPrint() {
    for (uint8_t i = 0; i < VALUE_COUNT; i++) {
        Serial.printf("[READ] %-16s %10.*f %s -> %s/%s\n", VALUES[i].name, VALUES[i].decimals,
                      sensorData.values[i], VALUES[i].unit, config.mqtt_prefix, VALUES[i].key);
    }
}
