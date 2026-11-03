#pragma once
#include <Arduino.h>

// Buttons of the MaxxFan keypad emulated by the VanTurtle board.
// Expander pin = port * 8 + 2 + button index.
enum FanButton : uint8_t {
  BTN_AUTO = 0,
  BTN_DIRECTION,
  BTN_ON_OFF,
  BTN_FASTER,
  BTN_SLOWER,
  BTN_BEEP,
  BTN_COUNT
};

extern const char* const BUTTON_KEYS[BTN_COUNT];   // MQTT / API keys
extern const char* const BUTTON_NAMES[BTN_COUNT];  // display names
extern const char* const PORT_LABELS[2];           // "RJ11", "RJ45"

namespace fan {
  using ChangeCb = void (*)();

  void begin();
  void loop();                       // call from main loop only (owns I2C)

  // Thread-safe requests (web server task, MQTT callback, ...)
  bool requestPress(uint8_t port, FanButton btn);
  bool requestLed(uint8_t port, bool on);
  bool requestSyncOff();
  bool requestSpeed(uint8_t port, uint8_t level);   // 1..10 via 10x slower + (level-1)x faster, 0 = close
  bool requestClose(uint8_t port);                  // close the lid: slower until past the lowest speed

  bool autoHold(uint8_t port);
  bool led(uint8_t port);
  bool expanderOk();
  bool syncRunning();

  void onChange(ChangeCb cb);        // called (from loop) when inputs/LEDs change
  FanButton buttonFromKey(const String& key);   // BTN_COUNT if unknown
}
