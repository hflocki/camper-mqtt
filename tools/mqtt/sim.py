#!/usr/bin/env python3
"""MQTT simulator for the camper devices: test the display, Road Buddy and Home Assistant
at home without the van. Publishes the same topics as the real firmware (retained).

  python3 tools/mqtt/sim.py --host 192.168.8.1                 # scenario "normal"
  python3 tools/mqtt/sim.py --host 192.168.8.1 empty           # tanks run empty
  python3 tools/mqtt/sim.py --host 192.168.8.1 offline --device heater
  python3 tools/mqtt/sim.py --host 192.168.8.1 replay camper.jsonl --speed 10
  python3 tools/mqtt/sim.py --list                             # all scenarios

Real devices on the same broker overwrite the simulated values; switch them off or use
--skip. Ctrl+C ends the simulation; --cleanup then deletes all retained topics it sent.
"""
import argparse
import base64
import json
import math
import random
import signal
import sys
import time

from common import add_broker_args, connect

DEVICES = ["tanklevel", "level", "heater", "maxxfan", "temp", "battery", "router"]

SCENARIOS = {
    "normal":  "all devices online, steady values with a little noise",
    "drive":   "driving: GPS speed 0 -> 80 km/h, level values move; with --contact a window stays open",
    "tilt":    "parking on a slope: van tilted, after 90 s it is level",
    "empty":   "fresh water 30 -> 5 %, gas 25 -> 8 % within 3 minutes",
    "offline": "--device goes offline after 30 s and comes back after 2 minutes",
    "fridge":  "fridge warms up from 5 to 11 degC (camper-temp alarm, Zigbee sensor with --fridge)",
    "battery": "battery drains from 40 to 12 % state of charge",
    "frost":   "outside temperature drops below 0 degC (needs --outdoor, a Zigbee sensor marked for the weather page)",
    "replay":  "play back a file from record.py (positional FILE, --speed)",
}


class Sim:
    def __init__(self, client, args):
        self.c = client
        self.a = args
        self.root = args.root
        self.sent = set()          # retained topics, for --cleanup
        self.last = {}             # topic -> payload, to send only changes between full rounds
        self.online = {d: True for d in DEVICES}
        self.t0 = time.monotonic()
        # state
        self.water, self.gas = 62.0, 71.0
        self.pitch, self.roll = 0.2, -0.3
        self.speed = 0.0
        self.fridge, self.freezer, self.room = 5.0, -17.0, 20.5
        self.outdoor = 8.0
        self.soc = 78.0
        self.contact_open = False

    # ---- publishing ----
    def pub(self, sub, payload, force=False, retain=True):
        # "<device>/..." is relative to --root, everything else (GPS, Zigbee) is a full topic
        topic = f"{self.root}/{sub}" if sub.split("/")[0] in DEVICES else sub
        if isinstance(payload, (dict, list)):
            payload = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        payload = str(payload)
        if not force and self.last.get(topic) == payload:
            return
        self.last[topic] = payload
        self.c.publish(topic, payload, qos=0, retain=retain)
        if retain:
            self.sent.add(topic)
        if self.a.verbose:
            print(f"{time.monotonic() - self.t0:7.1f}  {topic}  {payload[:100]}")

    def zigbee(self, ieee, cluster, attr, val, force):
        base = self.a.zigbee_base.rstrip("/")
        self.pub(f"{base}/{ieee}/1/{cluster}/{attr}", {"data": {"val": val, "type_sm": "report"}}, force)

    def info(self, dev):
        up = int(time.monotonic() - self.t0) + 3600
        self.pub(f"{dev}/info", {"version": "sim", "ip": "127.0.0.1", "mac": "00:00:00:00:00:00",
                                 "rssi": -60 - random.randint(0, 8), "uptime": up, "reset": "power on", "heap": 150000}, True)

    def publish_all(self, force):
        n = lambda v, s=0.3: v + random.uniform(-s, s)
        for d in DEVICES:
            if d in self.a.skip or d == "router":
                continue
            self.pub(f"{d}/status", "online" if self.online[d] else "offline", force)
        on = lambda d: self.online[d] and d not in self.a.skip
        if on("tanklevel"):
            w, g = max(0, min(100, self.water)), max(0, min(100, self.gas))
            self.pub("tanklevel/water/percent", round(w), force)
            self.pub("tanklevel/water/liters", round(w * 0.9, 1), force)
            self.pub("tanklevel/gas/level", round(g), force)
            self.pub("tanklevel/gas/content", round(g * 0.11, 2), force)
            self.pub("tanklevel/gas/battery", 87, force)
            self.pub("tanklevel/gas/ble_state", "OK", force)
        if on("level"):
            p, r = n(self.pitch, 0.05), n(self.roll, 0.05)
            tol = 0.5
            self.pub("level/pitch", f"{p:.1f}", force)
            self.pub("level/roll", f"{r:.1f}", force)
            self.pub("level/tolerance", tol, force)
            self.pub("level/is_level", "ON" if abs(p) <= tol and abs(r) <= tol else "OFF", force)
            # wedge under each wheel (cm): lift every wheel to the highest one
            # (3.5 m wheelbase, 1.7 m track; pitch > 0 = front up, roll > 0 = right up)
            tp, tr = math.tan(math.radians(p)), math.tan(math.radians(r))
            h = {"fl": 1.75 * tp - 0.85 * tr, "fr": 1.75 * tp + 0.85 * tr,
                 "rl": -1.75 * tp - 0.85 * tr, "rr": -1.75 * tp + 0.85 * tr}
            top = max(h.values())
            for k, v in h.items():
                self.pub(f"level/wedge_{k}", f"{(top - v) * 100:.1f}", force)
        if on("heater"):
            cs = "heater/control_status"
            self.pub(f"{cs}/alive", "ON", force)
            self.pub(f"{cs}/current_temp_room", f"{n(self.room, 0.1):.1f}", force)
            self.pub(f"{cs}/room_setpoint", "21.0", force)
            self.pub(f"{cs}/hvac_mode", "heat", force)
            self.pub(f"{cs}/hvac_action", "heating" if self.room < 21 else "idle", force)
            self.pub(f"{cs}/fan_mode", "eco", force)
            self.pub(f"{cs}/water_mode", "off", force)
            self.pub(f"{cs}/current_temp_water", "38.0", force)
            self.pub(f"{cs}/operating_status_text", "Heizen" if self.room < 21 else "Bereit", force)
            self.pub(f"{cs}/error_code", "0", force)
            self.pub(f"{cs}/heater_error", "OFF", force)
        if on("maxxfan"):
            self.pub("maxxfan/p1/auto_hold", "OFF", force)
            self.pub("maxxfan/p1/led", "OFF", force)
        if on("temp"):
            alarm = []
            if self.fridge > self.a.fridge_limit:
                alarm.append(f"Kühlschrank zu warm: {self.fridge:.1f} °C (Grenze {self.a.fridge_limit:.1f} °C)".replace(".", ","))
            self.pub("temp/sensors", [
                {"id": "fridge", "name": "Kühlschrank", "t": round(n(self.fridge, 0.1), 2), "alarm": 1 if alarm else 0},
                {"id": "freezer", "name": "Gefrierfach", "t": round(n(self.freezer, 0.1), 2), "alarm": 0},
            ], force)
            self.pub("temp/alarm_text", "; ".join(alarm), force)
        if on("battery"):
            s = max(0, min(100, self.soc))
            v = 11.8 + s / 100 * 1.6
            self.pub("battery/batteries", [{"name": "Bordbatterie", "ok": True, "soc": round(s), "v": round(v, 2),
                                            "w": -56, "rt": int(s / 100 * 100 * 12.8 / 56 * 60)}], force)
        if on("router"):
            self.pub("router/mobile", {"rsrp": -92 + random.randint(-3, 3), "rssi": -65, "conntype": "LTE"}, force)
            self.pub(self.a.gps_topic, {"lat": 51.0, "lon": 10.0, "speed": round(self.speed, 1)}, force)
        if self.a.contact:
            self.zigbee(self.a.contact, "0006", "0000", 1 if self.contact_open else 0, force)
        if self.a.fridge:
            self.zigbee(self.a.fridge, "0402", "0000", round(n(self.fridge, 0.1), 2), force)
        if self.a.outdoor:
            self.zigbee(self.a.outdoor, "0402", "0000", round(n(self.outdoor, 0.1), 2), force)

    # ---- scenarios: change the state over time ----
    def step(self, scenario, t):
        if scenario == "drive":
            self.speed = min(80.0, max(0.0, (t - 10) * 1.5))
            self.contact_open = True
            wob = 1.5 if self.speed > 5 else 0
            self.pitch, self.roll = 0.3 + wob * math.sin(t / 3), -0.2 + wob * math.cos(t / 4)
        elif scenario == "tilt":
            f = max(0.0, 1 - t / 90)
            self.pitch, self.roll = 0.2 + 2.8 * f, -0.3 - 1.9 * f
        elif scenario == "empty":
            f = min(1.0, t / 180)
            self.water, self.gas = 30 - 25 * f, 25 - 17 * f
        elif scenario == "offline":
            back = not (30 <= t < 150)
            if self.online[self.a.device] != back:
                self.online[self.a.device] = back
                print(f"{t:6.0f} s  {self.a.device} {'online' if back else 'offline'}")
        elif scenario == "fridge":
            self.fridge = 5 + min(6.0, t / 20)
        elif scenario == "battery":
            self.soc = 40 - min(28.0, t / 5)
        elif scenario == "frost":
            self.outdoor = 4 - min(6.0, t / 15)


def replay(client, args):
    with open(args.file, encoding="utf-8") as f:
        recs = [json.loads(line) for line in f if line.strip()]
    print(f"{len(recs)} messages, speed x{args.speed}")
    start = time.monotonic()
    for r in recs:
        wait = r["t"] / args.speed - (time.monotonic() - start)
        if wait > 0:
            time.sleep(wait)
        p = r["payload"]
        payload = base64.b64decode(p["b64"]) if isinstance(p, dict) else p
        client.publish(r["topic"], payload, retain=bool(r.get("retain")))
        if args.verbose:
            print(f"{r['t']:8.1f}  {r['topic']}")
    time.sleep(0.5)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    add_broker_args(ap)
    ap.add_argument("scenario", nargs="?", default="normal", help="scenario, see --list")
    ap.add_argument("file", nargs="?", help="replay: file from record.py")
    ap.add_argument("--list", action="store_true", help="list the scenarios")
    ap.add_argument("--device", default="tanklevel", choices=DEVICES, help="offline: which device")
    ap.add_argument("--skip", action="append", default=[], choices=DEVICES, help="do not simulate this device (repeatable)")
    ap.add_argument("--duration", type=float, default=0, help="end after this many seconds (0 = Ctrl+C)")
    ap.add_argument("--speed", type=float, default=1.0, help="replay: time factor (10 = ten times faster)")
    ap.add_argument("--gps-topic", default="camper/router/gps", help="GPS topic with lat/lon/speed (default: camper/router/gps)")
    ap.add_argument("--zigbee-base", default="camper/zigbee/data", help="Zigbee hub base topic (default: camper/zigbee/data)")
    ap.add_argument("--contact", help="IEEE of a Zigbee door/window contact to simulate")
    ap.add_argument("--fridge", help="IEEE of a Zigbee climate sensor in the fridge")
    ap.add_argument("--outdoor", help="IEEE of a Zigbee climate sensor outside (frost)")
    ap.add_argument("--fridge-limit", type=float, default=8.0, help="alarm limit of the camper-temp fridge sensor")
    ap.add_argument("--cleanup", action="store_true", help="delete the retained topics on exit")
    ap.add_argument("-v", "--verbose", action="store_true", help="print every message")
    args = ap.parse_args()

    if args.list:
        for k, v in SCENARIOS.items():
            print(f"  {k:9} {v}")
        return 0
    if args.scenario not in SCENARIOS:
        ap.error(f"unknown scenario {args.scenario!r}, see --list")

    c = connect(args, "camper-sim")
    if args.scenario == "replay":
        if not args.file:
            ap.error("replay needs a file from record.py")
        replay(c, args)
        c.loop_stop()
        c.disconnect()
        return 0

    sim = Sim(c, args)
    stop = False

    def handler(*_):
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)
    print(f"scenario {args.scenario}: {SCENARIOS[args.scenario]} (Ctrl+C ends)")
    last_full = last_info = -1e9
    while not stop:
        t = time.monotonic() - sim.t0
        if args.duration and t >= args.duration:
            break
        sim.step(args.scenario, t)
        full = t - last_full >= 30          # all values every 30 s, like the firmware's republish
        sim.publish_all(full)
        if full:
            last_full = t
        if t - last_info >= 60:
            for d in DEVICES:
                if d not in args.skip and d != "router" and sim.online[d]:
                    sim.info(d)
            last_info = t
        time.sleep(1)

    if args.cleanup:
        for topic in sorted(sim.sent):
            c.publish(topic, b"", retain=True)
        print(f"{len(sim.sent)} retained topics deleted")
    time.sleep(0.5)
    c.loop_stop()
    c.disconnect()
    return 0


if __name__ == "__main__":
    sys.exit(main())
