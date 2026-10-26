#pragma once

// Optional personal overrides, not in git (see config_local.h.example). Everything defined there
// wins over the defaults in this file, e.g. own passwords or WiFi / MQTT for a freshly flashed device.
#if __has_include("config_local.h")
#include "config_local.h"
#endif
// =====================================================================
//  Camper Display - Guition ESP32-4848S040 (4", 480x480, ESP32-S3)
//  Wall panel + web page, both fed live from the MQTT broker
// =====================================================================

#define FW_NAME                 "camper-display"
#define FW_VERSION              "1.6.0"

// ---- Device / access point ------------------------------------------
#ifndef DEFAULT_DISPLAY_NAME
#define DEFAULT_DISPLAY_NAME    ""                  // name shown in the header (web UI: Einstellungen)
#endif
#ifndef DEFAULT_HOSTNAME
#define DEFAULT_HOSTNAME        "camper-display"
#endif
#ifndef AP_SSID_PREFIX
#define AP_SSID_PREFIX          "Camper-Display-"   // + last 6 hex digits of the MAC
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD     "display1234"       // min. 8 characters - CHANGE ME
#endif
#ifndef DEFAULT_OTA_PASSWORD
#define DEFAULT_OTA_PASSWORD    "display1234"       // Arduino IDE network port - CHANGE ME
#endif
#ifndef WIFI_AP_FALLBACK_MS
#define WIFI_AP_FALLBACK_MS     60000               // open setup AP after 1 min without WiFi
#endif

// ---- MQTT ------------------------------------------------------------
#ifndef DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PORT       1883
#endif
#ifndef DEFAULT_PREFIX_SELF
#define DEFAULT_PREFIX_SELF     "camper/display"
#endif
#ifndef DEFAULT_PREFIX_TANK
#define DEFAULT_PREFIX_TANK     "camper/tanklevel"
#endif
#ifndef DEFAULT_PREFIX_LEVEL
#define DEFAULT_PREFIX_LEVEL    "camper/level"
#endif
#ifndef DEFAULT_PREFIX_HEATER
#define DEFAULT_PREFIX_HEATER   "camper/heater"
#endif
#ifndef DEFAULT_PREFIX_FAN
#define DEFAULT_PREFIX_FAN      "camper/maxxfan"
#endif
#ifndef DEFAULT_PREFIX_TEMP
#define DEFAULT_PREFIX_TEMP     "camper/temp"
#endif
#ifndef DEFAULT_PREFIX_BATTERY
#define DEFAULT_PREFIX_BATTERY  "camper/battery"
#endif

// ---- Battery warnings (camper-battery) ------------------------------------
#ifndef BAT_WARN_SOC
#define BAT_WARN_SOC            20      // warn below this state of charge in % (monitors with current sensor)
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
#define DEFAULT_WEB_USER        "admin" // login only applies once a password is set
#endif
#ifndef DEFAULT_WEB_PASSWORD
#define DEFAULT_WEB_PASSWORD    ""
#endif
#ifndef DEFAULT_FAN_PORT
#define DEFAULT_FAN_PORT        1                   // VanTurtle port 1 = RJ45
#endif
#ifndef DEFAULT_FINDER_FILTER
#define DEFAULT_FINDER_FILTER   "zigbee2mqtt/#"
#endif
// ---- Pushover (push messages for warnings) --------------------------
// Token of your Pushover application and your user key. Can also be entered in the
// web page (Einstellungen -> Pushover); these are only the factory defaults.
#ifndef DEFAULT_PUSHOVER_TOKEN
#define DEFAULT_PUSHOVER_TOKEN  ""
#endif
#ifndef DEFAULT_PUSHOVER_USER
#define DEFAULT_PUSHOVER_USER   ""
#endif
#ifndef DEFAULT_PUSHOVER_TITLE
#define DEFAULT_PUSHOVER_TITLE  "Camper"
#endif
#ifndef PUSHOVER_URL
#define PUSHOVER_URL            "https://api.pushover.net/1/messages.json"
#endif
#ifndef PUSHOVER_HOST
#define PUSHOVER_HOST           "api.pushover.net"   // must match PUSHOVER_URL
#endif
#ifndef PUSHOVER_STABLE_MS
#define PUSHOVER_STABLE_MS      30000   // a warning must be active this long before it is sent
#endif
#ifndef PUSHOVER_RETRIES
#define PUSHOVER_RETRIES        5       // attempts per message (no WiFi / server error)
#endif
#ifndef PUSHOVER_RETRY_MS
#define PUSHOVER_RETRY_MS       60000
#endif
#ifndef PUSHOVER_QUEUE
#define PUSHOVER_QUEUE          8
#endif

#ifndef MQTT_BUFFER_SIZE
#define MQTT_BUFFER_SIZE        4096                // larger messages are dropped by PubSubClient
#endif
#ifndef MQTT_RECONNECT_MS
#define MQTT_RECONNECT_MS       5000
#endif
#ifndef SELF_PUBLISH_MS
#define SELF_PUBLISH_MS         60000
#endif

// ---- Time --------------------------------------------------------------
#ifndef DEFAULT_NTP_SERVER
#define DEFAULT_NTP_SERVER      "pool.ntp.org"
#endif
#ifndef DEFAULT_TZ
#define DEFAULT_TZ              "CET-1CEST,M3.5.0,M10.5.0/3"   // Europe/Berlin
#endif

// ---- Pages / tiles (user extensible) ----------------------------------
#ifndef MAX_PAGES
#define MAX_PAGES               8
#endif
#ifndef MAX_TILES
#define MAX_TILES               40
#endif

// ---- Display hardware: Guition ESP32-4848S040 (ST7701S RGB + GT911) -----
#ifndef LCD_WIDTH
#define LCD_WIDTH               480
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT              480
#endif
#ifndef LCD_BL_PIN
#define LCD_BL_PIN              38
#endif
#ifndef LCD_SPI_CS
#define LCD_SPI_CS              39
#endif
#ifndef LCD_SPI_SCK
#define LCD_SPI_SCK             48
#endif
#ifndef LCD_SPI_MOSI
#define LCD_SPI_MOSI            47
#endif
#ifndef LCD_DE
#define LCD_DE                  18
#endif
#ifndef LCD_VSYNC
#define LCD_VSYNC               17
#endif
#ifndef LCD_HSYNC
#define LCD_HSYNC               16
#endif
#ifndef LCD_PCLK
#define LCD_PCLK                21
#endif
#ifndef LCD_R0
#define LCD_R0 11
#endif
#ifndef LCD_R1
#define LCD_R1 12
#endif
#ifndef LCD_R2
#define LCD_R2 13
#endif
#ifndef LCD_R3
#define LCD_R3 14
#endif
#ifndef LCD_R4
#define LCD_R4 0
#endif
#ifndef LCD_G0
#define LCD_G0 8
#endif
#ifndef LCD_G1
#define LCD_G1 20
#endif
#ifndef LCD_G2
#define LCD_G2 3
#endif
#ifndef LCD_G3
#define LCD_G3 46
#endif
#ifndef LCD_G4
#define LCD_G4 9
#endif
#ifndef LCD_G5
#define LCD_G5 10
#endif
#ifndef LCD_B0
#define LCD_B0 4
#endif
#ifndef LCD_B1
#define LCD_B1 5
#endif
#ifndef LCD_B2
#define LCD_B2 6
#endif
#ifndef LCD_B3
#define LCD_B3 7
#endif
#ifndef LCD_B4
#define LCD_B4 15
#endif
#ifndef LCD_PCLK_HZ
#define LCD_PCLK_HZ             12000000
#endif
// ST7701 init sequence: 9 = current Guition panels, 1 = older batches.
// If the picture stays black / has wrong colours, try the other one.
#ifndef LCD_INIT_TYPE
#define LCD_INIT_TYPE           9
#endif
// 0..3, rotates drawing and touch together (wall mount orientation)
#ifndef LCD_ROTATION
#define LCD_ROTATION            0
#endif

#ifndef TOUCH_SDA
#define TOUCH_SDA               19
#endif
#ifndef TOUCH_SCL
#define TOUCH_SCL               45
#endif
#ifndef TOUCH_I2C_HZ
#define TOUCH_I2C_HZ            400000
#endif
// Touch axis correction if taps land in the wrong place
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY           0
#endif
#ifndef TOUCH_INVERT_X
#define TOUCH_INVERT_X          0
#endif
#ifndef TOUCH_INVERT_Y
#define TOUCH_INVERT_Y          0
#endif

// ---- Display behaviour defaults (editable in the web UI) ---------------
#ifndef DEFAULT_BRIGHTNESS
#define DEFAULT_BRIGHTNESS      80      // %
#endif
#ifndef DEFAULT_DIM_BRIGHTNESS
#define DEFAULT_DIM_BRIGHTNESS  10      // % after timeout, 0 = screen off
#endif
#ifndef DEFAULT_SCREEN_TIMEOUT
#define DEFAULT_SCREEN_TIMEOUT  120     // s, 0 = never dim
#endif

// LVGL draw buffer: lines per partial buffer (two buffers)
#ifndef LV_BUF_LINES
#define LV_BUF_LINES            48
#endif
#ifndef LV_BUF_IN_PSRAM
#define LV_BUF_IN_PSRAM         1       // 0 = internal RAM (slightly faster drawing, but HTTPS / Pushover then lacks memory)
#endif
