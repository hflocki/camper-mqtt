#pragma once
#include <Arduino.h>

// Small web page for firmware updates (OTA) and a status JSON.
// Runs only while the device is awake; deep sleep switches WiFi off anyway.
namespace webota {

typedef String (*StatusFn)();   // returns the status as a JSON object
typedef void (*ProgressFn)(int percent);   // called while an upload runs (loop() is blocked then)

// user empty = no login. host is used for mDNS (http://<host>.local)
void begin(const char* host, const char* version, const char* user, const char* pass,
           StatusFn status, ProgressFn progress);
void loop();         // call every loop; also starts mDNS once WiFi is up
bool busy();         // an upload is running (do not sleep, show progress)
int progress();      // 0-100 while busy, -1 otherwise
bool finished();     // upload done and ok, the device restarts in a moment

}  // namespace webota
