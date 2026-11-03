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
| `fwcheck.h/.cpp` | Firmware-Kennung `CAMPER-FW:<gerät>:<version>:<chip>;` und Prüfung beim Upload (Chip, Gerät, `.merged.bin`, Größe), Fehler im Klartext. Braucht `FW_DEVICE` in `config.h`. Einbinden: `fwcheck::begin(request->contentLength(), request->hasParam("force"))` beim ersten Stück, `fwcheck::chunk()` für jedes, im Request-Handler `fwcheck::ok()` / `fwcheck::error()`; `fwcheck::info()` und `evlog::json()` unter `GET /api/system` für die Webseite (Prüfung im Browser: `fwInspect()` in den Web-Oberflächen) | alle ESP32-Geräte, temp auch ESP8266 (ohne Rückfall) |
| `evlog.h/.cpp` | Ereignis-Log im RTC-Speicher (übersteht Neustart, Absturz und Update), `evlog::begin()` als Erstes in `setup()`, `evlog::add()`, `evlog::watch(wlan, mqtt)` in `loop()` | alle ESP32-Geräte, temp auch ESP8266 (ohne Rückfall) |
| `safeguard.h/.cpp` | Rückfall auf die vorherige Firmware, wenn ein Update kein WLAN bekommt; WLAN-Änderung mit Rückweg (nur ESP32). Einbinden: `guard::begin()` am Anfang von `setup()`, `guard::loop(verbunden)` in `loop()`, `guard::otaDone()` nach erfolgreichem `Update.end()` bzw. in `ArduinoOTA.onEnd` (nur Firmware), `guard::wifiTrialStart(alteSsid, altesPasswort)` vor dem Überschreiben der WLAN-Daten, beim Start `wifiTrialPending()` / `wifiTrialOk()` / `wifiTrialRestore()`, `guard::status()` im Web-UI | alle ESP32-Geräte, temp auch ESP8266 (ohne Rückfall) |
| `fw_config.h` | holt `FW_VERSION`, `FW_DEVICE` und `GUARD_*` für die drei Module oben aus `fw_id.h` (falls vorhanden, z. B. Road Buddy, dessen `config.h` persönliche Daten enthält) oder aus `config.h` | wie oben |
| `devinfo.h/.cpp` | Diagnose-JSON für `<prefix>/info` (Version, IP, MAC, RSSI, Laufzeit, Reset-Grund, freier Speicher) und Zeitsteuerung `devinfoDue()` | alle Geräte |

Beim `wifi_manager` haben tanklevel, maxxfan, heater, display und toolbox noch eigene, abweichende Fassungen. Sie werden einzeln umgestellt, jeweils mit Test am Gerät.
