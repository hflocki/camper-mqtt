// WiFi station with optional static IP, access point (+ captive portal) for setup and as fallback.
#pragma once

#include <Arduino.h>

namespace wifi {

void begin();
void loop();
bool connected();
bool ap_active();
String ip();
int rssi();
String ap_ssid();          // configured name or <hostname>-XXXX
int8_t tx_power();         // in 0.25 dBm
uint32_t drops();          // lost connections since boot
const char *last_reason(); // reason of the last lost connection
const char *state_text();  // German status text for the web UI
const char *reset_reason();

}  // namespace wifi
