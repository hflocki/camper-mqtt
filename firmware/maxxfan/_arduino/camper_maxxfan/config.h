#pragma once

// Optional personal overrides, not in git (see config_local.h.example). Everything defined there
// wins over the defaults in this file, e.g. own passwords or WiFi / MQTT for a freshly flashed device.
#if __has_include("config_local.h")
#include "config_local.h"
#endif
// =====================================================================
//  Camper MaxxFan - VanTurtle board (TCA9555) -> MQTT / Home Assistant
// =====================================================================

#define FW_VERSION              "1.2.0"

// ---- Device / access point ------------------------------------------
#ifndef DEFAULT_HOSTNAME
#define DEFAULT_HOSTNAME        "camper-maxxfan"
#endif
#ifndef AP_SSID_PREFIX
#define AP_SSID_PREFIX          "Camper-MaxxFan-"   // + last 6 hex digits of the MAC
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD     "maxxfan1234"       // min. 8 characters - CHANGE ME
#endif
#ifndef DEFAULT_OTA_PASSWORD
#define DEFAULT_OTA_PASSWORD    "maxxfan1234"       // Arduino IDE network port - CHANGE ME
#endif

// ---- I2C / port expander ---------------------------------------------
// ESP32-C3 Super Mini: GPIO5 = SDA, GPIO6 = SCL (same as the ESPHome config)
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN             5
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN             6
#endif
#ifndef I2C_FREQ_HZ
#define I2C_FREQ_HZ             50000
#endif
#ifndef TCA9555_ADDR
#define TCA9555_ADDR            0x27
#endif

// 1 = button outputs are active HIGH (like the ESPHome gpio switches), 0 = active LOW
#ifndef OUTPUT_ACTIVE_HIGH
#define OUTPUT_ACTIVE_HIGH      1
#endif
// 1 = "Auto Hold" input reads HIGH when active (ESPHome default), 0 = inverted
#ifndef HOLD_ACTIVE_HIGH
#define HOLD_ACTIVE_HIGH        1
#endif

// ---- Behaviour defaults (all editable in the web UI) -----------------
#ifndef DEFAULT_PULSE_MS
#define DEFAULT_PULSE_MS        150     // length of a simulated button press
#endif
#ifndef DEFAULT_PORT0_ENABLED
#define DEFAULT_PORT0_ENABLED   0       // Port 0 = RJ11
#endif
#ifndef DEFAULT_PORT1_ENABLED
#define DEFAULT_PORT1_ENABLED   1       // Port 1 = RJ45
#endif
#ifndef DEFAULT_SYNC_PORT
#define DEFAULT_SYNC_PORT       1       // port used by "sync to OFF"
#endif

// ---- MQTT ------------------------------------------------------------
#ifndef DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PORT       1883
#endif
#ifndef DEFAULT_MQTT_PREFIX
#define DEFAULT_MQTT_PREFIX     "camper/maxxfan"
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
#ifndef DEFAULT_MQTT_SERVER
#define DEFAULT_MQTT_SERVER     ""
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
#ifndef STATE_REPUBLISH_MS
#define STATE_REPUBLISH_MS      60000   // full state publish interval
#endif

// ---- Timing ----------------------------------------------------------
#ifndef INPUT_POLL_MS
#define INPUT_POLL_MS           50
#endif
#ifndef SYNC_AUTO_SETTLE_MS
#define SYNC_AUTO_SETTLE_MS     500
#endif
#ifndef SYNC_HOLD_TIMEOUT_MS
#define SYNC_HOLD_TIMEOUT_MS    10000
#endif
#ifndef WIFI_AP_FALLBACK_MS
#define WIFI_AP_FALLBACK_MS     60000   // open setup AP if STA is down this long
#endif
