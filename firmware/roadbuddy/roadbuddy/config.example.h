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

// Level sensor topic (pitch, roll, is_level, tolerance, status). Water and gas topics are defined at the top of roadbuddy.ino
const char* mqtt_topic = "camper/level/#";

// Mobile signal of the router for the signal bars in the header ("" = WiFi bars of this device only)
#define ROUTER_TOPIC "camper/router/mobile"

// Login for the update page (http://roadbuddy.local). Empty user = no login
#define WEB_USER ""
#define WEB_PASS ""

// Note: a build with real values here stores WiFi, MQTT and the web login in flash.
// Firmware from GitHub Actions (built with this example file) reads them back from there,
// so it can be installed over the update page. Flash once over USB with your own config.h first.

// ---------- Sensors ----------
// Choose which sensors RoadBuddy shows (1 = enabled, 0 = disabled).
// A disabled sensor gets no page and no MQTT subscription. The system page is always shown.
#define USE_LEVEL 1   // pitch / roll level sensor
#define USE_WATER 1   // fresh water tank
#define USE_GAS   1   // gas bottle
#define USE_TEMP  1   // temperatures from temp-node (sensors 1-4 on one page, 5-8 on a second)
#define USE_BATTERY 1 // batteries from battery-node (up to 3: charge, voltage, power, runtime)

#endif
