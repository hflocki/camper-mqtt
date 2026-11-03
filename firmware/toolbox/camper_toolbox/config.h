#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Optional personal overrides, not in git (see config_local.h.example). Everything defined there
// wins over the defaults in this file, e.g. own passwords, WiFi / MQTT or a different plugin set.
#if __has_include("config_local.h")
#include "config_local.h"
#endif

// ============================================================
// >>> PLUGINS - choose what this ESP should do <<<
// ------------------------------------------------------------
// The number is the count of instances of that plugin:
//   0 = not compiled in (saves flash and RAM)
//   1 = one device, e.g. one gas bottle
//   2 = two devices of the same kind, e.g. two gas bottles ("Gas" and "Gas 2")
// Several plugins can run on one ESP at the same time. Every compiled-in instance
// can additionally be switched on / off in the web UI (the device then restarts).
//
// Bluetooth limits of one ESP32:
//   - passive sensors (DIMES, Mopeka) only listen, any number works
//   - connecting devices (pump, roof hatch, fridge) keep a BLE connection each:
//     PLUGIN_CARAWATER + PLUGIN_RVTECH + PLUGIN_WCS_FRIDGE <= 3
//     (NimBLE default CONFIG_BT_NIMBLE_MAX_CONNECTIONS), e.g.
//       pump 1 + hatch 1 + fridge 1 = 3 ok
//       pump 0 + hatch 2 + fridge 1 = 3 ok
//       pump 1 + hatch 2 + fridge 1 = 4 -> build error
// ============================================================
#ifndef PLUGIN_DIMES
#define PLUGIN_DIMES          0      // Gas: Rotarex DIMES / SRG WAVE (passive BLE)
#endif
#ifndef PLUGIN_MOPEKA_PRO
#define PLUGIN_MOPEKA_PRO     0      // Gas: Mopeka Pro family (Pro Check, Pro Plus, Universal ...)
#endif
#ifndef PLUGIN_MOPEKA_STD
#define PLUGIN_MOPEKA_STD     0      // Gas: Mopeka Standard / Std Check (older model)
#endif
#ifndef PLUGIN_CARAWATER
#define PLUGIN_CARAWATER      0      // Water pump: CaraWater (e.g. ISP 15), BLE connection (counts towards 3)
#endif
#ifndef PLUGIN_RVTECH
#define PLUGIN_RVTECH         1      // Roof hatch: RV Tech skylight motor control, BLE connection (counts towards 3)
#endif
#ifndef PLUGIN_WCS_FRIDGE
#define PLUGIN_WCS_FRIDGE     0      // Fridge: WCS compressor fridge, BLE connection (counts towards 3)
#endif

// Derived switches (do not edit)
#define CT_BLE_PASSIVE  (PLUGIN_DIMES + PLUGIN_MOPEKA_PRO + PLUGIN_MOPEKA_STD)
#define CT_BLE_LINKS    (PLUGIN_CARAWATER + PLUGIN_RVTECH + PLUGIN_WCS_FRIDGE)
#define CT_USE_BLE      ((CT_BLE_PASSIVE + CT_BLE_LINKS) > 0)
#define CT_PLUGIN_COUNT (CT_BLE_PASSIVE + CT_BLE_LINKS)

#if CT_BLE_LINKS > 3
#error "At most 3 connecting BLE plugins fit on one ESP32: PLUGIN_CARAWATER + PLUGIN_RVTECH + PLUGIN_WCS_FRIDGE must be 3 or less"
#endif
#if CT_PLUGIN_COUNT > 8
#error "At most 8 plugin instances per device"
#endif
#if CT_PLUGIN_COUNT == 0
#warning "No plugin selected in config.h - the device only offers WiFi, MQTT and the web UI"
#endif

// ============================================================
// GENERAL CONSTANTS
// ============================================================
#define FW_VERSION            "1.0.0"
#define FW_DEVICE             "toolbox"   // device id in the firmware marker (fwcheck.h)

// Network upload from the Arduino IDE (port in the IDE, password DEFAULT_OTA_PASSWORD).
// 0 = off, updates then only through the web UI (with login, if set).
#ifndef ENABLE_ARDUINO_OTA
#define ENABLE_ARDUINO_OTA    1
#endif

#ifndef DEVICE_NAME
#define DEVICE_NAME           "Camper Toolbox"      // Name in Home Assistant and in the web UI
#endif
#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID       "Camper-Toolbox"      // Base name; "-XXXXXX" (last 3 MAC bytes) is appended
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD   "campertoolbox1234"   // Test placeholder (min. 8 characters) - change in the web UI
#endif
#ifndef DEFAULT_OTA_PASSWORD
#define DEFAULT_OTA_PASSWORD  "campertoolbox1234"   // Test placeholder - change before the first flash
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
// Topics: <prefix>/<plugin>/<value>, commands <prefix>/cmd/<plugin>/<value>/set,
// <prefix>/status, <prefix>/info (docs/TOPICS.md). The Home Assistant node ID is derived
// from it ("camper/toolbox" -> "camper_toolbox"), so two toolboxes need two prefixes.
#ifndef DEFAULT_MQTT_PREFIX
#define DEFAULT_MQTT_PREFIX   "camper/toolbox"
#endif

// WiFi TX power in 0.25 dBm steps: 70 = 17.5 dBm (factory default, selectable in the web UI).
// Full power (19.5 dBm) tends to be unstable on ESP32-C3 Super Mini boards; if the
// connection keeps dropping, try 8.5 dBm (34) first.
#ifndef DEFAULT_TX_POWER
#define DEFAULT_TX_POWER      70
#endif

// Own access point while connected to the WiFi (web UI, tab "WLAN"):
//   true  = AP always on (reachable at 192.168.4.1 at any time)
//   false = AP only as fallback when the WiFi is not reachable (more stable STA connection)
#ifndef DEFAULT_AP_ALWAYS
#define DEFAULT_AP_ALWAYS     true
#endif
#ifndef AP_FALLBACK_MS
#define AP_FALLBACK_MS        60000  // AP-only-as-fallback: start the AP after 60 s without WiFi
#endif

#ifndef MQTT_CORE_MS
#define MQTT_CORE_MS          30000  // Device status / WiFi RSSI publish interval
#endif
#ifndef WS_UPDATE_MS
#define WS_UPDATE_MS          1000   // Web UI live update interval
#endif

#ifndef CONFIG_FILE
#define CONFIG_FILE           "/config.json"
#endif
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME          "camper-toolbox"
#endif

// ============================================================
// BLUETOOTH (shared by all BLE plugins)
// ============================================================
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
// Connecting plugins (pump, roof hatch, fridge)
#ifndef BLE_CONNECT_TIMEOUT
#define BLE_CONNECT_TIMEOUT   4000   // Connection attempt timeout in ms (non-blocking)
#endif
#ifndef BLE_RETRY_MS
#define BLE_RETRY_MS          10000  // Wait between connection attempts
#endif
#ifndef BLE_RETRY_MAX_MS
#define BLE_RETRY_MAX_MS      60000  // Back-off limit while the device is out of range
#endif
#ifndef BLE_RELEASE_MS
#define BLE_RELEASE_MS        (30UL * 60UL * 1000UL) // "Release for the app" ends after 30 min (0 = never)
#endif

// ============================================================
// PLUGIN: GAS (DIMES, Mopeka Pro, Mopeka Standard)
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
#ifndef GAS_DATA_TIMEOUT_MS
#define GAS_DATA_TIMEOUT_MS   (30UL * 60UL * 1000UL) // Gas values older than this are "no data"
#endif
#ifndef MQTT_GAS_MS
#define MQTT_GAS_MS           30000  // Periodic publish (also published immediately on new data)
#endif

// ---- DIMES / SRG WAVE: value mapping -------------------------------------
// Each value is decoded as:
//   value = unsigned integer (LEN bytes at OFFSET, little or big endian) * FACTOR + ADD
// LEVEL must give the raw fill level in % (0-100), BATTERY the battery in %.
// Offsets refer to:
//   SRC_MANUFACTURER : manufacturer data WITHOUT the 2 company ID bytes
//                      (same view as nRF Connect / Home Assistant)
//   SRC_SERVICE_DATA : service data of DIMES_UUID
// The web UI shows the received bytes with their offsets, so other passive BLE
// level sensors can be added by changing this block (e.g. in config_local.h).
#define SRC_MANUFACTURER      0
#define SRC_SERVICE_DATA      1
#define COMPANY_ANY           -1

#ifndef DIMES_MODEL
#define DIMES_MODEL           "DIMES / SRG WAVE"
#endif
#ifndef DIMES_SOURCE
#define DIMES_SOURCE          SRC_MANUFACTURER
#endif
#ifndef DIMES_COMPANY_ID
#define DIMES_COMPANY_ID      0xFFFF   // SRC_MANUFACTURER only (COMPANY_ANY = any)
#endif
#ifndef DIMES_UUID
#define DIMES_UUID            ""       // SRC_SERVICE_DATA only: service data UUID
#endif
#ifndef DIMES_LEVEL_OFFSET
#define DIMES_LEVEL_OFFSET    2
#endif
#ifndef DIMES_LEVEL_LEN
#define DIMES_LEVEL_LEN       1        // 1, 2 or 4 bytes
#endif
#ifndef DIMES_LEVEL_BIG_ENDIAN
#define DIMES_LEVEL_BIG_ENDIAN false
#endif
#ifndef DIMES_LEVEL_FACTOR
#define DIMES_LEVEL_FACTOR    1.0f
#endif
#ifndef DIMES_LEVEL_ADD
#define DIMES_LEVEL_ADD       0.0f
#endif
#ifndef DIMES_BATTERY_ENABLED
#define DIMES_BATTERY_ENABLED true     // false if the sensor does not report a battery level
#endif
#ifndef DIMES_BATTERY_OFFSET
#define DIMES_BATTERY_OFFSET  1
#endif
#ifndef DIMES_BATTERY_LEN
#define DIMES_BATTERY_LEN     1
#endif
#ifndef DIMES_BATTERY_BIG_ENDIAN
#define DIMES_BATTERY_BIG_ENDIAN false
#endif
#ifndef DIMES_BATTERY_FACTOR
#define DIMES_BATTERY_FACTOR  1.0f
#endif
#ifndef DIMES_BATTERY_ADD
#define DIMES_BATTERY_ADD     0.0f
#endif

// ---- Mopeka ---------------------------------------------------------------
// The sensor measures the liquid height in mm. Level % = (height - empty) / (full - empty).
// Typical values: Europe 6 kg 38/336, 11 kg 38/366, 14 kg 38/467, US 20 lb 38/254 mm.
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
#define MOPEKA_STD_COMPANY_ID    0x000D // Texas Instruments (Mopeka Standard)
// Mopeka Standard: the speed of sound depends on the gas mix (web UI, % propane, rest butane)
#ifndef DEFAULT_LPG_PROPANE
#define DEFAULT_LPG_PROPANE      100
#endif

// ============================================================
// PLUGIN: CARAWATER PUMP
// ------------------------------------------------------------
// Protocol taken from the CaraWater ISP 15. Other CaraWater pumps using the same
// app (e.g. ISP 20) probably work too, but are untested. The pump accepts only ONE
// connection at a time - use "Bluetooth für App freigeben" to let the app connect.
// ============================================================
#define PUMP_SERVICE_UUID     "0000ff00-0000-1000-8000-00805f9b34fb"
#define PUMP_NOTIFY_UUID      "0000ff01-0000-1000-8000-00805f9b34fb"
#define PUMP_WRITE_UUID       "0000ff02-0000-1000-8000-00805f9b34fb"
// Alternative layout (other Seaflo based pumps), tried if the one above is not found
#define PUMP_ALT_SERVICE_UUID "0000fff0-0000-1000-8000-00805f9b34fb"
#define PUMP_ALT_NOTIFY_UUID  "0000fff1-0000-1000-8000-00805f9b34fb"
#define PUMP_ALT_WRITE_UUID   "0000fff2-0000-1000-8000-00805f9b34fb"
#ifndef PUMP_OFFLINE_MS
#define PUMP_OFFLINE_MS       10000  // No status frame for 10 s = pump offline
#endif
#ifndef MQTT_PUMP_MS
#define MQTT_PUMP_MS          10000  // Periodic publish (also published on changes)
#endif

// ============================================================
// PLUGIN: RV TECH ROOF HATCH
// ------------------------------------------------------------
// ASCII telegrams over a BLE UART bridge (characteristic FFE1):
//   send "P_d=NNN\r\n" (target position), receive "P_a=NNN\r\n" (reported position)
// Positions 0-100, presets: open 100, closed 0, rain 12, vent 102.
// MAC address and PIN are set in the web UI (not in config_local.h).
// Ported from the Home Assistant integration rvtech-hacs.
// ============================================================
#define RVTECH_SERVICE_UUID   "0000ffe0-0000-1000-8000-00805f9b34fb"
#define RVTECH_CHAR_UUID      "0000ffe1-0000-1000-8000-00805f9b34fb"
#ifndef RVTECH_TARGET_TIMEOUT_MS
#define RVTECH_TARGET_TIMEOUT_MS 60000 // A sent target counts as "moving" for at most 60 s
#endif
#ifndef RVTECH_SEND_WAIT_MS
#define RVTECH_SEND_WAIT_MS   10000  // A command waits this long for a (re)connection
#endif
#ifndef MQTT_RVTECH_MS
#define MQTT_RVTECH_MS        30000
#endif

// ============================================================
// PLUGIN: WCS FRIDGE
// ------------------------------------------------------------
// The fridge sends ASCII status messages "/SC0/t0,t1,t2,<temp zone 1>,<temp zone 2>,
// <set zone 1>,<set zone 2>,t7,<voltage x10>,t9,<battery %>,...".
// The GATT UUIDs are not documented: by default the plugin subscribes to every
// characteristic that can notify. Fixed UUIDs, the Bluetooth PIN and an optional
// poll command are set in the web UI (not in config_local.h).
// ============================================================
#ifndef WCS_OFFLINE_MS
#define WCS_OFFLINE_MS        120000 // No status message for 2 min = fridge offline
#endif
#ifndef MQTT_WCS_MS
#define MQTT_WCS_MS           30000
#endif

// ============================================================
// CORE CONFIGURATION (stored in /config.json; plugin settings live in the plugins)
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

// Device wide runtime values (WiFi diagnosis)
struct CoreData {
    int rssi;              // WiFi signal strength (written by wifi_manager)
    uint32_t wifi_drops;   // WiFi disconnects since boot
    char wifi_reason[32];  // Reason of the last WiFi disconnect
};

extern AppConfig config;
extern CoreData coreData;

#endif // CONFIG_H
