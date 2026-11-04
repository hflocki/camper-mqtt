# AGENTS.md – Hinweise für KI-Assistenten und Mitwirkende

Monorepo für Camper-Elektronik: jedes Gerät ist ein ESP8266/ESP32 mit reiner **Arduino**-Firmware (kein ESPHome), eigener Web-Oberfläche, MQTT und Home-Assistant-Discovery. Die Geräte reden über einen MQTT-Broker (z. B. auf dem LTE-Router RUT956) miteinander; Home Assistant ist optional. Überblick und gemeinsame Regeln: [`README.md`](README.md).

## Arbeitsweise (verbindlich)

- **Sprache:** Code-Kommentare auf **Englisch**. Web-Oberflächen, Geräte-READMEs und sonstige Doku auf **Deutsch**. Die Haupt-README gibt es zweimal: `README.md` (Englisch) und `README.de.md` (Deutsch) – Änderungen immer in beiden.
- **Keine persönlichen Daten** in Code, Configs oder Doku: keine Fahrzeugnamen, echten Koordinaten, Namen oder E-Mail-Adressen. Gerätenamen und IDs bleiben generisch (`camper-display`, `Camper`).
- **Zugangsdaten nie ins Repo:** Passwörter, WLAN, Broker, Tokens (z. B. Pushover) nur in `config_local.h` (per `.gitignore` ausgeschlossen, Vorlage `config_local.h.example`) oder auf dem Gerät. Beim Road Buddy ist `config.h` selbst die persönliche Datei (Vorlage `config.example.h`).
- **Git macht der Besitzer selbst.** Änderungen als Patch liefern, nicht committen oder pushen. Patch erzeugen mit `git add -A && git diff --cached --binary > name.patch`, vorher mit `git apply --check` gegen den aktuellen `main` prüfen. Angewendet wird so:
  ```
  git pull && git apply X.patch && rm X.patch && git add -A && git commit -m "..." && git push && git tag <gerät>-v<version> && git push origin <gerät>-v<version>
  ```
  Commit-Messages auf Englisch, Präfix mit dem Gerät (`display: …`, `heater: …`).
- **Immer mit Release:** Der gelieferte Befehl endet mit Tag und Tag-Push für **jedes** im Patch geänderte Gerät (`git tag display-v2.9.1 && git tag battery-v1.3.2 && git push origin display-v2.9.1 && git push origin battery-v1.3.2`). **Jeden Tag einzeln pushen:** Kommen mehr als drei Tags in einem Push, startet GitHub für keinen davon einen Workflow, dann fehlen die Releases.. Der Tag löst in GitHub Actions das Release mit der `.bin` aus. Ein Tag gilt nur einmal: jeder Patch, der ein Gerät ändert, erhöht dessen `FW_VERSION`, auch bei Folge-Patches am selben Tag. Patches nur für Doku/Werkzeuge ohne Firmware-Änderung bekommen keinen Tag.
- Folge-Patches setzen auf dem zuletzt gelieferten Stand auf (nicht auf `main`), wenn der Besitzer ihn schon angewendet hat; vorher mit `git apply --check` gegen genau diesen Stand prüfen.
- **Python-Werkzeuge** laufen in einer venv und bringen eine `requirements.txt` mit (auch wenn nur die Standardbibliothek gebraucht wird – dann steht das als Kommentar drin).
- **Keine Downloads im Repo-Hauptordner:** `.bin`, `.zip`, Bilder usw. gehören in `firmware/<gerät>/…` oder gar nicht ins Repo (`.gitignore` fängt Hauptordner-Dateien ab).
- Bei jeder Änderung an einem Gerät `FW_VERSION` in dessen `config.h` erhöhen und die README des Geräts mitpflegen.
- **Nur Arduino:** Firmware wird mit der Arduino IDE bzw. `arduino-cli` gebaut, keine `platformio.ini` für eigene Geräte. Einzige Ausnahme ist `firmware/omg` (OpenMQTTGateway, fremdes PlatformIO-Projekt).
- **Firmware-Kennung:** Alle Geräte haben `fwcheck.*`, `evlog.*` und `safeguard.*` und brauchen `FW_DEVICE` in `config.h` (Road Buddy: `fw_id.h`), gleich der Geräte-ID im Tag. Die Prüfung im Browser (`fwInspect()`) und im Gerät (`fwcheck.cpp`) müssen dieselben Regeln haben.
- **Web-Oberflächen:** `web/index.html` ist die Quelle, nach jeder Änderung `python3 tools/web/pack_web.py` (packt nach `data/index.html.gz`); CI prüft mit `--check`. Das Display bettet seine Seite stattdessen per `embed_web.py` ein.

## Aufbau

| Pfad | Inhalt |
| --- | --- |
| `firmware/<gerät>/` | ein Gerät, vollständig mit README, Arduino-Sketch unter `<sketch>/` (Ordnername = Name der `.ino`, z. B. `camper_temp/`; Heizung: `truma_heater/`) |
| `firmware/_common/` | Original der gemeinsamen Quelldateien (z. B. `wifi_manager.*`); nur dort ändern, dann `python3 tools/common/sync_common.py` kopiert sie in die Sketches (Liste in `manifest.json`), CI prüft mit `--check` |
| `firmware/_template/` | Muster-Gerät; neues Gerät mit `python firmware/_template/tools/new_device.py <id> "<Name>"` |
| `docs/mockups/` | klickbare Mockups aller Web-Oberflächen (erzeugt, nicht von Hand ändern) |
| `tools/mqtt/` | Simulator `sim.py` (Szenarien für alle Geräte, Wiedergabe von Mitschnitten) und Recorder `record.py`, Python mit venv; dasselbe als Gerät mit Web-Oberfläche: `firmware/emulator` |
| `tools/web/` | `pack_web.py`: packt `web/index.html` → `data/index.html.gz` aller Geräte, `--check` für CI |
| `tools/mockups/` | Generator der Mockups: `python3 tools/mockups/make_mockups.py`, Beispieldaten je Gerät in `mock/<gerät>.js` |
| `.github/workflows/build.yml` | baut bei jedem Push alle Geräte; Tag `<gerät>-v<version>` erzeugt ein Release mit `.bin` |

Gemeinsames MQTT-Schema: `camper/<gerät>/…` retained, `…/status` = `online`/`offline` als Last Will, `…/info` = Diagnose-JSON (`devinfo.*` aus `firmware/_common`), Befehle unter `…/cmd/…` bzw. `…/set/…` nicht retained. Alle Topics stehen in [`docs/TOPICS.md`](docs/TOPICS.md); neue oder geänderte Topics dort mitpflegen.

## Bauen und prüfen

Board-Paket esp32 **3.3.x**, Bibliotheken und genaue Versionen stehen in der README jedes Geräts und in `build.yml`. FQBNs (wie in CI):

| Gerät | FQBN |
| --- | --- |
| tanklevel, battery, temp (C3), heater (C3), maxxfan, emulator, toolbox, template | `esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=min_spiffs` |
| heater (ESP32 DevKit) | `esp32:esp32:esp32:PartitionScheme=min_spiffs` |
| battery (AtomS3) | `esp32:esp32:m5stack_atoms3:CDCOnBoot=cdc,PartitionScheme=default_8MB` |
| temp (ESP8266) | `esp8266:esp8266:nodemcuv2:eesz=4M1M` |
| level | `esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=min_spiffs` |
| display | `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB` |
| roadbuddy | `esp32:esp32:m5stack_core2` |

```bash
arduino-cli compile --fqbn <fqbn> firmware/<gerät>/<sketch>
```

### Display (`firmware/display`)

- LVGL 9.2.x. `firmware/display/lv_conf.h` muss **neben** den `lvgl`-Ordner in `Arduino/libraries/` (CI kopiert sie dorthin). Nicht in den Sketch-Ordner verschieben.
- Web-Oberfläche: `web/index.html` bearbeiten, danach `python3 firmware/display/tools/embed_web.py` (erzeugt `web_ui.h`). Danach Mockups neu erzeugen.
- Schriften `font_de_*.c`: nur über `python3 firmware/display/tools/make_fonts.py` erzeugen (braucht Node/npx, lv_font_conv 1.5.3). Neue Font-Awesome-Icons dort eintragen, sonst erscheinen am Display Kästchen.
- Module: `ui.*` Oberfläche, `datastore.*` Werte, `mqtt_client.*`, `web.*`, `settings.*` (NVS), `notify.*` Pushover, `weather.*` Open-Meteo (eigener Task, Mutex wird beim ersten Zugriff angelegt), `icons.*` Symbole der Temperatur-/Batterie-Kachel, `otaprog.*` Fortschritt des Datei-Updates, `history.*` 24-h-Verlauf im RAM (`/api/history`), `rules.*` Automationen (`/api/rules`, NVS-Namespace `rules`), `router.*` Mobilfunk-Signal des Routers per MQTT (Balken in der Kopfzeile), `alerts.*` eingebaute Grenzwert-Warnungen (Wasser, Gas, Frost) und Hysterese-Helfer `alerts::below/above/latch` für alle Schwellen-Warnungen, `netdev.*` Geräteliste aus `<root>/+/status` und `<root>/+/info` (`/api/devices`, Systemseite), `depart.*` Abfahrtscheck (Fahrt aus dem Router-GPS `camper/rut`, Tür `camper/door`, Markise, Kontakte; `<display>/driving`, `<display>/depart`).
- Alles, was die UI beim Start abfragt, muss vor `ui::begin()` initialisiert sein (sonst Boot-Schleife).

### Road Buddy (`firmware/roadbuddy`)

- Zeichnet mit M5GFX (kein LVGL), übernimmt aber Optik und Logik vom Display: Farben `COL_*` und `SEG_COL` sind die `C_*`-Werte aus `display/.../ui.cpp`. Beim Ändern der Palette oder der Schwellen (Tanks, Segmentleiste) beide Geräte anpassen.
- Bleibt reiner Empfänger und Kurzblick fürs Ankommen (Level, Wasser, Gas, Temperatur, Batterie, System), keine Steuerung.
- Arduino-Präprozessor: Typen, die in Funktionsparametern vorkommen (z. B. `enum Glyph`), vor der ersten Funktion deklarieren; keine Default-Argumente.
- `config.h` darf nur von `roadbuddy.ino` eingebunden werden (definiert Variablen). Zugangsdaten landen zusätzlich im NVS, damit die CI-`.bin` per Update-Seite (`webota.cpp`) installiert werden kann.

### Emulator (`firmware/emulator`)

Testgerät, das die fest verbauten Geräte nachbildet. Alles Gerätespezifische steht in `emu.cpp`: Feldtabelle `FIELDS[]` (Werte und Einstellungen, daraus baut die Webseite ihre Karten), Builder je Gerät (`build*`, müssen dieselben Topics und Formate senden wie die echte Firmware), Befehle (`cmd*`) und Szenarien (JSON-Format wie beim Upload, Standard-Szenarien in `BUILTIN_SCEN`, hochgeladene als `/scen/<id>.json` im LittleFS; Format in der README des Emulators). `mqtt_ha.cpp` hält eine Broker-Verbindung je Gerät mit eigenem Last Will plus die eigene (Router, Zigbee) und löscht Topics, die ein Gerät nicht mehr sendet. Ändert sich ein Topic oder Format an einem echten Gerät, den Builder hier mitziehen und `firmware/emulator/tests/run.sh` laufen lassen (Host-Test, braucht nur g++ und ArduinoJson). Die Beispieldaten des Mockups (`tools/mockups/mock/emulator.js`) kommen aus `tests/run.sh --schema`.

### Toolbox (`firmware/toolbox`)

Bluetooth-Geräte als Plugins auf einem ESP32 (früher eigenes Repo `camper_toolbox`). Kern unter `src/core/` (WLAN mit eigenem `wifi_manager`, MQTT/HA, Webserver, BLE-Scan `ble.*`, BLE-Verbindung `ble_link.*`, Plugin-Basis `plugin.h`, `registry.*`, `settings.*`), Gerätetreiber unter `src/plugins/`. Die gemeinsamen Module aus `firmware/_common` liegen im Sketch-Hauptordner und werden aus `src/` mit `../../` eingebunden.

- **Neues Gerät = neues Plugin:** Klasse von `Plugin` ableiten (`plugin.h`), Felder (`fields()`), HA-Entitäten (`entities()`), `publish()`, `command()`, `live()` für die Web-Oberfläche; Fabrik `create<Name>(index)` in der Plugin-Datei, Schalter `PLUGIN_<NAME>` in `config.h` (Zahl = Instanzen, 0 = nicht einkompiliert, dazu in `CT_BLE_PASSIVE` bzw. `CT_BLE_LINKS` eintragen) und Deklaration plus `ADD_PLUGINS` in `registry.cpp`. Die Web-Oberfläche baut Karten und Einstellungen aus `live()` und `fields()`, braucht also meist keine Änderung.
- Höchstens 3 Plugins mit BLE-Verbindung (Pumpe, Dachhaube, Kühlschrank) pro ESP, passive Sensoren beliebig (`#error` in `config.h`).
- Standard in `config.h` = Hardware des Besitzers (nur `PLUGIN_RVTECH 1`), damit die Release-`.bin` passt; andere schalten Plugins in `config_local.h` zu.
- Neue Plugin-Topics in `docs/TOPICS.md` eintragen.

### Bluetooth-Gateway (`firmware/omg`)

OpenMQTTGateway wird nicht ins Repo kopiert: `firmware/omg/setup.py` klont es (Tag `OMG_REF`) nach `firmware/omg/OpenMQTTGateway/` (gitignored) und legt `camper_env.ini` daneben. Eigene Anpassungen nur dort bzw. als Build-Flags, nie im Upstream-Code. Bauen mit PlatformIO: `python3 setup.py build camper-ble`.

### Heizung (`firmware/heater`)

GPL-3.0 (Protokoll aus GPL-Projekten portiert). Der ESP ist LIN-**Slave** wie eine iNet Box, das CP Plus bleibt Master. Board-Profile in `truma_heater/config.h`; Heizungsmodell im Web-UI wählbar. Tests unter `tests/`.

## Lizenzen

`firmware/heater`: GPL-3.0, übrige Geräte überwiegend MIT – genaue Zuordnung in der Lizenztabelle der [`README.md`](README.md).
