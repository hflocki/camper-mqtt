#!/usr/bin/env bash
# Host-side tests for the LIN / iNet Box protocol code (no ESP32 needed).
#   1. protocol_test: synthetic CP Plus session (identify, assign NAD, init, status, command)
#   2. replay:        real CP Plus / Combi D6 E captures (WomoLIN project) fed through our parser,
#                     our answers compared with the original iNet Box answers
set -euo pipefail
cd "$(dirname "$0")"
SRC=../truma_heater
OUT=build
mkdir -p "$OUT"
CXX=${CXX:-g++}
$CXX -std=gnu++17 -Wall -I stub -I "$SRC" -o "$OUT/protocol_test" protocol_test.cpp "$SRC/inetbox.cpp"
$CXX -std=gnu++17 -Wall -I stub -I "$SRC" -o "$OUT/replay" replay.cpp "$SRC/inetbox.cpp"

for f in captures/*.log; do
  python3 log2replay.py "$f" "$OUT/$(basename "$f" .log).txt" > /dev/null
done
mkdir -p "$OUT/community"
for f in captures/community/*.log; do
  python3 log2replay.py "$f" "$OUT/community/$(basename "$f" .log).txt" > /dev/null
done

echo "== protocol_test"
"$OUT/protocol_test" | tail -2

echo "== replay: init with Combi 6"
"$OUT/replay" "$OUT/cp_plus-init-combi_6.txt" | tail -3

echo "== replay: Combi D6 E session, then fan HIGH / 22 C command (compare with original iNet Box)"
"$OUT/replay" "$OUT/cp_plus-combi_6d_e-inet_box.txt" cmd:22,A "$OUT/truma_app-set_fan_high.txt" | grep -E "pid 3D|slots|NAD=" 

echo "== replay: all captures (parser, checksums)"
for f in "$OUT"/*.txt; do
  r=$("$OUT/replay" "$f" | grep -oE "frames=[0-9]+ crc_err=[0-9]+")
  printf "  %-36s %s\n" "$(basename "$f" .txt)" "$r"
done

echo "== replay: community captures (ESPHome observer logs, iNet Box at NAD 0x03)"
for f in "$OUT"/community/*.txt; do
  r=$("$OUT/replay" nad:03 "$f" | grep -oE "frames=[0-9]+ crc_err=[0-9]+")
  printf "  %-36s %s\n" "$(basename "$f" .txt)" "$r"
done

echo "== commands from the Truma app: our heater update vs. original iNet Box (payload only)"
# The app sets 22 C for fan/enable captures; payload frames 0x23/0x24 must be identical,
# only the message counter (and with it the checksum) in frame 0x22 differs.
for f in "$OUT"/truma_app-set_heating_to_*.txt "$OUT"/truma_app-set_fan_*.txt "$OUT"/truma_app-*able_heating.txt; do
  b=$(basename "$f" .txt)
  case $b in
    *fan_high) c=cmd:22,A ;; *fan_eco|*enable_heating) c=cmd:22,1 ;; *disable_heating) c=cmd:0,0 ;;
    *) c=cmd:${b##*_},1 ;;
  esac
  n=$("$OUT/replay" "$OUT/cp_plus-combi_6d_e-inet_box.txt" "$c" "$f" | grep -cE "^DIFF pid 3D real: 03 2[3-9]" || true)
  printf "  %-36s %-10s %s\n" "$b" "$c" "$([ "$n" = 0 ] && echo identical || echo "$n payload diffs")"
done

echo "== timer from the Truma app: our timer update vs. original iNet Box (payload)"
# Base: the app capture (timer status from the CP Plus, state buffer reads). After each command
# the first state read carries our timer frame; it must equal the last frame the original sent.
A="$OUT/community/timer_heater-app-off_set20eco_set21eco_water_eco_off.txt"
ours=$("$OUT/replay" nad:03 dev:02 "$A" timer:22:00-06:30,20,1,0 "$A" timer:22:00-06:30,21,1,40 "$A" timeroff "$A" | sed -n 's/^UPDATE type 3C len 12: //p')
real=$(printf '%s\n' \
  "72 0B 01 00 00 00 00 00 00 00 01 01 01 00 16 1E 06 00" \
  "7C 0B 01 00 00 00 3A 0C 00 00 01 01 01 00 16 1E 06 00" \
  "7C 0B 01 00 00 00 3A 0C 00 00 01 01 00 00 16 1E 06 00")
i=0
for c in "22:00-06:30, 20 C, eco, water off" "22:00-06:30, 21 C, eco, water eco" "timer off"; do
  i=$((i+1))
  o=$(echo "$ours" | sed -n "${i}p"); r=$(echo "$real" | sed -n "${i}p")
  printf "  %-36s %s\n" "$c" "$([ "$o" = "$r" ] && echo identical || echo "DIFF ours: $o")"
  [ "$o" = "$r" ] || exit 1
done


echo "== sniffer: changed frames of the CP Plus <-> heater PIDs (fan eco -> high on the CP Plus)"
s=$("$OUT/replay" sniff "$OUT/cp_plus-set_fan_high.txt" | grep "^SNIFF PID 07")
echo "$s" | sed 's/^/  /'
[ "$(echo "$s" | wc -l)" = 2 ] || exit 1

echo "== ventilation level from PID 0x20 (synthetic frames, byte 5 high nibble as in inetbox2mqtt)"
printf 'M 20 00 00 00 00 00 30 00 00\n' > "$OUT/vent.txt"
r=$("$OUT/replay" "$OUT/vent.txt" | grep "^vent:")
echo "  $r"
[ "$r" = "vent: raw 0x3 (3)" ] || exit 1

