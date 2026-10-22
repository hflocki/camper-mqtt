#pragma once
#include <Arduino.h>

// LVGL user interface on the panel
namespace ui {
  void begin();
  void loop();                          // refresh values, header, pending commands
  void requestRebuild();                // pages / tiles / builtin pages changed
  void showPageByName(const String& nameOrIndex);
}
