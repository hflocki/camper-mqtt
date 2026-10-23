// MQTT client, topics in the inetbox2mqtt layout and Home Assistant auto discovery.
//
// State:   <base>/control_status/<key>   (retained)
// Command: <base>/set/<key>
// LWT:     <base>/status                 online / offline
#pragma once

#include <Arduino.h>

namespace mqtt {

void begin();
void loop();
bool connected();
// Force a full state publish (e.g. after settings change).
void request_publish();

}  // namespace mqtt
