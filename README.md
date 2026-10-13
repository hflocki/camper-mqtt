# RoadBuddy – M5Stack Core2 Camper-Display

Kleines Touch-Display für das Wohnmobil auf Basis des **M5Stack Core2**. Es zeigt die wichtigsten Werte aus dem Bordnetz per MQTT an: Neigung (Wasserwaage), Frischwassertank und Gasflasche. Zum Akku-Sparen schaltet sich das Display automatisch aus, danach geht das Gerät in den Deep Sleep, WLAN und MQTT sind dann komplett aus.

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
- Die Zeiten lassen sich oben im Sketch anpassen: `DISPLAY_TIMEOUT_MS` und `SLEEP_DELAY_MS`.

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

Weitere Topics unter diesen Präfixen (z. B. `rssi`, `voltage`, `raw_level`) werden ignoriert. Die Topics für Wasser und Gas stehen als Konstanten am Anfang von `roadbuddy.ino`, das Level-Topic in `config.h`.

Die Sensoren dazu:

- Level: Fork des Projekts [ESP32-Adrafruit-BNO085-HomeAssistant-Caravan-Levelsensor-MQTT](https://github.com/klehnst/ESP32-Adrafruit-BNO085-HomeAssistant-Caravan-Levelsensor-MQTT) von klehnst (ESP32 + Adafruit BNO085)
- Wasser: [camper_waterlevel](https://github.com/hflocki/camper_waterlevel) (ESP32-C3 + Votronic Tankgeber)
- Gas: [camper_gaslevel](https://github.com/hflocki/camper-gaslevel) (ESP32-C3, passives BLE-Advertising)

---

## Installation (Arduino IDE)

1. **Board-Paket** „M5Stack“ von M5Stack über den Boardverwalter installieren und als Board **M5Core2** wählen.
2. **Bibliotheken** über den Bibliotheksverwalter:
   - **M5Unified** (zieht **M5GFX** automatisch mit)
   - **PubSubClient** von Nick O'Leary
3. Falls vorhanden, die alte Bibliothek **M5Core2** entfernen, sonst kommt der Fehler `rom/miniz.h: No such file or directory`.
4. `config.example.h` nach `config.h` kopieren und die eigenen Zugangsdaten eintragen.
5. Der Ordner muss genauso heißen wie die Hauptdatei (`roadbuddy/roadbuddy.ino`), sonst findet die Arduino IDE den Sketch nicht.
6. Kompilieren und hochladen.

### Projektstruktur

```
roadbuddy/
├── roadbuddy.ino        – kompletter Sketch (UI, MQTT, Sleep)
├── config.example.h     – Vorlage für WLAN- und MQTT-Daten
├── config.h             – eigene Zugangsdaten (nicht im Git)
├── .gitignore
└── README.md
```

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
| `TOPIC_WATER_*` / `TOPIC_GAS_*` | MQTT-Präfixe für Wasser und Gas |

WLAN-Hostname und MQTT-Client-ID beginnen mit `roadbuddy`.

---

## Fehlersuche

- **Display zeigt falsche Pitch-/Roll-Werte:** Im Serial Monitor (115200 Baud) wird jedes eingehende Topic mit Wert ausgegeben. So lässt sich prüfen, welche Topics wirklich ankommen.
- **Gerät wacht nicht auf:** Display kurz antippen. Hilft das nicht, den Power-Knopf links am Gehäuse drücken, um das Gerät neu zu starten.
- **Seite zeigt `--`:** Es wurde noch kein Wert empfangen. Prüfen, ob der Sensor mit `retain` sendet, sonst erscheint der Wert erst bei der nächsten Meldung.

---

## Hinweise

- Zugangsdaten gehören nur in `config.h`. Diese Datei ist in `.gitignore` eingetragen.
- Der Prozentwert des Core2-Akkus ist nur eine Schätzung aus der Spannung.
