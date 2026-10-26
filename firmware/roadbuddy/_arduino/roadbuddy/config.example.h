#ifndef CONFIG_H
#define CONFIG_H

// Copy this file to config.h and fill in your own values.
// config.h is listed in .gitignore and must never be committed.

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// MQTT broker
const char* mqtt_server = "192.168.1.10";
const int mqtt_port = 1883;
const char* mqtt_user = "YOUR_MQTT_USER";
const char* mqtt_pass = "YOUR_MQTT_PASSWORD";

// Level sensor topic (pitch / roll). Water and gas topics are defined at the top of roadbuddy.ino
const char* mqtt_topic = "camper/level/#";

// ---------- Sensors ----------
// Choose which sensors RoadBuddy shows (1 = enabled, 0 = disabled).
// A disabled sensor gets no page and no MQTT subscription. The system page is always shown.
#define USE_LEVEL 1   // pitch / roll level sensor
#define USE_WATER 1   // fresh water tank
#define USE_GAS   1   // gas bottle
#define USE_TEMP  1   // temperatures from camper-temp (sensors 1-4 on one page, 5-8 on a second)
#define USE_BATTERY 1 // batteries from camper-battery (up to 3: charge, voltage, power, runtime)

#endif
