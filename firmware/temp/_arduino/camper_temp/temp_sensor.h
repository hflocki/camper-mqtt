#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include "config.h"

void tempInit();
void tempLoop();

// Search the 1-Wire bus again (on the next loop, the search must not run in the web task)
void tempRequestScan();

// Apply a new resolution / sensor list from the web UI (on the next loop)
void tempRequestReconfigure();

// Reset min/max of all sensors
void tempResetMinMax();

// Address helpers: "28AA1D6700000B" style (16 hex digits, byte 0 first)
void addrToHex(const uint8_t* addr, char* out);   // out: at least 17 bytes
bool hexToAddr(const char* hex, uint8_t* addr);   // false on invalid input or bad CRC

// Index of a configured sensor with this address, -1 if none
int findSensorByAddr(const uint8_t* addr);

void tempPrintBus();

#endif // TEMP_SENSOR_H
