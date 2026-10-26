# RoadBuddy – M5Stack Core2 Camper-Display

Kleines Touch-Display für das Wohnmobil auf Basis des **M5Stack Core2**. Es zeigt die wichtigsten Werte aus dem Bordnetz per MQTT an: Neigung (Wasserwaage), Frischwassertank, Gasflasche, Temperaturen und Batterien. Zum Akku-Sparen schaltet sich das Display automatisch aus, danach geht das Gerät in den Deep Sleep, WLAN und MQTT sind dann komplett aus.

---

## Wähle deine Entwicklungsumgebung

Es gibt nur einen Quellcode in `_arduino/roadbuddy/`, PlatformIO baut direkt aus diesem Ordner. Du hast die Wahl:

| Ordner | Für wen | Anleitung |
| ------ | ------- | --------- |
| [`_arduino/`](_arduino/) | Arduino IDE (1.x und 2.x) | [Anleitung Arduino](_arduino/README.md) |
| [`_platformio/`](_platformio/) | PlatformIO (VS Code oder CLI) | [Anleitung PlatformIO](_platformio/README.md) |

In beiden Varianten brauchst du eine eigene `_arduino/roadbuddy/config.h` mit WLAN- und MQTT-Zugangsdaten. Als Vorlage liegt `config.example.h` bei, deine `config.h` ist per `.gitignore` vom Git ausgeschlossen.

---

## Seiten

Die Seiten werden mit den beiden äußeren Touch-Tasten (links/rechts) umgeschaltet. Die mittlere Taste schaltet das Display aus.

| Seite | Inhalt |
| ----- | ------ |
| Level | Wasserwaage mit Blase (grün ≤ 1°, gelb ≤ 3°, rot darüber) und zwei kippende Fahrzeug-Icons für Pitch (Seitenansicht) und Roll (Heckansicht) mit Gradzahl |
| Wassertank | Tank mit animierter Wasseroberfläche, Füllstand in % und Liter, Sensor-Status |
| Gas | Gasflasche mit Füllstand, in % und Restmenge, Status, Akku des Gassensors |
| Temperaturen | Sensoren von camper-temp mit Namen und Wert. 1–2 Sensoren als große Zeilen, 3–4 als 2×2-Kacheln. Bei 5–8 Sensoren kommt automatisch eine zweite Temperaturseite dazu. Alarm (zu warm/zu kalt) = roter Rahmen. Farbe: blau unter 10 °C, grün bis 28 °C, gelb bis 45 °C, rot darüber |
| Batterien | Bis zu 3 Batterien von camper-battery: Name, Ladestand (farbiger Streifen: rot unter 20 %, gelb unter 40 %), Spannung, Leistung (+ = lädt, z. B. Solar), Restlaufzeit beim Entladen („noch 3,2 Tage“, „5 h 20 min“). Der BM2 an der Starterbatterie zeigt statt Leistung seinen Zustand (Normal, Schwach, Kritisch, Lädt, Erhaltungsladung). Fehler oder schwache Starterbatterie = roter Rahmen |
| System | WLAN-Status mit Signalstärke, IP, Broker, MQTT-Status, Akku, Laufzeit |

Welche Seiten es gibt, steht in `config.h` (`USE_LEVEL`, `USE_WATER`, `USE_GAS`, `USE_TEMP`, `USE_BATTERY`).

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
| `camper/tanklevel/water/percent` | Füllstand Frischwasser in % |
| `camper/tanklevel/water/liters` | Tankinhalt in Litern |
| `camper/tanklevel/gas/level` | Füllstand Gasflasche in % |
| `camper/tanklevel/gas/content` | Restmenge der Gasflasche (kg) |
| `camper/tanklevel/gas/battery` | Akku des Gassensors in % |
| `camper/tanklevel/gas/ble_state` | `OK`, sonst wird „BLE“ als Warnung gezeigt |
| `camper/tanklevel/status` | `online` / `offline` (Last Will, gilt für Wasser und Gas) |
| `camper/temp/sensors` | Alle Temperatursensoren als JSON-Liste: `[{"id":"fridge","name":"Kühlschrank","t":4.31,"alarm":0}, …]` |
| `camper/temp/status` | `online` / `offline` von camper-temp |
| `camper/battery/batteries` | Alle Batterien als JSON-Liste: `[{"name":"Bordbatterie","ok":true,"soc":78,"v":13.31,"w":-56,"rt":2830}, …]` (ab camper-battery 1.2.2 mit Restlaufzeit) |
| `camper/battery/status` | `online` / `offline` von camper-battery |

Weitere Topics unter diesen Präfixen (z. B. `rssi`, `voltage`, `raw_level`, Grauwasser) werden ignoriert. Die Topics für Wasser und Gas stehen als Konstanten am Anfang des Sketches, das Level-Topic in `config.h`. Die früheren getrennten Geräte (`camper/waterlevel`, `camper/gaslevel`) werden nicht mehr unterstützt.

Die Sensoren dazu:

- Level: [camper-level](../level) (ESP32 + Adafruit BNO085, Fork des Levelsensors von klehnst)
- Wasser und Gas: [camper-tanklevel](../tanklevel) (ein ESP32-C3 für Frischwasser, Grauwasser und Gas)
- Temperaturen: [camper-temp](../temp) (bis zu 8 DS18B20 mit Alarm-Grenzwerten)
- Batterien: [camper-battery](../battery) (Daly, Ective BM X, BM2 über Bluetooth)

Mehr zeigt der RoadBuddy bewusst nicht: Heizung, Lüfter und Warnungen laufen auf dem großen Wandpanel [camper-display](../display).

---

## Anpassungen

Alle Einstellungen stehen am Anfang von `roadbuddy.ino`:

| Konstante | Wirkung |
| --------- | ------- |
| `DISPLAY_TIMEOUT_MS` | Zeit bis das Display ausgeht (Standard 60 s) |
| `SLEEP_DELAY_MS` | Zeit von Display-aus bis Deep Sleep (Standard 5 min) |
| `STALE_MS` | Ab wann Level-Werte als veraltet gelten (Standard 30 s) |
| `BUBBLE_SIGN_X` / `BUBBLE_SIGN_Y` | Auf `-1.0f` setzen, wenn die Blase in die falsche Richtung wandert |
| `BUBBLE_FULL_SCALE_DEG` | Winkel, bei dem die Blase den Rand erreicht (Standard 10°) |
| `ICON_SIGN_PITCH` / `ICON_SIGN_ROLL` | Kipprichtung der Fahrzeug-Icons umdrehen |
| `ICON_GAIN` | Übertreibung des Kippwinkels, damit kleine Winkel sichtbar sind |
| `TOPIC_TANK_*` | MQTT-Topics für Wasser und Gas |
| `TOPIC_TEMP_*` | MQTT-Topics von camper-temp |

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
    └── platformio.ini           – baut direkt aus _arduino/roadbuddy
```

---

## Hinweise

- Zugangsdaten gehören nur in `config.h`, diese Datei ist in `.gitignore` eingetragen und darf nie committet werden.
- Der Prozentwert des Core2-Akkus ist nur eine Schätzung aus der Spannung.

---

## Fertige Firmware

Wird im Haupt-Repo per GitHub Actions gebaut, siehe [Fertige Firmware](../../README.md#fertige-firmware). Der CI-Build nutzt `config.example.h` als `config.h`, die `.bin` enthält also keine Zugangsdaten. Für den Camper selbst mit eigener `config.h` bauen.
