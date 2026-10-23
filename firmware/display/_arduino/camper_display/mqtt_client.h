#pragma once
#include <Arduino.h>
#include <vector>

namespace mqttc {
  void begin();
  void loop();
  bool connected();
  void resubscribe();            // after tile / prefix changes
  void publishSelfState();

  // Topic finder (helps to find zigbee2mqtt and other topics)
  struct Found { String topic; String payload; };
  void finderStart(const String& filter);
  void finderStop();
  bool finderActive();
  const String& finderFilter();
  std::vector<Found> finderTake();       // new / changed topics since the last call
  std::vector<Found> finderAll();
}

// Commands shared by the touch UI and the web page. Only known commands are
// accepted; everything else is rejected (returns false).
namespace cmd {
  bool heater(const String& key, const String& value);
  bool fan(const String& key, const String& value = "");
  bool level(const String& key);
  bool tile(size_t index, const String& action);   // on / off / toggle / press / <number>
}
