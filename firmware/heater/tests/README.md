# Tests

Host-side tests for the LIN / iNet Box protocol code. They run on the Mac or Linux with `g++` and `python3`, no ESP32 needed.

```bash
./tests/run.sh
```

| Test | What it does |
|---|---|
| `protocol_test.cpp` | Synthetic CP Plus session: read-by-identifier, NAD assignment, init request, device + heater status frames, room/fan command, checksums |
| `replay.cpp` | Feeds real LIN analyzer captures into `InetBox` byte by byte. Master frames are replayed, for every slave slot (0x18, 0x3D) our answer is compared with the answer of the original Truma iNet Box |

`replay` accepts several capture files and commands in between, e.g. `cmd:22,A` = room 22 °C, fan HIGH (`HeatingMode` value in hex).

## Captures

`captures/` contains LIN bus recordings (Microchip LIN analyzer, 9600 baud) of a Truma CP Plus with Combi 6 / Combi D6 E and an original iNet Box, from the WomoLIN project (muccc/WomoLIN, GPL-3.0).

`captures/community/` contains ESPHome `truma_inetbox` logs (observer mode, VERBOSE) from a community member: CP Plus with Combi heater, Aventa aircon and an original iNet Box at NAD 0x03. Only the LIN-related log lines are kept. `replay nad:03 <file>` replays them with our node address fixed to 0x03, because the captures contain no NAD assignment.

| Capture | Actions (in order) |
|---|---|
| `heater_vent-off_1_2_3_4_5` | Heater ventilation (CP Plus) off → level 1 … 5 |
| `heater_vent-5_6_7_8_9_10_off` | Ventilation level 5 → 6 … 10 → off |
| `aircon_auto-app_N-…` | Aircon AUTO via Truma app: on, 22 °C → 24 °C, off (app_1: → vent) |
| `aircon_auto-cpplus-…` | The same on the CP Plus |
| `timer_heater-app-…` | Truma app: timer off → 22:00–06:30 20 °C eco, water off → 21 °C eco, water eco → off |
| `timer_heater-cpplus-…` | The same on the CP Plus (two captures: a running timer cannot be edited there) |
| `timer_aircon-…` | Aircon timer 20/21 °C cool via app and CP Plus |
| `clock-cpplus-plus1h_minus1h` | Clock set one hour forward and back on the CP Plus |
| `aircon_acc-…` | CP Plus climate automatic (ACC, enabled in the service menu): 22 °C, water off/eco, set via CP Plus and app |
| `shore_power-off_on_off` | Shore power plugged in and removed again (CP Plus with Combi and Aventa aircon) |
| `heater_vent_sensor-off_1-10_off` | Heater ventilation 1 … 10 on the CP Plus with the ESPHome `VENT_MODE` sensor (values in the log) |

## Findings from the captures

- All 29 captures (about 6,900 frames): all CP Plus status frames (devices, heater, timer, clock, config) are decoded without checksum errors.
- For all 14 Truma app commands in the captures (room 13–22 °C, fan eco/high, heating on/off) the heater update frame we generate is byte-identical in its payload to the one of the original iNet Box; only the message counter (and therefore the checksum) differs. `run.sh` checks this automatically.
- Timer (community captures, Truma app): the original iNet Box sends the timer as frame 0x3C with length **0x12 (18 bytes)**: the 17-byte layout of esphome-truma_inetbox plus one trailing 0x00 (inetbox.py uses 24 bytes). All writes were acknowledged with result 0. `run.sh` replays the capture with our three commands (set 20 °C eco water off, set 21 °C eco water eco, off) – our frames are byte-identical. The app sends each change several times; to switch the timer off it resends the last values with `active = 0`.
- On the CP Plus a running timer cannot be edited, only switched off and set again; the CP Plus then reports the new timer status (0x3D) on its own.
- Aircon timer: write frame 0x3E (length 0x12: aircon mode, 00, fan, 01, target temp, …, water target, active, start/stop), status frame 0x3F (length 0x16). esphome-truma_inetbox calls 0x3F "aircon manual init"; it is the aircon timer status. Ignored by the firmware.
- Ventilation level: the ESPHome `VENT_MODE` sensor (PID 0x20, CP Plus → heater) reported exactly the levels 1–10 and 0 set on the CP Plus. The raw 0x20 frames are not in the logs: esphome-truma_inetbox logs PIDs 0x20–0x22 only at VERY_VERBOSE. Byte position (byte 5, high nibble) taken from inetbox2mqtt. The WomoLIN Combi D6 E bus has no PID 0x20 at all (heater commands on 0x03–0x07).
- ACC (climate automatic of the CP Plus): status frame 0x37 (length 0x12) appears once ACC is enabled; it carries room target and water target. Not used by the firmware.
- Shore power: no frame carries a "shore power" flag. The Aventa aircon runs on 230 V only, so it appears and disappears on the bus: with shore power it answers its LIN slot (PID 0x17), the CP Plus re-sends the device list (frames 0x0B) with the aircon (device 3/3) `state = 1` (online); after unplugging, the aircon stops answering and the next device list shows it with `state = 0`. The heater status is unchanged. The app most likely derives "shore power" from this aircon state, so without a 230 V device on the bus there is nothing to read.
- The CP Plus re-sends the device list during a running session when a device comes or goes; the firmware logs each device with its online state.
- Clock: the original iNet Box never sets the clock (no frame 0x14 in any capture). Changing the time on the CP Plus is reported immediately with a clock status frame (0x15).
- Community captures: all 19 logs (about 16,000 frames incl. aircon, ACC and timer frames) decode without checksum errors.
- **Heater ventilation (CP Plus „Lüften“, levels 1–10) is not visible in the heater status frame (0x33).** Room target, heating mode and water target stay 0 for every level; only the operating status changes (4 start → 5 running, back to 4 when switched off). The vent level is therefore neither readable nor settable via the iNet Box interface (esphome-truma_inetbox and inetbox.py have no such mode either; inetbox2mqtt only reads it from the CP Plus ↔ heater frame 0x20, which the iNet Box slot does not see). The firmware reports `hvac_action = fan` when the heater runs with room and water off (ventilation or fan after-run).
- Aircon AUTO: the CP Plus reports it with the *manual* aircon status frame 0x35 (operating mode 0x07, fan 0x77); the AUTO frame 0x37 never appears. Writes from the original iNet Box use frame 0x34 (length 0x0C): `mode, 00, fan, 01, target temp (u16, 0 = keep), …` – on: mode 0x04 → 0x07 with target 0, then target 22 °C; off: mode 0x00, fan 0x70. The firmware ignores aircon frames (heater only).
- Differences to the original iNet Box (all also present in the proven ESPHome implementation, so they are accepted by the CP Plus):
  - The original pads state-buffer answers to 41 bytes (transport length 0x29), we send the real length (e.g. 0x1D).
  - Read-by-identifier 0x00: the original answers variant `0x00`, we answer `0x01`.
  - Read-by-identifier 0x20 / 0x22: the original answers `02 02 00` / `03 07 00`, we answer `17 46 00` (shown as T23.70.0 in the CP Plus INDEX menu) / empty.
