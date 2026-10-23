#ifndef GAS_SENSOR_H
#define GAS_SENSOR_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// Gas level: passive BLE advertising sensor (default: Rotarex DIMES / SRG WAVE)

bool gasInit();
void gasLoop();

// Call after the sensor source / MAC / mapping was changed in the web UI
void gasReset();

// Call after tank size or fill stop changed (keeps the last reading)
void gasRecalculate();

// Nearby BLE devices for the web UI (address, name, rssi, company ID, service data UUID)
void gasFillSeenList(JsonArray arr);
void gasPrintSeenList();

#endif // GAS_SENSOR_H
