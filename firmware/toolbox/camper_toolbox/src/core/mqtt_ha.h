#ifndef CT_MQTT_HA_H
#define CT_MQTT_HA_H

#include <Arduino.h>
#include "../../config.h"

void mqttInit();
void mqttLoop();
bool mqttIsConnected();

// Re-send the discovery on the next loop (e.g. after a plugin setting changed entities)
void mqttRequestDiscovery();

// Home Assistant node ID derived from the MQTT prefix ("camper/toolbox" -> "camper_toolbox")
const char* mqttNodeId();

#endif // CT_MQTT_HA_H
