#!/usr/bin/env python3
"""Record MQTT messages into a JSON-lines file that sim.py can replay.

  python3 tools/mqtt/record.py --host 192.168.8.2 -t 'camper/#' -o camper.jsonl
  python3 tools/mqtt/record.py -t 'camper/zigbee/data/#' --duration 600 -o pj1203.jsonl

One line per message: {"t": seconds since start, "topic": ..., "payload": text or
{"b64": ...} for binary data, "retain": true/false}. Stop with Ctrl+C.
"""
import argparse
import base64
import json
import signal
import sys
import time

from common import add_broker_args, connect


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    add_broker_args(ap)
    ap.add_argument("-t", "--topic", action="append", help="topic filter, repeatable (default: <root>/#)")
    ap.add_argument("-o", "--out", default="mqtt-record.jsonl", help="output file (default: mqtt-record.jsonl)")
    ap.add_argument("--duration", type=float, default=0, help="stop after this many seconds (0 = until Ctrl+C)")
    ap.add_argument("--no-retained", action="store_true", help="skip retained messages sent at subscribe time")
    ap.add_argument("-q", "--quiet", action="store_true", help="do not print every message")
    args = ap.parse_args()
    filters = args.topic or [args.root + "/#"]

    out = open(args.out, "w", encoding="utf-8")
    start = time.monotonic()
    count = 0

    def on_message(_c, _u, msg):
        nonlocal count
        if args.no_retained and msg.retain:
            return
        try:
            payload = msg.payload.decode("utf-8")
        except UnicodeDecodeError:
            payload = {"b64": base64.b64encode(msg.payload).decode("ascii")}
        rec = {"t": round(time.monotonic() - start, 3), "topic": msg.topic, "payload": payload, "retain": bool(msg.retain)}
        out.write(json.dumps(rec, ensure_ascii=False) + "\n")
        out.flush()
        count += 1
        if not args.quiet:
            text = payload if isinstance(payload, str) else "<binary>"
            print(f"{rec['t']:8.1f}  {msg.topic}  {text[:120]}")

    def on_connect(c, _u, _f, rc, _p):
        if rc != 0:
            print(f"connect failed: {rc}", file=sys.stderr)
            return
        for f in filters:
            c.subscribe(f)
        print(f"recording {', '.join(filters)} -> {args.out}", file=sys.stderr)

    c = connect(args, "camper-record", on_connect, on_message)

    stop = False

    def handler(*_):
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)
    while not stop and (args.duration <= 0 or time.monotonic() - start < args.duration):
        time.sleep(0.2)
    c.loop_stop()
    c.disconnect()
    out.close()
    print(f"\n{count} messages in {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
