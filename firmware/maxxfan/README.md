# Camper MaxxFan – VanTurtle-Board per MQTT

Firmware für einen **ESP32-C3** mit dem **VanTurtle**-Board (TCA9555-Port-Expander), die eine MaxxFan-Dachhaube per **MQTT** steuert – reines **Arduino**, ohne ESPHome. Aufbau wie [camper-tanklevel](../tanklevel) und [camper-heater](../heater):

- Tastendrücke der MaxxFan-Bedienung (Auto, Richtung, Ein/Aus, Schneller, Langsamer, Piep) für **Port 0 (RJ11)** und **Port 1 (RJ45)**
- Eingang **Auto Hold** und **Controller-LED** je Port
- **Stufe setzen** (0–10): 1–10 wie früher: 10× Langsamer bis zur kleinsten Stufe, dann (Stufe−1)× Schneller. **Stufe 0 = Haube zu** (wie „Schließen“)
- **Schließen:** 11× Langsamer, schließt die Haube aus jeder Stufe. „Ein/Aus“ öffnet die Haube nur
- **Sync auf AUS** (Auto drücken → auf Auto Hold warten → Ein/Aus), wie das bisherige ESPHome-Script
- Home-Assistant-Auto-Discovery, Web-Oberfläche mit Bedienung, Einstellungen und Firmware-Update

## Ordner

```
camper_maxxfan/        Sketch für die Arduino IDE
camper_maxxfan/web/    Quelle der Web-Oberfläche (index.html)
camper_maxxfan/data/   LittleFS-Inhalt: index.html.gz (komprimiert, wird hochgeladen)
```

| Datei | Inhalt |
| --- | --- |
| `config.h` | Pins, I2C-Adresse, Polarität, Standardwerte, `FW_VERSION` |
| `tca9555.*` | Treiber für den Port-Expander (keine Library nötig) |
| `fan_control.*` | Tastendrücke (nicht blockierend), Auto-Hold-Eingang, LED, Sync-Ablauf |
| `mqtt_ha.*` | MQTT, Home-Assistant-Discovery |
| `webserver.*` | Web-Oberfläche aus LittleFS, REST-API, Firmware-Update |
| `wifi_manager.*` | WLAN, feste IP, Setup-AP |
| `fwcheck.*`, `evlog.*` | Prüfung beim Firmware-Upload, Ereignis-Log (Kopien aus `firmware/_common`) |
| `safeguard.*` | Rückfall nach Update, WLAN-Änderung mit Rückweg (Kopie aus `firmware/_common`) |
| `settings.*` | Einstellungen im NVS |

## Hardware

Der ESP32-C3 sitzt zusammen mit dem VanTurtle-Board direkt im Lüftergehäuse und wird aus der Lüfterversorgung (9–14 V) gespeist. Laut VanTurtle eine Sicherung zwischen VCC und Batterie vorsehen; das Board wird etwas warm, also nicht dicht einpacken. Wird ein 5-V-Wandler für den ESP genutzt, muss er 14 V Eingang abkönnen.


| Signal | ESP32-C3 | `config.h` |
| --- | --- | --- |
| I2C SDA | GPIO5 | `I2C_SDA_PIN` |
| I2C SCL | GPIO6 | `I2C_SCL_PIN` |
| TCA9555 | Adresse 0x27 | `TCA9555_ADDR` |

Belegung des TCA9555 (wie in der VanTurtle-Vorlage), je Port `n` = 0 oder 1, Pin = `n*8 + x`:

| x | Funktion | Richtung |
| --- | --- | --- |
| 0 | Auto Hold | Eingang |
| 1 | Controller-LED | Ausgang |
| 2 | Auto | Taste |
| 3 | Richtung | Taste |
| 4 | Ein/Aus | Taste |
| 5 | Schneller | Taste |
| 6 | Langsamer | Taste |
| 7 | Piep | Taste |

Tasten werden als Impuls ausgegeben (Standard 150 ms, im Web-UI einstellbar), mit gleich langer Pause bis zum nächsten Druck am selben Port. Ausgänge sind wie bei ESPHome aktiv HIGH (`OUTPUT_ACTIVE_HIGH`), der Auto-Hold-Eingang aktiv HIGH (`HOLD_ACTIVE_HIGH`).

## Arduino IDE

> **Eigene Passwörter, WLAN und Broker:** `config_local.h.example` im Sketch-Ordner nach `config_local.h` kopieren und anpassen. Die Datei geht nicht nach GitHub. Details in der [Haupt-README](../../README.md#eigene-einstellungen-config_localh).

| Einstellung | Wert |
| --- | --- |
| Board | ESP32C3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 4MB |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** |

Die Firmware ist ca. 1,18 MB groß und passt knapp in die Standard-Partition; mit Minimal SPIFFS bleibt Luft für künftige Updates.

**Libraries** (Bibliotheksverwalter): PubSubClient (Nick O'Leary), ArduinoJson 7.x (Benoit Blanchon), ESP Async WebServer (ESP32Async), Async TCP (ESP32Async). Board-Paket: esp32 von Espressif, 3.x.

**Flashen**

1. In `config.h` `DEFAULT_AP_PASSWORD` und `DEFAULT_OTA_PASSWORD` ändern.
2. `camper_maxxfan.ino` öffnen, Partition Scheme prüfen, hochladen.
3. Serial Monitor schließen, `Cmd+Shift+P` → **Upload LittleFS to Pico/ESP8266/ESP32** (Plugin [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload)). Das lädt die Web-Oberfläche `data/index.html.gz` hoch.

Ohne LittleFS-Image zeigt das Gerät eine Notseite mit Hinweis und Firmware-Update. Die Einstellungen liegen im NVS, nicht im Dateisystem: Ein LittleFS-Upload löscht sie nicht.

**Web-Oberfläche ändern:** Quelle ist `web/index.html`, hochgeladen wird `data/index.html.gz`:

```
python3 tools/web/pack_web.py maxxfan      # im Repo-Hauptordner
```

Ohne das Repo-Werkzeug geht es auch so (im Sketch-Ordner `camper_maxxfan`): `gzip -9 -n -c web/index.html > data/index.html.gz`. Die GitHub Action prüft bei jedem Push, ob die gepackte Seite zur `index.html` passt.

**Updates ohne USB** (das Gerät sitzt im Lüftergehäuse): im Web-UI unter **WiFi → Firmware- & Web-Update**. Welche Datei gemeint ist, erkennt die Firmware am Namen:

| Datei | Wirkung |
| --- | --- |
| `….ino.bin` (*Sketch → Kompilierte Binärdatei exportieren*) | neue Firmware, danach Neustart |
| `data/index.html.gz` | nur die Web-Oberfläche wird ersetzt, ohne Neustart |
| LittleFS-Image (Dateiname enthält `littlefs` oder `spiffs`) | ganzes Dateisystem, danach Neustart |

Alternativ per Arduino-OTA (`camper-maxxfan` unter Netzwerk-Ports); auch ein Dateisystem-Upload über `espota -s` wird angenommen. Ändert sich nur die Firmware, muss die Web-Oberfläche nicht neu hochgeladen werden.

## Web-Oberfläche

Gleicher Aufbau und gleiche Optik wie bei camper-tanklevel, camper-temp und camper-heater: Kopfzeile mit Statuspunkten (Board, WiFi, MQTT), Reiter mit Symbolen.

- **Live:** Ein/Öffnen, Schließen, Auto, Richtung, Langsamer, Schneller, Stufe 1–10 setzen, Auto-Hold-Anzeige, Sync auf AUS
- **MaxxFan:** Bedienteil je Port (Controller-LED, Piep als Verbindungstest), Ports ein/aus, Tastendruck-Dauer, Sync-Port
- **MQTT:** Broker, Topic-Prefix, Home-Assistant-Discovery, Web-Login
- **WiFi:** Verbindungsstatus, WLAN, feste IP, Access Point, Firmware-Update, Neustart, Werkseinstellungen

Zum Ausprobieren ohne Gerät: [`docs/mockups/maxxfan.html`](../../docs/mockups/maxxfan.html) im Repo-Hauptordner (siehe [Mockups](../../README.md#mockups)).

## Sicheres Firmware-Update und Ereignisse

Seit 1.7.0 trägt jede Firmware eine Kennung (`CAMPER-FW:maxxfan:<version>:<chip>;`, `fwcheck.*` aus `firmware/_common`). Im Web-UI unter **WiFi → Firmware- & Web-Update** steht die installierte Version. Eine gewählte Datei wird schon im Browser geprüft und beim Hochladen im Gerät noch einmal, bevor etwas aktiviert wird:

| Datei | Ergebnis |
| --- | --- |
| `.bin` dieses Geräts | Gerät, Version und Vergleich mit der installierten (neuer / gleich / älter, bei gleich oder älter mit Rückfrage) |
| `.merged.bin` aus der Arduino IDE | abgelehnt: das ist das komplette Flash-Abbild für USB, fürs Web-Update die `.bin` ohne „merged“ nehmen |
| `.bin` eines anderen Geräts oder Chips | abgelehnt, z. B. „Firmware für „tanklevel“, dieses Gerät ist „maxxfan““ |
| `.bin` ohne Kennung (ältere Firmware, fremde Datei) | gesperrt, mit „Ohne Kennung hochladen“ trotzdem möglich (für ein Downgrade auf eine Version vor 1.7.0) |
| `data/index.html.gz` | Web-Oberfläche, ohne Neustart |

Schlägt etwas fehl, nennt die Meldung den Grund statt nur „Update failed“. Die Karte **Ereignisse** darunter zeigt die letzten 24 Ereignisse: Start mit Grund (Einschalten, Absturz, Watchdog …), WLAN und MQTT verbunden/getrennt, Uploads mit Ergebnis, Rückfall und gespeicherte Einstellungen (`evlog.*`). Das Log bleibt bei Neustart, Absturz und Update erhalten, bei Stromausfall ist es leer. Updates über den Netzwerkport der Arduino IDE werden nicht geprüft, aber ebenfalls protokolliert.

## Absicherung gegen Aussperren

Damit das Gerät erreichbar bleibt, ohne dass jemand hinmuss (`safeguard.*` aus `firmware/_common`):

- **Rückfall nach Update:** Nach einem Firmware-Update über das Web-UI oder den IDE-Netzwerkport läuft die neue Version auf Probe. Hat sie nach 5 Minuten kein WLAN oder startet sie mehr als dreimal neu, startet der ESP wieder die vorherige Firmware (sie liegt noch im zweiten App-Bereich). Ab 1 Minute stabilem WLAN gilt die neue Version als gut. Flashen per USB wird nie zurückgerollt.
- **WLAN-Änderung mit Rückweg:** Werden SSID, Passwort oder feste IP geändert, während das Gerät verbunden ist, merkt es sich die funktionierenden Werte. Klappt die Verbindung mit den neuen nach dem Neustart nicht (etwa 1 Minute), setzt es die alten wieder ein und startet neu. Ist das Gerät beim Ändern gar nicht verbunden (Einrichtung über den Setup-AP), gelten die neuen Werte sofort.
- Was zuletzt passiert ist, steht im Web-UI unter **WiFi → Verbindungsstatus** in der Zeile **Absicherung**.

Der Schutz greift erst bei Updates, die von dieser Version **aus** gemacht werden. Das Update auf diese Version selbst ist noch nicht abgesichert.

## Erstbenutzung

1. Beim ersten Start läuft nur der Access Point **`Camper-MaxxFan-XXXXXX`** (Passwort aus `config.h`, `XXXXXX` = MAC-Endung).
2. Verbinden, `http://192.168.4.1` öffnen (öffnet sich meist automatisch).
3. **Einstellungen**: WLAN (optional feste IP), MQTT-Broker, aktive Ports → Speichern & Neustart.
4. Ab Werk ist nur **Port 1 (RJ45)** aktiv. Wer RJ11 nutzt, schaltet Port 0 ein.

Ist das WLAN länger als eine Minute weg, öffnet sich der Setup-AP wieder; der ESP versucht parallel weiter, sich zu verbinden.

## MQTT

Prefix (Web-UI) Standard `camper/maxxfan`. Zustände sind retained.

| Topic | Wert |
| --- | --- |
| `<prefix>/status` | `online` / `offline` (Last Will) |
| `<prefix>/info` | Diagnose (retained, beim Verbinden und jede Minute): JSON mit `version`, `ip`, `mac`, `rssi`, `uptime` (s), `reset` (Grund des letzten Neustarts), `heap` |
| `<prefix>/p<n>/auto_hold` | `ON` / `OFF` |
| `<prefix>/p<n>/led` | `ON` / `OFF` |
| `<prefix>/expander` | `ON` / `OFF` (TCA9555 erreichbar) |
| `<prefix>/rssi` | WLAN-Signal (dBm) |

**Befehle**

| Topic | Payload | Wirkung |
| --- | --- | --- |
| `<prefix>/cmd/p<n>/<taste>` | beliebig (HA: `PRESS`) | Taste drücken: `auto`, `direction`, `on_off`, `faster`, `slower`, `beep` |
| `<prefix>/cmd/p<n>/press` | Tastenname | dasselbe, Taste im Payload |
| `<prefix>/cmd/p<n>/speed/set` | `0` … `10` | Stufe setzen (Lüfter muss laufen), `0` = Haube schließen |
| `<prefix>/cmd/p<n>/close` | beliebig | Haube schließen (11× Langsamer) |
| `<prefix>/cmd/p<n>/led/set` | `ON` / `OFF` / `TOGGLE` | Controller-LED |
| `<prefix>/cmd/sync_off` | beliebig | Lüfter auf AUS synchronisieren |
| `<prefix>/cmd/reboot` | beliebig | Neustart |

Beispiel:

```
mosquitto_pub -h <broker> -t camper/maxxfan/cmd/p1/faster -m PRESS
mosquitto_sub -h <broker> -t 'camper/maxxfan/#' -v
```

### Home Assistant

Mit Discovery erscheint ein Gerät **„Camper MaxxFan“** mit Tasten-Entitäten je aktivem Port (Piep und LED unter *Konfiguration*), Stufen-Regler (0–10, 0 = zu), Taste „Close Lid“, LED-Schalter, Auto-Hold-Sensor, „Sync to OFF“, Neustart und Diagnose (WLAN, Port-Expander). Deaktivierte Ports werden aus HA entfernt.

**Umstieg von ESPHome:** Das alte ESPHome-Gerät in HA unter *Einstellungen → Geräte & Dienste → ESPHome* löschen. Die Entity-IDs ändern sich, Automationen und Dashboards (z. B. Display-Panels) müssen auf die neuen Entitäten oder direkt auf die MQTT-Topics umgestellt werden.

**Hinweis:** Die MaxxFan meldet ihren Zustand (an/aus, Stufe) nicht zurück – wie bei ESPHome werden nur Tastendrücke gesendet. „Sync auf AUS“ bringt sie in einen bekannten Zustand.

## Versionen

| Version | Änderungen |
| --- | --- |
| 1.7.0 | Sichere Updates: Firmware-Kennung, Prüfung der Datei im Browser und im Gerät (falsches Gerät, falscher Chip, .merged.bin, zu groß), Fehlergrund im Klartext, installierte Version und Ereignis-Log im Web-UI |
| 1.6.0 | Absicherung gegen Aussperren: Rückfall auf die vorherige Firmware, wenn ein Update kein WLAN bekommt; geänderte WLAN-Daten werden zurückgesetzt, wenn sie nicht funktionieren |
| 1.5.0 | Stufe setzen wieder wie in 1.3.0 (von unten), Stufe 0 schließt die Haube (Web-UI, MQTT, HA-Regler) |
| 1.4.0 | „Schließen“ (Web-UI, MQTT `cmd/p<n>/close`, HA-Taste); Stufe setzen von oben (10× Schneller), damit die Haube dabei nicht zugeht; Web-Oberfläche und LittleFS-Image per Web-UI oder OTA hochladbar (kein USB nötig) |
| 1.3.0 | Web-Oberfläche aus LittleFS (`data/index.html.gz`) wie bei tanklevel und temp, Notseite mit Firmware-Update, falls das Image fehlt |
| 1.2.0 | Web-Oberfläche im gleichen Stil wie die anderen Camper-Geräte (Statuspunkte, Reiter Live / MaxxFan / MQTT / WiFi) |
| 1.1.0 | Stufe 1–10 setzen (MQTT, HA-Regler, Web-UI) |
| 1.0.0 | Erste Arduino/MQTT-Version, ersetzt die ESPHome-Konfiguration |

## Fertige Firmware

Wird im Haupt-Repo per GitHub Actions gebaut, siehe [Fertige Firmware](../../README.md#fertige-firmware).

## Lizenz

MIT
