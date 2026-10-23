#pragma once
#include <Arduino.h>

namespace wifimgr {
  void   begin();
  void   loop();
  bool   apActive();
  bool   staConnected();
  String apSsid();
  String ipString();       // STA IP if connected, otherwise AP IP
  int    rssi();
  bool   timeValid();
}
