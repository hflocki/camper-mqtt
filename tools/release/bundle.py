#!/usr/bin/env python3
"""Collect the firmware files listed in .github/bundle.txt into one folder for the bundle release.

Usage: bundle.py <artifact dir> <output dir>

For every listed device it copies <device>-node-<target>.bin from the build artifacts,
adds the packed web page (data/index.html.gz) as <device>-node-web.html.gz where the
device has one, and writes NOTES.md with the firmware versions. Standard library only.
"""
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIST = ROOT / ".github" / "bundle.txt"
FOLDER = {"template": "_template"}   # matrix device name -> folder under firmware/


def entries():
    for line in LIST.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            device, target = line.split()
            yield device, target


def version(device):
    base = ROOT / "firmware" / FOLDER.get(device, device)
    for name in ("config.h", "fw_id.h"):
        for f in sorted(base.glob(f"*/{name}")):
            m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', f.read_text(encoding="utf-8"))
            if m:
                return m.group(1)
    return "?"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    rows, missing = [], []
    for device, target in entries():
        name = f"{device}-node-{target}.bin"
        found = next(src.rglob(name), None)
        if not found:
            missing.append(name)
            continue
        shutil.copy2(found, out / name)
        web = sorted((ROOT / "firmware" / FOLDER.get(device, device)).glob("*/data/index.html.gz"))
        web_name = ""
        if web:
            web_name = f"{device}-node-web.html.gz"
            shutil.copy2(web[0], out / web_name)
        rows.append(f"| {device} | {version(device)} | `{name}` | {f'`{web_name}`' if web_name else '–'} |")
    if missing:
        sys.exit("missing build artifacts: " + ", ".join(missing))
    notes = [
        "All firmware files in one place, built from the same commit.",
        "",
        "| Device | Version | Firmware | Web page |",
        "| --- | --- | --- | --- |",
        *rows,
        "",
        "Install the `.bin` through the device's web page (Road Buddy: its update page). "
        "Where a web page is listed, upload the `.html.gz` afterwards as well.",
    ]
    (out / "NOTES.md").write_text("\n".join(notes) + "\n", encoding="utf-8")
    print("\n".join(rows))


if __name__ == "__main__":
    main()
