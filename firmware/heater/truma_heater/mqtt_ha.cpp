#include "mqtt_ha.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <map>

#include "config.h"
#include "heater_control.h"
#include "logger.h"
#include "settings.h"
#include "wifi_manager.h"

using namespace truma;

namespace mqtt {

static const char *const TAG = "mqtt";

static WiFiClient g_net;
static PubSubClient g_client(g_net);
static std::map<String, String> g_cache;  // last published payload per key
static String g_node_id;
static uint32_t g_last_attempt = 0;
static uint32_t g_last_check = 0;
static uint32_t g_last_full = 0;
static bool g_force = true;
static bool g_discovery_pending = false;
static uint32_t g_reboot_at = 0;
static uint32_t g_log_seq = 0;  // next log line to forward to MQTT

static String topic(const char *section, const char *key) { return g_settings.mqtt_base + "/" + section + "/" + key; }
static String status_topic() { return g_settings.mqtt_base + "/status"; }

static void pub_raw(const String &t, const String &payload, bool retain) {
  if (!g_client.publish(t.c_str(), payload.c_str(), retain)) {
    LOGW(TAG, "Publish failed: %s", t.c_str());
  }
}

// Publishes a state value only if it changed (or a full refresh is due).
static void pub(const char *key, const String &value) {
  auto it = g_cache.find(key);
  if (!g_force && it != g_cache.end() && it->second == value) return;
  g_cache[key] = value;
  pub_raw(topic("control_status", key), value, true);
}

static String fmt1(float v) {
  char b[16];
  snprintf(b, sizeof(b), "%.1f", v);
  return String(b);
}

// ---------------------------------------------------------------------------------------------
// State publishing
// ---------------------------------------------------------------------------------------------

static void publish_state() {
  const TrumaState &s = heater::state();

  pub("alive", s.cpplus_connected ? "ON" : "OFF");
  pub("lin_traffic", s.lin_traffic ? "ON" : "OFF");
  pub("release", FW_VERSION);
  pub("rssi", String(wifi::rssi()));
  pub("decoke", heater::decoke_active() ? "ON" : "OFF");
  pub("decoke_minutes", String(g_settings.decoke_minutes));
  pub("decoke_remaining", String((heater::decoke_remaining_s() + 59) / 60));
  pub("room_setpoint", fmt1(heater::room_setpoint()));
  pub("fan_mode", heater::fan_mode());
  if (s.cpplus_version[0]) pub("cpplus_version", s.cpplus_version);
  if (s.heater_version[0]) pub("heater_version", s.heater_version);

  if (s.heater_valid) {
    const auto &h = s.heater;
    float room = temp_code_to_decimal(h.current_temp_room);
    float water = temp_code_to_decimal(h.current_temp_water);
    if (!isnan(room)) pub("current_temp_room", fmt1(room));
    if (!isnan(water)) pub("current_temp_water", fmt1(water));
    pub("target_temp_room", String((int) lroundf(temp_code_to_decimal(h.target_temp_room, 0))));
    pub("target_temp_water", String((int) lroundf(temp_code_to_decimal(h.target_temp_water, 0))));
    pub("water_mode", water_mode_to_str(h.target_temp_water));
    pub("heating_mode", heating_mode_to_str(h.heating_mode));
    pub("energy_mix", energy_mix_to_str(h.energy_mix_a));
    pub("el_power_level", String((uint16_t) h.el_power_level_a));
#if HEATER_HAS_ELECTRIC
    if (h.energy_mix_a == EnergyMix::MIX || h.energy_mix_a == EnergyMix::ELECTRICITY) {
      pub("energy_select", String(energy_mix_to_str(h.energy_mix_a)) + "_" + String((uint16_t) h.el_power_level_a));
    } else {
      pub("energy_select", "gas");
    }
#endif
    pub("operating_status", String((uint8_t) h.operating_status));
    pub("operating_status_text", operating_status_to_str(h.operating_status));
    pub("error_code", String(heater::error_code()));
    // Either byte marks an error or warning: E212 arrives as low 0xD4 / high 0x00, W255 as low 0xFF.
    pub("heater_error", heater::error_code() != 0 ? "ON" : "OFF");
    pub("hvac_mode", heater::room_on() ? "heat" : "off");
    pub("hvac_action", heater::hvac_action());
  }

  if (s.vent_valid) {
    pub("vent_mode", vent_mode_to_str(s.vent_raw));
    pub("vent_level", String(vent_level_from_raw(s.vent_raw)));
  }

  if (s.timer_valid) {
    const auto &t = s.timer;
    bool active = t.timer_active == TimerActive::ON;
    pub("timer_active", active ? "ON" : "OFF");
    char b[16];
    snprintf(b, sizeof(b), "%02u:%02u-%02u:%02u", t.timer_start_hours, t.timer_start_minutes, t.timer_stop_hours,
             t.timer_stop_minutes);
    pub("timer", active ? String(b) : String("off"));
    // Stored times are kept by the CP Plus while the timer is off.
    pub("timer_times", b);
    pub("timer_temp_room", String(temp_code_to_decimal(t.timer_target_temp_room, 0), 1));
    pub("timer_heating_mode", heating_mode_to_str(t.timer_heating_mode));
    pub("timer_water_mode", water_mode_to_str(t.timer_target_temp_water));
  }

  if (s.clock_valid) {
    char b[8];
    snprintf(b, sizeof(b), "%02u:%02u", s.clock.clock_hour, s.clock.clock_minute);
    pub("clock", b);
  }
}

// ---------------------------------------------------------------------------------------------
// Log forwarding: <base>/log (not retained). Only lines up to mqtt_log_level are sent.
// ---------------------------------------------------------------------------------------------

static void publish_log() {
  if (g_settings.mqtt_log_level > (uint8_t) LogLevel::DEBUG) return;
  const String t = g_settings.mqtt_base + "/log";
  char buf[160];
  LogLevel lvl;
  // A few lines per loop so a burst does not block the loop; no logging in here (would feed itself).
  for (int i = 0; i < 5 && log_get_line(g_log_seq, buf, sizeof(buf), lvl); i++, g_log_seq++) {
    if ((uint8_t) lvl > g_settings.mqtt_log_level) continue;
    g_client.publish(t.c_str(), buf, false);
  }
}

// ---------------------------------------------------------------------------------------------
// Home Assistant discovery
// ---------------------------------------------------------------------------------------------

enum Avail { AVAIL_DEVICE, AVAIL_HEATER };

static void disc_common(JsonDocument &doc, const char *object, const char *name, Avail avail) {
  doc["name"] = name;
  doc["unique_id"] = g_node_id + "_" + object;
  JsonArray av = doc["availability"].to<JsonArray>();
  JsonObject a1 = av.add<JsonObject>();
  a1["topic"] = status_topic();
  a1["payload_available"] = "online";
  a1["payload_not_available"] = "offline";
  if (avail == AVAIL_HEATER) {
    JsonObject a2 = av.add<JsonObject>();
    a2["topic"] = topic("control_status", "alive");
    a2["payload_available"] = "ON";
    a2["payload_not_available"] = "OFF";
    doc["availability_mode"] = "all";
  }
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["identifiers"].to<JsonArray>().add(g_node_id);
  dev["name"] = "Truma Heater";
  dev["manufacturer"] = "DIY";
  dev["model"] = "iNet Box emulation (LIN)";
  dev["sw_version"] = FW_VERSION;
  dev["configuration_url"] = "http://" + wifi::ip() + "/";
}

static void disc_publish(const char *component, const char *object, JsonDocument &doc) {
  String t = g_settings.ha_prefix + "/" + component + "/" + g_node_id + "/" + object + "/config";
  String payload;
  serializeJson(doc, payload);
  pub_raw(t, payload, true);
}

static void disc_sensor(const char *object, const char *name, const char *key, const char *device_class,
                        const char *unit, bool diagnostic, Avail avail, const char *icon = nullptr) {
  JsonDocument doc;
  disc_common(doc, object, name, avail);
  doc["state_topic"] = topic("control_status", key);
  if (device_class) doc["device_class"] = device_class;
  if (unit) {
    doc["unit_of_measurement"] = unit;
    doc["state_class"] = "measurement";
  }
  if (diagnostic) doc["entity_category"] = "diagnostic";
  if (icon) doc["icon"] = icon;
  disc_publish("sensor", object, doc);
}

static void disc_binary(const char *object, const char *name, const char *key, const char *device_class,
                        bool diagnostic, Avail avail, const char *icon = nullptr) {
  JsonDocument doc;
  disc_common(doc, object, name, avail);
  doc["state_topic"] = topic("control_status", key);
  doc["payload_on"] = "ON";
  doc["payload_off"] = "OFF";
  if (device_class) doc["device_class"] = device_class;
  if (diagnostic) doc["entity_category"] = "diagnostic";
  if (icon) doc["icon"] = icon;
  disc_publish("binary_sensor", object, doc);
}

static void disc_button(const char *object, const char *name, const char *key, const char *device_class,
                        const char *icon) {
  JsonDocument doc;
  disc_common(doc, object, name, AVAIL_DEVICE);
  doc["command_topic"] = topic("set", key);
  doc["payload_press"] = "1";
  doc["entity_category"] = "config";
  if (device_class) doc["device_class"] = device_class;
  if (icon) doc["icon"] = icon;
  disc_publish("button", object, doc);
}

static void publish_discovery() {
  if (!g_settings.ha_discovery) return;
  LOGI(TAG, "Publishing Home Assistant discovery");

  {  // Room thermostat
    JsonDocument doc;
    disc_common(doc, "room", "Room", AVAIL_HEATER);
    JsonArray modes = doc["modes"].to<JsonArray>();
    modes.add("off");
    modes.add("heat");
    doc["mode_command_topic"] = topic("set", "hvac_mode");
    doc["mode_state_topic"] = topic("control_status", "hvac_mode");
    doc["temperature_command_topic"] = topic("set", "room_setpoint");
    doc["temperature_state_topic"] = topic("control_status", "room_setpoint");
    doc["current_temperature_topic"] = topic("control_status", "current_temp_room");
    doc["action_topic"] = topic("control_status", "hvac_action");
    JsonArray fans = doc["fan_modes"].to<JsonArray>();
    fans.add("eco");
    fans.add("high");
#if HEATER_SUPPORTS_BOOST
    fans.add("boost");
#endif
    doc["fan_mode_command_topic"] = topic("set", "fan_mode");
    doc["fan_mode_state_topic"] = topic("control_status", "fan_mode");
    doc["min_temp"] = 5;
    doc["max_temp"] = 30;
    doc["temp_step"] = 1;
    doc["precision"] = 0.1;
    doc["temperature_unit"] = "C";
    doc["icon"] = "mdi:radiator";
    disc_publish("climate", "room", doc);
  }

  {  // Hot water
    JsonDocument doc;
    disc_common(doc, "water", "Hot water", AVAIL_HEATER);
    doc["command_topic"] = topic("set", "water_mode");
    doc["state_topic"] = topic("control_status", "water_mode");
    JsonArray opts = doc["options"].to<JsonArray>();
    opts.add("off");
    opts.add("eco");
    opts.add("high");
#if WATER_SUPPORTS_BOOST
    opts.add("boost");
#endif
    doc["icon"] = "mdi:water-boiler";
    disc_publish("select", "water", doc);
  }

#if HEATER_HAS_ELECTRIC
  {  // Energy source
    JsonDocument doc;
    disc_common(doc, "energy", "Energy source", AVAIL_HEATER);
    doc["command_topic"] = topic("set", "energy_select");
    doc["state_topic"] = topic("control_status", "energy_select");
    JsonArray opts = doc["options"].to<JsonArray>();
    for (const char *o : {"gas", "mix_900", "mix_1800", "electricity_900", "electricity_1800"}) opts.add(o);
    doc["icon"] = "mdi:lightning-bolt";
    disc_publish("select", "energy", doc);
  }
#endif

  {  // Decoking switch
    JsonDocument doc;
    disc_common(doc, "decoke", "Decoking", AVAIL_HEATER);
    doc["command_topic"] = topic("set", "decoke");
    doc["state_topic"] = topic("control_status", "decoke");
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["icon"] = "mdi:fire-alert";
    disc_publish("switch", "decoke", doc);
  }
  {  // Decoking duration
    JsonDocument doc;
    disc_common(doc, "decoke_minutes", "Decoking duration", AVAIL_DEVICE);
    doc["command_topic"] = topic("set", "decoke_minutes");
    doc["state_topic"] = topic("control_status", "decoke_minutes");
    doc["min"] = 5;
    doc["max"] = 120;
    doc["step"] = 5;
    doc["unit_of_measurement"] = "min";
    doc["entity_category"] = "config";
    doc["icon"] = "mdi:timer-cog-outline";
    disc_publish("number", "decoke_minutes", doc);
  }

  disc_sensor("current_temp_room", "Room temperature", "current_temp_room", "temperature", "°C", false, AVAIL_HEATER);
  disc_sensor("current_temp_water", "Water temperature", "current_temp_water", "temperature", "°C", false,
              AVAIL_HEATER);
  disc_sensor("operating_status", "Operating status", "operating_status", nullptr, nullptr, true, AVAIL_HEATER,
              "mdi:state-machine");
  disc_sensor("error_code", "Error code", "error_code", nullptr, nullptr, true, AVAIL_HEATER, "mdi:alert-decagram");
  // Only filled on heaters that use LIN frame 0x20 (stays unknown otherwise).
  disc_sensor("vent_level", "Ventilation level", "vent_level", nullptr, nullptr, false, AVAIL_HEATER, "mdi:fan");
  // The timer used to be a read-only sensor: remove that old entity.
  pub_raw(g_settings.ha_prefix + "/sensor/" + g_node_id + "/timer/config", "", true);
  {  // Timer: text entity, "HH:MM-HH:MM[,temp[,fan[,water]]]" or "off"
    JsonDocument doc;
    disc_common(doc, "timer", "Timer", AVAIL_HEATER);
    doc["command_topic"] = topic("set", "timer");
    doc["state_topic"] = topic("control_status", "timer");
    doc["max"] = 40;
    doc["pattern"] = "^(off|on|\\d{1,2}:\\d{2}-\\d{1,2}:\\d{2}(,[^,]*){0,3})$";
    doc["icon"] = "mdi:timer-outline";
    disc_publish("text", "timer", doc);
  }
  {  // Timer on/off with the stored times
    JsonDocument doc;
    disc_common(doc, "timer_switch", "Timer", AVAIL_HEATER);
    doc["command_topic"] = topic("set", "timer_active");
    doc["state_topic"] = topic("control_status", "timer_active");
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["icon"] = "mdi:timer-play-outline";
    disc_publish("switch", "timer_switch", doc);
  }
  disc_sensor("timer_temp_room", "Timer temperature", "timer_temp_room", "temperature", "°C", false, AVAIL_HEATER);
  disc_sensor("timer_heating_mode", "Timer fan mode", "timer_heating_mode", nullptr, nullptr, false, AVAIL_HEATER,
              "mdi:fan-clock");
  disc_sensor("decoke_remaining", "Decoking remaining", "decoke_remaining", nullptr, nullptr, false, AVAIL_HEATER,
              "mdi:timer-sand");
  disc_sensor("clock", "CP Plus clock", "clock", nullptr, nullptr, true, AVAIL_HEATER, "mdi:clock-outline");
  disc_sensor("rssi", "WiFi signal", "rssi", "signal_strength", "dBm", true, AVAIL_DEVICE);
  if (g_settings.mqtt_log_level <= (uint8_t) LogLevel::DEBUG) {
    JsonDocument doc;
    disc_common(doc, "log", "Last log message", AVAIL_DEVICE);
    doc["state_topic"] = g_settings.mqtt_base + "/log";
    doc["entity_category"] = "diagnostic";
    doc["icon"] = "mdi:text-box-outline";
    disc_publish("sensor", "log", doc);
  }

  disc_binary("alive", "CP Plus connected", "alive", "connectivity", true, AVAIL_DEVICE);
  disc_binary("lin_traffic", "LIN traffic", "lin_traffic", "connectivity", true, AVAIL_DEVICE);
  disc_binary("heater_error", "Heater error", "heater_error", "problem", false, AVAIL_HEATER);
  // Replaced by the timer switch.
  pub_raw(g_settings.ha_prefix + "/binary_sensor/" + g_node_id + "/timer_active/config", "", true);

  disc_button("sync_clock", "Sync CP Plus clock", "sync_clock", nullptr, "mdi:clock-check-outline");
  disc_button("restart", "Restart", "reboot", "restart", nullptr);
}

// ---------------------------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------------------------

static void on_message(char *t, byte *payload, unsigned int length) {
  String tp(t);
  String value;
  value.reserve(length);
  for (unsigned int i = 0; i < length; i++) value += (char) payload[i];
  value.trim();

  // HA restarted: send discovery again.
  if (tp == g_settings.ha_prefix + "/status") {
    if (value == "online") g_discovery_pending = true;
    return;
  }

  const String prefix = g_settings.mqtt_base + "/set/";
  if (!tp.startsWith(prefix)) return;
  const String key = tp.substring(prefix.length());
  LOGI(TAG, "Command %s = %s", key.c_str(), value.c_str());

  const TrumaState &s = heater::state();

  if (key == "target_temp_room") {
    heater::set_room_temp(value.toFloat());
  } else if (key == "heating_mode") {
    heater::set_fan_mode(value, true);
  } else if (key == "target_temp_water") {
    heater::set_water_temp(value.toFloat());
  } else if (key == "water_mode") {
    heater::set_water_mode(value);
  } else if (key == "energy_mix") {
    uint16_t level = s.heater_valid ? (uint16_t) s.heater.el_power_level_a : 0;
    heater::set_energy_mix(value, level);
  } else if (key == "el_power_level") {
    uint16_t level = value.toInt();
    String mix = s.heater_valid ? energy_mix_to_str(s.heater.energy_mix_a) : "gas";
    if (level > 0 && (mix == "gas" || mix == "none")) mix = "mix";
    heater::set_energy_mix(mix, level);
  } else if (key == "energy_select") {
    int sep = value.indexOf('_');
    String mix = sep > 0 ? value.substring(0, sep) : value;
    uint16_t level = sep > 0 ? value.substring(sep + 1).toInt() : 0;
    heater::set_energy_mix(mix, level);
  } else if (key == "hvac_mode") {
    heater::set_room_on(value.equalsIgnoreCase("heat"));
  } else if (key == "room_setpoint") {
    heater::set_setpoint(value.toFloat());
  } else if (key == "fan_mode") {
    heater::set_fan_mode(value, false);
  } else if (key == "decoke") {
    if (value.equalsIgnoreCase("ON") || value == "1") heater::decoke_start();
    else heater::decoke_stop(true);
  } else if (key == "decoke_minutes") {
    heater::set_decoke_minutes(value.toInt());
  } else if (key == "sync_clock") {
    heater::sync_clock();
  } else if (key == "timer") {
    heater::set_timer(value);
  } else if (key == "timer_active") {
    heater::set_timer_active(value.equalsIgnoreCase("ON") || value == "1");
  } else if (key == "sniffer") {
    heater::set_sniffer(value.equalsIgnoreCase("ON") || value == "1");
  } else if (key == "reboot") {
    g_reboot_at = millis() + 1000;
  } else {
    LOGW(TAG, "Unknown command topic %s", key.c_str());
  }
}

// ---------------------------------------------------------------------------------------------

static void connect() {
  if (g_settings.mqtt_host.isEmpty() || !wifi::connected()) return;
  if (millis() - g_last_attempt < 10000 && g_last_attempt != 0) return;
  g_last_attempt = millis();

  g_client.setServer(g_settings.mqtt_host.c_str(), g_settings.mqtt_port);
  String client_id = g_node_id;
  const char *user = g_settings.mqtt_user.isEmpty() ? nullptr : g_settings.mqtt_user.c_str();
  const char *pass = g_settings.mqtt_pass.isEmpty() ? nullptr : g_settings.mqtt_pass.c_str();
  String will = status_topic();

  LOGI(TAG, "Connecting to %s:%u", g_settings.mqtt_host.c_str(), g_settings.mqtt_port);
  if (!g_client.connect(client_id.c_str(), user, pass, will.c_str(), 1, true, "offline")) {
    LOGW(TAG, "Connect failed, state %d", g_client.state());
    return;
  }
  LOGI(TAG, "Connected");
  pub_raw(will, "online", true);
  g_client.subscribe((g_settings.mqtt_base + "/set/#").c_str());
  if (g_settings.ha_discovery) g_client.subscribe((g_settings.ha_prefix + "/status").c_str());
  g_discovery_pending = true;
  g_force = true;
  // Also forward the most recent lines (e.g. boot messages, reason for the reconnect).
  uint32_t next = log_next_seq();
  if (g_log_seq == 0 || next - g_log_seq > 20) g_log_seq = next > 20 ? next - 20 : 0;
}

void begin() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char id[24];
  snprintf(id, sizeof(id), "truma_%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  g_node_id = id;

  g_client.setBufferSize(1800);
  g_client.setKeepAlive(30);
  g_client.setSocketTimeout(3);
  g_client.setCallback(on_message);
}

void loop() {
  if (g_reboot_at != 0 && (int32_t) (millis() - g_reboot_at) > 0) {
    LOGI(TAG, "Rebooting on request");
    delay(100);
    ESP.restart();
  }

  if (!g_client.connected()) {
    connect();
    return;
  }
  g_client.loop();

  if (g_discovery_pending) {
    g_discovery_pending = false;
    publish_discovery();
    g_force = true;
  }

  const uint32_t now = millis();
  if (now - g_last_full > MQTT_PUBLISH_INTERVAL_MS) {
    g_last_full = now;
    g_force = true;
  }
  if (g_force || now - g_last_check > 1000) {
    g_last_check = now;
    publish_state();
    g_force = false;
  }
  publish_log();
}

bool connected() { return g_client.connected(); }

void request_publish() { g_force = true; }

}  // namespace mqtt
