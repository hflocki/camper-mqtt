// Version and device id for the firmware marker and the shared modules (fwcheck, safeguard).
// A separate file because config.h holds the personal data and may only be included once.
#pragma once
#define FW_VERSION "1.3.1"
#define FW_DEVICE  "roadbuddy"
// Awake only briefly: a new firmware counts as good after 15 s with WiFi
#define GUARD_CONFIRM_MS 15000UL
