#include "webserver.h"

#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>

#include "config.h"
#include "heater_control.h"
#include "heater_model.h"
#include "logger.h"
#include "mqtt_ha.h"
#include "settings.h"
#include "web_ui.h"
#include "wifi_manager.h"

using namespace truma;

namespace web {

static const char *const TAG = "web";

static AsyncWebServer g_server(80);

// Handlers run in the async_tcp task. Everything that touches heater/settings state is handed over to the
// Arduino loop through this queue, and the state JSON is rendered in the loop and only copied here.
struct WebCommand {
  char action[20];
  char value[32];
};
static QueueHandle_t g_cmd_queue = nullptr;
static SemaphoreHandle_t g_json_mutex = nullptr;
static String g_state_json = "{}";
static uint32_t g_last_render = 0;
static volatile uint32_t g_reboot_at = 0;
static bool g_ota_ok = false;

static bool check_auth(AsyncWebServerRequest *req) {
  if (g_settings.web_user.isEmpty()) return true;
  if (req->authenticate(g_settings.web_user.c_str(), g_settings.web_pass.c_str())) return true;
  req->requestAuthentication();
  return false;
}

static void render_state() {
  const TrumaState &s = heater::state();
  JsonDocument doc;

  JsonObject sys = doc["system"].to<JsonObject>();
  sys["fw"] = FW_VERSION;
  sys["ip"] = wifi::ip();
  sys["rssi"] = wifi::rssi();
  sys["ap"] = wifi::ap_active();
  sys["wifi"] = wifi::connected();
  sys["mqtt"] = mqtt::connected();
  sys["uptime"] = millis() / 1000;
  sys["heap"] = ESP.getFreeHeap();
  sys["wifi_state"] = wifi::state_text();
  sys["wifi_drops"] = wifi::drops();
  sys["wifi_reason"] = wifi::last_reason();
  sys["reset_reason"] = wifi::reset_reason();
  sys["ssid"] = g_settings.wifi_ssid;

  JsonObject lin = doc["lin"].to<JsonObject>();
  lin["traffic"] = s.lin_traffic;
  lin["cpplus"] = s.cpplus_connected;
  lin["init"] = s.init_received;
  lin["fault"] = s.lin_fault;
  lin["frames"] = s.frames_ok;
  lin["crc_errors"] = s.crc_errors;
  lin["sniffer"] = heater::sniffer();
  lin["diag"] = heater::lin_diag();
  lin["raw"] = heater::raw_dump();
  lin["ack_errors"] = s.ack_errors;
  lin["pending"] = s.update_pending;
  lin["cpplus_version"] = s.cpplus_version;
  lin["heater_version"] = s.heater_version;

  JsonObject h = doc["heater"].to<JsonObject>();
  h["valid"] = s.heater_valid;
  if (s.heater_valid) {
    float room = temp_code_to_decimal(s.heater.current_temp_room);
    float water = temp_code_to_decimal(s.heater.current_temp_water);
    if (!isnan(room)) h["room"] = roundf(room * 10) / 10;
    if (!isnan(water)) h["water"] = roundf(water * 10) / 10;
    h["target_room"] = lroundf(temp_code_to_decimal(s.heater.target_temp_room, 0));
    h["water_mode"] = water_mode_to_str(s.heater.target_temp_water);
    h["heating_mode"] = heating_mode_to_str(s.heater.heating_mode);
    h["energy_mix"] = energy_mix_to_str(s.heater.energy_mix_a);
    const EnergyMix mx = s.heater.energy_mix_a;
    h["energy"] = (mx == EnergyMix::MIX || mx == EnergyMix::ELECTRICITY)
                      ? String(energy_mix_to_str(mx)) + "_" + String((uint16_t) s.heater.el_power_level_a)
                      : String("gas");
    h["status"] = (uint8_t) s.heater.operating_status;
    h["status_text"] = operating_status_to_str(s.heater.operating_status);
    h["error"] = heater::error_code();
    h["on"] = heater::room_on();
    h["action"] = heater::hvac_action();
  }
  if (s.vent_valid) h["vent"] = vent_level_from_raw(s.vent_raw);
  h["setpoint"] = heater::room_setpoint();
  h["fan"] = heater::fan_mode();

  // Active heater model: the page only shows what this model offers.
  const heater::ModelInfo &mi = heater::model();
  JsonObject m = doc["model"].to<JsonObject>();
  m["key"] = mi.key;
  m["water"] = mi.water;
  m["water_boost"] = mi.water_boost;
  m["electric"] = mi.electric;
  m["diesel"] = mi.diesel;
  JsonArray fans = m["fans"].to<JsonArray>();
  const char *fl[4];
  int n = heater::fan_modes(fl, 4);
  for (int i = 0; i < n; i++) fans.add(fl[i]);
  JsonArray list = m["list"].to<JsonArray>();
  for (size_t i = 0; i < heater::MODEL_COUNT; i++) {
    JsonObject o = list.add<JsonObject>();
    o["k"] = heater::MODELS[i].key;
    o["n"] = heater::MODELS[i].name;
  }
  const char *det = heater::detected_name(s.heater_device);
  if (det) m["detected"] = det;
  const char *mm = heater::model_mismatch(s.heater_device);
  if (mm) m["mismatch"] = mm;

  JsonObject dc = doc["decoke"].to<JsonObject>();
  dc["active"] = heater::decoke_active();
  dc["remaining"] = heater::decoke_remaining_s();
  dc["minutes"] = g_settings.decoke_minutes;

  if (s.timer_valid) {
    char b[16];
    snprintf(b, sizeof(b), "%02u:%02u-%02u:%02u", s.timer.timer_start_hours, s.timer.timer_start_minutes,
             s.timer.timer_stop_hours, s.timer.timer_stop_minutes);
    doc["timer"] = s.timer.timer_active == TimerActive::ON ? b : "off";
    JsonObject t = doc["timer_cfg"].to<JsonObject>();
    char hm[6];
    t["active"] = s.timer.timer_active == TimerActive::ON;
    snprintf(hm, sizeof(hm), "%02u:%02u", s.timer.timer_start_hours, s.timer.timer_start_minutes);
    t["start"] = hm;
    snprintf(hm, sizeof(hm), "%02u:%02u", s.timer.timer_stop_hours, s.timer.timer_stop_minutes);
    t["stop"] = hm;
    t["temp"] = temp_code_to_decimal(s.timer.timer_target_temp_room, 0);
    t["fan"] = heating_mode_to_str(s.timer.timer_heating_mode);
    t["water"] = water_mode_to_str(s.timer.timer_target_temp_water);
  }
  if (s.clock_valid) {
    char b[8];
    snprintf(b, sizeof(b), "%02u:%02u", s.clock.clock_hour, s.clock.clock_minute);
    doc["cpplus_clock"] = b;
  }

  String out;
  serializeJson(doc, out);
  xSemaphoreTake(g_json_mutex, portMAX_DELAY);
  g_state_json = out;
  xSemaphoreGive(g_json_mutex);
}

static void handle_command(const WebCommand &c) {
  const String a(c.action);
  const String v(c.value);
  LOGI(TAG, "Command %s = %s", c.action, c.value);
  if (a == "room_on") heater::set_room_on(v == "1");
  else if (a == "setpoint") heater::set_setpoint(v.toFloat());
  else if (a == "fan") heater::set_fan_mode(v, false);
  else if (a == "water") heater::set_water_mode(v);
  else if (a == "energy") {
    int sep = v.indexOf('_');
    heater::set_energy_mix(sep > 0 ? v.substring(0, sep) : v, sep > 0 ? v.substring(sep + 1).toInt() : 0);
  } else if (a == "model") heater::set_model(v);
  else if (a == "decoke") v == "1" ? (void) heater::decoke_start() : heater::decoke_stop(true);
  else if (a == "decoke_minutes") heater::set_decoke_minutes(v.toInt());
  else if (a == "sync_clock") heater::sync_clock();
  else if (a == "timer") heater::set_timer(v);
  else if (a == "sniffer") heater::set_sniffer(v == "1");
  else if (a == "rawdump") heater::set_raw_dump(v == "1");
  else if (a == "log_level") {
    uint8_t lvl = constrain(v.toInt(), 0, 3);
    g_settings.log_level = lvl;
    log_set_level((LogLevel) lvl);
    Preferences p;
    p.begin("truma", false);
    p.putUChar("loglvl", lvl);
    p.end();
  }
  mqtt::request_publish();
  g_last_render = 0;
}

static void config_json(AsyncWebServerRequest *req) {
  if (!check_auth(req)) return;
  JsonDocument doc;
  doc["ssid"] = g_settings.wifi_ssid;
  doc["ap_ssid"] = wifi::ap_ssid();
  doc["ap_always"] = g_settings.ap_always;
  doc["wifi_tx_power"] = wifi::tx_power();
  doc["hostname"] = g_settings.hostname;
  doc["static_ip"] = g_settings.static_ip;
  doc["gateway"] = g_settings.gateway;
  doc["subnet"] = g_settings.subnet;
  doc["dns"] = g_settings.dns;
  doc["mqtt_host"] = g_settings.mqtt_host;
  doc["mqtt_port"] = g_settings.mqtt_port;
  doc["mqtt_user"] = g_settings.mqtt_user;
  doc["mqtt_base"] = g_settings.mqtt_base;
  doc["ha_discovery"] = g_settings.ha_discovery;
  doc["ha_prefix"] = g_settings.ha_prefix;
  doc["ntp_server"] = g_settings.ntp_server;
  doc["tz"] = g_settings.tz;
  doc["web_user"] = g_settings.web_user;
  doc["log_level"] = (uint8_t) log_get_level();
  doc["mqtt_log_level"] = g_settings.mqtt_log_level;
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// Writes the submitted settings directly to NVS and reboots. Empty password fields keep the stored value.
static void config_save(AsyncWebServerRequest *req) {
  if (!check_auth(req)) return;
  Preferences p;
  p.begin("truma", false);
  auto put = [&](const char *param, const char *key) {
    if (req->hasParam(param, true)) p.putString(key, req->getParam(param, true)->value());
  };
  auto put_secret = [&](const char *param, const char *key) {
    if (req->hasParam(param, true) && !req->getParam(param, true)->value().isEmpty())
      p.putString(key, req->getParam(param, true)->value());
  };
  put("ssid", "ssid");
  put_secret("wifi_pass", "wpass");
  put("hostname", "host");
  put_secret("ap_pass", "appass");
  put("static_ip", "ip");
  put("gateway", "gw");
  put("subnet", "mask");
  put("dns", "dns");
  put("mqtt_host", "mhost");
  if (req->hasParam("mqtt_port", true)) p.putUShort("mport", req->getParam("mqtt_port", true)->value().toInt());
  put("mqtt_user", "muser");
  put_secret("mqtt_pass", "mpass");
  put("mqtt_base", "mbase");
  auto put_bool = [&](const char *param, const char *key) {
    if (req->hasParam(param, true)) p.putBool(key, req->getParam(param, true)->value() == "1");
  };
  put_bool("ha_discovery", "hadisc");
  put_bool("ap_always", "apalways");
  put("ap_ssid", "apssid");
  if (req->hasParam("wifi_tx_power", true))
    p.putUChar("txpwr", constrain(req->getParam("wifi_tx_power", true)->value().toInt(), 8, 84));
  put("ha_prefix", "hapre");
  put("ntp_server", "ntp");
  put("tz", "tz");
  put("web_user", "wuser");
  if (req->hasParam("mqtt_log_level", true))
    p.putUChar("mloglvl", constrain(req->getParam("mqtt_log_level", true)->value().toInt(), 0, 4));
  put_secret("web_pass", "wwpass");
  auto checked = [&](const char *param) {
    return req->hasParam(param, true) && req->getParam(param, true)->value() == "1";
  };
  if (checked("clear_mqtt_pass")) p.putString("mpass", "");
  if (checked("clear_web_pass")) {
    p.putString("wuser", "");
    p.putString("wwpass", "");
  }
  p.end();
  req->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
  g_reboot_at = millis() + 1500;
}

void begin() {
  g_cmd_queue = xQueueCreate(8, sizeof(WebCommand));
  g_json_mutex = xSemaphoreCreateMutex();

  g_server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!check_auth(req)) return;
    req->send(200, "text/html", (const uint8_t *) INDEX_HTML, strlen(INDEX_HTML));
  });

  g_server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!check_auth(req)) return;
    xSemaphoreTake(g_json_mutex, portMAX_DELAY);
    String copy = g_state_json;
    xSemaphoreGive(g_json_mutex);
    req->send(200, "application/json", copy);
  });

  g_server.on("/api/cmd", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!check_auth(req)) return;
    if (!req->hasParam("a", true)) {
      req->send(400, "application/json", "{\"ok\":false}");
      return;
    }
    WebCommand c = {};
    strlcpy(c.action, req->getParam("a", true)->value().c_str(), sizeof(c.action));
    if (req->hasParam("v", true)) strlcpy(c.value, req->getParam("v", true)->value().c_str(), sizeof(c.value));
    bool ok = xQueueSend(g_cmd_queue, &c, 0) == pdTRUE;
    req->send(ok ? 200 : 503, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  g_server.on("/api/config", HTTP_GET, config_json);
  g_server.on("/api/config", HTTP_POST, config_save);

  // Incremental log: /api/log?since=N returns {"next":M,"lines":"..."} with all lines >= N.
  g_server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!check_auth(req)) return;
    uint32_t since = req->hasParam("since") ? strtoul(req->getParam("since")->value().c_str(), nullptr, 10) : 0;
    String lines;
    lines.reserve(2048);
    uint32_t next = log_read_since(since, lines);
    JsonDocument doc;
    doc["next"] = next;
    doc["lines"] = lines;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  g_server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!check_auth(req)) return;
    req->send(200, "application/json", "{\"ok\":true}");
    g_reboot_at = millis() + 1000;
  });

  // Firmware update (.bin from PlatformIO: .pio/build/<env>/firmware.bin)
  g_server.on(
      "/update", HTTP_POST,
      [](AsyncWebServerRequest *req) {
        if (!check_auth(req)) return;
        req->send(g_ota_ok ? 200 : 500, "application/json", g_ota_ok ? "{\"ok\":true}" : "{\"ok\":false}");
        if (g_ota_ok) g_reboot_at = millis() + 1500;
      },
      [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!g_settings.web_user.isEmpty() &&
            !req->authenticate(g_settings.web_user.c_str(), g_settings.web_pass.c_str())) {
          return;
        }
        if (index == 0) {
          LOGI(TAG, "OTA update started: %s", filename.c_str());
          g_ota_ok = false;
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            LOGE(TAG, "OTA begin failed: %s", Update.errorString());
            return;
          }
        }
        if (Update.isRunning() && Update.write(data, len) != len) {
          LOGE(TAG, "OTA write failed: %s", Update.errorString());
        }
        if (final) {
          g_ota_ok = Update.end(true);
          if (g_ota_ok) LOGI(TAG, "OTA update finished (%u bytes)", (unsigned) (index + len));
          else LOGE(TAG, "OTA end failed: %s", Update.errorString());
        }
      });

  g_server.onNotFound([](AsyncWebServerRequest *req) {
    // Captive portal: send every unknown request to the UI while the setup AP is active.
    if (wifi::ap_active()) {
      req->redirect("http://" + WiFi.softAPIP().toString() + "/");
      return;
    }
    req->send(404, "text/plain", "Not found");
  });

  g_server.begin();
  LOGI(TAG, "Web server started");
}

void loop() {
  WebCommand c;
  while (xQueueReceive(g_cmd_queue, &c, 0) == pdTRUE) handle_command(c);

  if (millis() - g_last_render > 1000 || g_last_render == 0) {
    g_last_render = millis();
    render_state();
  }

  if (g_reboot_at != 0 && (int32_t) (millis() - g_reboot_at) > 0) {
    LOGI(TAG, "Rebooting");
    delay(200);
    ESP.restart();
  }
}

}  // namespace web
