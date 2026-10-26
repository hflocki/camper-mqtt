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
  // Static IP check: ip, gateway and subnet valid and the gateway inside the subnet. err = reason
  bool   staticIpValid(const String& ip, const String& gw, const String& sn, String& err);
  bool   dhcpFallback();   // static IP did not work, running with DHCP until the next restart
}
