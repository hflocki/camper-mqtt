// High level heater control shared by MQTT and web UI: remembered setpoint/fan mode, decoking, command mapping.
#pragma once

#include <Arduino.h>

#include "inetbox.h"

namespace heater {

void begin(truma::InetBox *box);
void loop();

// Latest state snapshot (refreshed in loop()).
const truma::TrumaState &state();

// Room heating
bool set_room_on(bool on);                  // uses the remembered setpoint and fan mode
bool set_room_temp(float celsius);          // 0 = off; while off only remembers the setpoint
bool set_setpoint(float celsius);           // HA thermostat: remember, apply only if heating is on
// off/eco/high/boost. With power_on=false (HA thermostat) the mode is only remembered while heating is off.
bool set_fan_mode(const String &mode, bool power_on);
bool set_water_mode(const String &mode);    // off/eco/high/boost
bool set_water_temp(float celsius);         // 0/40/60/200
bool set_energy_mix(const String &mix, uint16_t power_level);
bool sync_clock();

// CP Plus timer. spec: "off", "on" (re-activate stored times) or "HH:MM-HH:MM[,temp[,fan[,water]]]".
// Missing temp/fan fall back to the timer's current values, then to the remembered setpoint and fan mode.
// water: off/eco/high/boost or °C; missing = unchanged.
bool set_timer(const String &spec);
bool set_timer_active(bool on);

// Decoking ("Freibrennen"): max. temperature + fan HIGH for a limited time, then restore.
bool decoke_start();
void decoke_stop(bool restore);
bool decoke_active();
uint32_t decoke_remaining_s();
void set_decoke_minutes(uint16_t minutes);

// Derived values for publishing
bool room_on();
const char *hvac_action();  // off / idle / heating
float room_setpoint();      // remembered or current target
const char *fan_mode();     // current fan mode, or the remembered one while off
// Fan modes of the selected model (eco/high[/boost] or night/auto/boost). Returns the count.
int fan_modes(const char *out[], int max);
int error_code();

// LIN sniffer (diagnostics): logs every changed frame of all other PIDs as warning "SNIFF ..."
// so it shows up in the web log and the MQTT log with default settings. Not persisted.
void set_sniffer(bool on);
bool sniffer();
// LIN bus diagnosis: plain-text verdict of the last 10 s, raw hex dump for 60 s
String lin_diag();
void set_raw_dump(bool on);
bool raw_dump();

}  // namespace heater
