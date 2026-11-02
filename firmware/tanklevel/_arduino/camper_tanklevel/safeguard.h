#ifndef SAFEGUARD_H
#define SAFEGUARD_H

// Keeps a device reachable after risky changes, without anybody having to get to it:
//
// 1. Firmware rollback: after an OTA update the new firmware runs on probation. If it does
//    not reach the WiFi within GUARD_TIMEOUT_MS, or restarts more than GUARD_MAX_BOOTS
//    times before that, the device boots the previous firmware from the other app slot.
//    Only updates written by this firmware are guarded (web upload, ArduinoOTA); flashing
//    over USB is never rolled back.
// 2. WiFi trial: when SSID/password are changed while the device is connected, the working
//    values are kept. If the new ones do not connect after the next start, the wifi manager
//    puts the old ones back.
//
// State lives in its own NVS namespace ("safeguard"), independent of the device settings.
// ESP32 only; on other chips the functions do nothing.

#include <Arduino.h>

#ifndef GUARD_TIMEOUT_MS
#define GUARD_TIMEOUT_MS   300000UL   // new firmware must reach the WiFi within 5 min
#endif
#ifndef GUARD_CONFIRM_MS
#define GUARD_CONFIRM_MS   60000UL    // ... and keep it for 1 min to count as good
#endif
#ifndef GUARD_MAX_BOOTS
#define GUARD_MAX_BOOTS    3          // starts allowed on probation (crash loop protection)
#endif

namespace guard {

void begin();              // first thing in setup(): counts the start, rolls back a crash loop
void otaDone();            // a firmware image was written successfully, restart follows
void loop(bool healthy);   // healthy = connected to the WiFi (or no WiFi configured)
bool onProbation();        // new firmware not confirmed yet

// WiFi trial. Call wifiTrialStart() with the values the device is connected with right now,
// before they are overwritten; a running trial keeps its (older, known good) values.
void wifiTrialStart(const String& ssid, const String& pass, const String& extra = "");
bool wifiTrialPending();   // values were changed before this start and are being tried now
bool wifiTrialRestore(String& ssid, String& pass, String& extra);   // old values, ends the trial
void wifiTrialOk();

// Text for the web UI: probation, last rollback or restored WiFi; "" = nothing to report
String status();

} // namespace guard

#endif // SAFEGUARD_H
