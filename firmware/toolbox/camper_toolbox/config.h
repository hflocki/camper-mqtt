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
//   - passive sensors (none at the moment; gas and tanks are read by tanklevel-node) only listen
//   - connecting devices (pump, roof hatch, fridge) keep a BLE connection each:
//     PLUGIN_CARAWATER + PLUGIN_RVTECH + PLUGIN_WCS_FRIDGE <= 3
//     (NimBLE default CONFIG_BT_NIMBLE_MAX_CONNECTIONS), e.g.
//       pump 1 + hatch 1 + fridge 1 = 3 ok
//       pump 0 + hatch 2 + fridge 1 = 3 ok
//       pump 1 + hatch 2 + fridge 1 = 4 -> build error
// ============================================================
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
#define CT_BLE_PASSIVE  0   // passive plugins (only listen to advertisements); none at the moment
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
#define FW_VERSION            "1.2.2"
#define FW_DEVICE             "toolbox"   // device id in the firmware marker (fwcheck.h)

// Network upload from the Arduino IDE (port in the IDE, password DEFAULT_OTA_PASSWORD).
// 0 = off, updates then only through the web UI (with login, if set).
#ifndef ENABLE_ARDUINO_OTA
#define ENABLE_ARDUINO_OTA    1
#endif

#ifndef DEVICE_NAME
#define DEVICE_NAME           "Toolbox Node"      // Name in Home Assistant and in the web UI
#endif
#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID       "Toolbox-Node"      // Base name; "-XXXXXX" (last 3 MAC bytes) is appended
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
#define OTA_HOSTNAME          "toolbox-node"
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
// Third layout (BLE UART bridge FFE0/FFE1), tried after the two above
#define PUMP_UART_SERVICE_UUID "0000ffe0-0000-1000-8000-00805f9b34fb"
#define PUMP_UART_CHAR_UUID    "0000ffe1-0000-1000-8000-00805f9b34fb"
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
// The fridge talks over a BLE UART bridge (service FFE0, characteristic FFE1). Other
// UUIDs, the Bluetooth PIN and an optional poll command are set in the web UI
// (not in config_local.h); empty UUID fields mean FFE0/FFE1.
// ============================================================
#define WCS_SERVICE_UUID      "0000ffe0-0000-1000-8000-00805f9b34fb"
#define WCS_CHAR_UUID         "0000ffe1-0000-1000-8000-00805f9b34fb"
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
