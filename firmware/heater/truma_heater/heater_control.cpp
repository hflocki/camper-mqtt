#include "heater_control.h"

#include "config.h"
#include "logger.h"
#include "settings.h"

using namespace truma;

namespace heater {

static const char *const TAG = "heater";

static InetBox *g_box = nullptr;
static TrumaState g_state;

// Decoking
static bool g_decoke_active = false;
static bool g_decoke_confirmed = false;
static uint32_t g_decoke_start_ms = 0;
static float g_decoke_prev_temp = 0;
static HeatingMode g_decoke_prev_mode = HeatingMode::MODE_OFF;

#if HEATER_SUPPORTS_BOOST
static const HeatingMode DECOKE_MODE = HeatingMode::MODE_BOOST;
#else
static const HeatingMode DECOKE_MODE = HeatingMode::MODE_HIGH;
#endif

// ---- helpers ------------------------------------------------------------------------------------

static bool parse_fan_mode(const String &s, HeatingMode &out) {
  String m = s;
  m.trim();
  m.toLowerCase();
  if (m == "off") out = HeatingMode::MODE_OFF;
  else if (m == "eco") out = HeatingMode::MODE_ECO;
  else if (m == "high") out = HeatingMode::MODE_HIGH;
#if HEATER_SUPPORTS_BOOST
  else if (m == "boost") out = HeatingMode::MODE_BOOST;
#endif
  else return false;
  return true;
}

static HeatingMode remembered_fan_mode() {
  HeatingMode m;
  if (!parse_fan_mode(g_settings.fan_mode, m) || m == HeatingMode::MODE_OFF) m = HeatingMode::MODE_ECO;
  return m;
}

static bool send(const Command &cmd) {
  if (g_box == nullptr) return false;
  if (!g_state.heater_valid) {
    LOGW(TAG, "Command ignored: no heater status from CP Plus yet");
    return false;
  }
  if (!g_box->send_command(cmd)) {
    LOGW(TAG, "Command queue full");
    return false;
  }
  return true;
}

static bool send_room(float temp, HeatingMode mode) {
  Command c;
  c.type = CommandType::SET_ROOM;
  c.value = temp;
  c.mode = mode;
  return send(c);
}

static void user_override() {
  // Any manual change of the room heating ends a running decoke cycle without restoring.
  if (g_decoke_active) {
    LOGI(TAG, "Decoking cancelled by user change");
    g_decoke_active = false;
  }
}

// ---- public -------------------------------------------------------------------------------------

void begin(InetBox *box) { g_box = box; }

const TrumaState &state() { return g_state; }

bool room_on() { return g_state.heater_valid && g_state.heater.target_temp_room != TargetTemp::OFF; }

float room_setpoint() {
  if (room_on() && !g_decoke_active) return temp_code_to_decimal(g_state.heater.target_temp_room, 0);
  return g_settings.room_setpoint;
}

const char *fan_mode() {
  if (room_on()) return heating_mode_to_str(g_state.heater.heating_mode);
  return heating_mode_to_str(remembered_fan_mode());
}

const char *hvac_action() {
  uint8_t st = (uint8_t) g_state.heater.operating_status;
  bool running = g_state.heater_valid && st >= (uint8_t) OperatingStatus::START_OR_COOL_DOWN;
  if (!room_on()) {
    // Ventilation selected on the CP Plus (or fan after-run): the status frame only shows the
    // heater running with room and water off; the vent level itself is not reported to the
    // iNet Box (verified with community captures, levels 1-10).
    bool water_off = !g_state.heater_valid || g_state.heater.target_temp_water == TargetTemp::OFF;
    return (running && water_off) ? "fan" : "off";
  }
  return running ? "heating" : "idle";
}

int error_code() {
  if (!g_state.heater_valid) return 0;
  return g_state.heater.error_code_high * 100 + g_state.heater.error_code_low;
}

bool set_room_on(bool on) {
  user_override();
  if (!on) return send_room(0, HeatingMode::MODE_OFF);
  return send_room(g_settings.room_setpoint, remembered_fan_mode());
}

bool set_room_temp(float celsius) {
  user_override();
  if (isnan(celsius) || celsius < 5) return send_room(0, HeatingMode::MODE_OFF);
  if (celsius > 30) celsius = 30;
  g_settings.room_setpoint = celsius;
  settings_save_heater();
  if (room_on()) {
    Command c;
    c.type = CommandType::SET_ROOM_TEMP;
    c.value = celsius;
    return send(c);
  }
  return send_room(celsius, remembered_fan_mode());
}

bool set_setpoint(float celsius) {
  if (isnan(celsius)) return false;
  if (celsius < 5) celsius = 5;
  if (celsius > 30) celsius = 30;
  g_settings.room_setpoint = celsius;
  settings_save_heater();
  if (!room_on()) return true;  // only remembered, applied on next "heat"
  user_override();
  Command c;
  c.type = CommandType::SET_ROOM_TEMP;
  c.value = celsius;
  return send(c);
}

bool set_fan_mode(const String &mode, bool power_on) {
  HeatingMode m;
  if (!parse_fan_mode(mode, m)) {
    LOGW(TAG, "Unsupported fan mode '%s'", mode.c_str());
    return false;
  }
  if (m == HeatingMode::MODE_OFF) {
    user_override();
    return send_room(0, HeatingMode::MODE_OFF);
  }
  g_settings.fan_mode = heating_mode_to_str(m);
  settings_save_heater();

  if (room_on()) {
    user_override();
    Command c;
    c.type = CommandType::SET_FAN_MODE;
    c.mode = m;
    return send(c);
  }
  if (!power_on) return true;  // remembered for the next switch-on
  user_override();
  return send_room(g_settings.room_setpoint, m);
}

bool set_water_temp(float celsius) {
#if !WATER_SUPPORTS_BOOST
  if (celsius >= 80) celsius = 60;
#endif
  Command c;
  c.type = CommandType::SET_WATER;
  c.value = celsius;
  return send(c);
}

bool set_water_mode(const String &mode) {
  String m = mode;
  m.trim();
  m.toLowerCase();
  if (m == "off") return set_water_temp(0);
  if (m == "eco") return set_water_temp(40);
  if (m == "high") return set_water_temp(60);
  if (m == "boost") return set_water_temp(200);
  LOGW(TAG, "Unsupported water mode '%s'", mode.c_str());
  return false;
}

bool set_energy_mix(const String &mix, uint16_t power_level) {
  String m = mix;
  m.trim();
  m.toLowerCase();
  Command c;
  c.type = CommandType::SET_ENERGY_MIX;
  c.level = decimal_to_el_power_level(power_level);
  if (m == "gas" || m == "diesel") c.mix = EnergyMix::GAS;
  else if (m == "mix") c.mix = EnergyMix::MIX;
  else if (m == "electricity" || m == "electric") c.mix = EnergyMix::ELECTRICITY;
  else return false;
  return send(c);
}

bool sync_clock() {
  Command c;
  c.type = CommandType::SYNC_CLOCK;
  return send(c);
}

// ---- timer --------------------------------------------------------------------------------------

static bool parse_hhmm(const String &s, uint16_t &minutes) {
  int colon = s.indexOf(':');
  if (colon < 1) return false;
  long h = s.substring(0, colon).toInt();
  long m = s.substring(colon + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  minutes = h * 60 + m;
  return true;
}

static bool parse_water(const String &s, float &out) {
  String w = s;
  w.trim();
  w.toLowerCase();
  if (w.isEmpty()) return false;
  if (w == "off") out = 0;
  else if (w == "eco") out = 40;
  else if (w == "high") out = 60;
  else if (w == "boost") out = 200;
  else out = w.toFloat();
#if !WATER_SUPPORTS_BOOST
  if (out >= 80) out = 60;
#endif
  return true;
}

// Defaults for a timer command: current timer values if set, else the remembered room settings.
static void timer_defaults(float &temp, HeatingMode &mode) {
  temp = g_settings.room_setpoint;
  mode = remembered_fan_mode();
  if (!g_state.timer_valid) return;
  const auto &t = g_state.timer;
  if (t.timer_target_temp_room != TargetTemp::OFF) temp = temp_code_to_decimal(t.timer_target_temp_room, temp);
  if (t.timer_heating_mode != HeatingMode::MODE_OFF) mode = t.timer_heating_mode;
}

static bool send_timer(uint16_t start, uint16_t stop, float temp, HeatingMode mode, float water) {
  if (start == stop) {
    LOGW(TAG, "Timer: start and stop are equal");
    return false;
  }
  if (!g_state.timer_valid) {
    LOGW(TAG, "Timer: no timer status from CP Plus yet");
    return false;
  }
  Command c;
  c.type = CommandType::SET_TIMER;
  c.start = start;
  c.stop = stop;
  c.value = temp;
  c.mode = mode;
  c.water = water;
  return send(c);
}

bool set_timer_active(bool on) {
  if (!on) {
    Command c;
    c.type = CommandType::TIMER_OFF;
    return send(c);
  }
  if (!g_state.timer_valid) {
    LOGW(TAG, "Timer: no timer status from CP Plus yet");
    return false;
  }
  const auto &t = g_state.timer;
  float temp;
  HeatingMode mode;
  timer_defaults(temp, mode);
  return send_timer(t.timer_start_hours * 60 + t.timer_start_minutes, t.timer_stop_hours * 60 + t.timer_stop_minutes,
                    temp, mode, NAN);
}

bool set_timer(const String &spec) {
  String s = spec;
  s.trim();
  if (s.equalsIgnoreCase("off") || s == "0") return set_timer_active(false);
  if (s.equalsIgnoreCase("on") || s == "1") return set_timer_active(true);

  // Split "HH:MM-HH:MM,temp,fan,water"
  String parts[4];
  int n = 0;
  int from = 0;
  while (n < 4) {
    int comma = s.indexOf(',', from);
    parts[n++] = comma < 0 ? s.substring(from) : s.substring(from, comma);
    if (comma < 0) break;
    from = comma + 1;
  }
  for (auto &p : parts) p.trim();

  int dash = parts[0].indexOf('-');
  uint16_t start, stop;
  if (dash < 0 || !parse_hhmm(parts[0].substring(0, dash), start) || !parse_hhmm(parts[0].substring(dash + 1), stop)) {
    LOGW(TAG, "Timer: invalid format '%s' (expected HH:MM-HH:MM[,temp[,fan[,water]]])", spec.c_str());
    return false;
  }

  float temp;
  HeatingMode mode;
  timer_defaults(temp, mode);
  if (n > 1 && !parts[1].isEmpty()) temp = parts[1].toFloat();
  if (n > 2 && !parts[2].isEmpty() && !parse_fan_mode(parts[2], mode)) {
    LOGW(TAG, "Timer: unsupported fan mode '%s'", parts[2].c_str());
    return false;
  }
  float water = NAN;
  if (n > 3 && !parse_water(parts[3], water)) water = NAN;
  return send_timer(start, stop, temp, mode, water);
}

// ---- decoking -----------------------------------------------------------------------------------

bool decoke_start() {
  if (!g_state.heater_valid) {
    LOGW(TAG, "Decoking not possible: no heater status");
    return false;
  }
  if (g_decoke_active) return true;
  g_decoke_prev_temp = temp_code_to_decimal(g_state.heater.target_temp_room, 0);
  g_decoke_prev_mode = g_state.heater.heating_mode;
  if (!send_room(DECOKE_TARGET_TEMP, DECOKE_MODE)) return false;
  g_decoke_active = true;
  g_decoke_confirmed = false;
  g_decoke_start_ms = millis();
  LOGI(TAG, "Decoking started for %u min (previous: %.0f C / %s)", g_settings.decoke_minutes, g_decoke_prev_temp,
       heating_mode_to_str(g_decoke_prev_mode));
  return true;
}

void decoke_stop(bool restore) {
  if (!g_decoke_active) return;
  g_decoke_active = false;
  LOGI(TAG, "Decoking finished%s", restore ? ", restoring previous settings" : "");
  if (!restore) return;
  if (g_decoke_prev_temp < 5 || g_decoke_prev_mode == HeatingMode::MODE_OFF) {
    send_room(0, HeatingMode::MODE_OFF);
  } else {
    send_room(g_decoke_prev_temp, g_decoke_prev_mode);
  }
}

bool decoke_active() { return g_decoke_active; }

uint32_t decoke_remaining_s() {
  if (!g_decoke_active) return 0;
  uint32_t total = (uint32_t) g_settings.decoke_minutes * 60;
  uint32_t elapsed = (millis() - g_decoke_start_ms) / 1000;
  return elapsed >= total ? 0 : total - elapsed;
}

void set_decoke_minutes(uint16_t minutes) {
  if (minutes < 1) minutes = 1;
  if (minutes > 240) minutes = 240;
  g_settings.decoke_minutes = minutes;
  settings_save_heater();
}

// ---- loop ---------------------------------------------------------------------------------------

void set_sniffer(bool on) {
  if (g_box != nullptr) g_box->set_sniffer(on);
}

bool sniffer() { return g_box != nullptr && g_box->sniffer(); }

static void sniffer_loop() {
  SniffFrame f;
  for (int i = 0; i < 8 && g_box->sniff_pop(f); i++) {
    char hex[3 * 8 + 1];
    char *p = hex;
    for (uint8_t j = 0; j < f.len; j++) p += snprintf(p, 4, j ? " %02X" : "%02X", f.data[j]);
    *p = 0;
    LOGW("sniff", "SNIFF PID %02X: %s", f.pid, hex);
  }
}

void loop() {
  if (g_box == nullptr) return;
  g_box->get_state(g_state);
  sniffer_loop();
  if (!g_state.heater_valid) return;

  // Log error changes once (also reaches the MQTT log and the web UI log).
  static int last_error = 0;
  const int err = error_code();
  if (err != last_error) {
    if (err != 0) LOGW(TAG, "Heater error code %d", err);
    else LOGI(TAG, "Heater error code %d cleared", last_error);
    last_error = err;
  }

  const auto &h = g_state.heater;
  const float target = temp_code_to_decimal(h.target_temp_room, 0);

  if (g_decoke_active) {
    const bool at_decoke = target >= DECOKE_TARGET_TEMP && h.heating_mode == DECOKE_MODE;
    if (!g_decoke_confirmed) {
      if (at_decoke && !g_state.update_pending) g_decoke_confirmed = true;
    } else if (!at_decoke && !g_state.update_pending) {
      // Changed on the CP Plus or elsewhere: respect it.
      LOGI(TAG, "Decoking cancelled: heater settings changed externally");
      g_decoke_active = false;
    }
    if (g_decoke_active && decoke_remaining_s() == 0) decoke_stop(true);
    return;
  }

  // Follow changes made on the CP Plus so "heat" restores what was used last (RAM only).
  if (target >= 5 && !g_state.update_pending) {
    g_settings.room_setpoint = target;
    if (h.heating_mode == HeatingMode::MODE_ECO || h.heating_mode == HeatingMode::MODE_HIGH ||
        h.heating_mode == HeatingMode::MODE_BOOST) {
      const char *m = heating_mode_to_str(h.heating_mode);
      if (g_settings.fan_mode != m) g_settings.fan_mode = m;
    }
  }
}

}  // namespace heater
