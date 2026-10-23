#pragma once
// =====================================================================
//  Camper Display - Guition ESP32-4848S040 (4", 480x480, ESP32-S3)
//  Wall panel + web page, both fed live from the MQTT broker
// =====================================================================

#define FW_NAME                 "camper-display"
#define FW_VERSION              "1.1.0"

// ---- Device / access point ------------------------------------------
#define DEFAULT_HOSTNAME        "camper-display"
#define AP_SSID_PREFIX          "Camper-Display-"   // + last 6 hex digits of the MAC
#define DEFAULT_AP_PASSWORD     "display1234"       // min. 8 characters - CHANGE ME
#define DEFAULT_OTA_PASSWORD    "display1234"       // Arduino IDE network port - CHANGE ME
#define WIFI_AP_FALLBACK_MS     60000               // open setup AP after 1 min without WiFi

// ---- MQTT ------------------------------------------------------------
#define DEFAULT_MQTT_PORT       1883
#define DEFAULT_PREFIX_SELF     "camper/display"
#define DEFAULT_PREFIX_TANK     "camper/tanklevel"
#define DEFAULT_PREFIX_LEVEL    "camper/level"
#define DEFAULT_PREFIX_HEATER   "camper/heater"
#define DEFAULT_PREFIX_FAN      "camper/maxxfan"
#define DEFAULT_PREFIX_TEMP     "camper/temp"
#define DEFAULT_FAN_PORT        1                   // VanTurtle port 1 = RJ45
#define DEFAULT_FINDER_FILTER   "zigbee2mqtt/#"
#define MQTT_BUFFER_SIZE        4096                // larger messages are dropped by PubSubClient
#define MQTT_RECONNECT_MS       5000
#define SELF_PUBLISH_MS         60000

// ---- Time --------------------------------------------------------------
#define DEFAULT_NTP_SERVER      "pool.ntp.org"
#define DEFAULT_TZ              "CET-1CEST,M3.5.0,M10.5.0/3"   // Europe/Berlin

// ---- Pages / tiles (user extensible) ----------------------------------
#define MAX_PAGES               8
#define MAX_TILES               40

// ---- Display hardware: Guition ESP32-4848S040 (ST7701S RGB + GT911) -----
#define LCD_WIDTH               480
#define LCD_HEIGHT              480
#define LCD_BL_PIN              38
#define LCD_SPI_CS              39
#define LCD_SPI_SCK             48
#define LCD_SPI_MOSI            47
#define LCD_DE                  18
#define LCD_VSYNC               17
#define LCD_HSYNC               16
#define LCD_PCLK                21
#define LCD_R0 11
#define LCD_R1 12
#define LCD_R2 13
#define LCD_R3 14
#define LCD_R4 0
#define LCD_G0 8
#define LCD_G1 20
#define LCD_G2 3
#define LCD_G3 46
#define LCD_G4 9
#define LCD_G5 10
#define LCD_B0 4
#define LCD_B1 5
#define LCD_B2 6
#define LCD_B3 7
#define LCD_B4 15
#define LCD_PCLK_HZ             12000000
// ST7701 init sequence: 9 = current Guition panels, 1 = older batches.
// If the picture stays black / has wrong colours, try the other one.
#define LCD_INIT_TYPE           9
// 0..3, rotates drawing and touch together (wall mount orientation)
#define LCD_ROTATION            0

#define TOUCH_SDA               19
#define TOUCH_SCL               45
#define TOUCH_I2C_HZ            400000
// Touch axis correction if taps land in the wrong place
#define TOUCH_SWAP_XY           0
#define TOUCH_INVERT_X          0
#define TOUCH_INVERT_Y          0

// ---- Display behaviour defaults (editable in the web UI) ---------------
#define DEFAULT_BRIGHTNESS      80      // %
#define DEFAULT_DIM_BRIGHTNESS  10      // % after timeout, 0 = screen off
#define DEFAULT_SCREEN_TIMEOUT  120     // s, 0 = never dim

// LVGL draw buffer: lines per partial buffer (two buffers)
#define LV_BUF_LINES            48
