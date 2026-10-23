#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Optional personal overrides, not in git (see config_local.h.example). Everything defined there
// wins over the defaults in this file, e.g. own passwords or WiFi / MQTT for a freshly flashed device.
#if __has_include("config_local.h")
#include "config_local.h"
#endif

// ============================================================
// GENERAL CONSTANTS
// ============================================================
#define FW_VERSION            "1.1.0"

#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID       "Camper-Temp"      // Base name; "-XXXXXX" (last 3 MAC bytes) is appended
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD   "camptemp1234"     // Test placeholder (min. 8 characters) - change in the web UI
#endif
#ifndef DEFAULT_OTA_PASSWORD
#define DEFAULT_OTA_PASSWORD  "camptemp1234"     // Test placeholder - change before the first flash
#endif
#ifndef DEFAULT_MQTT_SERVER
#define DEFAULT_MQTT_SERVER   ""                 // Set in the web UI
#endif
#ifndef DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PORT     1883
#endif

// ---- Factory defaults for WiFi / MQTT / web login -----------------------
// Used only while nothing is stored on the device yet: first boot, after "factory
// reset" in the web UI or after flashing with "Erase All Flash". Put your own values
// into config_local.h, then a freshly flashed device connects right away.
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
#define DEFAULT_MQTT_PREFIX   "camper/temp"      // Topics: <prefix>/<sensor id>, <prefix>/status, <prefix>/rssi
#endif

// WiFi TX power in 0.25 dBm steps: 70 = 17.5 dBm (factory default, selectable in the web UI)
#ifndef DEFAULT_TX_POWER
#define DEFAULT_TX_POWER      70
#endif

// Own access point while connected to the WiFi (web UI, tab "WiFi"):
//   true  = AP always on (reachable at 192.168.4.1 at any time)
//   false = AP only as fallback when the WiFi is not reachable (more stable STA connection)
#ifndef DEFAULT_AP_ALWAYS
#define DEFAULT_AP_ALWAYS     true
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
#define OTA_HOSTNAME          "camper-temp"
#endif

// ============================================================
// 1-WIRE BUS (DS18B20)
// A 4.7 kOhm pull-up from DATA to 3.3 V is required (once per bus).
// ESP8266: GPIO4 (D2 on Wemos D1 mini / NodeMCU). Avoid GPIO0, GPIO2 and GPIO15 (boot pins).
// ESP32-C3: GPIO4 works as well. Classic ESP32: GPIO4.
// ============================================================
#ifndef ONEWIRE_PIN
#define ONEWIRE_PIN           4
#endif

#ifndef MAX_SENSORS
#define MAX_SENSORS           8      // Sensors that can be named and published
#endif
#ifndef MAX_BUS_DEVICES
#define MAX_BUS_DEVICES       12     // Devices kept from a bus scan (configured + new ones)
#endif

#ifndef DEFAULT_INTERVAL_S
#define DEFAULT_INTERVAL_S    10     // Measurement interval in seconds (web UI, 5-600)
#endif
#ifndef DEFAULT_RESOLUTION
#define DEFAULT_RESOLUTION    12     // 9-12 bit: 0.5 / 0.25 / 0.125 / 0.0625 °C (12 bit = 750 ms conversion)
#endif
#ifndef BUS_RESCAN_MS
#define BUS_RESCAN_MS         300000 // Rescan the bus every 5 min (finds sensors plugged in later)
#endif
#ifndef MAX_OFFSET
#define MAX_OFFSET            10.0f  // Allowed calibration offset +/- °C
#endif

// ============================================================
// ALARMS (per sensor, set in the web UI)
// A limit is violated for "delay" minutes -> alarm. It clears as soon as the value is back
// inside the limit by ALARM_HYSTERESIS (no delay), so a value around the limit does not flap.
// ============================================================
#ifndef ALARM_HYSTERESIS
#define ALARM_HYSTERESIS      0.5f   // °C
#endif
#ifndef MAX_ALARM_DELAY_MIN
#define MAX_ALARM_DELAY_MIN   240    // Max. delay in minutes
#endif
#ifndef ALARM_NONE
#define ALARM_NONE            0
#endif
#ifndef ALARM_LOW
#define ALARM_LOW             1
#endif
#ifndef ALARM_HIGH
#define ALARM_HIGH            2
#endif

// ============================================================
// TYPES
// ============================================================
struct TempSensorConfig {
    bool used;
    uint8_t addr[8];       // 1-Wire ROM address (byte 0 = family code, 0x28 for DS18B20)
    char name[32];         // Display name, e.g. "Fridge evaporator"
    char key[24];          // MQTT sub topic / id, e.g. "fridge_evap" -> <prefix>/fridge_evap
    float offset;          // Calibration offset in °C, added to the reading
    float alarm_lo;        // Alarm below this value in °C, NAN = off
    float alarm_hi;        // Alarm above this value in °C, NAN = off
    uint16_t alarm_delay;  // Minutes the limit must be violated before the alarm is raised
};

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

    uint16_t interval_s;   // Measurement interval in seconds
    uint8_t resolution;    // DS18B20 resolution in bit (9-12)

    TempSensorConfig sensors[MAX_SENSORS];
};

// ============================================================
// SENSOR DATA STRUCTS
// ============================================================
// One device found on the 1-Wire bus (also the ones not configured yet)
struct BusDevice {
    uint8_t addr[8];
    float temp;            // Last raw reading in °C (without offset), NAN = error
};

// Live values of a configured sensor
struct TempData {
    bool ok;               // Last reading was valid
    bool present;          // Found on the bus during the last scan
    float temp;            // Temperature incl. offset in °C
    float min;             // Lowest value since boot (or since the last reset)
    float max;             // Highest value since boot
    bool has_minmax;
    unsigned long last_ok; // millis() of the last valid reading
    uint32_t errors;       // Failed readings since boot
    uint8_t alarm;         // ALARM_NONE / ALARM_LOW / ALARM_HIGH (active alarm)
    uint8_t pending;       // Limit currently violated, waiting for the delay
    unsigned long pending_since;
};

struct SensorData {
    BusDevice bus[MAX_BUS_DEVICES];
    uint8_t busCount;
    TempData temps[MAX_SENSORS];
    uint32_t readCount;    // Completed measurement cycles (used to detect new data)
    uint32_t alarmSeq;     // Incremented on every alarm change (used to publish right away)
    int rssi;              // WiFi signal strength (written by wifi_manager)
    uint32_t wifi_drops;   // WiFi disconnects since boot
    char wifi_reason[32];  // Reason of the last WiFi disconnect
};

extern AppConfig config;
extern SensorData sensorData;

#endif // CONFIG_H
