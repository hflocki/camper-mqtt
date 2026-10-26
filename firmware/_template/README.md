# Camper Example

> TODO: Ein Satz, was das Gerät misst oder steuert.

ESP32-Firmware für das Wohnmobil im Verbund [camper-mqtt](../../README.md): Werte per **MQTT** mit Home-Assistant-Discovery, eigene **Web-Oberfläche** zum Einrichten, Updates ohne USB.

## Funktionen

- Werte per MQTT unter `camper/example/<key>`, Home Assistant legt die Entitäten automatisch an
- Web-Oberfläche: Live-Werte, Einstellungen, MQTT, WLAN, optionaler Login
- Setup-WLAN `Camper-Example-XXXXXX` (192.168.4.1) beim ersten Start und wenn das WLAN 60 s fehlt
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
| `camper/example/rssi` | WLAN-Signal in dBm |
| `camper/example/<key>` | ein Topic pro Wert, siehe `VALUES[]` in `sensor.cpp` |
| `camper/example/cmd/measure` | beliebiger Inhalt: sofort messen |

TODO: Tabelle der eigenen Werte und Befehle.

## Serieller Monitor (115200 Baud)

`read` (Werte anzeigen), `measure` (sofort messen), `noauth` (Web-Login löschen), `help`.

## Versionen

| Version | Änderungen |
|---|---|
| 1.0.0 | Erste Version aus der camper-mqtt-Vorlage |

## Lizenz

MIT, siehe [LICENSE](LICENSE).
