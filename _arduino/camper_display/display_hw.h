#pragma once
#include <Arduino.h>

// Panel, backlight, GT911 touch and the LVGL driver glue.
namespace disp {
  bool begin();
  void loop();                    // runs LVGL timers + screen timeout
  void setBrightness(uint8_t pct);
  void wake();
  void sleep();                   // dim / off according to settings
  bool isAwake();
  bool touchOk();
}
