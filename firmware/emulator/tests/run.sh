#!/usr/bin/env bash
# Host test of the emulator logic (emu.cpp) - no ESP32 needed, only g++ and ArduinoJson.
#   ./run.sh        checks
#   ./run.sh -v     checks plus all payloads of every emulated device
# ArduinoJson: the library folder of the Arduino IDE is searched, or set ARDUINOJSON=<path to .../ArduinoJson/src>
set -euo pipefail
cd "$(dirname "$0")"
SRC=../camper_emulator
if [[ -z "${ARDUINOJSON:-}" ]]; then
  for d in ~/Documents/Arduino/libraries/ArduinoJson/src ~/Arduino/libraries/ArduinoJson/src; do
    [[ -f "$d/ArduinoJson.h" ]] && ARDUINOJSON=$d && break
  done
fi
[[ -n "${ARDUINOJSON:-}" ]] || { echo "ArduinoJson not found, set ARDUINOJSON=<.../ArduinoJson/src>"; exit 1; }
mkdir -p build
${CXX:-g++} -std=gnu++17 -Wall -DEMU_HOST_TEST -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
  -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 \
  -I . -I "$ARDUINOJSON" -I "$SRC" -o build/test_emu test_emu.cpp "$SRC/emu.cpp"
./build/test_emu "$@"
