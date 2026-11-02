# Gemeinsamer Code

Hier liegt das **Original** von Quelldateien, die mehrere Geräte benutzen. Jeder Sketch-Ordner bekommt eine Kopie, damit er für sich allein in der Arduino IDE kompiliert – Nachbauer brauchen nur den Ordner ihres Geräts.

## Regeln

- Gemeinsame Dateien **nur hier** ändern, nie in einem Sketch-Ordner. Die GitHub Action bricht ab, sobald eine Kopie vom Original abweicht.
- Welche Datei in welchen Sketch gehört, steht in [`manifest.json`](manifest.json).
- Nach jeder Änderung die Kopien aktualisieren:

  ```bash
  python3 -m venv .venv && source .venv/bin/activate
  pip install -r tools/common/requirements.txt
  python3 tools/common/sync_common.py
  ```

  Das Skript listet die betroffenen Geräte. Bei jedem davon `FW_VERSION` erhöhen und taggen (siehe `AGENTS.md`).
- Nur prüfen, ohne zu schreiben: `python3 tools/common/sync_common.py --check`.

## Inhalt

| Datei | Zweck | Geräte |
| --- | --- | --- |
| `wifi_manager.h/.cpp` | WLAN mit Access-Point-Fallback, Reset-Grund, Sendeleistung (ESP32 und ESP8266) | template, temp, battery, level |

Die Geräte tanklevel, maxxfan, heater und display haben noch eigene, abweichende Fassungen. Sie werden einzeln umgestellt, jeweils mit Test am Gerät.
