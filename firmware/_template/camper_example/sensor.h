#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include "config.h"

// One published value. The list VALUES[] in sensor.cpp drives everything else:
// MQTT topics, Home Assistant entities and the tiles of the web UI.
struct ValueDef {
    const char* key;       // MQTT sub topic and id: <prefix>/<key>  (a-z, 0-9, _)
    const char* name;      // Name in Home Assistant and in the web UI
    const char* unit;      // "" = none
    const char* devClass;  // Home Assistant device_class, nullptr = none
    const char* icon;      // mdi icon
    uint8_t decimals;      // Digits after the decimal point (MQTT, web UI)
};

extern const ValueDef VALUES[];
extern const uint8_t VALUE_COUNT;

void sensorInit();
void sensorLoop();

// Measure right away (web UI button, MQTT command). Safe to call from the web task.
void sensorRequestMeasure();

// Serial command "read"
void sensorPrint();

#endif // SENSOR_H
