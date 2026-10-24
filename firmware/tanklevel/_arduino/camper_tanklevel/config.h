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
#define FW_VERSION            "1.4.0"

#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID       "Camper-Tanklevel" // Base name; "-XXXXXX" (last 3 MAC bytes) is appended
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD   "tanklevel1234"    // Test placeholder (min. 8 characters) - change in the web UI
#endif
#ifndef DEFAULT_OTA_PASSWORD
#define DEFAULT_OTA_PASSWORD  "tanklevel1234"    // Test placeholder - change before the first flash
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
#define DEFAULT_MQTT_PREFIX   "camper/tanklevel" // Topics: <prefix>/water/..., <prefix>/grey/..., <prefix>/gas/..., <prefix>/status
#endif

// WiFi TX power in 0.25 dBm steps: 70 = 17.5 dBm (factory default, selectable in the web UI).
// Full power (19.5 dBm) tends to be unstable on ESP32-C3 Super Mini boards; if the
// connection keeps dropping, try 8.5 dBm (34) first.
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

#ifndef MQTT_WATER_MS
#define MQTT_WATER_MS         5000   // Fresh / grey water: periodic publish interval
#endif
#ifndef MQTT_GAS_MS
#define MQTT_GAS_MS           30000  // Gas: periodic publish (also published immediately on new data)
#endif
#ifndef WS_UPDATE_MS
#define WS_UPDATE_MS          1000   // Web UI live update interval
#endif

// ============================================================
// SENSORS ON/OFF  (1 = on, 0 = off)
// ------------------------------------------------------------
// Factory default for the first boot (and after a LittleFS upload).
// Afterwards each sensor can also be switched in the web UI (tabs "Wasser" / "Grau" / "Gas");
// the device then restarts. Gas off = Bluetooth is not started at all.
// ============================================================
#ifndef WATER_ENABLED
#define WATER_ENABLED         1      // Fresh water level (Votronic tank sender)
#endif
#ifndef GREY_ENABLED
#define GREY_ENABLED          0      // Grey water level (second Votronic tank sender) - optional
#endif
#ifndef GAS_ENABLED
#define GAS_ENABLED           1      // Gas level (BLE sensor)
#endif

#ifndef CONFIG_FILE
#define CONFIG_FILE           "/config.json"
#endif
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME          "camper-tanklevel"
#endif

// ============================================================
// FRESH / GREY WATER (Votronic analog tank senders)
// Both inputs must be ADC1 pins (ADC2 does not work while WiFi is active).
// The pins are chosen automatically for the selected chip:
//   ESP32-C3: GPIO0 / GPIO1 (avoid GPIO2: boot strapping pin)
//   ESP32-S3: GPIO1 / GPIO2 (GPIO0 is a boot pin and has no ADC on the S3)
//   ESP32   : GPIO34 / GPIO35 (input only, ADC1)
// ============================================================
#if CONFIG_IDF_TARGET_ESP32S3
#ifndef ADC_PIN
#define ADC_PIN               1      // Fresh water: ADC1_CH0
#endif
#ifndef GREY_ADC_PIN
#define GREY_ADC_PIN          2      // Grey water:  ADC1_CH1
#endif
#elif CONFIG_IDF_TARGET_ESP32C3
#ifndef ADC_PIN
#define ADC_PIN               0      // Fresh water: ADC1_CH0
#endif
#ifndef GREY_ADC_PIN
#define GREY_ADC_PIN          1      // Grey water:  ADC1_CH1
#endif
#else
#ifndef ADC_PIN
#define ADC_PIN               34     // Fresh water: ADC1_CH6
#endif
#ifndef GREY_ADC_PIN
#define GREY_ADC_PIN          35     // Grey water:  ADC1_CH7
#endif
#endif
#ifndef ADC_SAMPLES
#define ADC_SAMPLES           8      // Oversampling per measurement (reduces ADC noise)
#endif
#ifndef SENSOR_UPDATE_MS
#define SENSOR_UPDATE_MS      1000   // Read the ADCs every second
#endif
#ifndef FILTER_WINDOW_SIZE
#define FILTER_WINDOW_SIZE    75     // Moving average: 75 x 1 s = 75 s
#endif
#ifndef DEFAULT_TANK_SIZE
#define DEFAULT_TANK_SIZE     100.0f // Fresh water tank capacity in liters (web UI)
#endif
#ifndef DEFAULT_GREY_SIZE
#define DEFAULT_GREY_SIZE     90.0f  // Grey water tank capacity in liters (web UI)
#endif

// Grey water sender type (factory default, selectable in the web UI, tab "Grau"):
//   GREY_TYPE_ELECTRODE : Votronic tank electrode, multi-point table like the fresh water tank
//   GREY_TYPE_FL        : Votronic Tank-Sensor FL (capacitive), linear signal,
//                         calibrated with "empty" / "full" in the web UI
#ifndef GREY_TYPE_ELECTRODE
#define GREY_TYPE_ELECTRODE   0
#endif
#ifndef GREY_TYPE_FL
#define GREY_TYPE_FL          1
#endif
#ifndef GREY_SENSOR_TYPE
#define GREY_SENSOR_TYPE      GREY_TYPE_ELECTRODE
#endif

// FL placeholder until "empty" / "full" were taught in the web UI
#ifndef FL_DEFAULT_EMPTY_V
#define FL_DEFAULT_EMPTY_V    0.10f
#endif
#ifndef FL_DEFAULT_FULL_V
#define FL_DEFAULT_FULL_V     2.40f
#endif
#ifndef MAX_CALIB_POINTS
#define MAX_CALIB_POINTS      15     // Max. number of calibration points per tank
#endif

// ============================================================
// GAS (passive BLE advertising sensor)
// ============================================================
// Where the sensor data comes from
#ifndef SRC_MANUFACTURER
#define SRC_MANUFACTURER      0      // Advertisement: manufacturer specific data (passive)
#endif
#ifndef SRC_SERVICE_DATA
#define SRC_SERVICE_DATA      1      // Advertisement: service data of a given UUID (passive)
#endif

#ifndef COMPANY_ANY
#define COMPANY_ANY           -1     // Accept manufacturer data of any company ID
#endif

// Gas sensor type (selectable in the web UI, tab "Gas")
#define GAS_TYPE_DEFINITION   0      // DIMES / SRG WAVE or any sensor of the GAS SENSOR DEFINITION below
#define GAS_TYPE_MOPEKA       1      // Mopeka Pro family (Pro Check, Pro Check Universal, Pro Plus ...)
#ifndef DEFAULT_GAS_TYPE
#define DEFAULT_GAS_TYPE      GAS_TYPE_DEFINITION
#endif

// Mopeka: the sensor measures the liquid height in mm. Level % = (height - empty) / (full - empty).
// Presets in the web UI: Europe 6 kg 38/336, 11 kg 38/366, 14 kg 38/467, US 20 lb 38/254 mm.
#ifndef DEFAULT_MOPEKA_EMPTY_MM
#define DEFAULT_MOPEKA_EMPTY_MM  38     // Europe 11 kg bottle
#endif
#ifndef DEFAULT_MOPEKA_FULL_MM
#define DEFAULT_MOPEKA_FULL_MM   366
#endif
// Readings below this quality are ignored (0 = none, 1 = low, 2 = medium, 3 = high)
#ifndef MOPEKA_MIN_QUALITY
#define MOPEKA_MIN_QUALITY       2
#endif
#define MOPEKA_COMPANY_ID        0x0059 // Nordic Semiconductor (used by all Mopeka Pro sensors)

// ============================================================
// >>> GAS SENSOR DEFINITION - edit this block for other BLE sensors <<<
//
// Each value is decoded as:
//   value = unsigned integer (LEN bytes at OFFSET, little or big endian) * FACTOR + ADD
// LEVEL must give the raw fill level in % (0-100), BATTERY the battery in %.
// Content (liters) is calculated from LEVEL, bottle size and fill stop (web UI).
// Only passive advertisement data is supported (no connection, no PIN, no pairing).
//
// Offsets refer to:
//   SRC_MANUFACTURER : manufacturer data WITHOUT the 2 company ID bytes
//                      (same view as nRF Connect / Home Assistant)
//   SRC_SERVICE_DATA : service data of SENSOR_UUID
// The web UI shows the received bytes with their offsets (tab "Gas").
// ============================================================

// --- Default: Rotarex DIMES / SRG WAVE ---
// manufacturer data, company ID 0xFFFF, payload: [1] = battery %, [2] = level %
#ifndef SENSOR_MODEL
#define SENSOR_MODEL          "DIMES / SRG WAVE"
#endif
#ifndef SENSOR_SOURCE
#define SENSOR_SOURCE         SRC_MANUFACTURER
#endif
#ifndef SENSOR_COMPANY_ID
#define SENSOR_COMPANY_ID     0xFFFF   // SRC_MANUFACTURER only (COMPANY_ANY = any)
#endif
#ifndef SENSOR_UUID
#define SENSOR_UUID           ""       // SRC_SERVICE_DATA only: service data UUID
#endif

#ifndef LEVEL_OFFSET
#define LEVEL_OFFSET          2
#endif
#ifndef LEVEL_LEN
#define LEVEL_LEN             1        // 1, 2 or 4 bytes
#endif
#ifndef LEVEL_BIG_ENDIAN
#define LEVEL_BIG_ENDIAN      false
#endif
#ifndef LEVEL_FACTOR
#define LEVEL_FACTOR          1.0f
#endif
#ifndef LEVEL_ADD
#define LEVEL_ADD             0.0f
#endif

#ifndef BATTERY_ENABLED
#define BATTERY_ENABLED       true     // false if the sensor does not report a battery level
#endif
#ifndef BATTERY_OFFSET
#define BATTERY_OFFSET        1
#endif
#ifndef BATTERY_LEN
#define BATTERY_LEN           1
#endif
#ifndef BATTERY_BIG_ENDIAN
#define BATTERY_BIG_ENDIAN    false
#endif
#ifndef BATTERY_FACTOR
#define BATTERY_FACTOR        1.0f
#endif
#ifndef BATTERY_ADD
#define BATTERY_ADD           0.0f
#endif

/* --- Example: sensor sending service data (replace the block above) ---
#ifndef SENSOR_MODEL
#define SENSOR_MODEL          "My sensor"
#endif
#ifndef SENSOR_SOURCE
#define SENSOR_SOURCE         SRC_SERVICE_DATA
#endif
#ifndef SENSOR_COMPANY_ID
#define SENSOR_COMPANY_ID     COMPANY_ANY
#endif
#ifndef SENSOR_UUID
#define SENSOR_UUID           "fcd2"   // Service data UUID (16 bit or 128 bit)
#endif
#ifndef LEVEL_OFFSET
#define LEVEL_OFFSET          3
#endif
#ifndef LEVEL_LEN
#define LEVEL_LEN             2
#endif
#ifndef LEVEL_BIG_ENDIAN
#define LEVEL_BIG_ENDIAN      false
#endif
#ifndef LEVEL_FACTOR
#define LEVEL_FACTOR          0.1f     // e.g. value in 0.1 %
#endif
#ifndef LEVEL_ADD
#define LEVEL_ADD             0.0f
#endif
#ifndef BATTERY_ENABLED
#define BATTERY_ENABLED       true
#endif
#ifndef BATTERY_OFFSET
#define BATTERY_OFFSET        1
#endif
#ifndef BATTERY_LEN
#define BATTERY_LEN           1
#endif
#ifndef BATTERY_BIG_ENDIAN
#define BATTERY_BIG_ENDIAN    false
#endif
#ifndef BATTERY_FACTOR
#define BATTERY_FACTOR        1.0f
#endif
#ifndef BATTERY_ADD
#define BATTERY_ADD           0.0f
#endif
*/
// ============================================================

#ifndef DEFAULT_CAPACITY
#define DEFAULT_CAPACITY      22.0f  // Gross bottle volume in liters (web UI)
#endif
#ifndef DEFAULT_FILL_STOP_ON
#define DEFAULT_FILL_STOP_ON  true   // Bottle has a mechanical fill stop (web UI)
#endif
#ifndef DEFAULT_FILL_STOP
#define DEFAULT_FILL_STOP     80.0f  // Fill stop in % (web UI)
#endif

// Passive scan duty cycle (units of 0.625 ms). Window < interval leaves airtime for WiFi.
#ifndef BLE_SCAN_INTERVAL
#define BLE_SCAN_INTERVAL     160    // 100 ms
#endif
#ifndef BLE_SCAN_WINDOW
#define BLE_SCAN_WINDOW       80     // 50 ms (50 % duty cycle)
#endif
#ifndef BLE_MAX_SEEN
#define BLE_MAX_SEEN          30     // Devices kept in the "nearby" list of the web UI
#endif
#ifndef BLE_SEEN_TIMEOUT_MS
#define BLE_SEEN_TIMEOUT_MS   120000 // Drop devices from that list after 2 min silence
#endif
#ifndef BLE_RAW_MAX
#define BLE_RAW_MAX           32     // Max. bytes kept for the raw data view
#endif

// Gas values older than this are reported as "no data"
#ifndef DATA_TIMEOUT_MS
#define DATA_TIMEOUT_MS       (30UL * 60UL * 1000UL)
#endif

// ============================================================
// TYPES
// ============================================================
struct CalibPoint {
    float volt;
    float percent;         // Fill level in % (0-100) at this voltage; liters = percent * tank size
};

// value = (bytes at offset, length, endianness) * factor + add
struct ValueMap {
    bool enabled;
    uint8_t offset;        // Byte offset in the payload
    uint8_t len;           // 1, 2 or 4 bytes (unsigned)
    bool big_endian;
    float factor;
    float add;
};

// Settings of one water tank (fresh or grey)
struct TankConfig {
    float tank_size;       // Tank capacity in liters
    bool filter_active;    // Moving-average filter on/off
    CalibPoint calibTable[MAX_CALIB_POINTS];  // Volt-to-percent table, sorted by voltage
    uint8_t calibCount;
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

    // Enabled sensors (applied at boot)
    bool water_enabled;
    bool grey_enabled;
    bool gas_enabled;

    // Water tanks
    TankConfig fresh;
    TankConfig grey;
    uint8_t grey_type;     // GREY_TYPE_ELECTRODE or GREY_TYPE_FL

    // Gas (data mapping is defined at compile time, see GAS SENSOR DEFINITION)
    char sensor_mac[18];   // "AA:BB:CC:DD:EE:FF" (upper case), empty = no sensor
    uint8_t gas_type;      // GAS_TYPE_DEFINITION or GAS_TYPE_MOPEKA
    uint16_t mopeka_empty_mm;  // Mopeka: liquid height of an empty bottle
    uint16_t mopeka_full_mm;   // Mopeka: liquid height of a full bottle
    float capacity;        // Gross bottle capacity in liters
    bool fill_stop_on;     // Bottle has a mechanical fill stop
    float fill_stop;       // Fill stop in %
};

// ============================================================
// SENSOR DATA STRUCTS
// ============================================================
struct WaterData {
    float raw_voltage;     // Raw voltage from the ADC
    float avg_voltage;     // Filtered voltage
    float liters;          // Calculated content in liters
    float percent;         // Calculated content in % (0-100)
};

struct GasData {
    bool has_data;         // At least one valid reading since boot
    float battery;         // Sensor battery in % (NAN = unknown)
    float raw_level;       // Raw sensor level in % (1:1 value; Mopeka: % between empty and full height)
    float distance_mm;     // Mopeka: measured liquid height in mm (NAN = none)
    float temperature;     // Mopeka: sensor temperature in °C (NAN = none)
    int8_t quality;        // Mopeka: read quality 0-3 (-1 = none)
    float level;           // Level scaled to the fill stop in %
    float content;         // Remaining content in liters
    int ble_rssi;          // Signal strength of the gas sensor
    unsigned long last_update;  // millis() of the last valid reading
    uint32_t updates;      // Counter of valid readings (used to detect new data)
    char ble_state[24];    // Human readable BLE state for the web UI / MQTT
    char raw_hex[BLE_RAW_MAX * 3 + 1];  // Last payload of the configured sensor as hex
    uint32_t adv_count;    // Advertisements received since boot (any device, for diagnosis)
    bool scanning;         // BLE scan is running
};

struct SensorData {
    WaterData water;       // Fresh water
    WaterData grey;        // Grey water
    GasData gas;
    int rssi;              // WiFi signal strength (written by wifi_manager)
    uint32_t wifi_drops;   // WiFi disconnects since boot
    char wifi_reason[32];  // Reason of the last WiFi disconnect
};

extern AppConfig config;
extern SensorData sensorData;

#endif // CONFIG_H
