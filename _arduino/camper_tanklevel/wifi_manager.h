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

#endif // WIFI_MANAGER_H
