#pragma once
#include <Arduino.h>

// Schedules a restart from the main loop (safe to call from any task).
void requestReboot(uint32_t delayMs = 800);
