#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================
// GENERAL CONSTANTS
// ============================================================
#define FW_VERSION            "1.0.0"

#define DEFAULT_AP_SSID       "Camper-Temp"      // Base name; "-XXXXXX" (last 3 MAC bytes) is appended
#define DEFAULT_AP_PASSWORD   "camptemp1234"     // Test placeholder (min. 8 characters) - change in the web UI
#define DEFAULT_OTA_PASSWORD  "camptemp1234"     // Test placeholder - change before the first flash
#define DEFAULT_MQTT_SERVER   ""                 // Set in the web UI
#define DEFAULT_MQTT_PORT     1883
#define DEFAULT_MQTT_PREFIX   "camper/temp"      // Topics: <prefix>/<sensor id>, <prefix>/status, <prefix>/rssi

// WiFi TX power in 0.25 dBm steps: 70 = 17.5 dBm (factory default, selectable in the web UI)
#define DEFAULT_TX_POWER      70

// Own access point while connected to the WiFi (web UI, tab "WiFi"):
//   true  = AP always on (reachable at 192.168.4.1 at any time)
//   false = AP only as fallback when the WiFi is not reachable (more stable STA connection)
#define DEFAULT_AP_ALWAYS     true
#define AP_FALLBACK_MS        60000  // AP-only-as-fallback: start the AP after 60 s without WiFi

#define MQTT_REPUBLISH_MS     60000  // Re-send all values at least this often (also sent after every reading)
#define WS_UPDATE_MS          1000   // Web UI live update interval

#define CONFIG_FILE           "/config.json"
#define OTA_HOSTNAME          "camper-temp"

// ============================================================
// 1-WIRE BUS (DS18B20)
// A 4.7 kOhm pull-up from DATA to 3.3 V is required (once per bus).
// ESP8266: GPIO4 (D2 on Wemos D1 mini / NodeMCU). Avoid GPIO0, GPIO2 and GPIO15 (boot pins).
// ESP32-C3: GPIO4 works as well. Classic ESP32: GPIO4.
// ============================================================
#define ONEWIRE_PIN           4

#define MAX_SENSORS           8      // Sensors that can be named and published
#define MAX_BUS_DEVICES       12     // Devices kept from a bus scan (configured + new ones)

#define DEFAULT_INTERVAL_S    10     // Measurement interval in seconds (web UI, 5-600)
#define DEFAULT_RESOLUTION    12     // 9-12 bit: 0.5 / 0.25 / 0.125 / 0.0625 °C (12 bit = 750 ms conversion)
#define BUS_RESCAN_MS         300000 // Rescan the bus every 5 min (finds sensors plugged in later)
#define MAX_OFFSET            10.0f  // Allowed calibration offset +/- °C

// ============================================================
// TYPES
// ============================================================
struct TempSensorConfig {
    bool used;
    uint8_t addr[8];       // 1-Wire ROM address (byte 0 = family code, 0x28 for DS18B20)
    char name[32];         // Display name, e.g. "Fridge evaporator"
    char key[24];          // MQTT sub topic / id, e.g. "fridge_evap" -> <prefix>/fridge_evap
    float offset;          // Calibration offset in °C, added to the reading
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
};

struct SensorData {
    BusDevice bus[MAX_BUS_DEVICES];
    uint8_t busCount;
    TempData temps[MAX_SENSORS];
    uint32_t readCount;    // Completed measurement cycles (used to detect new data)
    int rssi;              // WiFi signal strength (written by wifi_manager)
    uint32_t wifi_drops;   // WiFi disconnects since boot
    char wifi_reason[32];  // Reason of the last WiFi disconnect
};

extern AppConfig config;
extern SensorData sensorData;

#endif // CONFIG_H
