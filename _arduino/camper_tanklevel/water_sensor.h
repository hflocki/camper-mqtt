#ifndef WATER_SENSOR_H
#define WATER_SENSOR_H

#include <Arduino.h>
#include "config.h"

// Fresh water level: Votronic analog tank sender on ADC_PIN
bool waterInit();
void waterLoop();

#endif // WATER_SENSOR_H
