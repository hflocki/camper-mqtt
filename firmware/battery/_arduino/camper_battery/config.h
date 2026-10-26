#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "bms_proto.h"

// Optional personal overrides, not in git (see config_local.h.example). Everything defined there
// wins over the defaults in this file, e.g. own passwords or WiFi / MQTT for a freshly flashed device.
#if __has_include("config_local.h")
#include "config_local.h"
#endif

// ============================================================
// GENERAL CONSTANTS
// ============================================================
#define FW_VERSION            "1.2.2"

#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID       "Camper-Battery"   // Base name; "-XXXXXX" (last 3 MAC bytes) is appended
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD   "campbatt1234"     // Test placeholder (min. 8 characters) - change in the web UI
#endif
#ifndef DEFAULT_OTA_PASSWORD
#define DEFAULT_OTA_PASSWORD  "campbatt1234"     // Test placeholder - change before the first flash
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
#define DEFAULT_MQTT_PREFIX   "camper/battery"   // Topics: <prefix>/<battery id>/..., <prefix>/status
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
#define OTA_HOSTNAME          "camper-battery"
#endif

// ============================================================
// BLUETOOTH
// The ESP keeps a permanent connection to every configured BMS and asks for new values
// every "interval" seconds. While connected, most BMS do not advertise, so the vendor
// app (or Home Assistant) cannot connect at the same time.
// ============================================================
#ifndef MAX_BMS
#define MAX_BMS               3      // NimBLE default allows 3 simultaneous connections
#endif
#ifndef MAX_FOUND
#define MAX_FOUND             12     // Devices kept from the BLE scan (for the web UI)
#endif
#ifndef DEFAULT_INTERVAL_S
#define DEFAULT_INTERVAL_S    10     // Poll interval in seconds (web UI, 5-300)
#endif
#ifndef BLE_CONNECT_TIMEOUT_MS
#define BLE_CONNECT_TIMEOUT_MS 6000
#endif
#ifndef BLE_RESPONSE_TIMEOUT_MS
#define BLE_RESPONSE_TIMEOUT_MS 3000
#endif
#ifndef BM2_WAIT_MS
#define BM2_WAIT_MS           4000   // BM2 notifies about once per second
#endif
#ifndef BLE_RETRY_MS
#define BLE_RETRY_MS          15000  // Wait before reconnecting after a failed attempt (doubles up to 2 min)
#endif
#ifndef BLE_SCAN_MS
#define BLE_SCAN_MS           5000   // Duration of one scan
#endif
#ifndef BLE_SCAN_EVERY_MS
#define BLE_SCAN_EVERY_MS     60000  // Scan this often while a configured BMS is not connected
#endif
#ifndef BMS_STALE_MS
#define BMS_STALE_MS          120000 // Values older than this count as unavailable
#endif

// ============================================================
// STATUS LED (optional RGB LED, WS2812)
// M5Stack AtomS3 Lite: switched on automatically (board "M5AtomS3", RGB LED on GPIO35).
// Other boards with an RGB LED: set STATUS_LED_PIN in config_local.h. -1 = no LED.
//   green  = every configured BMS delivers values
//   yellow = at least one BMS missing / no answer
//   red    = no WiFi or no MQTT connection (blinking)
//   blue   = no BMS set up yet / scanning
// ============================================================
#ifndef STATUS_LED_PIN
#if defined(ARDUINO_M5STACK_ATOMS3) && defined(PIN_RGB_LED)
#define STATUS_LED_PIN        PIN_RGB_LED
#else
#define STATUS_LED_PIN        -1
#endif
#endif
#ifndef STATUS_LED_BRIGHTNESS
#define STATUS_LED_BRIGHTNESS 24     // 0-255, kept low so it does not light up the camper at night
#endif

// ============================================================
// TYPES
// ============================================================
struct BmsConfig {
    bool used;
    char mac[18];          // "AA:BB:CC:DD:EE:FF", upper case
    uint8_t type;          // BmsType
    char name[32];         // Display name, e.g. "Bordbatterie"
    char key[24];          // MQTT sub topic / id, e.g. "house" -> <prefix>/house/...
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

    uint16_t interval_s;   // Poll interval in seconds
    BmsConfig bms[MAX_BMS];
};

// ============================================================
// LIVE DATA
// ============================================================
// Live state of one configured BMS (copied out of the BLE task with bmsGetLive())
struct BmsLive {
    bool connected;
    bool valid;            // sample holds a reading younger than BMS_STALE_MS
    BmsSample s;
    unsigned long last_ok; // millis() of the last good reading, 0 = none yet
    uint32_t reads;        // Good readings since boot
    uint32_t errors;       // Failed requests / connects since boot
    int8_t rssi;
    char state[40];        // Readable state for the web UI
};

// One device seen in the BLE scan
struct FoundDevice {
    char mac[18];
    char name[24];
    uint8_t type;          // Detected BmsType, BMS_NONE = unknown
    uint8_t addr_type;     // BLE address type (public / random)
    int8_t rssi;
    unsigned long seen;    // millis()
};

struct SensorData {
    int rssi;              // WiFi signal strength (written by wifi_manager)
    uint32_t wifi_drops;   // WiFi disconnects since boot
    char wifi_reason[32];  // Reason of the last WiFi disconnect
};

extern AppConfig config;
extern SensorData sensorData;

#endif // CONFIG_H
