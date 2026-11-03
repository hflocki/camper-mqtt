#pragma once
#include <Arduino.h>

struct Settings {
  String   hostname;

  String   wifiSsid;
  String   wifiPass;
  bool     staticIp = false;
  String   ip, gateway, subnet, dns;
  String   apPass;

  String   mqttHost;
  uint16_t mqttPort = 1883;
  String   mqttUser;
  String   mqttPass;
  String   mqttPrefix;
  bool     discovery = true;

  String   webUser;          // empty = no login
  String   webPass;

  bool     portEnabled[2] = {false, true};
  uint16_t pulseMs = 150;
  uint8_t  syncPort = 1;
};

extern Settings settings;

void settingsLoad();
void settingsSave();
void settingsFactoryReset();
