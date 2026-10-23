#include "settings.h"
#include "config.h"
#include <Preferences.h>

Settings settings;

static Preferences prefs;
static const char* NS = "maxxfan";

void settingsLoad() {
  prefs.begin(NS, true);
  settings.hostname   = prefs.getString("host",   DEFAULT_HOSTNAME);
  settings.wifiSsid   = prefs.getString("ssid",   "");
  settings.wifiPass   = prefs.getString("wpass",  "");
  settings.staticIp   = prefs.getBool  ("sip",    false);
  settings.ip         = prefs.getString("ip",     "");
  settings.gateway    = prefs.getString("gw",     "");
  settings.subnet     = prefs.getString("sn",     "255.255.255.0");
  settings.dns        = prefs.getString("dns",    "");
  settings.apPass     = prefs.getString("appass", DEFAULT_AP_PASSWORD);
  settings.mqttHost   = prefs.getString("mhost",  "");
  settings.mqttPort   = prefs.getUShort("mport",  DEFAULT_MQTT_PORT);
  settings.mqttUser   = prefs.getString("muser",  "");
  settings.mqttPass   = prefs.getString("mpass",  "");
  settings.mqttPrefix = prefs.getString("prefix", DEFAULT_MQTT_PREFIX);
  settings.discovery  = prefs.getBool  ("disc",   true);
  settings.webUser    = prefs.getString("wuser",  "");
  settings.webPass    = prefs.getString("wpw",    "");
  settings.portEnabled[0] = prefs.getBool("p0", DEFAULT_PORT0_ENABLED);
  settings.portEnabled[1] = prefs.getBool("p1", DEFAULT_PORT1_ENABLED);
  settings.pulseMs    = prefs.getUShort("pulse",  DEFAULT_PULSE_MS);
  settings.syncPort   = prefs.getUChar ("sport",  DEFAULT_SYNC_PORT);
  prefs.end();

  // Sanity checks
  if (settings.hostname.isEmpty())   settings.hostname = DEFAULT_HOSTNAME;
  if (settings.mqttPrefix.isEmpty()) settings.mqttPrefix = DEFAULT_MQTT_PREFIX;
  while (settings.mqttPrefix.endsWith("/")) settings.mqttPrefix.remove(settings.mqttPrefix.length() - 1);
  if (settings.apPass.length() < 8)  settings.apPass = DEFAULT_AP_PASSWORD;
  if (settings.pulseMs < 50 || settings.pulseMs > 2000) settings.pulseMs = DEFAULT_PULSE_MS;
  if (settings.syncPort > 1) settings.syncPort = DEFAULT_SYNC_PORT;
}

void settingsSave() {
  prefs.begin(NS, false);
  prefs.putString("host",   settings.hostname);
  prefs.putString("ssid",   settings.wifiSsid);
  prefs.putString("wpass",  settings.wifiPass);
  prefs.putBool  ("sip",    settings.staticIp);
  prefs.putString("ip",     settings.ip);
  prefs.putString("gw",     settings.gateway);
  prefs.putString("sn",     settings.subnet);
  prefs.putString("dns",    settings.dns);
  prefs.putString("appass", settings.apPass);
  prefs.putString("mhost",  settings.mqttHost);
  prefs.putUShort("mport",  settings.mqttPort);
  prefs.putString("muser",  settings.mqttUser);
  prefs.putString("mpass",  settings.mqttPass);
  prefs.putString("prefix", settings.mqttPrefix);
  prefs.putBool  ("disc",   settings.discovery);
  prefs.putString("wuser",  settings.webUser);
  prefs.putString("wpw",    settings.webPass);
  prefs.putBool  ("p0",     settings.portEnabled[0]);
  prefs.putBool  ("p1",     settings.portEnabled[1]);
  prefs.putUShort("pulse",  settings.pulseMs);
  prefs.putUChar ("sport",  settings.syncPort);
  prefs.end();
}

void settingsFactoryReset() {
  prefs.begin(NS, false);
  prefs.clear();
  prefs.end();
}
