#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Optional personal overrides, not in git (see config_local.h.example). Everything defined there
// wins over the defaults in this file, e.g. own passwords, I2C pins or WiFi / MQTT for a freshly
// flashed device.
#if __has_include("config_local.h")
#include "config_local.h"
#endif

// ============================================================
// GENERAL CONSTANTS
// ============================================================
#define FW_VERSION            "2.1.0"

#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID       "Camper-Level"     // Base name; "-XXXXXX" (last 3 MAC bytes) is appended
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD   "level1234"        // Test placeholder (min. 8 characters) - change in the web UI
#endif
#ifndef DEFAULT_OTA_PASSWORD
#define DEFAULT_OTA_PASSWORD  "level1234"        // Test placeholder - change before the first flash
#endif
#ifndef DEFAULT_MQTT_SERVER
#define DEFAULT_MQTT_SERVER   ""                 // Set in the web UI
#endif
#ifndef DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PORT     1883
#endif

// ---- Factory defaults for WiFi / MQTT / web login -----------------------
// Used only while nothing is stored on the device yet: first boot, after uploading
// the LittleFS image or flashing with "Erase All Flash". Put your own values into
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
#define DEFAULT_MQTT_PREFIX   "camper/level"     // Topics: <prefix>/pitch, <prefix>/wedge_fl, <prefix>/status ...
#endif

// WiFi TX power in 0.25 dBm steps: 70 = 17.5 dBm (factory default, selectable in the web UI).
// Full power (19.5 dBm) tends to be unstable on ESP32-C3 Super Mini boards.
#ifndef DEFAULT_TX_POWER
#define DEFAULT_TX_POWER      70
#endif

// Own access point while connected to the WiFi (web UI, tab "WiFi"):
//   true  = AP always on (reachable at 192.168.4.1 at any time)
//   false = AP only as fallback when the WiFi is not reachable (more stable STA connection)
#ifndef DEFAULT_AP_ALWAYS
#define DEFAULT_AP_ALWAYS     true
#endif
#define AP_FALLBACK_MS        60000  // AP-only-as-fallback: start the AP after 60 s without WiFi

#define CONFIG_FILE           "/config.json"
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME          "camper-level"
#endif

// ============================================================
// I2C - Adafruit BNO085 breakout
// ESP32-S3: GPIO1 / GPIO2, ESP32-C3 Super Mini: e.g. GPIO4 / GPIO10.
// If the sensor is not found, swap SDA and SCL in config_local.h.
// ============================================================
#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA           1
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL           2
#endif
#ifndef BNO08X_I2C_ADDR
#define BNO08X_I2C_ADDR       0x4A   // 0x4B if the DI pin of the breakout is pulled high
#endif

// ============================================================
// VEHICLE / LEVELLING (all editable in the web UI)
// ============================================================
#ifndef DEFAULT_TRACK_WIDTH
#define DEFAULT_TRACK_WIDTH   180.0f   // cm - distance between left and right wheel (track width)
#endif
#ifndef DEFAULT_WHEELBASE
#define DEFAULT_WHEELBASE     350.0f   // cm - distance between front and rear axle
#endif
#ifndef DEFAULT_TOLERANCE
#define DEFAULT_TOLERANCE     0.5f     // degrees: pitch and roll within this = level
#endif
#define MAX_TARE_DEG          10.0f    // allowed tare offset +/- degrees

#define SENSOR_UPDATE_MS      100      // 10 Hz sensor report
#define FILTER_ALPHA          0.15f    // EMA smoothing factor (0..1, smaller = smoother)
#define MQTT_FAST_MS          200      // 5 Hz while levelling ("MQTT schnell")
#define MQTT_SLOW_MS          2000     // 0.5 Hz normal operation
#define MQTT_FAST_TIMEOUT_MS  600000   // fast mode switches itself off after 10 min
#define MQTT_SETTINGS_MS      60000    // settings topics: on change, at least every minute
#define WS_UPDATE_MS          200      // 5 Hz web UI live update

// ============================================================
// MOUNTING ORIENTATION
// Flat: sensor lies horizontally, the USB port points in the given direction
// Wall: sensor hangs vertically on a wall, USB points DOWN
// ============================================================
enum MountOrientation {
    MOUNT_USB_FRONT  = 0,  // flat, USB to the front (driving direction)
    MOUNT_USB_BACK   = 1,
    MOUNT_USB_LEFT   = 2,
    MOUNT_USB_RIGHT  = 3,
    MOUNT_WALL_FRONT = 4,  // on the front wall (board faces backwards)
    MOUNT_WALL_BACK  = 5,
    MOUNT_WALL_LEFT  = 6,
    MOUNT_WALL_RIGHT = 7,
    MOUNT_COUNT      = 8
};

// ============================================================
// CONFIGURATION STRUCT (stored in /config.json)
// ============================================================
struct AppConfig {
    char wifi_ssid[64];
    char wifi_pass[64];
    char ap_ssid[32];
    char ap_pass[32];
    int8_t wifi_tx_power;  // TX power in 0.25 dBm steps
    bool ap_always;        // true = own AP stays on while connected, false = fallback only

    char mqtt_server[64];
    uint16_t mqtt_port;
    char mqtt_user[32];
    char mqtt_pass[32];
    char mqtt_prefix[32];

    char web_user[32];     // Web UI login, empty = no login
    char web_pass[32];

    MountOrientation mount;
    float track_width;     // cm
    float wheelbase;       // cm
    float tolerance;       // degrees

    float tare_pitch;      // zero offsets in degrees
    float tare_roll;
    bool tare_active;
    bool filter_active;    // EMA smoothing on/off
    bool mqtt_fast;        // true = 5 Hz, false = 0.5 Hz
};

// ============================================================
// SENSOR DATA
// ============================================================
struct SensorData {
    float raw_pitch;       // after mounting orientation, before tare
    float raw_roll;
    float pitch;           // after tare, filter and rounding: + = front up
    float roll;            //                                   + = right side down (see sensor.cpp)
    float wedge_fl;        // wedge height in cm under each wheel
    float wedge_fr;
    float wedge_rl;
    float wedge_rr;
    bool is_level;         // pitch and roll within the tolerance
    bool sensor_ok;
    uint32_t updates;      // sensor reports since boot (used to detect a stuck sensor)
    int rssi;              // WiFi signal strength (written by wifi_manager)
    uint32_t wifi_drops;   // WiFi disconnects since boot
    char wifi_reason[32];  // reason of the last WiFi disconnect
};

extern AppConfig config;
extern SensorData sensorData;

#endif // CONFIG_H
