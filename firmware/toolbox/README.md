> [!WARNING]
> **IN ENTWICKLUNG.** Die Plugins sind teils mangels Hardware noch nicht am echten Gerät getestet, auch die Dachhaube (RV Tech) noch nicht.

# Camper Toolbox

Ein ESP32 als **Bluetooth-Gateway mit Plugins**: Er liest Bluetooth-Geräte im Camper aus bzw. steuert sie und stellt alles über eine Web-Oberfläche und MQTT (mit Home-Assistant-Discovery) bereit. Es wird nichts verdrahtet, nur ein ESP32 mit Strom.

Jedes Gerät ist ein **Plugin**. Welche in die Firmware kommen, legst du in `config.h` bzw. `config_local.h` fest. Neue Bluetooth-Geräte kommen als weiteres Plugin dazu und nutzen den fertigen Rahmen (WLAN, MQTT, Web-Oberfläche, Bluetooth-Verbindung, Updates).

Früher ein eigenes Repo (`hflocki/camper_toolbox`), jetzt Teil von camper-mqtt mit derselben Update-Absicherung wie die übrigen Geräte.

Wie die Web-Oberfläche aussieht, zeigt das klickbare Mockup [`docs/mockups/toolbox.html`](../../docs/mockups/toolbox.html) (herunterladen und im Browser öffnen).

## Plugins

Zahl = Anzahl der Geräte, `0` = nicht einkompiliert. Ab Werk (und in der Release-`.bin`) ist nur die Dachhaube aktiv.

| Schalter | Gerät | Bluetooth | Ab Werk | Einstellungen in der Web-UI |
| --- | --- | --- | --- | --- |
| `PLUGIN_RVTECH` | Dachhaube RV Tech (Heki-Motorsteuerung) | Verbindung, ggf. mit PIN | **1** | MAC, PIN |
| `PLUGIN_CARAWATER` | Wasserpumpe CaraWater (ISP 15) | Verbindung | 0 | MAC |
| `PLUGIN_WCS_FRIDGE` | WCS Kompressor-Kühlschrank | Verbindung, ggf. mit PIN | 0 | MAC, PIN, optional UUIDs / Abfrage |
| `PLUGIN_DIMES` | Gas: Rotarex DIMES / SRG WAVE | passiv (nur mithören) | 0 | MAC, Flaschengröße, Füllstopp |
| `PLUGIN_MOPEKA_PRO` | Gas: Mopeka Pro (Pro Check, Universal …) | passiv | 0 | MAC, Leer-/Voll-Höhe |
| `PLUGIN_MOPEKA_STD` | Gas: Mopeka Standard / Std Check | passiv | 0 | MAC, Leer-/Voll-Höhe, Propan-Anteil |

- Passive Sensoren: beliebig viele. Geräte mit Verbindung: höchstens **3** pro ESP32 (sonst bricht der Build mit einer Meldung ab).
- Zweite Instanz, z. B. zwei Dachhauben: `#define PLUGIN_RVTECH 2` → „Dachhaube“ und „Dachhaube 2“ mit eigener MAC und PIN.
- Jedes einkompilierte Plugin lässt sich zusätzlich in der Web-UI (Tab **System**) aus- und einschalten.
- Gas ist im camper-mqtt-System schon Teil von [`tanklevel`](../tanklevel). Die Gas-Plugins hier sind für alle, die nur Bluetooth-Geräte ohne Tankgeber haben.

Beispiel `config_local.h` mit Pumpe und Kühlschrank zusätzlich:

```cpp
#define PLUGIN_CARAWATER  1
#define PLUGIN_WCS_FRIDGE 1
```

## Hardware

**ESP32-C3 Super Mini** (so baut die CI), ESP32-S3 und klassischer ESP32 gehen ebenso. Es wird nichts angeschlossen. Der ESP sollte in Bluetooth-Reichweite der Geräte sitzen, für die Dachhaube also oben im Fahrzeug.

## Arduino IDE

**Board-Einstellungen**

| Einstellung | Wert |
| --- | --- |
| Board | ESP32C3 Dev Module |
| USB CDC On Boot | **Enabled** |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** – WLAN und Bluetooth brauchen gut 1,4 MB |

Board-Paket: esp32 von Espressif Systems, 3.3.x (CI: 3.3.2).

**Bibliotheken**

| Bibliothek | Autor | Version |
| --- | --- | --- |
| PubSubClient | Nick O'Leary | 2.8 |
| ArduinoJson | Benoit Blanchon | 7.4.2 |
| ESP Async WebServer | ESP32Async | 3.12.1 |
| Async TCP | ESP32Async | 3.5.0 |
| NimBLE-Arduino | h2zero | 2.5.1 |

**Flashen**

1. `config_local.h.example` nach `config_local.h` kopieren und WLAN, MQTT und Passwörter eintragen (die Datei geht nicht nach GitHub).
2. `camper_toolbox/camper_toolbox.ino` öffnen, Board-Einstellungen prüfen, per USB hochladen.
3. `http://<ip>/` öffnen. Solange die Web-Oberfläche fehlt, steht dort ein Upload-Formular: `data/index.html.gz` hochladen.
4. Ab dann Updates nur noch über die Web-UI (Tab **System → Firmware- & Web-Update**).

Ohne eingetragenes WLAN öffnet der ESP das Setup-WLAN `Camper-Toolbox-XXXXXX`, die Seite liegt dann unter `http://192.168.4.1`.

Nach einer Änderung an `web/index.html` im Repo-Hauptordner `python3 tools/web/pack_web.py toolbox` ausführen (packt nach `data/index.html.gz`).

## Bluetooth-Geräte einrichten

**MAC-Adresse und PIN werden in der Web-UI eingetragen**, in den Einstellungen des jeweiligen Plugins, nicht in `config_local.h`.

1. Plugin aktivieren (Standard bzw. `config_local.h`) und flashen.
2. Web-UI → Plugin → **„Suchen“** → Gerät aus der Liste wählen (übernimmt die MAC).
3. Hat das Gerät eine **PIN** (Dachhaube, Kühlschrank): eintragen (1–6 Ziffern, auch `000000`), dieselbe wie in der Hersteller-App.
4. **Handy-App trennen** (App schließen oder Bluetooth am Handy aus): Die Geräte erlauben nur eine Verbindung. Später kann das Handy über **„Bluetooth für App freigeben“** wieder ran (endet nach 30 min von selbst).

| Status | Bedeutung / was tun |
| --- | --- |
| `Nicht erreichbar` | Gerät nicht in Reichweite oder noch mit dem Handy verbunden |
| `Kopplung fehlgeschlagen` | PIN falsch oder Gerät noch an ein Handy gekoppelt |
| `PIN nötig?` | Gerät verlangt eine Kopplung, aber keine (oder eine falsche) PIN eingetragen |
| `Falsches Gerät?` | Erwartete Bluetooth-Dienste nicht gefunden – andere MAC gewählt? |
| `Warte auf Daten` | Verbunden, aber noch keine Werte |
| `Verbunden` | Alles in Ordnung |

## MQTT / Home Assistant

Basis-Topic `camper/toolbox` (in der Web-UI änderbar). Jedes Plugin hat eigene Topics unter `camper/toolbox/<plugin>/…`, Befehle unter `camper/toolbox/cmd/<plugin>/<key>/set`. Dazu `…/status` (Last Will) und `…/info` (Diagnose) wie bei allen Geräten. Alle Topics: [`docs/TOPICS.md`](../../docs/TOPICS.md#toolbox-campertoolbox).

**Dachhaube** (`rvtech`):

| Topic | Inhalt |
| --- | --- |
| `camper/toolbox/rvtech/cover` | `open` / `closed` / `opening` / `closing` |
| `camper/toolbox/rvtech/cover_position` | 0 … 100 % |
| `camper/toolbox/cmd/rvtech/cover/set` | `OPEN` / `CLOSE` |
| `camper/toolbox/cmd/rvtech/cover_position/set` | `0` … `100` |
| `camper/toolbox/cmd/rvtech/regen/set` | Voreinstellung Regen (ebenso `auf`, `zu`, `lueften`) |

In Home Assistant erscheint ein Gerät „Camper Toolbox“ (WLAN, Status) und je Plugin ein eigenes Gerät, die Dachhaube als Abdeckung (`cover`).

> Wer die Toolbox schon aus dem alten Repo nutzt: Das Basis-Topic war `camper/tool`, der Hostname `camper-tool`. Ein Gerät mit gespeicherten Einstellungen behält sein altes Topic, bis es in der Web-UI geändert wird. Ändert man es, legt Home Assistant die Entitäten neu an.

### Dachhaube am Display

Ab camper-display 2.10.3 hat das Display eine eigene Seite **Dachhaube** und eine kleine Kachel für die Übersicht (beide ab Werk aus, einschalten unter *Einstellungen → Anzeige*). Sie lesen `camper/toolbox/rvtech/…` und senden die Voreinstellungen und die Position an `camper/toolbox/cmd/rvtech/…`. Eigene Kacheln dafür braucht es nicht mehr; wer sie angelegt hat, kann sie löschen.

- Basis-Topic am Display: *Einstellungen → Topics der Geräte → Toolbox* (Standard `camper/toolbox`; ein Gerät aus dem alten Repo sendet auf `camper/tool`).
- Eine zweite Haube (`rvtech2`) kennt das Display noch nicht, ebenso der Abfahrtscheck.

## Neues Plugin schreiben

1. Neue Datei `src/plugins/plugin_<name>.cpp`, Klasse von `Plugin` ableiten (`src/core/plugin.h`): Einstellungen `fields()`, HA-Entitäten `entities()`, MQTT-Werte `publish()`, Befehle `command()`, Web-Karte `live()`. Für Geräte mit Verbindung gibt es `BleLink` (`src/core/ble_link.h`), für passive Sensoren `onAdvertisement()`.
2. Fabrik `Plugin* create<Name>(uint8_t index)` am Ende der Datei.
3. `config.h`: Schalter `PLUGIN_<NAME>` (Standard `0`) und in `CT_BLE_PASSIVE` bzw. `CT_BLE_LINKS` aufnehmen.
4. `src/core/registry.cpp`: Fabrik deklarieren und eine `ADD_PLUGINS`-Zeile.
5. Topics in `docs/TOPICS.md` eintragen, Plugin-Tabelle oben ergänzen.

Die Web-Oberfläche baut Karten, Knöpfe und Einstellungen aus `live()` und `fields()` selbst und braucht in der Regel keine Änderung.

## Serieller Monitor

115200 Baud. Befehle: `read` (alle Werte), `scan` (Bluetooth-Geräte in der Nähe), `noauth` (vergessenen Web-Login löschen), `help`.

Für eine Fehlermeldung den Monitor vom Einschalten an mitschneiden, bis das Problem auftritt (beim Kühlschrank mindestens 3 Minuten). Dort stehen u. a. Verbindungsaufbau und Trennungsgrund (`Reason 0x…`), das Ergebnis der Kopplung (`Security: …`), alle Dienste des Geräts (`GATT table:`) und die ersten empfangenen Pakete (`RX … bytes`). Den Log als Text schicken, MAC-Adressen dürfen geschwärzt werden.

## Sicheres Update, Ereignisse und Rückfall

Jede Firmware hat eine Kennung (`CAMPER-FW:toolbox:<version>:<chip>;`) und dieselbe Absicherung wie alle camper-mqtt-Geräte (`fwcheck.*`, `evlog.*`, `safeguard.*` aus `firmware/_common`):

- **Prüfung beim Update** (Tab **System → Firmware- & Web-Update**): Die installierte Version steht dabei. Eine gewählte Datei wird schon im Browser und beim Hochladen im Gerät geprüft. Abgelehnt werden eine `.merged.bin`, Firmware für ein anderes Gerät oder einen anderen Chip und zu große Dateien. Firmware aus dem alten Repo (ohne Kennung) geht nur mit „Ohne Kennung hochladen“.
- **Ereignisse:** Start mit Grund, WLAN und MQTT, Uploads mit Ergebnis, Rückfall, gespeicherte Einstellungen. Bleibt bei Neustart, Absturz und Update erhalten, nicht bei Stromausfall.
- **Rückfall nach Update:** Hat eine neue Firmware nach 5 Minuten kein WLAN oder startet sie mehr als dreimal neu, startet wieder die vorherige.
- **WLAN-Änderung mit Rückweg:** Klappt ein geänderter WLAN-Zugang nach dem Neustart etwa 1 Minute lang nicht, kommen die alten Daten zurück.

Ein Update ersetzt nur das Programm: WLAN, MQTT, Login, Plugin-Einstellungen (MAC, PIN …) und Bluetooth-Kopplungen bleiben erhalten.

**Umstieg vom alten Repo:** Die alte Firmware (0.1.x) prüft nichts und meldet bei Fehlern nur „Update failed“. Die neue `.bin` (`camper-toolbox-esp32c3.bin`) über das alte Update-Feld hochladen, danach `data/index.html.gz`. Die `-full.bin` bzw. `.merged.bin` nur per USB.

## Versionen

| Version | Änderungen |
| --- | --- |
| 1.0.1 | Web-Oberfläche im Aussehen der übrigen camper-mqtt-Geräte (Farben, Kopfzeile mit Statuspunkten, Tab-Leiste mit Symbolen, Werte als Kacheln, Schalter statt Häkchen) |
| 1.0.0 | Ins camper-mqtt-Repo übernommen: sicheres Update (Kennung, Prüfung im Browser und im Gerät), Ereignis-Log, Rückfall und WLAN-Rückweg, Topic `info`; Basis-Topic `camper/toolbox`, Hostname `camper-toolbox`; ab Werk nur die Dachhaube (RV Tech) |
| 0.1.x | Eigenes Repo `camper_toolbox`: Plugins Gas (DIMES, Mopeka), Wasserpumpe, Dachhaube, Kühlschrank |

## Fertige Firmware

Wird im Haupt-Repo per GitHub Actions gebaut, Tag `toolbox-v<version>`, Datei `camper-toolbox-esp32c3.bin`. Siehe [Fertige Firmware](../../README.md#fertige-firmware).

## Lizenz

MIT, siehe [LICENSE](LICENSE). Das Dachhauben-Protokoll ist aus der Home-Assistant-Integration rvtech-hacs portiert.
