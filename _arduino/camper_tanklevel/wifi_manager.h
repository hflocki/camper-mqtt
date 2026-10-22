#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include "config.h"

enum WiFiState {
    WIFI_STATE_INIT,
    WIFI_STATE_AP,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_RECONNECTING,
    WIFI_STATE_FALLBACK_AP
};

void wifiManagerInit();
void wifiManagerLoop();
WiFiState getWiFiState();
String getWiFiStateString();
bool isConnected();
void startAP(int channel = 1);
void connectToWiFi();

// Reason of the last reset (power on, brownout, crash, watchdog ...) for the web UI
const char* resetReasonString();

// TX power values selectable in the web UI (0.25 dBm steps)
bool isValidTxPower(int v);

// Default access point name: DEFAULT_AP_SSID + "-" + last 6 hex digits of the WiFi MAC
const char* defaultApSsid();

#endif // WIFI_MANAGER_H
