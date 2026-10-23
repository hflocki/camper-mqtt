#pragma once
#include <Arduino.h>

// Web server: serves the page, a WebSocket for live values / commands and
// the settings API. The page shows the same pages and tiles as the panel.
namespace web {
  void begin();
  void loop();                 // dispatch queued commands, push value changes
  void configChanged();        // tell connected browsers to reload the page layout
}
