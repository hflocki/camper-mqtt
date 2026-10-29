# AGENTS.md – Hinweise für KI-Assistenten und Mitwirkende

Monorepo für Camper-Elektronik: jedes Gerät ist ein ESP8266/ESP32 mit reiner **Arduino**-Firmware (kein ESPHome), eigener Web-Oberfläche, MQTT und Home-Assistant-Discovery. Die Geräte reden über einen MQTT-Broker (z. B. auf dem LTE-Router RUT956) miteinander; Home Assistant ist optional. Überblick und gemeinsame Regeln: [`README.md`](README.md).

## Arbeitsweise (verbindlich)

- **Sprache:** Code-Kommentare auf **Englisch**. Web-Oberflächen, READMEs und sonstige Doku auf **Deutsch**.
- **Keine persönlichen Daten** in Code, Configs oder Doku: keine Fahrzeugnamen, echten Koordinaten, Namen oder E-Mail-Adressen. Gerätenamen und IDs bleiben generisch (`camper-display`, `Camper`).
- **Zugangsdaten nie ins Repo:** Passwörter, WLAN, Broker, Tokens (z. B. Pushover) nur in `config_local.h` (per `.gitignore` ausgeschlossen, Vorlage `config_local.h.example`) oder auf dem Gerät. Beim Road Buddy ist `config.h` selbst die persönliche Datei (Vorlage `config.example.h`).
- **Git macht der Besitzer selbst.** Änderungen als Patch liefern, nicht committen oder pushen. Patch erzeugen mit `git add -A && git diff --cached --binary > name.patch`, vorher mit `git apply --check` gegen den aktuellen `main` prüfen. Angewendet wird so:
  ```
  git pull && git apply X.patch && rm X.patch && git add -A && git commit -m "..." && git push
  ```
  Commit-Messages auf Englisch, Präfix mit dem Gerät (`display: …`, `heater: …`).
- **Python-Werkzeuge** laufen in einer venv und bringen eine `requirements.txt` mit (auch wenn nur die Standardbibliothek gebraucht wird – dann steht das als Kommentar drin).
- **Keine Downloads im Repo-Hauptordner:** `.bin`, `.zip`, Bilder usw. gehören in `firmware/<gerät>/…` oder gar nicht ins Repo (`.gitignore` fängt Hauptordner-Dateien ab).
- Bei jeder Änderung an einem Gerät `FW_VERSION` in dessen `config.h` erhöhen und die README des Geräts mitpflegen.

## Aufbau

| Pfad | Inhalt |
| --- | --- |
| `firmware/<gerät>/` | ein Gerät, vollständig mit README, Arduino-Sketch unter `_arduino/<sketch>/` (Heizung: `truma_heater/`) |
| `firmware/_template/` | Muster-Gerät; neues Gerät mit `python firmware/_template/tools/new_device.py <id> "<Name>"` |
| `docs/mockups/` | klickbare Mockups aller Web-Oberflächen (erzeugt, nicht von Hand ändern) |
| `tools/mockups/` | Generator der Mockups: `python3 tools/mockups/make_mockups.py`, Beispieldaten je Gerät in `mock/<gerät>.js` |
| `.github/workflows/build.yml` | baut bei jedem Push alle Geräte; Tag `<gerät>-v<version>` erzeugt ein Release mit `.bin` |

Gemeinsames MQTT-Schema: `camper/<gerät>/…` retained, `…/status` = `online`/`offline` als Last Will, Befehle unter `…/cmd/…` bzw. `…/set/…` nicht retained.

## Bauen und prüfen

Board-Paket esp32 **3.3.x**, Bibliotheken und genaue Versionen stehen in der README jedes Geräts und in `build.yml`. FQBNs (wie in CI):

| Gerät | FQBN |
| --- | --- |
| tanklevel, battery, temp (C3), heater, maxxfan, template | `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs` |
| battery (AtomS3) | `esp32:esp32:m5stack_atoms3:CDCOnBoot=cdc,PartitionScheme=default_8MB` |
| temp (ESP8266) | `esp8266:esp8266:nodemcuv2:eesz=4M1M` |
| level | `esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=min_spiffs` |
| display | `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB` |
| roadbuddy | `esp32:esp32:m5stack_core2` |

```bash
arduino-cli compile --fqbn <fqbn> firmware/<gerät>/_arduino/<sketch>
```

### Display (`firmware/display`)

- LVGL 9.2.x. `firmware/display/_arduino/lv_conf.h` muss **neben** den `lvgl`-Ordner in `Arduino/libraries/` (CI kopiert sie dorthin). Nicht in den Sketch-Ordner verschieben.
- Web-Oberfläche: `web/index.html` bearbeiten, danach `python3 firmware/display/tools/embed_web.py` (erzeugt `web_ui.h`). Danach Mockups neu erzeugen.
- Schriften `font_de_*.c`: nur über `python3 firmware/display/tools/make_fonts.py` erzeugen (braucht Node/npx, lv_font_conv 1.5.3). Neue Font-Awesome-Icons dort eintragen, sonst erscheinen am Display Kästchen.
- Module: `ui.*` Oberfläche, `datastore.*` Werte, `mqtt_client.*`, `web.*`, `settings.*` (NVS), `notify.*` Pushover, `weather.*` Open-Meteo (eigener Task, Mutex wird beim ersten Zugriff angelegt), `icons.*` Symbole der Temperatur-/Batterie-Kachel, `otaprog.*` Fortschritt des Datei-Updates, `history.*` 24-h-Verlauf im RAM (`/api/history`), `rules.*` Automationen (`/api/rules`, NVS-Namespace `rules`).
- Alles, was die UI beim Start abfragt, muss vor `ui::begin()` initialisiert sein (sonst Boot-Schleife).

### Bluetooth-Gateway (`firmware/omg`)

OpenMQTTGateway wird nicht ins Repo kopiert: `firmware/omg/setup.py` klont es (Tag `OMG_REF`) nach `firmware/omg/OpenMQTTGateway/` (gitignored) und legt `camper_env.ini` daneben. Eigene Anpassungen nur dort bzw. als Build-Flags, nie im Upstream-Code. Bauen mit PlatformIO: `python3 setup.py build camper-ble`.

### Heizung (`firmware/heater`)

GPL-3.0 (Protokoll aus GPL-Projekten portiert). Der ESP ist LIN-**Slave** wie eine iNet Box, das CP Plus bleibt Master. Board-Profile in `truma_heater/config.h`; Heizungsmodell im Web-UI wählbar. Tests unter `tests/`.

## Lizenzen

`firmware/heater`: GPL-3.0, übrige Geräte überwiegend MIT – genaue Zuordnung in der Lizenztabelle der [`README.md`](README.md).
