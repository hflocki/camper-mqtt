# RoadBuddy – M5Stack Core2 Camper-Display

Kleines Touch-Display für das Wohnmobil auf Basis des **M5Stack Core2**. Es zeigt die wichtigsten Werte aus dem Bordnetz per MQTT an: Neigung (Wasserwaage), Frischwassertank und Gasflasche. Zum Akku-Sparen schaltet sich das Display automatisch aus, danach geht das Gerät in den Deep Sleep, WLAN und MQTT sind dann komplett aus.

---

## Wähle deine Entwicklungsumgebung

Der Code ist in beiden Varianten identisch, du hast die Wahl:

| Ordner | Für wen | Anleitung |
| ------ | ------- | --------- |
| [`_arduino/`](_arduino/) | Arduino IDE (1.x und 2.x) | [Anleitung Arduino](_arduino/README.md) |
| [`_platformio/`](_platformio/) | PlatformIO (VS Code oder CLI) | [Anleitung PlatformIO](_platformio/README.md) |

In beiden Varianten brauchst du eine eigene `config.h` mit WLAN- und MQTT-Zugangsdaten. Als Vorlage liegt jeweils eine `config.example.h` bei, deine `config.h` ist per `.gitignore` vom Git ausgeschlossen.

---

## Seiten

Die Seiten werden mit den beiden äußeren Touch-Tasten (links/rechts) umgeschaltet. Die mittlere Taste schaltet das Display aus.

| Seite | Inhalt |
| ----- | ------ |
| 1 – Level | Wasserwaage mit Blase (grün ≤ 1°, gelb ≤ 3°, rot darüber) und zwei kippende Fahrzeug-Icons für Pitch (Seitenansicht) und Roll (Heckansicht) mit Gradzahl |
| 2 – System | WLAN-Status mit Signalstärke, IP, Broker, MQTT-Status, Akku, Laufzeit |
| 3 – Wassertank | Tank mit animierter Wasseroberfläche, Füllstand in % und Liter, Sensor-Status |
| 4 – Gas | Gasflasche mit Füllstand, in % und Restmenge, Status, Akku des Gassensors |

In der Kopfzeile stehen links ein WLAN-Symbol (grün = WLAN und MQTT ok, gelb = MQTT fehlt, rotes Kreuz = kein WLAN), in der Mitte die Seitenpunkte und rechts der Akku des Core2 (blau mit „+“ beim Laden).

Werte, die länger als 30 Sekunden nicht aktualisiert wurden (Level-Sensor), werden als `--` angezeigt. Meldet ein Sensor `offline`, wird seine Seite ausgegraut.

---

## Stromsparen

| Zeit ohne Berührung | Verhalten |
| ------------------- | --------- |
| 60 s | Display aus (WLAN und MQTT laufen weiter, ein Antippen zeigt sofort aktuelle Werte) |
| 5 min nach Display-aus | Deep Sleep: WLAN und MQTT aus, minimaler Verbrauch |

- **Aufwecken:** Berührung irgendwo auf dem Display. Das Gerät startet neu, verbindet WLAN und MQTT und zeigt die zuletzt geöffnete Seite. Die weckende Berührung löst keinen Seitenwechsel aus.
- Das Display kann auch manuell mit der mittleren Taste ausgeschaltet werden, der Schlaf-Timer läuft dann ab diesem Moment.
- Zusätzlich weckt ein Timer das Gerät alle 24 Stunden, es schläft sofort wieder ein (Sicherheitsnetz, kostet praktisch keinen Strom).

---

## MQTT-Topics

Das Display ist reiner Empfänger und fragt nichts ab, die Aktualisierungsrate bestimmen die Sensoren. Bei `retain`-Meldungen erscheinen die Werte direkt nach dem Aufwachen.

| Topic | Bedeutung |
| ----- | --------- |
| `camper/level/pitch` | Neigung vorne/hinten in Grad |
| `camper/level/roll` | Neigung links/rechts in Grad |
| `camper/waterlevel/percent` | Füllstand Frischwasser in % |
| `camper/waterlevel/liters` | Tankinhalt in Litern |
| `camper/waterlevel/status` | `online` / `offline` (Last Will) |
| `camper/gaslevel/level` | Füllstand Gasflasche in % |
| `camper/gaslevel/content` | Restmenge der Gasflasche (kg) |
| `camper/gaslevel/battery` | Akku des Gassensors in % |
| `camper/gaslevel/ble_state` | `OK`, sonst wird „BLE“ als Warnung gezeigt |
| `camper/gaslevel/status` | `online` / `offline` (Last Will) |

Weitere Topics unter diesen Präfixen (z. B. `rssi`, `voltage`, `raw_level`) werden ignoriert. Die Topics für Wasser und Gas stehen als Konstanten am Anfang des Sketches, das Level-Topic in `config.h`.

Die Sensoren dazu:

- Level: [ESP32-Adrafruit-BNO085-HomeAssistant-Caravan-Levelsensor-MQTT](https://github.com/klehnst/ESP32-Adrafruit-BNO085-HomeAssistant-Caravan-Levelsensor-MQTT) von klehnst (ESP32 + Adafruit BNO085)
- Wasser: [camper_waterlevel](https://github.com/hflocki/camper_waterlevel) (ESP32-C3 + Votronic Tankgeber)
- Gas: camper_gaslevel (ESP32-C3, passives BLE-Advertising)

---

## Anpassungen

Alle Einstellungen stehen am Anfang des Sketches (`roadbuddy.ino` bzw. `main.cpp`):

| Konstante | Wirkung |
| --------- | ------- |
| `DISPLAY_TIMEOUT_MS` | Zeit bis das Display ausgeht (Standard 60 s) |
| `SLEEP_DELAY_MS` | Zeit von Display-aus bis Deep Sleep (Standard 5 min) |
| `STALE_MS` | Ab wann Level-Werte als veraltet gelten (Standard 30 s) |
| `BUBBLE_SIGN_X` / `BUBBLE_SIGN_Y` | Auf `-1.0f` setzen, wenn die Blase in die falsche Richtung wandert |
| `BUBBLE_FULL_SCALE_DEG` | Winkel, bei dem die Blase den Rand erreicht (Standard 10°) |
| `ICON_SIGN_PITCH` / `ICON_SIGN_ROLL` | Kipprichtung der Fahrzeug-Icons umdrehen |
| `ICON_GAIN` | Übertreibung des Kippwinkels, damit kleine Winkel sichtbar sind |
| `TOPIC_WATER_*` / `TOPIC_GAS_*` | MQTT-Präfixe für Wasser und Gas |

WLAN-Hostname und MQTT-Client-ID beginnen mit `roadbuddy`.

---

## Projektstruktur

```
m5core2-roadbuddy/
├── README.md                    – diese Übersicht
├── .gitignore
├── _arduino/
│   ├── README.md                – Anleitung für die Arduino IDE
│   └── roadbuddy/
│       ├── roadbuddy.ino        – kompletter Sketch
│       └── config.example.h     – Vorlage für WLAN- und MQTT-Daten
└── _platformio/
    ├── README.md                – Anleitung für PlatformIO
    ├── platformio.ini
    ├── src/main.cpp             – kompletter Sketch (identisch zu roadbuddy.ino)
    └── include/config.example.h – Vorlage für WLAN- und MQTT-Daten
```

**Hinweis für Entwickler:** `roadbuddy.ino` und `main.cpp` sind inhaltlich identisch. Wer am Code ändert, sollte beide Dateien synchron halten, zum Beispiel mit `diff _arduino/roadbuddy/roadbuddy.ino _platformio/src/main.cpp`.

---

## Hinweise

- Zugangsdaten gehören nur in `config.h`, diese Datei ist in `.gitignore` eingetragen und darf nie committet werden.
- Der Prozentwert des Core2-Akkus ist nur eine Schätzung aus der Spannung.
