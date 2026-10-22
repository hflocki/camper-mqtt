#pragma once
// =====================================================================
//  Camper MaxxFan - VanTurtle board (TCA9555) -> MQTT / Home Assistant
// =====================================================================

#define FW_VERSION              "1.1.0"

// ---- Device / access point ------------------------------------------
#define DEFAULT_HOSTNAME        "camper-maxxfan"
#define AP_SSID_PREFIX          "Camper-MaxxFan-"   // + last 6 hex digits of the MAC
#define DEFAULT_AP_PASSWORD     "maxxfan1234"       // min. 8 characters - CHANGE ME
#define DEFAULT_OTA_PASSWORD    "maxxfan1234"       // Arduino IDE network port - CHANGE ME

// ---- I2C / port expander ---------------------------------------------
// ESP32-C3 Super Mini: GPIO5 = SDA, GPIO6 = SCL (same as the ESPHome config)
#define I2C_SDA_PIN             5
#define I2C_SCL_PIN             6
#define I2C_FREQ_HZ             50000
#define TCA9555_ADDR            0x27

// 1 = button outputs are active HIGH (like the ESPHome gpio switches), 0 = active LOW
#define OUTPUT_ACTIVE_HIGH      1
// 1 = "Auto Hold" input reads HIGH when active (ESPHome default), 0 = inverted
#define HOLD_ACTIVE_HIGH        1

// ---- Behaviour defaults (all editable in the web UI) -----------------
#define DEFAULT_PULSE_MS        150     // length of a simulated button press
#define DEFAULT_PORT0_ENABLED   0       // Port 0 = RJ11
#define DEFAULT_PORT1_ENABLED   1       // Port 1 = RJ45
#define DEFAULT_SYNC_PORT       1       // port used by "sync to OFF"

// ---- MQTT ------------------------------------------------------------
#define DEFAULT_MQTT_PORT       1883
#define DEFAULT_MQTT_PREFIX     "camper/maxxfan"
#define STATE_REPUBLISH_MS      60000   // full state publish interval

// ---- Timing ----------------------------------------------------------
#define INPUT_POLL_MS           50
#define SYNC_AUTO_SETTLE_MS     500
#define SYNC_HOLD_TIMEOUT_MS    10000
#define WIFI_AP_FALLBACK_MS     60000   // open setup AP if STA is down this long
