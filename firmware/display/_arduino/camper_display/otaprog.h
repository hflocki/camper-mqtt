#pragma once

// Progress of a firmware upload over the web page, for the update screen on the panel.
// Written by the web server task, read by the UI in the main loop.
namespace otaprog {

void set(int pct);   // 0..100, -1 = no update running
int get();           // -1 = none

}  // namespace otaprog
