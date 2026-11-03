#pragma once
#include <Arduino.h>

namespace mqttha {
  void begin();
  void loop();
  bool connected();
  void markDirty();   // publish state on next loop
}
