#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
//  Camper Example - template for a new camper-mqtt device
//  Copy with tools/new_device.py, then fill the places marked "TODO".
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
#define FW_VERSION            "1.0.0"
#define DEVICE_NAME           "Camper Example"   // web UI title, HA device name, serial banner

#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID       "Camper-Example"   // Base name; "-XXXXXX" (last 3 MAC bytes) is appended
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD   "example1234"      // Test placeholder (min. 8 characters) - change in config_local.h
#endif
#ifndef DEFAULT_OTA_PASSWORD
#define DEFAULT_OTA_PASSWORD  "example1234"      // Test placeholder - change in config_local.h
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
#define DEFAULT_MQTT_PREFIX   "camper/example"   // Topics: <prefix>/<value key>, <prefix>/status, <prefix>/rssi
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
#define MQTT_REPUBLISH_MS     60000  // Re-send all values at least this often (also sent after every reading)
#endif
#ifndef WS_UPDATE_MS
#define WS_UPDATE_MS          1000   // Web UI live update interval
#endif

#ifndef CONFIG_FILE
#define CONFIG_FILE           "/config.json"
#endif
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME          "camper-example"   // also the mDNS name: camper-example.local
#endif

// ============================================================
// SENSOR / HARDWARE  (TODO: adapt to your sensor)
// The example reads an analog input and turns it into a voltage with a
// scale factor and an offset from the web UI.
// ============================================================
#ifndef SENSOR_PIN
#define SENSOR_PIN            2      // ADC pin (ESP32-C3: GPIO0-4, ESP32-S3: GPIO1-10)
#endif
#ifndef DEFAULT_INTERVAL_S
#define DEFAULT_INTERVAL_S    10     // Measurement interval in seconds (web UI, 1-600)
#endif
#ifndef DEFAULT_SCALE
#define DEFAULT_SCALE         1.0f   // Example setting: factor applied to the measured voltage
#endif
#ifndef DEFAULT_OFFSET
#define DEFAULT_OFFSET        0.0f   // Example setting: added after scaling
#endif
#ifndef MAX_VALUES
#define MAX_VALUES            8      // Values the device publishes (see VALUES[] in sensor.cpp)
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

    // ---- Device settings (TODO: add your own, plus load/save/validate in webserver.cpp) ----
    uint16_t interval_s;   // Measurement interval in seconds
    float scale;           // Example: factor for the measured voltage
    float offset;          // Example: offset added after scaling
};

// ============================================================
// LIVE DATA
// ============================================================
struct SensorData {
    float values[MAX_VALUES];  // Current value per entry of VALUES[] (sensor.cpp), NAN = unknown
    bool ok;                   // Last reading was valid
    unsigned long last_ok;     // millis() of the last valid reading
    uint32_t errors;           // Failed readings since boot
    uint32_t readCount;        // Completed measurement cycles (used to detect new data)
    int rssi;                  // WiFi signal strength (written by wifi_manager)
    uint32_t wifi_drops;       // WiFi disconnects since boot
    char wifi_reason[32];      // Reason of the last WiFi disconnect
};

extern AppConfig config;
extern SensorData sensorData;

#endif // CONFIG_H
