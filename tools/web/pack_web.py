#!/usr/bin/env python3
"""Pack the web UI of every device: web/index.html -> data/index.html.gz.

The devices serve data/index.html.gz from LittleFS (uploaded in the web UI or with the
Arduino LittleFS upload). After editing web/index.html, run this script and commit both.

  python3 tools/web/pack_web.py           pack all pages that changed
  python3 tools/web/pack_web.py temp      only these devices
  python3 tools/web/pack_web.py --check   compare only, exit code 1 if a .gz is outdated (CI)

The check compares the unpacked content, so a .gz made with "gzip -9 -n" passes as well.
The display embeds its page into the firmware instead (firmware/display/tools/embed_web.py).
"""
import argparse
import gzip
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def pages():
    # every sketch with a data/ folder next to web/index.html
    for html in sorted(ROOT.glob("firmware/*/*/web/index.html")):
        data = html.parent.parent / "data"
        if data.is_dir():
            yield html, data / "index.html.gz"


def device_of(html: Path) -> str:
    return html.relative_to(ROOT).parts[1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("devices", nargs="*", help="device folders (default: all)")
    ap.add_argument("--check", action="store_true", help="compare only, do not write")
    args = ap.parse_args()

    found = set()
    outdated = []
    for html, gz in pages():
        dev = device_of(html)
        if args.devices and dev not in args.devices:
            continue
        found.add(dev)
        src = html.read_bytes()
        try:
            same = gzip.decompress(gz.read_bytes()) == src
        except (OSError, EOFError):
            same = False
        if same:
            continue
        outdated.append(dev)
        if args.check:
            print(f"outdated: {gz.relative_to(ROOT)}")
            continue
        # mtime=0: identical input gives identical output (no git noise)
        packed = gzip.compress(src, compresslevel=9, mtime=0)
        gz.write_bytes(packed)
        print(f"packed: {gz.relative_to(ROOT)} ({len(src)} -> {len(packed)} bytes)")

    missing = sorted(set(args.devices) - found)
    if missing:
        print("no web page with data/ folder: " + ", ".join(missing))
        return 1
    if args.check:
        if outdated:
            print("Run: python3 tools/web/pack_web.py")
            return 1
        print("all web pages packed")
        return 0
    if not outdated:
        print("nothing to do")
    else:
        print("Bump FW_VERSION in config.h and tag: " + ", ".join(outdated))
    return 0


if __name__ == "__main__":
    sys.exit(main())
