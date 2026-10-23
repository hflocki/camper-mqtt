#include "settings.h"

#include <Preferences.h>

#include "config.h"

Settings g_settings;

static const char *const NS = "truma";

void settings_load() {
  Preferences p;
  p.begin(NS, true);
  g_settings.wifi_ssid = p.getString("ssid", DEFAULT_WIFI_SSID);
  g_settings.wifi_pass = p.getString("wpass", DEFAULT_WIFI_PASSWORD);
  g_settings.hostname = p.getString("host", DEFAULT_HOSTNAME);
  g_settings.ap_pass = p.getString("appass", DEFAULT_AP_PASSWORD);
  g_settings.ap_ssid = p.getString("apssid", "");
  g_settings.ap_always = p.getBool("apalways", false);
  g_settings.wifi_tx_power = p.getUChar("txpwr", 0);
  g_settings.static_ip = p.getString("ip", "");
  g_settings.gateway = p.getString("gw", "");
  g_settings.subnet = p.getString("mask", "");
  g_settings.dns = p.getString("dns", "");

  g_settings.mqtt_host = p.getString("mhost", DEFAULT_MQTT_SERVER);
  g_settings.mqtt_port = p.getUShort("mport", DEFAULT_MQTT_PORT);
  g_settings.mqtt_user = p.getString("muser", DEFAULT_MQTT_USER);
  g_settings.mqtt_pass = p.getString("mpass", DEFAULT_MQTT_PASSWORD);
  g_settings.mqtt_base = p.getString("mbase", DEFAULT_MQTT_BASE);
  g_settings.ha_discovery = p.getBool("hadisc", true);
  g_settings.ha_prefix = p.getString("hapre", DEFAULT_HA_PREFIX);

  g_settings.ntp_server = p.getString("ntp", DEFAULT_NTP_SERVER);
  g_settings.tz = p.getString("tz", DEFAULT_TZ);

  g_settings.web_user = p.getString("wuser", DEFAULT_WEB_USER);
  g_settings.web_pass = p.getString("wwpass", DEFAULT_WEB_PASSWORD);

  g_settings.decoke_minutes = p.getUShort("decmin", DECOKE_DEFAULT_MINUTES);
  g_settings.room_setpoint = p.getFloat("setp", 20.0f);
  g_settings.fan_mode = p.getString("fan", "eco");
  g_settings.log_level = p.getUChar("loglvl", 2);
  g_settings.mqtt_log_level = p.getUChar("mloglvl", 1);
  p.end();

  if (g_settings.hostname.isEmpty()) g_settings.hostname = DEFAULT_HOSTNAME;
  if (g_settings.ap_pass.length() < 8) g_settings.ap_pass = DEFAULT_AP_PASSWORD;
  if (g_settings.mqtt_base.isEmpty()) g_settings.mqtt_base = DEFAULT_MQTT_BASE;
  if (g_settings.mqtt_base.endsWith("/")) g_settings.mqtt_base.remove(g_settings.mqtt_base.length() - 1);
  if (g_settings.ha_prefix.isEmpty()) g_settings.ha_prefix = DEFAULT_HA_PREFIX;
  if (g_settings.room_setpoint < 5 || g_settings.room_setpoint > 30) g_settings.room_setpoint = 20.0f;
  if (g_settings.fan_mode != "eco" && g_settings.fan_mode != "high" && g_settings.fan_mode != "boost")
    g_settings.fan_mode = "eco";
  if (g_settings.decoke_minutes < 1 || g_settings.decoke_minutes > 240) g_settings.decoke_minutes = DECOKE_DEFAULT_MINUTES;
}

void settings_save() {
  Preferences p;
  p.begin(NS, false);
  p.putString("ssid", g_settings.wifi_ssid);
  p.putString("wpass", g_settings.wifi_pass);
  p.putString("host", g_settings.hostname);
  p.putString("appass", g_settings.ap_pass);
  p.putString("apssid", g_settings.ap_ssid);
  p.putBool("apalways", g_settings.ap_always);
  p.putUChar("txpwr", g_settings.wifi_tx_power);
  p.putString("ip", g_settings.static_ip);
  p.putString("gw", g_settings.gateway);
  p.putString("mask", g_settings.subnet);
  p.putString("dns", g_settings.dns);
  p.putString("mhost", g_settings.mqtt_host);
  p.putUShort("mport", g_settings.mqtt_port);
  p.putString("muser", g_settings.mqtt_user);
  p.putString("mpass", g_settings.mqtt_pass);
  p.putString("mbase", g_settings.mqtt_base);
  p.putBool("hadisc", g_settings.ha_discovery);
  p.putString("hapre", g_settings.ha_prefix);
  p.putString("ntp", g_settings.ntp_server);
  p.putString("tz", g_settings.tz);
  p.putString("wuser", g_settings.web_user);
  p.putString("wwpass", g_settings.web_pass);
  p.putUChar("loglvl", g_settings.log_level);
  p.putUChar("mloglvl", g_settings.mqtt_log_level);
  p.end();
  settings_save_heater();
}

void settings_save_heater() {
  Preferences p;
  p.begin(NS, false);
  p.putUShort("decmin", g_settings.decoke_minutes);
  p.putFloat("setp", g_settings.room_setpoint);
  p.putString("fan", g_settings.fan_mode);
  p.end();
}
