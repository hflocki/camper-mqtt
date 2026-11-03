#ifndef WATER_SENSOR_H
#define WATER_SENSOR_H

#include <Arduino.h>
#include "config.h"

// Fresh and grey water level: Votronic analog tank senders on ADC_PIN / GREY_ADC_PIN.
// Only the tanks enabled in the config are read.
bool waterInit();
void waterLoop();

#endif // WATER_SENSOR_H
