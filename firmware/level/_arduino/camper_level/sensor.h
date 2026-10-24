#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include "config.h"

bool sensorInit();
void sensorLoop();

// Tare = current position becomes "level". All of them save the config.
void sensorSetTare();
void sensorResetTare();
void sensorSetTareValues(float pitch, float roll);

// Soft reset of the BNO085 (e.g. if it hangs); no calibration data is lost
void sensorRestart();

// Recalculate the wedges after the vehicle size or tolerance changed
void sensorRecalc();

const char* mountName(MountOrientation m);
bool mountFromName(const String& name, MountOrientation& out);

#endif // SENSOR_H
