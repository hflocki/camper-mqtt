# camper-mqtt – Stand 02.11.2026 abends

Repo: https://github.com/hflocki/camper-mqtt · Regeln: `AGENTS.md` · private Roadmap: `plan/ROADMAP.md`

Lokal kompilieren war in dieser Umgebung nicht möglich (Download-Server des ESP32-Pakets gesperrt). Geprüft: Syntax der neuen Module gegen die echten Header, Webseite im Mockup mit Chrome ohne JS-Fehler, Simulator/Recorder gegen einen lokalen Broker. Die echten Builds macht GitHub Actions – bei rotem Lauf das Log mitbringen.

## Patches aus diesem Chat (in dieser Reihenfolge)

| Patch | Inhalt | Tags |
| --- | --- | --- |
| `common-wifi.patch` | `firmware/_common/` (Original gemeinsamer Dateien, `manifest.json`), `tools/common/sync_common.py`, CI-Job `common` (`--check`) | keine (eingespielt) |
| `devinfo-followup.patch` | `devinfo.h/.cpp` in allen 9 Sketches, `<prefix>/info` jede Minute; Road Buddy neu mit Last Will `camper/roadbuddy/status` | battery-v1.3.2, display-v2.9.13, heater-v1.3.1, level-v2.1.1, maxxfan-v1.5.1, roadbuddy-v1.2.3, tanklevel-v1.6.1, temp-v1.2.1 |
| `warnings-devices-tools.patch` | Display: Geräteliste, Grenzwert-Warnungen mit Hysterese, Pushover-Ruhezeit; `tools/mqtt/` | display-v2.9.14 |
| `outdoor-awning.patch` | Display: Außensensor wählbar, Markise mit Wind-Warnung | display-v2.9.15 |

Noch offen: prüfen, ob alle Tags gepusht sind und die Releases mit `.bin` unter https://github.com/hflocki/camper-mqtt/releases stehen.

## Fertig in diesem Chat

**Gemeinsamer Code (Variante B)**
- Original in `firmware/_common/`, Kopien in den Sketches, jeder Sketch bleibt allein flashbar.
- Ändern nur im Original, dann `python3 tools/common/sync_common.py` (nennt die Geräte für Versionssprung/Tag). CI bricht bei Abweichung ab.
- `wifi_manager.*` gemeinsam für template, temp, battery, level. `devinfo.*` für alle Geräte.

**Info-Topic** `<prefix>/info` (retained, beim Verbinden + jede Minute): `version`, `ip`, `mac`, `rssi`, `uptime`, `reset`, `heap`.

**Display 2.9.13 → 2.9.15**
- Geräteliste (`netdev.*`): liest `camper/+/status` und `camper/+/info`, System-Seite am Display (bis 10 Zeilen) und auf der Webseite, `/api/devices`.
- Warnungen (`alerts.*`, Einstellungen → 🌦 Dienste → ⚠️ Warnungen): Frischwasser < 10 %, Gas < 15 %, Batterie < 20 % (einstellbar, ersetzt `BAT_WARN_SOC` als Vorgabe), Frost (ab Werk aus), Hysterese für %/Temperatur/Böen, Pushover-Ruhezeit 30 min.
- Zigbee/BLE-Klimasensoren: Feld „Warnung über °C“ (Kühl-/Gefrierschrank).
- Außentemperatur: Sensor wählbar (eigener Klimasensor oder camper-temp), gilt für Wetter-Seite, Wetter-Kachel, Frost. Leer = wie bisher erster Sensor mit Haken „Wetter“.
- Markise: Zustand per Knopf auf der Wetter-Seite (Display + Webseite), per MQTT `camper/display/cmd/awning` = ON/OFF/TOGGLE, Status `camper/display/awning`; später wahlweise ein Zigbee-Kontakt. Warnung „Markise einfahren“ ab 40 km/h Böen (eigene Grenze).
- Einstellungs-Sicherung gab es schon (System → Sichern/Einspielen).

**Werkzeuge** `tools/mqtt/` (venv, `requirements.txt`: paho-mqtt)
- `sim.py`: Szenarien normal, drive, tilt, empty, offline, fridge, battery, frost, replay. `--skip`, `--cleanup`, `--contact/--fridge/--outdoor <IEEE>`.
- `record.py`: Mitschnitt als JSONL (`*.jsonl` im Hauptordner gitignored).

## Wichtige Dateien

| Bereich | Datei | Inhalt |
| --- | --- | --- |
| Gemeinsam | `firmware/_common/manifest.json` | welche Datei in welchen Sketch |
| Gemeinsam | `tools/common/sync_common.py` | kopieren / `--check` |
| Display | `alerts.cpp/.h` | Hysterese-Helfer `below/above/latch`, Wasser/Gas/Frost/Markise, `awning()`/`setAwning()` |
| Display | `netdev.cpp/.h` | Geräteliste |
| Display | `sensors.cpp` | `outdoor()` mit gewähltem Sensor, `contactState()`, Klimasensor `tmax` |
| Display | `notify.cpp` | Ruhezeit `poQuiet` |
| Display | `ui.cpp` | `refreshNetDevices()`, Markisen-Karte auf der Wetter-Seite, Batterie-Grenze `settings.wnBat` |
| Display | `web/index.html` → `web_ui.h` | Abschnitt ⚠️ Warnungen, `fillOutAwn()`, `loadDevices()`, Markisen-Karte. Danach `embed_web.py` und `make_mockups.py` |
| Werkzeuge | `tools/mqtt/sim.py`, `record.py` | Simulator, Recorder |

## Offen

**Praxis-Checks (Ende der Woche)**
- Firmware flashen auf maxxfan, level, tanklevel und mit dem Router testen.
- Geräteliste: tauchen alle Geräte mit Version auf? Road Buddy offline beim Schlafen ok?
- Warnungen im Testnetz mit `sim.py` durchspielen (empty, fridge, battery, offline).
- Markise: Knopf am Display und auf der Webseite, Warnung bei Böen.
- Außensensor-Auswahl mit einem camper-temp-Sensor.
- Aus dem vorigen Chat: Road Buddy Level-Seite (Farbe, Richtung, Toleranz), Display Level-Seite 480×480, Update über WLAN am Road Buddy, Webseite am Handy, Release display-v2.9.9 löschen.

**Als Nächstes umsetzen**
- Abfahrtscheck: Daten sind bekannt. Der RUT956 sendet retained auf `camper/rut` ein JSON mit `TIME` und `GPS` (`latitude`, `longitude`, `altitude`, `speed` – vermutlich km/h, im Stand 0 –, `accuracy`, `satellites`, `fix_status`, `timestamp`). Tür als JSON auf `camper/door`: `{"Door":{"name":"din2","type":"Digital In","state":"close"|"open","custom_name":"Door"}}`. Fahrt = `GPS.speed` > ~5 km/h für > 10 s bei `fix_status` = 1. Markise ist schon vorhanden. Einheit von `speed` bei der ersten Fahrt bestätigen.
- Aufräumen am Broker: retained Topics vom Simulator `camper/router/gps` und `camper/router/mobile` löschen (`mosquitto_pub -t <topic> -r -n`); alte Topics `camper/waterlevel`, `camper/gaslevel`, `camper/truma` prüfen und ggf. löschen.
- `docs/TOPICS.md` mit allen Topics.
- `wifi_manager` von tanklevel, maxxfan, heater, display auf das gemeinsame Original umstellen (einzeln, mit Test).
- Gemeinsames PlatformIO-Skript.

**Zurückgestellt:** Phase 3 Reichweite.

**Aus der früheren Liste weiter offen:** battery-v1.3.1 prüfen (jetzt 1.3.2), Zigbee-Praxistest, Beispielseite „Zigbee“ löschen, Energie-Seite (Mitschnitt PJ-1203-W mit `record.py`), Heki über RV-Tech, CampMatic-Topics, Heizung am LIN-Bus, OMG bauen, Markisenlicht als Automation, Spannungsbereich der Segmentleiste, Kachel-Optik der Webseite, SVG→C-Werkzeug.

## Für den nächsten Chat

Repo-Link, `AGENTS.md`, diese Datei, ggf. `plan/ROADMAP.md`, Fotos vom Display/Road Buddy, bei Fehlern das Actions-Log, für den Abfahrtscheck `router.jsonl`.
