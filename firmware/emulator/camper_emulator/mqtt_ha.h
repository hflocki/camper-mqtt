#ifndef MQTT_HA_H
#define MQTT_HA_H

#include <Arduino.h>
#include "config.h"

void mqttInit();
void mqttLoop();
bool mqttIsConnected();                // the emulator's own connection
uint8_t mqttConnState(uint8_t dev);    // 1 = connection of this emulated device is up

// Kept for the template structure; the emulator sends no Home Assistant discovery
void mqttPublishDiscovery();
void mqttRequestDiscovery();

#endif // MQTT_HA_H
