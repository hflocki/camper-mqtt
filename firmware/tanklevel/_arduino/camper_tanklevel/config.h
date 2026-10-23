#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================
// GENERAL CONSTANTS
// ============================================================
#define FW_VERSION            "1.3.0"

#define DEFAULT_AP_SSID       "Camper-Tanklevel" // Base name; "-XXXXXX" (last 3 MAC bytes) is appended
#define DEFAULT_AP_PASSWORD   "tanklevel1234"    // Test placeholder (min. 8 characters) - change in the web UI
#define DEFAULT_OTA_PASSWORD  "tanklevel1234"    // Test placeholder - change before the first flash
#define DEFAULT_MQTT_SERVER   ""                 // Set in the web UI
#define DEFAULT_MQTT_PORT     1883
#define DEFAULT_MQTT_PREFIX   "camper/tanklevel" // Topics: <prefix>/water/..., <prefix>/grey/..., <prefix>/gas/..., <prefix>/status

// WiFi TX power in 0.25 dBm steps: 70 = 17.5 dBm (factory default, selectable in the web UI).
// Full power (19.5 dBm) tends to be unstable on ESP32-C3 Super Mini boards; if the
// connection keeps dropping, try 8.5 dBm (34) first.
#define DEFAULT_TX_POWER      70

// Own access point while connected to the WiFi (web UI, tab "WiFi"):
//   true  = AP always on (reachable at 192.168.4.1 at any time)
//   false = AP only as fallback when the WiFi is not reachable (more stable STA connection)
#define DEFAULT_AP_ALWAYS     true
#define AP_FALLBACK_MS        60000  // AP-only-as-fallback: start the AP after 60 s without WiFi

#define MQTT_WATER_MS         5000   // Fresh / grey water: periodic publish interval
#define MQTT_GAS_MS           30000  // Gas: periodic publish (also published immediately on new data)
#define WS_UPDATE_MS          1000   // Web UI live update interval

// ============================================================
// SENSORS ON/OFF  (1 = on, 0 = off)
// ------------------------------------------------------------
// Factory default for the first boot (and after a LittleFS upload).
// Afterwards each sensor can also be switched in the web UI (tabs "Wasser" / "Grau" / "Gas");
// the device then restarts. Gas off = Bluetooth is not started at all.
// ============================================================
#define WATER_ENABLED         1      // Fresh water level (Votronic tank sender)
#define GREY_ENABLED          0      // Grey water level (second Votronic tank sender) - optional
#define GAS_ENABLED           1      // Gas level (BLE sensor)

#define CONFIG_FILE           "/config.json"
#define OTA_HOSTNAME          "camper-tanklevel"

// ============================================================
// FRESH / GREY WATER (Votronic analog tank senders)
// Both inputs must be ADC1 pins (ADC2 does not work while WiFi is active).
// The pins are chosen automatically for the selected chip:
//   ESP32-C3: GPIO0 / GPIO1 (avoid GPIO2: boot strapping pin)
//   ESP32-S3: GPIO1 / GPIO2 (GPIO0 is a boot pin and has no ADC on the S3)
//   ESP32   : GPIO34 / GPIO35 (input only, ADC1)
// ============================================================
#if CONFIG_IDF_TARGET_ESP32S3
#define ADC_PIN               1      // Fresh water: ADC1_CH0
#define GREY_ADC_PIN          2      // Grey water:  ADC1_CH1
#elif CONFIG_IDF_TARGET_ESP32C3
#define ADC_PIN               0      // Fresh water: ADC1_CH0
#define GREY_ADC_PIN          1      // Grey water:  ADC1_CH1
#else
#define ADC_PIN               34     // Fresh water: ADC1_CH6
#define GREY_ADC_PIN          35     // Grey water:  ADC1_CH7
#endif
#define ADC_SAMPLES           8      // Oversampling per measurement (reduces ADC noise)
#define SENSOR_UPDATE_MS      1000   // Read the ADCs every second
#define FILTER_WINDOW_SIZE    75     // Moving average: 75 x 1 s = 75 s
#define DEFAULT_TANK_SIZE     100.0f // Fresh water tank capacity in liters (web UI)
#define DEFAULT_GREY_SIZE     90.0f  // Grey water tank capacity in liters (web UI)

// Grey water sender type (factory default, selectable in the web UI, tab "Grau"):
//   GREY_TYPE_ELECTRODE : Votronic tank electrode, multi-point table like the fresh water tank
//   GREY_TYPE_FL        : Votronic Tank-Sensor FL (capacitive), linear signal,
//                         calibrated with "empty" / "full" in the web UI
#define GREY_TYPE_ELECTRODE   0
#define GREY_TYPE_FL          1
#define GREY_SENSOR_TYPE      GREY_TYPE_ELECTRODE

// FL placeholder until "empty" / "full" were taught in the web UI
#define FL_DEFAULT_EMPTY_V    0.10f
#define FL_DEFAULT_FULL_V     2.40f
#define MAX_CALIB_POINTS      15     // Max. number of calibration points per tank

// ============================================================
// GAS (passive BLE advertising sensor)
// ============================================================
// Where the sensor data comes from
#define SRC_MANUFACTURER      0      // Advertisement: manufacturer specific data (passive)
#define SRC_SERVICE_DATA      1      // Advertisement: service data of a given UUID (passive)

#define COMPANY_ANY           -1     // Accept manufacturer data of any company ID

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
#define SENSOR_MODEL          "DIMES / SRG WAVE"
#define SENSOR_SOURCE         SRC_MANUFACTURER
#define SENSOR_COMPANY_ID     0xFFFF   // SRC_MANUFACTURER only (COMPANY_ANY = any)
#define SENSOR_UUID           ""       // SRC_SERVICE_DATA only: service data UUID

#define LEVEL_OFFSET          2
#define LEVEL_LEN             1        // 1, 2 or 4 bytes
#define LEVEL_BIG_ENDIAN      false
#define LEVEL_FACTOR          1.0f
#define LEVEL_ADD             0.0f

#define BATTERY_ENABLED       true     // false if the sensor does not report a battery level
#define BATTERY_OFFSET        1
#define BATTERY_LEN           1
#define BATTERY_BIG_ENDIAN    false
#define BATTERY_FACTOR        1.0f
#define BATTERY_ADD           0.0f

/* --- Example: sensor sending service data (replace the block above) ---
#define SENSOR_MODEL          "My sensor"
#define SENSOR_SOURCE         SRC_SERVICE_DATA
#define SENSOR_COMPANY_ID     COMPANY_ANY
#define SENSOR_UUID           "fcd2"   // Service data UUID (16 bit or 128 bit)
#define LEVEL_OFFSET          3
#define LEVEL_LEN             2
#define LEVEL_BIG_ENDIAN      false
#define LEVEL_FACTOR          0.1f     // e.g. value in 0.1 %
#define LEVEL_ADD             0.0f
#define BATTERY_ENABLED       true
#define BATTERY_OFFSET        1
#define BATTERY_LEN           1
#define BATTERY_BIG_ENDIAN    false
#define BATTERY_FACTOR        1.0f
#define BATTERY_ADD           0.0f
*/
// ============================================================

#define DEFAULT_CAPACITY      22.0f  // Gross bottle volume in liters (web UI)
#define DEFAULT_FILL_STOP_ON  true   // Bottle has a mechanical fill stop (web UI)
#define DEFAULT_FILL_STOP     80.0f  // Fill stop in % (web UI)

// Passive scan duty cycle (units of 0.625 ms). Window < interval leaves airtime for WiFi.
#define BLE_SCAN_INTERVAL     160    // 100 ms
#define BLE_SCAN_WINDOW       80     // 50 ms (50 % duty cycle)
#define BLE_MAX_SEEN          30     // Devices kept in the "nearby" list of the web UI
#define BLE_SEEN_TIMEOUT_MS   120000 // Drop devices from that list after 2 min silence
#define BLE_RAW_MAX           32     // Max. bytes kept for the raw data view

// Gas values older than this are reported as "no data"
#define DATA_TIMEOUT_MS       (30UL * 60UL * 1000UL)

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
    float raw_level;       // Raw sensor level in % (1:1 value)
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
