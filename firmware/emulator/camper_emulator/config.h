#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
//  Camper Emulator - test device for the camper-mqtt display and Road Buddy:
//  sends the MQTT topics of the devices built into the van (tanks, level, heater,
//  roof fan, temperatures, batteries, router, Zigbee) with example values that
//  can be changed in its web UI. See emu.cpp.
// =====================================================================

#include <Arduino.h>

// Optional personal overrides, not in git (see config_local.h.example). Everything defined there
// wins over the defaults in this file, e.g. own passwords or WiFi / MQTT for a freshly flashed device.
#if __has_include("config_local.h")
#include "config_local.h"
#endif

// ============================================================
// GENERAL CONSTANTS
// ============================================================
#define FW_VERSION            "1.2.0"
#define FW_DEVICE            "emulator"   // device id in the firmware marker (fwcheck.h)
#define DEVICE_NAME           "Camper Emulator"   // web UI title, HA device name, serial banner

#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID       "Camper-Emulator"  // Base name; "-XXXXXX" (last 3 MAC bytes) is appended
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD   "emulator1234"      // Test placeholder (min. 8 characters) - change in config_local.h
#endif
#ifndef DEFAULT_OTA_PASSWORD
#define DEFAULT_OTA_PASSWORD  "emulator1234"      // Test placeholder - change in config_local.h
#endif
#ifndef DEFAULT_MQTT_SERVER
#define DEFAULT_MQTT_SERVER   ""                 // Set in the web UI
#endif
#ifndef DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PORT     1883
#endif

// ---- Factory defaults for WiFi / MQTT / web login -----------------------
// Used only while nothing is stored on the device yet: first boot, after "factory
// reset" or after flashing with "Erase All Flash". Put your own values into
// config_local.h, then a freshly flashed device connects right away.
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID       ""
#endif
#ifndef DEFAULT_WIFI_PASSWORD
#define DEFAULT_WIFI_PASSWORD   ""
#endif
#ifndef DEFAULT_MQTT_USER
#define DEFAULT_MQTT_USER       ""
#endif
#ifndef DEFAULT_MQTT_PASSWORD
#define DEFAULT_MQTT_PASSWORD   ""
#endif
#ifndef DEFAULT_WEB_USER
#define DEFAULT_WEB_USER        ""      // empty = no login
#endif
#ifndef DEFAULT_WEB_PASSWORD
#define DEFAULT_WEB_PASSWORD    ""
#endif
#ifndef DEFAULT_MQTT_PREFIX
#define DEFAULT_MQTT_PREFIX   "camper/emulator"   // Topics: <prefix>/<value key>, <prefix>/status, <prefix>/rssi
#endif

// WiFi TX power in 0.25 dBm steps: 70 = 17.5 dBm (factory default, selectable in the web UI)
#ifndef DEFAULT_TX_POWER
#define DEFAULT_TX_POWER      70
#endif

// Own access point while connected to the WiFi (web UI, tab "WiFi"):
//   true  = AP always on (reachable at 192.168.4.1 at any time)
//   false = AP only as fallback when the WiFi is not reachable (more stable STA connection)
#ifndef DEFAULT_AP_ALWAYS
#define DEFAULT_AP_ALWAYS     false
#endif
#ifndef AP_FALLBACK_MS
#define AP_FALLBACK_MS        60000  // AP-only-as-fallback: start the AP after 60 s without WiFi
#endif

#ifndef MQTT_REPUBLISH_MS
#define MQTT_REPUBLISH_MS     60000  // Re-send all values at least this often (changes go out within a second)
#endif
#ifndef WS_UPDATE_MS
#define WS_UPDATE_MS          1000   // Web UI live update interval
#endif

#ifndef CONFIG_FILE
#define CONFIG_FILE           "/config.json"
#endif
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME          "camper-emulator"   // also the mDNS name: camper-emulator.local
#endif

// ============================================================
// CONFIGURATION STRUCT (stored in /config.json)
// ============================================================
struct AppConfig {
    char wifi_ssid[64];
    char wifi_pass[64];
    char ap_ssid[32];
    char ap_pass[32];
    int8_t wifi_tx_power;  // TX power in 0.25 dBm steps (see TX_POWER_OPTIONS)
    bool ap_always;        // true = own AP stays on while connected, false = fallback only

    char mqtt_server[64];
    uint16_t mqtt_port;
    char mqtt_user[32];
    char mqtt_pass[32];
    char mqtt_prefix[32];

    char web_user[32];     // Web UI login, empty = no login
    char web_pass[32];
};

// ============================================================
// LIVE DATA
// ============================================================
// Only the WiFi part is used (written by wifi_manager, shared code of all devices)
struct SensorData {
    int rssi;                  // WiFi signal strength
    uint32_t wifi_drops;       // WiFi disconnects since boot
    char wifi_reason[32];      // Reason of the last WiFi disconnect
};

extern AppConfig config;
extern SensorData sensorData;

#endif // CONFIG_H
