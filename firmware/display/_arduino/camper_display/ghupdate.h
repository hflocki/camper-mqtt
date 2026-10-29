#pragma once
#include <Arduino.h>

// Firmware update straight from the GitHub releases of the repo (tag "display-v<version>", asset *.bin).
// Runs in its own task; the web page polls json() for the state.
namespace ghupdate {

void begin();
void check();      // look for the newest release
void install();    // download and flash the release found by check(), then restart
String json();     // {"state":"idle|checking|none|available|downloading|done|error","cur":"..","latest":"..","pct":0,"msg":".."}

// Progress of any firmware update (GitHub or file upload) for the update screen on the panel
void setProgress(int pct, const char* source);   // pct -1 = no update running
int progress();                                  // -1 = none
const char* progressSource();                    // "GitHub" / "Datei"

}  // namespace ghupdate
