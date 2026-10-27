// Compile-time configuration. Runtime settings (WiFi, MQTT, ...) are edited in the web UI and stored in NVS.
//
// Arduino IDE: choose your board below (exactly one line active). Pins of a profile can be changed in
// its block further down.
#pragma once

#include <Arduino.h>

// Optional personal overrides, not in git (see config_local.h.example). Everything defined there wins over
// the defaults below, e.g. your own AP password or the board selection.
#if __has_include("config_local.h")
#include "config_local.h"
#endif

// ---------------------------------------------------------------------------------------------
// >>> BOARD SELECTION <<<
// ---------------------------------------------------------------------------------------------
#if !defined(BOARD_SMARTAVAN) && !defined(BOARD_ESP32_GENERIC) && !defined(BOARD_ESP32S3_GENERIC) && \
    !defined(BOARD_ESP32C3_GENERIC) && !defined(BOARD_ESP32C3_SUPERMINI)
// #define BOARD_SMARTAVAN       // smartavan heater controller (Arduino board: "ESP32 Dev Module")
// #define BOARD_ESP32_GENERIC   // ESP32 DevKit + TJA1020/1021 module (Arduino board: "ESP32 Dev Module")
// #define BOARD_ESP32S3_GENERIC // ESP32-S3 DevKit + LIN module (Arduino board: "ESP32S3 Dev Module")
// #define BOARD_ESP32C3_GENERIC // ESP32-C3 + LIN module (Arduino board: "ESP32C3 Dev Module")
#define BOARD_ESP32C3_SUPERMINI  // ESP32-C3 Super Mini + LIN module (Arduino board: "ESP32C3 Dev Module")
#endif

#define FW_NAME "camper-heater"
#define FW_VERSION "1.2.0"

// ---------------------------------------------------------------------------------------------
// Board profiles
// ---------------------------------------------------------------------------------------------
#if defined(BOARD_SMARTAVAN)
// smartavan heater controller: ESP32-WROOM-32D with on-board LIN transceiver, RJ12 + JST pass-through
#define BOARD_NAME "smartavan heater controller"
#ifndef LIN_UART_NUM
#define LIN_UART_NUM UART_NUM_2
#endif
#ifndef LIN_TX_PIN
#define LIN_TX_PIN 16
#endif
#ifndef LIN_RX_PIN
#define LIN_RX_PIN 17
#endif

#elif defined(BOARD_ESP32_GENERIC)
// Plain ESP32 DevKit + TJA1020/TJA1021 module, wiring as in mc0110/inetbox2mqtt ("ESP32" profile)
#define BOARD_NAME "ESP32 + LIN module"
#ifndef LIN_UART_NUM
#define LIN_UART_NUM UART_NUM_2
#endif
#ifndef LIN_TX_PIN
#define LIN_TX_PIN 17
#endif
#ifndef LIN_RX_PIN
#define LIN_RX_PIN 16
#endif
#ifndef MQTT_LED_PIN
#define MQTT_LED_PIN 4
#endif
#ifndef LIN_LED_PIN
#define LIN_LED_PIN 15
#endif

#elif defined(BOARD_ESP32S3_GENERIC)
// ESP32-S3 DevKit + LIN module (any free GPIOs, adjust with build flags)
#define BOARD_NAME "ESP32-S3 + LIN module"
#ifndef LIN_UART_NUM
#define LIN_UART_NUM UART_NUM_1
#endif
#ifndef LIN_TX_PIN
#define LIN_TX_PIN 17
#endif
#ifndef LIN_RX_PIN
#define LIN_RX_PIN 18
#endif

#elif defined(BOARD_ESP32C3_GENERIC)
// ESP32-C3 (single core, UART0/1 only) + LIN module
#define BOARD_NAME "ESP32-C3 + LIN module"
#ifndef LIN_UART_NUM
#define LIN_UART_NUM UART_NUM_1
#endif
#ifndef LIN_TX_PIN
#define LIN_TX_PIN 21
#endif
#ifndef LIN_RX_PIN
#define LIN_RX_PIN 20
#endif

#elif defined(BOARD_ESP32C3_SUPERMINI)
// ESP32-C3 Super Mini (perfboard build, see docs/lochraster_c3.png). All used pins are on the header next to
// 5V/GND/3V3. GPIO20/21 are avoided: the ROM bootloader prints its boot log on GPIO21, which would end up on
// the LIN bus during every boot.
#define BOARD_NAME "Camper Heater (ESP32-C3 Super Mini)"
#ifndef LIN_UART_NUM
#define LIN_UART_NUM UART_NUM_1
#endif
#ifndef LIN_TX_PIN
#define LIN_TX_PIN 4
#endif
#ifndef LIN_RX_PIN
#define LIN_RX_PIN 3
#endif
#ifndef LIN_LED_PIN
#define LIN_LED_PIN 8  // on-board blue LED
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW true
#endif
// The small ceramic antenna of many Super Mini boards fails to connect at full TX power. 8.5 dBm is the
// commonly used work-around.
#ifndef WIFI_TX_POWER_QDBM
#define WIFI_TX_POWER_QDBM 34
#endif

#else
#error "No board profile selected (see BOARD SELECTION at the top of config.h)."
#endif

#ifndef LIN_BAUD
#define LIN_BAUD 9600
#endif
// Log output on the USB serial port (115200 baud). Logs are always available in the web UI and via MQTT.
#ifndef SERIAL_LOG_ENABLED
#define SERIAL_LOG_ENABLED true
#endif
// Optional transceiver pins, -1 = not connected
#ifndef LIN_CS_PIN
#define LIN_CS_PIN -1     // driven HIGH to enable the transceiver (TJA1020/1021 SLP/EN)
#endif
#ifndef LIN_FAULT_PIN
#define LIN_FAULT_PIN -1  // active LOW fault output of the transceiver
#endif
// Optional status LEDs, -1 = none
#ifndef MQTT_LED_PIN
#define MQTT_LED_PIN -1   // on = MQTT connected
#endif
#ifndef LIN_LED_PIN
#define LIN_LED_PIN -1    // flicker = LIN traffic, on = CP Plus polls us
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW false
#endif

// Set to true to only listen on the bus (never answer). Useful for debugging next to a real iNet Box.
#ifndef LIN_OBSERVER_MODE
#define LIN_OBSERVER_MODE false
#endif

// ---------------------------------------------------------------------------------------------
// Heater features
// ---------------------------------------------------------------------------------------------
// Heater model, factory default only: the model is selected in the web UI (System tab) and stored on
// the device. One of: "combi" (gas), "combi_e" (gas + 230 V), "combi_d" (diesel), "combi_d_e"
// (diesel + 230 V), "vario" (VarioHeat). See heater_model.h for what each model offers.
#ifndef DEFAULT_HEATER_MODEL
// Older config_local.h files used feature flags; map them so such builds keep their behaviour.
#if defined(HEATER_SUPPORTS_BOOST) && HEATER_SUPPORTS_BOOST
#if defined(HEATER_HAS_ELECTRIC) && HEATER_HAS_ELECTRIC
#define DEFAULT_HEATER_MODEL "combi_e"
#else
#define DEFAULT_HEATER_MODEL "combi"
#endif
#elif defined(HEATER_HAS_ELECTRIC) && HEATER_HAS_ELECTRIC
#define DEFAULT_HEATER_MODEL "combi_d_e"
#else
#define DEFAULT_HEATER_MODEL "combi_d"
#endif
#endif

// Decoking ("Freibrennen", diesel only): target temperature used while active (fan HIGH)
#define DECOKE_TARGET_TEMP 30
#define DECOKE_DEFAULT_MINUTES 30

// ---------------------------------------------------------------------------------------------
// Defaults for runtime settings
// ---------------------------------------------------------------------------------------------
#ifndef DEFAULT_HOSTNAME
#define DEFAULT_HOSTNAME "truma-heater"
#endif
#ifndef DEFAULT_AP_PASSWORD
#define DEFAULT_AP_PASSWORD "truma1234"  // min. 8 characters
#endif
// Factory defaults for WiFi / MQTT / web login. Used only while nothing is stored in NVS yet
// (first boot, factory reset or "Erase All Flash"). Set your own in config_local.h.
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif
#ifndef DEFAULT_WIFI_PASSWORD
#define DEFAULT_WIFI_PASSWORD ""
#endif
#ifndef DEFAULT_MQTT_SERVER
#define DEFAULT_MQTT_SERVER ""
#endif
#ifndef DEFAULT_MQTT_USER
#define DEFAULT_MQTT_USER ""
#endif
#ifndef DEFAULT_MQTT_PASSWORD
#define DEFAULT_MQTT_PASSWORD ""
#endif
#ifndef DEFAULT_WEB_USER
#define DEFAULT_WEB_USER ""
#endif
#ifndef DEFAULT_WEB_PASSWORD
#define DEFAULT_WEB_PASSWORD ""
#endif
#ifndef DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PORT 1883
#endif
// Same scheme as the other camper devices (camper/tanklevel, camper/maxxfan ...).
// Set "service/truma" in the web UI if a tool expects the mc0110/inetbox2mqtt topics.
#ifndef DEFAULT_MQTT_BASE
#define DEFAULT_MQTT_BASE "camper/heater"
#endif
#ifndef DEFAULT_HA_PREFIX
#define DEFAULT_HA_PREFIX "homeassistant"
#endif
#ifndef DEFAULT_NTP_SERVER
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#endif
#ifndef DEFAULT_TZ
#define DEFAULT_TZ "CET-1CEST,M3.5.0,M10.5.0/3"  // Europe/Berlin
#endif

// WiFi stability (power save off, 17.5 dBm in 0.25 dBm units)
#ifndef WIFI_TX_POWER_QDBM
#define WIFI_TX_POWER_QDBM 70
#endif

// Interval for periodic MQTT full state publish (changes are published within 1 s)
#define MQTT_PUBLISH_INTERVAL_MS 60000
// CP Plus is considered disconnected if it did not poll us for this long
#define CPPLUS_TIMEOUT_MS 60000
// Re-sync the CP Plus clock every 24 h
#define CLOCK_RESYNC_INTERVAL_MS (24UL * 3600UL * 1000UL)
