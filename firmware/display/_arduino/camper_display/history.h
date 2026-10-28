#pragma once
#include <Arduino.h>

// 24 h history of the temperature sensors and batteries for the curves on the web page.
// One sample every HISTORY_STEP_S, kept in RAM (PSRAM if there is any): lost on a restart.
namespace history {

void begin();
void loop();
// {"step":300,"last":<unix time of the newest sample or 0>,"n":<samples>,
//  "s":[{"n":"Kühlschrank","k":"t","v":[4.5,null,...]}, ...]}   k: t = °C, b = % (battery), v = V (BM2)
void write(Print& out);

}  // namespace history
