# Camper Tanklevel – Frischwasser, Grauwasser + Gas auf einem ESP32

Eine Firmware für **einen** ESP32-C3 (oder klassischen ESP32), die zwei Projekte zusammenfasst und um Grauwasser erweitert:

- **Frischwasser** aus [camper-waterlevel](https://github.com/hflocki/camper-waterlevel): Votronic-Tankgeber am analogen Eingang, Volt-zu-Füllstand-Matrix, Glättungsfilter
- **Grauwasser** (optional): zweiter Votronic-Geber, wahlweise Tankelektrode (wie Frischwasser) oder kapazitiver Tank-Sensor FL, mit eigener Tankgröße, Matrix und Filter
- **Gas** aus [camper-gaslevel](https://github.com/hflocki/camper-gaslevel): BLE-Gassensor (Standard: Rotarex DIMES / SRG WAVE), passiv über Bluetooth-Advertising, Füllstopp-Berechnung

Alle Werte gehen per **MQTT** (mit Home-Assistant-Discovery) raus und sind in einer gemeinsamen **Web-Oberfläche** zu sehen und einzustellen. Jeder Sensor lässt sich einzeln ein- und ausschalten.

## Web-Oberfläche

**Live**

| Wasser + Gas (Standard) | Wasser + Grau + Gas |
| --- | --- |
| <img src="images/tab-live.png" width="240"> | <img src="images/tab-live-all.png" width="240"> |

| Nur Wasser | Nur Gas |
| --- | --- |
| <img src="images/tab-live-water-only.png" width="240"> | <img src="images/tab-live-gas-only.png" width="240"> |

**Einstellungen**

| Wasser | Gas |
| --- | --- |
| <img src="images/tab-water.png" width="240"> | <img src="images/tab-gas.png" width="240"> |

| Grau: Tankelektrode | Grau: Tank-Sensor FL |
| --- | --- |
| <img src="images/tab-grey.png" width="240"> | <img src="images/tab-grey-fl.png" width="240"> |

| MQTT | WiFi |
| --- | --- |
| <img src="images/tab-mqtt.png" width="240"> | <img src="images/tab-wifi.png" width="240"> |

Die Bilder zeigen Beispieldaten.

## Ordner

```
_arduino/camper_tanklevel/   Sketch für die Arduino IDE (.ino + .cpp/.h + data/index.html)
_platformio/                 platformio.ini, nutzt dieselben Quellen
```

| Datei | Inhalt |
| --- | --- |
| `config.h` | Pins, Standardwerte, **GAS SENSOR DEFINITION** (Byte-Zuordnung) |
| `water_sensor.cpp` | ADC für Frisch- und Grauwasser, gleitender Mittelwert, Interpolation über die Matrix |
| `gas_sensor.cpp` | NimBLE-Scan, Dekodierung, Geräteliste, Füllstopp |
| `mqtt_ha.cpp` | MQTT, HA-Discovery, Filter-Schalter |
| `webserver.cpp` | Web-UI, REST-API, Konfiguration in LittleFS |
| `wifi_manager.cpp` | WLAN mit AP-Fallback (Modem-Sleep für WiFi/BLE-Koexistenz) |

## Hardware

- ESP32-C3 Super Mini (getestet kompiliert) oder klassischer ESP32
| Sensor | ESP32-C3 | Klassischer ESP32 | `config.h` |
| --- | --- | --- | --- |
| Frischwasser (Votronic-Tankgeber) | **GPIO0** | GPIO34 | `ADC_PIN` |
| Grauwasser (Votronic-Tankgeber, optional) | **GPIO1** | GPIO35 | `GREY_ADC_PIN` |
| Gas (BLE) | keine Verkabelung, eingebautes Bluetooth | | |

Beide Tankeingänge müssen ADC1-Pins sein (ADC2 funktioniert nicht, solange WLAN aktiv ist). GPIO2 beim C3 nicht verwenden, das ist ein Boot-Pin. Verkabelung der Tankgeber wie im [camper-waterlevel](https://github.com/hflocki/camper-waterlevel#verkabelung) beschrieben, der Grauwasser-Geber bekommt eine eigene, gleich aufgebaute Messschaltung.

**Grauwasser-Geber** – im Tab **Grau** unter „Gebertyp“ wählbar (Werkseinstellung in `config.h`: `GREY_SENSOR_TYPE`):

| Gebertyp | Kalibrierung |
| --- | --- |
| Votronic Tankelektrode (wie Frischwasser) | Matrix mit mehreren Punkten, Startwerte wie beim Frischwasser |
| Votronic Tank-Sensor FL (kapazitiv) | Lineares Signal: bei leerem Tank „Leer übernehmen“, bei vollem Tank „Voll übernehmen“ |

Für Abwasser ist der kapazitive FL die robustere Wahl, weil sich Ablagerungen kaum auf den Messwert auswirken. Beim Wechsel des Gebertyps wird die Grauwasser-Matrix auf die Startwerte des Typs zurückgesetzt. Vor dem Anschluss mit dem Multimeter die Signalspannung bei leerem und vollem Tank prüfen: Liegt sie über ca. 2,5 V, ist ein Spannungsteiler nötig. Bei langer Leitung zum Tank hilft ein abgeschirmtes Kabel, der Glättungsfilter beruhigt den Wert zusätzlich.

## Arduino IDE

**Board-Einstellungen**

| Einstellung | Wert |
| --- | --- |
| Board | ESP32C3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 4MB |
| Partition Scheme | ⚠️ **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** |

WiFi + Bluetooth ergeben ca. 1,43 MB Firmware und passen nicht in die Standard-Partition (1,2 MB).

**Libraries** (Bibliotheksverwalter)

| Library | Autor | getestet |
| --- | --- | --- |
| PubSubClient | Nick O'Leary | 2.8 |
| ArduinoJson | Benoit Blanchon | 7.4.2 |
| ESP Async WebServer | ESP32Async | 3.12.1 |
| Async TCP | ESP32Async | 3.5.0 |
| NimBLE-Arduino | h2zero | 2.5.1 |

Board-Paket: esp32 von Espressif, 3.x (getestet 3.3.9).

**Flashen**

1. In `config.h` `DEFAULT_OTA_PASSWORD` und `DEFAULT_AP_PASSWORD` ändern.
2. `camper_tanklevel.ino` öffnen, Partition Scheme prüfen, hochladen.
3. Serial Monitor schließen, `Cmd+Shift+P` → **Upload LittleFS to Pico/ESP8266/ESP32** (Plugin [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload)).

Der LittleFS-Upload überschreibt die gespeicherte Konfiguration. Spätere Firmware-Updates gehen auch per OTA (`camper-tanklevel` unter Netzwerk-Ports).

## Erstbenutzung

1. Beim ersten Start läuft nur der Access Point **`Camper-Tanklevel-XXXXXX`** (Passwort aus `config.h`). `XXXXXX` sind die letzten 6 Stellen der WLAN-MAC des ESP, so lassen sich mehrere Geräte auseinanderhalten. Der Name ist im Tab WiFi änderbar.
2. Verbinden und `http://192.168.4.1` öffnen.
3. Tab **WiFi**: WLAN eintragen → Neustart.
4. Tab **MQTT**: Broker eintragen → Neustart.
5. Tab **Gas**: Sensor aus der Liste „Bluetooth-Geräte in der Nähe“ übernehmen, Flaschengröße und Füllstopp einstellen.
6. Tab **Wasser**: Tankgröße und ggf. Matrix anpassen (Standard: Votronic-Referenzwerte).
7. Optional Tab **Grau**: Grauwasser-Sensor einschalten (Neustart), Gebertyp wählen, Tankgröße einstellen und kalibrieren (Matrix bzw. Leer/Voll einlernen).

## MQTT / Home Assistant

Prefix (Web-UI) Standard `camper/tanklevel`:

| Topic | Wert |
| --- | --- |
| `<prefix>/status` | `online` / `offline` (Last Will) |
| `<prefix>/rssi` | WLAN-Signal |
| `<prefix>/water/liters`, `percent`, `voltage` | Frischwasser |
| `<prefix>/water/filter_active` | `ON` / `OFF` |
| `<prefix>/cmd/water/filter/set` | `ON` / `OFF` / `TOGGLE` |
| `<prefix>/grey/liters`, `percent`, `voltage` | Grauwasser (nur wenn eingeschaltet) |
| `<prefix>/grey/filter_active` | `ON` / `OFF` |
| `<prefix>/cmd/grey/filter/set` | `ON` / `OFF` / `TOGGLE` |
| `<prefix>/gas/level`, `raw_level`, `content` | Gas (nur nach erstem gültigen Wert) |
| `<prefix>/gas/battery`, `ble_rssi`, `ble_state` | Gassensor |

In Home Assistant erscheint **ein** Gerät „Camper Tanklevel“ mit allen Entitäten.

**Umstieg von den Einzelprojekten:** Die alten Geräte „Camper Wasserlevel Sensor“ und „Camper Gaslevel Sensor“ bleiben in HA stehen, weil ihre Discovery-Nachrichten retained sind. Zum Entfernen in HA unter *Einstellungen → Geräte* löschen oder die Topics `homeassistant/sensor/camper_water/#`, `homeassistant/switch/camper_water/#` und `homeassistant/sensor/camper_gaslevel/#` mit leerer Nachricht (retained) überschreiben.

## Sensoren ein- und ausschalten

Frischwasser und Gas sind standardmäßig an, Grauwasser ist aus. Jede Kombination ist möglich:

- **In `config.h`** (Werkseinstellung beim ersten Start):
  ```cpp
  #define WATER_ENABLED         1      // 1 = on, 0 = off
  #define GREY_ENABLED          0      // 1 = on, 0 = off
  #define GAS_ENABLED           1      // 1 = on, 0 = off
  ```
- **In der Web-UI:** oben im Tab **Wasser**, **Grau** bzw. **Gas** gibt es einen Schalter. Danach startet das Gerät neu.

Ein ausgeschalteter Sensor verschwindet von der Live-Seite und aus Home Assistant (die Entitäten werden per MQTT entfernt). Ist Gas aus, wird Bluetooth gar nicht gestartet.

## Anderer Gassensor

Block **GAS SENSOR DEFINITION** in `config.h` anpassen (Datenquelle, Hersteller-ID/UUID, Byte-Offsets). Der Tab **Gas** zeigt die empfangenen Rohbytes mit Offsets, damit sich die Zuordnung leicht finden lässt.

## Batterie des Gassensors

Der ESP scannt **passiv**: Er hört nur die Advertising-Pakete mit, die der Sensor ohnehin regelmäßig sendet. Es gibt keine Scan-Anfragen, keine Verbindung und kein Pairing. Die Batterielaufzeit des Sensors ändert sich dadurch nicht.

## Serieller Monitor

115200 Baud. Befehle: `read`, `scan` (BLE-Geräte), `filter` (Frischwasser-Filter umschalten), `help`.

## Versionen

Die laufende Version steht in der Web-UI im Tab **WiFi** und in Home Assistant in der Geräteinfo. Bei Änderungen `FW_VERSION` in `config.h` hochzählen.

| Version | Änderungen |
| --- | --- |
| 1.2.0 | Optionaler Grauwasser-Sensor an GPIO1: Votronic Tankelektrode oder Tank-Sensor FL (mit Leer/Voll-Einlernen), eigene Tankgröße, Matrix und Filter |
| 1.1.0 | Sensoren einzeln abschaltbar (`config.h` und Web-UI), AP-Name mit MAC-Endung, Live-Seite nur mit Anzeigen, Filter im Tab Wasser |
| 1.0.0 | Erste kombinierte Version aus camper-waterlevel und camper-gaslevel |

## Lizenz

MIT
