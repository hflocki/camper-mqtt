#ifndef MQTT_HA_H
#define MQTT_HA_H

#include <Arduino.h>
#include "config.h"

void mqttInit();
void mqttLoop();
bool mqttIsConnected();

// Settings changed (web UI / serial): publish them with the next loop
void mqttRequestSettings();

#endif // MQTT_HA_H
