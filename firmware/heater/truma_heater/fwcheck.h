// Shared source – edit only in firmware/_common, then run tools/common/sync_common.py
//
// Safe firmware upload over the web UI. Every firmware with this module carries a marker
//   CAMPER-FW:<device>:<version>:<chip>;     e.g. CAMPER-FW:maxxfan:1.7.0:esp32c3;
// (FW_DEVICE and FW_VERSION from config.h). An upload is checked while it streams in and
// rejected before anything is activated:
//   - not an ESP32 app image, or built for another chip (ESP32-S3 file on a C3 ...)
//   - a .merged.bin (full flash image for USB, with bootloader and partition table)
//   - larger than the update partition
//   - marker of another device, or no marker at all (unless forced: older firmware)
// Errors of the update itself are passed on as text. Results go to the event log (evlog).
// The web page does the same checks before the upload (see fwInspect() in web/index.html).
#ifndef FWCHECK_H
#define FWCHECK_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace fwcheck {

const char* id();                    // the marker of the running firmware
void info(JsonObject o);             // {"dev","ver","chip","chip_id","max"} for the web UI

// Firmware upload: begin() on the first chunk (total = request->contentLength()),
// chunk() for every chunk including the last, then ok() / error() in the request handler.
void begin(size_t total, bool force);
void chunk(const uint8_t* data, size_t len, bool final);
bool ok();
const String& error();               // German text, "" if ok

} // namespace fwcheck

#endif // FWCHECK_H
