#ifndef MQTT_HA_H
#define MQTT_HA_H

#include <Arduino.h>
#include "config.h"

void mqttInit();
void mqttLoop();
bool mqttIsConnected();
void mqttPublishDiscovery();

// Re-send the discovery on the next loop
void mqttRequestDiscovery();

#endif // MQTT_HA_H
