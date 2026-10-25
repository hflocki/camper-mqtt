#pragma once
#include <Arduino.h>

// Push notifications via Pushover (https://pushover.net) for the warnings shown on the
// overview (temperature alarms, heater errors). The HTTPS request runs in its own task,
// so the display never stalls while a message is sent.
namespace notify {
  void begin();
  void loop();                 // compares the current warnings with the ones already sent
  void requestTest();          // send a test message (from the web page)
  String status();             // last result for the web page, e.g. "OK 12:34" or "Fehler 400"
}
