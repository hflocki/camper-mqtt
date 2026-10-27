#!/usr/bin/env python3
"""Build clickable mockups of all device web pages into docs/mockups/.

Each mockup is the real page from the firmware plus a simulated device API
(tools/mockups/mock/core.js + one script per device), so the pages can be tried
in any browser without hardware. docs/mockups/index.html links them all.

Run from anywhere after changing a web page:
    python3 tools/mockups/make_mockups.py
Standard library only.
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MOCK = pathlib.Path(__file__).resolve().parent / "mock"
OUT = ROOT / "docs" / "mockups"

# id, title, description, web page source (None = built by the device's own generator)
DEVICES = [
    ("display", "Camper Display", "Wandpanel: Übersicht, Truma, MaxxFan, Level, Temperatur, Batterie, eigene Kacheln, Einstellungen",
     "firmware/display/_arduino/camper_display/web/index.html"),
    ("heater", "Truma Heizung", "iNet-Box-Emulation am CP Plus: Heizung, Warmwasser, Timer, Modellauswahl, LIN-Log",
     None),
    ("tanklevel", "Tanks", "Frischwasser, Grauwasser und Gasflasche (BLE) mit Kalibrierung",
     "firmware/tanklevel/_arduino/camper_tanklevel/web/index.html"),
    ("temp", "Temperatur", "DS18B20-Sensoren mit Min/Max und Alarmgrenzen",
     "firmware/temp/_arduino/camper_temp/web/index.html"),
    ("level", "Level", "Wasserwaage mit Keilhöhen und Tara",
     "firmware/level/_arduino/camper_level/web/index.html"),
    ("maxxfan", "MaxxFan", "Dachlüfter: Öffnen, Schließen, Stufe, Auto-Hold",
     "firmware/maxxfan/_arduino/camper_maxxfan/web/index.html"),
    ("battery", "Batterie", "BLE-Gateway für Daly, Ective und BM2",
     "firmware/battery/_arduino/camper_battery/web/index.html"),
    ("template", "Vorlage", "Muster für ein neues Gerät (firmware/_template)",
     "firmware/_template/_arduino/camper_example/web/index.html"),
]


def inject(html: str, script: str) -> str:
    """Put the simulation in front of the page's own scripts."""
    tag = "<script>\n" + script + "\n</script>\n"
    i = html.lower().find("<head>")
    if i < 0:
        raise SystemExit("no <head> found")
    i += len("<head>")
    return html[:i] + "\n" + tag + html[i:]


def build_heater() -> str:
    gen = ROOT / "firmware" / "heater" / "docs" / "gen_mockup.py"
    subprocess.run([sys.executable, str(gen)], check=True, cwd=gen.parent.parent, stdout=subprocess.DEVNULL)
    return (ROOT / "firmware" / "heater" / "docs" / "mockup" / "truma-heater-mockup.html").read_text(encoding="utf-8")


def card(dev_id, title, desc):
    return (f'<a class="dev" href="{dev_id}.html" data-id="{dev_id}">'
            f'<b>{title}</b><span>{desc}</span></a>')


def build_index():
    cards = "\n".join(card(d, t, x) for d, t, x, _ in DEVICES)
    return (MOCK / "index.html").read_text(encoding="utf-8").replace("<!--DEVICES-->", cards)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    core = (MOCK / "core.js").read_text(encoding="utf-8")
    for dev_id, _title, _desc, src in DEVICES:
        if src is None:
            html = build_heater()
        else:
            page = (ROOT / src).read_text(encoding="utf-8")
            sim = (MOCK / f"{dev_id}.js").read_text(encoding="utf-8")
            html = inject(page, sim + "\n" + core)
        (OUT / f"{dev_id}.html").write_text(html, encoding="utf-8")
        print(f"docs/mockups/{dev_id}.html")
    (OUT / "index.html").write_text(build_index(), encoding="utf-8")
    print("docs/mockups/index.html")


if __name__ == "__main__":
    main()
