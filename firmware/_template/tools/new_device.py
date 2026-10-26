#!/usr/bin/env python3
"""Create a new camper-mqtt device from firmware/_template.

Usage (from the repo root, inside a venv - see ../ANLEITUNG.md):
    python firmware/_template/tools/new_device.py <id> "<Name>"

    <id>    short name, a-z 0-9 _ (e.g. "waste"); becomes the folder firmware/<id>,
            the sketch camper_<id>, the MQTT prefix camper/<id> and the host name camper-<id>
    <Name>  display name (e.g. "Abwasser"); becomes "Camper Abwasser" in the web UI and in
            Home Assistant and "Camper-Abwasser-XXXXXX" for the setup access point

Only the Python standard library is used. Nothing is overwritten: the script stops if
firmware/<id> already exists.
"""

import argparse
import gzip
import re
import shutil
import sys
from pathlib import Path

TEMPLATE = Path(__file__).resolve().parent.parent          # firmware/_template
FIRMWARE = TEMPLATE.parent                                 # firmware/
SKIP = {"tools", "ANLEITUNG.md"}                           # template-only files
TEXT_SUFFIXES = {".h", ".cpp", ".ino", ".html", ".md", ".example", ".txt", ".ini", ".py", ""}


def replacements(dev_id: str, name: str) -> list[tuple[str, str]]:
    dash = dev_id.replace("_", "-")
    compact = re.sub(r"\s+", "", name)
    # Order matters: longer / more specific tokens first
    return [
        ("camper_example", f"camper_{dev_id}"),
        ("camper-example", f"camper-{dash}"),
        ("camper/example", f"camper/{dev_id}"),
        ("Camper-Example", f"Camper-{compact}"),
        ("Camper Example", f"Camper {name}"),
        ("example1234", f"{dash.replace('-', '')}1234"),
    ]


def main() -> int:
    ap = argparse.ArgumentParser(description="New camper-mqtt device from the template")
    ap.add_argument("id", help="short name, a-z 0-9 _ (e.g. waste)")
    ap.add_argument("name", help='display name (e.g. "Abwasser")')
    args = ap.parse_args()

    dev_id = args.id.strip().lower()
    name = args.name.strip()
    if not re.fullmatch(r"[a-z][a-z0-9_]{1,20}", dev_id):
        print("id: 2-21 characters, a-z 0-9 _, starting with a letter", file=sys.stderr)
        return 1
    if not name or len(name) > 24:
        print("name: 1-24 characters", file=sys.stderr)
        return 1

    target = FIRMWARE / dev_id
    if target.exists():
        print(f"{target} already exists - nothing changed", file=sys.stderr)
        return 1

    reps = replacements(dev_id, name)

    def convert(text: str) -> str:
        for old, new in reps:
            text = text.replace(old, new)
        return text

    for src in TEMPLATE.rglob("*"):
        rel = src.relative_to(TEMPLATE)
        if rel.parts[0] in SKIP or src.name == "config_local.h":
            continue
        dst = target / Path(convert(str(rel)))
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        if src.suffix in TEXT_SUFFIXES and src.suffix != ".gz":
            dst.write_text(convert(src.read_text(encoding="utf-8")), encoding="utf-8")
        else:
            shutil.copy2(src, dst)

    # The web page carries the device name, so its gzip copy is rebuilt from web/index.html
    sketch = target / "_arduino" / f"camper_{dev_id}"
    page = (sketch / "web" / "index.html").read_bytes()
    (sketch / "data" / "index.html.gz").write_bytes(gzip.compress(page, compresslevel=9, mtime=0))

    print(f"Created {target.relative_to(FIRMWARE.parent)}")
    print(f"  Sketch:      {sketch.relative_to(FIRMWARE.parent)}/camper_{dev_id}.ino")
    print(f"  MQTT prefix: camper/{dev_id}    Host: camper-{dev_id.replace('_', '-')}.local")
    print()
    print("Next steps (details in firmware/_template/ANLEITUNG.md):")
    print(f"  1. cp {sketch.relative_to(FIRMWARE.parent)}/config_local.h.example "
          f"{sketch.relative_to(FIRMWARE.parent)}/config_local.h  and fill it in")
    print("  2. sensor.cpp: VALUES[], sensorInit(), measure()  (look for TODO)")
    print("  3. after changing web/index.html: gzip -9 -n -c web/index.html > data/index.html.gz")
    print("  4. CI: add the device to .github/workflows/build.yml (snippet in ANLEITUNG.md)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
