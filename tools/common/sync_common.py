#!/usr/bin/env python3
"""Copy the shared sources from firmware/_common into every sketch listed in
firmware/_common/manifest.json, so each sketch folder stays self-contained.

"blocks" in the manifest are pasted INTO a file instead (e.g. the shared CSS of the web
pages into web/index.html), between a begin and an end marker. A file without the markers
gets the block in front of "before" (e.g. "</style>").

  python3 tools/common/sync_common.py           copy changed files, list affected devices
  python3 tools/common/sync_common.py --check   only compare, exit code 1 on any difference (CI)
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMMON = ROOT / "firmware" / "_common"
MANIFEST = COMMON / "manifest.json"


def device_of(sketch: str) -> str:
    # "firmware/<device>/..." -> "<device>"
    return Path(sketch).parts[1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="compare only, do not write")
    args = ap.parse_args()

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    differing = []   # (file, sketch)
    missing = []

    for name, sketches in manifest["files"].items():
        src = COMMON / name
        if not src.is_file():
            missing.append(str(src.relative_to(ROOT)))
            continue
        data = src.read_bytes()
        for sketch in sketches:
            dst = ROOT / sketch / name
            if not (ROOT / sketch).is_dir():
                missing.append(sketch)
                continue
            if dst.is_file() and dst.read_bytes() == data:
                continue
            differing.append((name, sketch))
            if not args.check:
                dst.write_bytes(data)

    for name, spec in manifest.get("blocks", {}).items():
        src = COMMON / name
        if not src.is_file():
            missing.append(str(src.relative_to(ROOT)))
            continue
        begin, end, before = spec["begin"], spec["end"], spec["before"]
        block = begin + "\n" + src.read_text(encoding="utf-8").rstrip("\n") + "\n" + end + "\n"
        for target in spec["files"]:
            dst = ROOT / target
            if not dst.is_file():
                missing.append(target)
                continue
            text = dst.read_text(encoding="utf-8")
            if begin in text and end in text:
                a = text.index(begin)
                b = text.index(end, a) + len(end)
                if b < len(text) and text[b] == "\n":
                    b += 1
                new = text[:a] + block + text[b:]
            elif before in text:
                i = text.index(before)
                new = text[:i] + block + text[i:]
            else:
                missing.append(f"{target} (no {before!r})")
                continue
            if new == text:
                continue
            differing.append((name, str(Path(target).parent.parent)))
            if not args.check:
                dst.write_text(new, encoding="utf-8")

    for m in missing:
        print(f"missing: {m}", file=sys.stderr)

    if args.check:
        for name, sketch in differing:
            print(f"out of sync: {sketch}/{name}")
        if differing:
            print("\nEdit the file in firmware/_common/ and run: python3 tools/common/sync_common.py")
        return 1 if (differing or missing) else 0

    if differing:
        for name, sketch in differing:
            print(f"updated: {sketch}/{name}")
        devices = sorted({device_of(s) for _, s in differing} - {"_template"})
        if devices:
            print("\nBump FW_VERSION in config.h and tag: " + ", ".join(devices))
    else:
        print("all copies up to date")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
