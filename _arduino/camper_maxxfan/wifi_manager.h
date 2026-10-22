#pragma once
#include <Arduino.h>

namespace wifimgr {
  void   begin();
  void   loop();
  bool   apActive();
  bool   staConnected();
  String apSsid();
  String macSuffix();
  String ipString();
  int    rssi();
}
