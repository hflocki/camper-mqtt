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

// Remember a sensor that was deleted or got a new id, so its old HA entity
// and retained topics are removed on the next loop
void mqttQueueRemoval(const char* addrHex, const char* oldKey, bool removeEntity);

// Readable text of all active alarms, "" if none
String alarmText();

#endif // MQTT_HA_H
