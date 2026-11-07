# Example Node

> TODO: Ein Satz, was das Gerät misst oder steuert.

ESP32-Firmware für das Wohnmobil im Verbund [VanNode](../../README.md): Werte per **MQTT** mit Home-Assistant-Discovery, eigene **Web-Oberfläche** zum Einrichten, Updates ohne USB.

## Funktionen

- Werte per MQTT unter `camper/example/<key>`, Home Assistant legt die Entitäten automatisch an
- Web-Oberfläche: Live-Werte, Einstellungen, MQTT, WLAN, optionaler Login
- Setup-WLAN `Example-Node-XXXXXX` (192.168.4.1) beim ersten Start und wenn das WLAN 60 s fehlt
- Firmware (`.bin`) und Web-Oberfläche (`index.html.gz`) im Web-UI hochladbar, dazu Arduino-OTA
- TODO: gerätespezifische Funktionen

## Hardware

| Teil | Hinweis |
|---|---|
| ESP32-C3 Super Mini oder ESP32-S3 | |
| TODO: Sensor | Anschluss an GPIO `SENSOR_PIN` (`config.h`) |

TODO: Anschlussplan / Foto.

## Arduino IDE

**Board-Paket:** esp32 von Espressif **3.3.x**

| Einstellung | Wert |
|---|---|
| Board | ESP32C3 Dev Module (bzw. ESP32S3 Dev Module) |
| USB CDC On Boot | Enabled |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** |

**Bibliotheken:**

| Bibliothek | Autor | Version |
|---|---|---|
| PubSubClient | Nick O'Leary | 2.8 |
| ArduinoJson | Benoit Blanchon | 7.4.2 |
| ESPAsyncWebServer | ESP32Async | 3.12.1 |
| AsyncTCP | ESP32Async | 3.5.0 |
| TODO: Sensor-Bibliothek | | |

**Erstes Flashen (USB):**
1. `config_local.h.example` nach `config_local.h` kopieren und WLAN, MQTT und Passwörter eintragen.
2. Sketch hochladen.
3. Web-Oberfläche einmalig: im Browser `http://<ip>/` öffnen, dort steht ein Upload-Formular → `data/index.html.gz` hochladen.

## Updates ohne USB

| Was | Wie |
|---|---|
| Firmware | Arduino IDE → *Sketch → Kompilierte Binärdatei exportieren*, die `.bin` im Web-UI unter **WiFi → Firmware- & Web-Update** hochladen (Neustart), oder per Netzwerkport in der IDE (OTA-Passwort) |
| Web-Oberfläche | `data/index.html.gz` im Web-UI hochladen – ersetzt nur die Seite, ohne Neustart, Einstellungen bleiben |
| Komplettes LittleFS-Image | Nur per USB oder IDE-Netzwerkport; löscht dabei auch die Einstellungen (`config.json`) |

## MQTT / Home Assistant

| Topic | Inhalt |
|---|---|
| `camper/example/status` | `online` / `offline` (Last Will) |
| `camper/example/info` | Diagnose (retained, beim Verbinden und jede Minute): JSON mit `version`, `ip`, `mac`, `rssi`, `uptime` (s), `reset` (Grund des letzten Neustarts), `heap` |
| `camper/example/rssi` | WLAN-Signal in dBm |
| `camper/example/<key>` | ein Topic pro Wert, siehe `VALUES[]` in `sensor.cpp` |
| `camper/example/cmd/measure` | beliebiger Inhalt: sofort messen |

TODO: Tabelle der eigenen Werte und Befehle.

## Serieller Monitor (115200 Baud)

`read` (Werte anzeigen), `measure` (sofort messen), `noauth` (Web-Login löschen), `help`.

## Sicheres Update, Ereignisse und Rückfall

Seit 1.1.0 hat jede Firmware eine Kennung (`CAMPER-FW:example:<version>:<chip>;`) und dieselbe Absicherung wie alle VanNode-Geräte (`fwcheck.*`, `evlog.*`, `safeguard.*` aus `firmware/_common`):

- **Prüfung beim Update** (Web-UI unter **WiFi → Firmware- & Web-Update**): Die installierte Version steht dabei. Eine gewählte Datei wird schon im Browser und beim Hochladen im Gerät noch einmal geprüft, bevor etwas aktiviert wird. Abgelehnt werden eine `.merged.bin` (komplettes Flash-Abbild für USB), Firmware für ein anderes Gerät oder einen anderen Chip und zu große Dateien. Eine Datei ohne Kennung (Version vor 1.1.0) geht nur mit „Ohne Kennung hochladen“. Fehler stehen im Klartext da statt „Update failed“.
- **Ereignisse:** Die letzten Ereignisse (Start mit Grund, WLAN und MQTT verbunden/getrennt, Uploads mit Ergebnis, Rückfall, gespeicherte Einstellungen) stehen in der Web-Oberfläche. Sie bleiben bei Neustart, Absturz und Update erhalten, nach einem Stromausfall ist das Log leer.
- **Rückfall nach Update:** Hat eine neue Firmware nach 5 Minuten kein WLAN oder startet sie mehr als dreimal neu, startet wieder die vorherige. Kam das Update über den Setup-AP (ohne WLAN), wird nur auf Neustart-Schleifen geachtet.
- **WLAN-Änderung mit Rückweg:** Werden die WLAN-Daten geändert, während das Gerät verbunden ist, merkt es sich die funktionierenden. Klappt die Verbindung mit den neuen nach dem Neustart etwa 1 Minute lang nicht, kommen die alten zurück.

## Versionen

| Version | Änderungen |
|---|---|
| 1.2.1 | VanNode-Symbol links neben der Überschrift der Webseite und als Favicon |
| 1.2.0 | Neue Namen: Hostname `example-node`, Setup-WLAN, Gerätename und Release-Datei `example-node-….bin`. Topics `camper/…` und Home-Assistant-IDs bleiben |
| 1.1.0 | Sicheres Update (Kennung, Prüfung im Browser und im Gerät, Fehler im Klartext), Ereignis-Log, Rückfall auf die vorherige Firmware und WLAN-Änderung mit Rückweg |
| 1.0.0 | Erste Version aus der VanNode-Vorlage |

## Lizenz

MIT, siehe [LICENSE](LICENSE).
