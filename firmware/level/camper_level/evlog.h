// Shared source – edit only in firmware/_common, then run tools/common/sync_common.py
//
// Small event log for the web UI: the last EVLOG_SIZE events (start, WiFi, MQTT, updates,
// rollback ...) with the time since that start. Kept in RTC memory, so it survives a
// restart, a crash and a firmware update, but not a power cut. On the ESP8266 it is
// plain RAM and starts empty after every restart.
#ifndef EVLOG_H
#define EVLOG_H

#include <Arduino.h>
#include <ArduinoJson.h>

#ifndef EVLOG_SIZE
#if defined(ESP8266)
#define EVLOG_SIZE 16
#else
#define EVLOG_SIZE 24
#endif
#endif

namespace evlog {

void begin();                          // first thing in setup(): counts the start
void add(const char* fmt, ...);        // printf style, German text for the web UI (max. 70 chars)
void json(JsonArray out);              // newest first: {"b": starts ago (0 = this one), "t": s after that start, "m": text}
const char* resetReason();             // reason of the last reset in German

// Logs changes of a connection state (call every loop): "WLAN verbunden (192.168.8.31)" ...
void watch(bool wifi, bool mqtt);

} // namespace evlog

#endif // EVLOG_H
