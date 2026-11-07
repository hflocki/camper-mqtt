#!/usr/bin/env python3
"""Camper emulator: sends the MQTT topics of the devices built into the camper (tanks,
level, heater, MaxxFan, temperatures, batteries, toolbox, router, Zigbee) with values you
change in a local web page, plus scenarios. For testing the displays and Home Assistant
at home. See README.md.

  python3 tools/emulator/emulator.py --host 192.168.8.1            then open http://localhost:8080
  python3 tools/emulator/emulator.py --selftest                    checks without broker (CI)

Broker: --host/MQTT_HOST, --port/MQTT_PORT, --user/MQTT_USER, password only as MQTT_PASSWORD.
"""
import argparse
import asyncio
import json
import os
import signal
import socket
import sys
import time
from pathlib import Path

from emu import DEVS, D_COUNT, M_OFF, M_ON, M_OFFLINE, Emu, ScenError, parse_scen, BUILTIN_SCEN, FIELDS

VERSION = "2.0.0"
HERE = Path(__file__).resolve().parent
REPUBLISH_S = 60          # re-send all values at least this often (changes go out within a second)


# ============================================================
# Broker connections
# ============================================================
# conns[0 .. D_COUNT-1]: one per emulated device with its own client id and last will, so
# the display sees each device go online / offline separately (router and Zigbee have none).
# conns[SELF]: the emulator itself (<prefix>/status, /info), also sends the router and Zigbee
# topics and receives the Zigbee commands.
#
# Every second all enabled devices are built and only changed payloads are sent; everything
# again every REPUBLISH_S. Topics a device no longer sends (sensor switched off, id or base
# topic changed, device "off") are deleted with an empty retained message. Home Assistant
# discovery is left out on purpose: emulated devices must not create entities next to the
# real ones.

SELF = D_COUNT


class Conn:
    def __init__(self):
        self.client = None
        self.prefix = ""
        self.sent = {}          # retained topic -> last payload
        self.force = True       # send everything on the next round
        self.last_info = 0.0


class Broker:
    def __init__(self, emu: Emu, args):
        import paho.mqtt.client as mqtt
        self.mqtt = mqtt
        self.emu = emu
        self.args = args
        self.conns = [Conn() for _ in range(D_COUNT + 1)]
        self.applied = [M_ON] * D_COUNT
        self.last_full = 0.0
        self.started = time.time()
        self.tag = f"{os.getpid() & 0xFFFF:04x}"
        self.ip = local_ip(args.host)
        emu.sender = self.send_once

    # ---- helpers ----
    def own(self, d):
        return DEVS[d][3]

    def conn_of(self, d):
        return d if self.own(d) else SELF

    def conn_prefix(self, c):
        return self.args.prefix if c == SELF else self.emu.prefix(c)

    def connected(self, c):
        cl = self.conns[c].client
        return bool(cl and cl.is_connected())

    def any_live(self):
        if self.connected(SELF):
            return self.conns[SELF].client
        for k in self.conns:
            if k.client and k.client.is_connected():
                return k.client
        return None

    def wanted(self, c):
        return c == SELF or (self.own(c) and self.emu.modes[c] == M_ON)

    def info_json(self, c):
        # same keys as devinfo.* of the real devices; the MAC is made up per emulated device
        return json.dumps({"version": f"emu-py-{VERSION}", "ip": self.ip, "mac": f"02:00:00:00:EE:{c:02X}",
                           "rssi": -50, "uptime": int(time.time() - self.started), "reset": "poweron", "heap": 0},
                          separators=(",", ":"))

    # ---- connections ----
    def open(self, c):
        k = self.conns[c]
        k.prefix = self.conn_prefix(c)
        name = "self" if c == SELF else DEVS[c][0]
        cl = self.mqtt.Client(self.mqtt.CallbackAPIVersion.VERSION2, client_id=f"vannode-emu-{name}-{self.tag}")
        cl.will_set(k.prefix + "/status", "offline", qos=1, retain=True)
        if self.args.user:
            cl.username_pw_set(self.args.user, os.environ.get("MQTT_PASSWORD", ""))
        cl.on_connect = lambda client, ud, flags, rc, props: self._on_connect(c, client, rc)
        cl.on_message = lambda client, ud, msg: self._on_message(msg)
        cl.reconnect_delay_set(1, 10)
        k.client = cl
        cl.connect_async(self.args.host, self.args.port, keepalive=30)
        cl.loop_start()

    def _on_connect(self, c, client, rc):
        if rc.is_failure:
            print(f"[MQTT] {DEVS[c][0] if c < SELF else 'self'}: connect failed ({rc})")
            return
        k = self.conns[c]
        if c == SELF:
            client.subscribe(self.emu.zigbee_cmd_base() + "#")
            print(f"[MQTT] connected to {self.args.host}:{self.args.port}")
        else:
            client.subscribe(k.prefix + "/cmd/#")
            client.subscribe(k.prefix + "/set/#")
        k.force = True
        k.last_info = 0

    def _on_message(self, msg):
        payload = msg.payload.decode("utf-8", "replace")
        if self.args.verbose:
            print(f"[MQTT] < {msg.topic} = {payload[:80]}")
        self.emu.command(msg.topic, payload)

    def close(self, c, clean=True):
        """clean: DISCONNECT (no last will); otherwise the TCP connection just drops (last will)."""
        k = self.conns[c]
        cl, k.client = k.client, None
        if not cl:
            return
        if clean:
            cl.disconnect()
            cl.loop_stop()
        else:
            cl.loop_stop()
            s = cl.socket()
            if s:
                try:
                    s.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                s.close()

    def send_once(self, topic, payload):
        cl = self.conns[SELF].client
        if cl:
            cl.publish(topic, payload, qos=0, retain=False)
        print(f"[MQTT] > {topic} = {payload}")

    # ---- publishing with change tracking ----
    def drop_topics(self, k, keep=None):
        via = self.any_live()
        for t in [t for t in k.sent if not keep or t not in keep]:
            if via:
                via.publish(t, "", qos=0, retain=True)
            del k.sent[t]

    def emit(self, k, force, seen, topic, payload):
        if not topic:
            return
        seen.add(topic)
        if not force and k.sent.get(topic) == payload:
            return
        if k.client.publish(topic, payload, qos=0, retain=True).rc == self.mqtt.MQTT_ERR_SUCCESS:
            k.sent[topic] = payload
            if self.args.verbose:
                print(f"[MQTT] > {topic} = {payload[:80]}")

    def apply_modes(self):
        """Carries out mode changes from the web page or a scenario."""
        if not self.connected(SELF):
            return   # deleting topics needs the broker
        for d in range(D_COUNT):
            m = self.emu.modes[d]
            if m == self.applied[d]:
                continue
            c = self.conn_of(d)
            k = self.conns[c]
            if m == M_OFF:
                if c != SELF:
                    self.drop_topics(k)          # values, status and info of this device
                    self.close(c, clean=True)    # clean: the broker does not send the will
                else:
                    # router / Zigbee share the own connection: delete only their topics
                    via = self.any_live()
                    for t, _ in self.emu.build(d):
                        if via:
                            via.publish(t, "", qos=0, retain=True)
                        k.sent.pop(t, None)
                print(f"[EMU] {DEVS[d][0]} off, topics deleted")
            elif m == M_OFFLINE:
                if c != SELF:
                    self.close(c, clean=False)
                    print(f"[EMU] {DEVS[d][0]} offline (connection dropped, last will)")
            else:
                k.force = True
                print(f"[EMU] {DEVS[d][0]} online")
            self.applied[d] = m

    def publish_round(self, full):
        now = time.monotonic()
        for c, k in enumerate(self.conns):
            if not self.connected(c):
                continue
            force = full or k.force
            k.force = False
            seen = set()
            if c == SELF:
                for d in range(D_COUNT):
                    if self.own(d):
                        continue
                    if self.emu.modes[d] == M_ON:
                        for t, p in self.emu.build(d):
                            self.emit(k, force, seen, t, p)
                    elif self.emu.modes[d] == M_OFFLINE:   # paused: keep its topics as they are
                        seen.update(t for t, _ in self.emu.build(d))
            else:
                for t, p in self.emu.build(c):
                    self.emit(k, force, seen, t, p)
            p = k.prefix
            self.emit(k, force, seen, p + "/status", "online")
            if k.last_info == 0 or now - k.last_info >= 60:   # diagnostics once a minute
                k.last_info = now
                self.emit(k, force, seen, p + "/rssi", "-50")
                self.emit(k, force, seen, p + "/info", self.info_json(c))
            else:
                seen.update((p + "/rssi", p + "/info"))
            self.drop_topics(k, seen)   # what this device did not send this round is gone

    def step(self):
        """Once a second from the event loop."""
        if not self.conns[SELF].client:
            self.open(SELF)
        if self.emu.take_clear_all():
            for k in self.conns:
                self.drop_topics(k)
                k.force = True
            print("[EMU] All retained topics deleted")
        # base topic / id changed: stale topics go in the next round; a device connection
        # reopens so its last will and subscriptions use the new prefix
        for d in range(D_COUNT):
            if not self.emu.take_topic_change(d):
                continue
            c = self.conn_of(d)
            if c == SELF:
                self.close(SELF)   # resubscribe the Zigbee commands
                continue
            k = self.conns[c]
            if k.client and self.conn_prefix(c) != k.prefix:
                self.drop_topics(k)
                self.close(c)
        self.apply_modes()
        if self.connected(SELF):
            for c in range(D_COUNT):
                if self.wanted(c) and not self.conns[c].client:
                    self.open(c)
        now = time.monotonic()
        full = now - self.last_full >= REPUBLISH_S
        if full:
            self.last_full = now
        self.publish_round(full)

    def conn_states(self):
        return [1 if self.connected(self.conn_of(d)) else 0 for d in range(D_COUNT)]

    def shutdown(self, keep):
        """keep: leave the values on the broker, only report the devices offline."""
        if keep:
            for c, k in enumerate(self.conns):
                if k.client and k.client.is_connected():
                    k.client.publish(k.prefix + "/status", "offline", qos=1, retain=True).wait_for_publish(2)
        else:
            via = self.any_live()
            for k in self.conns:
                for t in list(k.sent) + ([k.prefix + "/status"] if k.prefix else []):
                    if via:
                        via.publish(t, "", qos=1, retain=True).wait_for_publish(2)
                k.sent.clear()
        for c in range(len(self.conns)):
            self.close(c, clean=True)


def local_ip(host):
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect((host, 9))
            return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"


# ============================================================
# Web page
# ============================================================

def make_app(emu: Emu, broker: Broker, args):
    from aiohttp import web, WSMsgType

    clients = set()

    def jerr(msg, code=400):
        return web.json_response({"error": msg}, status=code)

    async def index(_):
        return web.FileResponse(HERE / "web" / "index.html", headers={"Cache-Control": "no-store"})

    async def schema(_):
        return web.json_response(emu.schema())

    async def scen_get(_):
        return web.Response(text=json.dumps(emu.export_scenarios(), ensure_ascii=False, indent=2),
                            content_type="application/json",
                            headers={"Content-Disposition": 'attachment; filename="emulator-szenarien.json"'})

    async def scen_post(req):
        try:
            doc = json.loads(await req.text())
        except ValueError as e:
            return jerr(f"Keine gültige JSON-Datei ({e})")
        try:
            return web.json_response({"report": emu.import_scenarios(doc)})
        except ValueError as e:
            return jerr(str(e))

    async def action(req):
        try:
            doc = await req.json()
        except ValueError:
            return jerr("Invalid JSON")
        try:
            if isinstance(doc.get("set"), dict):
                for k, v in doc["set"].items():
                    emu.set_value(k, v)
            elif isinstance(doc.get("mode"), list):
                emu.set_mode(int(doc["mode"][0]), int(doc["mode"][1]))
            elif isinstance(doc.get("noise"), list):
                emu.set_noise(int(doc["noise"][0]), bool(doc["noise"][1]))
            elif isinstance(doc.get("scenario"), str):
                emu.start_scenario(doc["scenario"], int(doc.get("dev") or 0))
            elif isinstance(doc.get("scen_del"), str):
                emu.delete_scenario(doc["scen_del"])
            elif doc.get("stop") is not None:
                emu.stop_scenario(doc.get("restore", True))
            elif "reset" in doc:
                emu.reset_values(int(doc["reset"] if doc["reset"] is not None else -1))
            elif doc.get("clear"):
                emu.request_clear_all()
            else:
                return jerr("Unknown action")
        except (ValueError, TypeError, IndexError) as e:
            return jerr(str(e))
        return web.json_response({"status": "ok"})

    def live():
        return json.dumps({"emu": emu.state(), "conn": broker.conn_states(), "mqtt": broker.connected(SELF),
                           "broker": f"{args.host}:{args.port}", "version": VERSION}, ensure_ascii=False)

    async def ws_handler(req):
        ws = web.WebSocketResponse(heartbeat=30)
        await ws.prepare(req)
        clients.add(ws)
        await ws.send_str(live())
        try:
            async for msg in ws:
                if msg.type == WSMsgType.ERROR:
                    break
        finally:
            clients.discard(ws)
        return ws

    async def ticker(app):
        async def loop():
            while True:
                emu.tick()
                broker.step()
                if clients:
                    data = live()
                    for ws in list(clients):
                        try:
                            await ws.send_str(data)
                        except (ConnectionError, RuntimeError):
                            clients.discard(ws)
                await asyncio.sleep(1)
        task = asyncio.create_task(loop())
        yield
        task.cancel()
        for ws in list(clients):
            await ws.close()

    app = web.Application()
    app.add_routes([web.get("/", index), web.get("/api/emu/schema", schema), web.get("/api/emu/scenarios", scen_get),
                    web.post("/api/emu/scenarios", scen_post), web.post("/api/emu", action), web.get("/ws", ws_handler)])
    app.cleanup_ctx.append(ticker)
    return app


# ============================================================
# Self test (no broker, standard library only)
# ============================================================

def selftest() -> int:
    import tempfile
    errors = []
    with tempfile.TemporaryDirectory() as tmp:
        emu = Emu(Path(tmp))
        sent = []
        emu.sender = lambda t, p: sent.append((t, p))
        for d in range(D_COUNT):
            out = emu.build(d)
            if d != 7 and not out:
                errors.append(f"{DEVS[d][0]}: nothing built")
            for t, p in out:
                if not t or "#" in t or "+" in t:
                    errors.append(f"{DEVS[d][0]}: bad topic {t!r}")
                if p.startswith(("{", "[")):
                    json.loads(p)
        for s in BUILTIN_SCEN:
            parse_scen(s)
            emu.start_scenario(s["id"], 2)
            emu.scen_start -= 200   # jump to the end
            emu.tick()
            for d in range(D_COUNT):
                emu.build(d)
            emu.stop_scenario(True)
        # commands like the display sends them
        emu.command("camper/heater/set/target_temp_room", "22")
        emu.command("camper/toolbox/cmd/rvtech/auf/set", "1")
        emu.command("camper/zigbee/cmd/a4c1380000000021/1/0006", "ON")
        emu.command("camper/maxxfan/cmd/p1/speed/set", "4")
        checks = [("setp", "22.0"), ("hvac", "heat"), ("rv_tgt", "100"), ("l1_state", "ON"), ("p1_speed", "4")]
        for k, want in checks:
            if emu.V(k) != want:
                errors.append(f"command: {k} = {emu.V(k)!r}, expected {want!r}")
        if not any(t == "camper/display/cmd/awning" for t, _ in sent):
            errors.append("scenario depart: awning command not sent")
        # upload, rejected input, delete
        emu.import_scenarios({"scenarios": [{"id": "t-1", "tracks": {"w_pct": [[0, 10], [5, 20]]}}]})
        for bad in ({"id": "x", "tracks": {"tk_pre": [[0, "a"]]}}, {"id": "Bad Id"}, {"id": "y", "tracks": {"w_pct": [[0, 500]]}}):
            try:
                emu.import_scenarios(bad)
                errors.append(f"accepted bad scenario {bad}")
            except (ValueError, ScenError):
                pass
        emu.delete_scenario("t-1")
        try:
            emu.set_value("w_pct", "101")
            errors.append("accepted 101 %")
        except ValueError:
            pass
        emu.flush()
    for e in errors:
        print("FAIL", e)
    print(f"{len(FIELDS)} fields, {len(BUILTIN_SCEN)} scenarios: " + ("OK" if not errors else f"{len(errors)} errors"))
    return 1 if errors else 0


# ============================================================

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("MQTT_HOST", "localhost"), help="broker (env MQTT_HOST)")
    ap.add_argument("--port", type=int, default=int(os.environ.get("MQTT_PORT", "1883")), help="port (env MQTT_PORT)")
    ap.add_argument("--user", default=os.environ.get("MQTT_USER", ""), help="user (env MQTT_USER), password only as MQTT_PASSWORD")
    ap.add_argument("--prefix", default="camper/emulator", help="topic of the emulator itself (default camper/emulator)")
    ap.add_argument("--web-port", type=int, default=8080, help="port of the web page (default 8080)")
    ap.add_argument("--listen", default="127.0.0.1", help="address of the web page; 0.0.0.0 = reachable in the network")
    ap.add_argument("--data", type=Path, default=HERE / "data", help="folder for values and own scenarios")
    ap.add_argument("--keep", action="store_true", help="on exit leave the values on the broker (only status offline)")
    ap.add_argument("-v", "--verbose", action="store_true", help="print every message")
    ap.add_argument("--selftest", action="store_true", help="check builders, commands and scenarios, no broker")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    try:
        from aiohttp import web
        import paho.mqtt.client  # noqa: F401
    except ImportError:
        sys.exit("Missing packages: python3 -m venv .venv && . .venv/bin/activate && "
                 "pip install -r tools/emulator/requirements.txt")

    emu = Emu(args.data)
    broker = Broker(emu, args)
    app = make_app(emu, broker, args)

    async def run():
        runner = web.AppRunner(app)
        await runner.setup()
        await web.TCPSite(runner, args.listen, args.web_port).start()
        shown = "localhost" if args.listen in ("127.0.0.1", "0.0.0.0") else args.listen
        print(f"[WEB] http://{shown}:{args.web_port}  (Ctrl+C ends, "
              + ("values stay on the broker)" if args.keep else "deletes the emulator's retained topics)"))
        stop = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            try:
                loop.add_signal_handler(sig, stop.set)
            except NotImplementedError:   # Windows
                pass
        try:
            await stop.wait()
        finally:
            await runner.cleanup()

    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass
    emu.flush()
    broker.shutdown(args.keep)
    print("[EMU] stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
