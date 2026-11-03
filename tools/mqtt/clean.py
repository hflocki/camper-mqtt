#!/usr/bin/env python3
"""Find and delete left-over retained topics on the broker.

Without arguments it lists every device under <root>/+/status like the display does and
marks left-overs: offline, or "online" without any <root>/<id>/info (an old firmware
that never sent one, or whose status was never cleared). With prefixes it collects all
retained topics below them and deletes them after asking.

  python3 tools/mqtt/clean.py --host 192.168.8.1                     # overview
  python3 tools/mqtt/clean.py --host 192.168.8.1 camper/gaslevel camper/waterlevel
  python3 tools/mqtt/clean.py --host 192.168.8.1 camper/truma --yes  # without asking

Deleting = publishing an empty retained message. A device that is still running sends
its topics again, so nothing is lost by deleting too much except values that only come
back with the next change.
"""
import argparse
import json
import sys
import threading
import time

from common import add_broker_args, connect


def collect(args, filters, wait):
    """Retained messages below the filters: {topic: payload bytes}."""
    seen = {}
    lock = threading.Lock()

    def on_connect(c, _u, _f, rc, _p=None):
        if rc != 0:
            print(f"connect failed: {rc}", file=sys.stderr)
            return
        for f in filters:
            c.subscribe(f, qos=0)

    def on_message(_c, _u, msg):
        if msg.retain and msg.payload:
            with lock:
                seen[msg.topic] = msg.payload

    c = connect(args, "camper-clean", on_connect=on_connect, on_message=on_message)
    time.sleep(wait)   # the broker sends all retained messages right after the subscribe
    c.loop_stop()
    c.disconnect()
    return seen


def overview(args):
    root = args.root
    seen = collect(args, [f"{root}/+/status", f"{root}/+/info"], args.wait)
    devs = {}
    for topic, payload in seen.items():
        parts = topic.split("/")
        if len(parts) != 3:
            continue
        d = devs.setdefault(parts[1], {})
        d[parts[2]] = payload.decode("utf-8", "replace")
    if not devs:
        print(f"no retained {root}/+/status found on {args.host}")
        return 0
    old = []
    print(f"{'device':16} {'status':9} info")
    for dev in sorted(devs):
        d = devs[dev]
        status = d.get("status", "-")
        info = "-"
        if "info" in d:
            try:
                j = json.loads(d["info"])
                info = f"v{j.get('version', '?')}  {j.get('ip', '')}"
            except ValueError:
                info = "(no JSON)"
        stale = status != "online" or "info" not in d
        if stale:
            old.append(f"{root}/{dev}")
        print(f"{dev:16} {status:9} {info}{'   <- left-over?' if stale else ''}")
    if old:
        port = f" --port {args.port}" if args.port != 1883 else ""
        print(f"\nDelete with:\n  python3 tools/mqtt/clean.py --host {args.host}{port} " + " ".join(old))
        print("A device that is only switched off right now reappears as soon as it runs again.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    add_broker_args(ap)
    ap.add_argument("prefix", nargs="*", help="delete all retained topics below these prefixes (e.g. camper/gaslevel)")
    ap.add_argument("--yes", action="store_true", help="delete without asking")
    ap.add_argument("--wait", type=float, default=2.0, help="seconds to collect retained messages (default 2)")
    args = ap.parse_args()

    if not args.prefix:
        return overview(args)

    for p in args.prefix:
        if any(ch in p for ch in "#+") or p.strip("/").count("/") < 1:
            print(f"refusing '{p}': give a device prefix like {args.root}/gaslevel (no wildcards, not the root)")
            return 1
    filters = [p.rstrip("/") + "/#" for p in args.prefix]
    seen = collect(args, filters, args.wait)
    if not seen:
        print("no retained topics found below " + ", ".join(args.prefix))
        return 0
    for t in sorted(seen):
        v = seen[t].decode("utf-8", "replace").replace("\n", " ")
        print(f"  {t} = {v[:60]}{'…' if len(v) > 60 else ''}")
    if not args.yes and input(f"Delete these {len(seen)} retained topics? [y/N] ").strip().lower() not in ("y", "j", "yes", "ja"):
        print("nothing deleted")
        return 0

    c = connect(args, "camper-clean")
    for t in sorted(seen):
        c.publish(t, b"", qos=1, retain=True).wait_for_publish(5)
    c.loop_stop()
    c.disconnect()
    print(f"deleted {len(seen)} topics")
    return 0


if __name__ == "__main__":
    sys.exit(main())
