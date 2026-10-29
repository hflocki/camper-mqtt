#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Central value store. Everything shown on the display and on the web page
// comes from here; MQTT writes into it, the UI and the WebSocket read from it.
// Only touched from the Arduino loop task.

enum Dev : uint8_t { DEV_TANK, DEV_LEVEL, DEV_HEATER, DEV_FAN, DEV_TEMP, DEV_BATTERY, DEV_COUNT };

// Fixed datapoint ids (index into the table in datastore.cpp)
enum DpId : uint8_t {
  // tank firmware
  DP_TANK_STATUS, DP_WATER_PCT, DP_WATER_L, DP_GREY_PCT, DP_GREY_L, DP_GAS_PCT, DP_GAS_L, DP_GAS_BAT,
  // level sensor
  DP_LVL_STATUS, DP_LVL_PITCH, DP_LVL_ROLL, DP_LVL_OK, DP_LVL_TOL, DP_LVL_FL, DP_LVL_FR, DP_LVL_RL, DP_LVL_RR,
  // heater
  DP_HEAT_STATUS, DP_HEAT_ALIVE, DP_HEAT_ROOM, DP_HEAT_SETPOINT, DP_HEAT_HVAC, DP_HEAT_ACTION,
  DP_HEAT_FANMODE, DP_HEAT_WATERMODE, DP_HEAT_WATER, DP_HEAT_OPSTATUS, DP_HEAT_ERRCODE, DP_HEAT_ERROR,
  DP_HEAT_DECOKE, DP_HEAT_DECOKE_REM,
  // maxxfan
  DP_FAN_STATUS, DP_FAN_HOLD, DP_FAN_LED,
  // camper-temp: JSON list of all sensors + readable alarm text
  DP_TEMP_STATUS, DP_TEMP_LIST, DP_TEMP_ALARM,
  // camper-battery: JSON list of all batteries
  DP_BAT_STATUS, DP_BAT_LIST,
  DP_COUNT
};

namespace store {
  void begin();
  void rebuildTopics();                    // after prefix / tile changes

  // MQTT input. Returns true if the topic belonged to a datapoint or tile.
  bool onMessage(const char* topic, const char* payload, unsigned int len);

  const String& get(DpId id);
  float         getf(DpId id, float def = NAN);
  bool          has(DpId id);
  bool          devOnline(Dev d);          // device status topic says "online"

  size_t        tileCount();
  const String& tile(size_t i);
  bool          tileHas(size_t i);
  bool          tileOn(size_t i);          // for switch tiles
  uint32_t      tileAgeMs(size_t i);       // since the last message for this tile (or since start)
  const String& tileRaw(size_t i);         // value as received, before the tile's text mapping

  const char*   dpKey(DpId id);            // short key used on the web page

  // full topic for a datapoint (empty if disabled)
  const String& dpTopic(DpId id);
  const char*   devPrefix(Dev d);

  // Change tracking: two independent consumers (display, web)
  enum Consumer : uint8_t { C_UI = 0, C_WEB = 1 };
  bool takeDirty(Consumer c, bool& anyDp, bool& anyTile);
  bool dpDirty(Consumer c, DpId id);       // read without clearing
  bool tileDirty(Consumer c, size_t i);
  void clearDirty(Consumer c);
  void markAllDirty();

  // JSON snapshot / delta for the web page
  void fillJson(JsonObject vals, bool onlyDirty);
}
