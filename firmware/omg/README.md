# camper-omg – Bluetooth-Sensoren über OpenMQTTGateway

> [!WARNING]
> **Noch nicht auf Hardware getestet.** Die Konfiguration ist geprüft (PlatformIO löst sie korrekt auf), gebaut und geflasht wurde sie hier noch nicht.

Ein ESP32 mit [OpenMQTTGateway](https://github.com/1technophile/OpenMQTTGateway) (OMG) hört Bluetooth-Thermometer im Camper ab und schickt ihre Werte per MQTT. Das Camper-Display liest sie direkt (*Einstellungen → 📡 Sensoren*).

OpenMQTTGateway wird **nicht** umgebaut: `setup.py` holt das Original in einer festen Version und legt nur `camper_env.ini` daneben. OMG liest jede `*_env.ini` im eigenen Ordner von selbst.

| Was | Wert |
| --- | --- |
| Topic | `camper/OMG/BTtoMQTT/<MAC>`, z. B. `camper/OMG/BTtoMQTT/A4C138AEF56D` |
| Inhalt | JSON, u. a. `tempc`, `hum`, `pres`, `batt`, `rssi`, `model` |
| Geräte | alle, die OMG/Theengs entschlüsselt: Govee H5075/H5179/H5100, Ruuvi-Tag, Xiaomi LYWSD03MMC/Mijia, Inkbird, SwitchBot Meter … |
| Nur Sensoren | `PublishOnlySensors=true`: Handys, Kopfhörer und andere Beacons werden nicht gesendet |
| OMG-Version | `v1.8.1` (in `setup.py`, `OMG_REF`) |

## Hardware

- **ESP32 DevKit** (klassischer ESP32, z. B. ESP32-WROOM-32) → Umgebung `camper-ble`
- **ESP32-C3** (DevKitM-1, Super Mini) → Umgebung `camper-ble-c3`

Versorgung über USB bzw. einen 12→5-V-Wandler. Mittig im Fahrzeug montieren, Bluetooth reicht je nach Ausbau 5–15 m.

## Bauen und flashen (PlatformIO)

```bash
cd firmware/omg
python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt
python3 setup.py upload camper-ble        # ESP32 per USB flashen (C3: camper-ble-c3)
python3 setup.py monitor camper-ble       # serielles Log
```

`python3 setup.py build camper-ble` baut nur und nennt den Pfad der `firmware.bin`. Beim ersten Mal lädt PlatformIO die ESP32-Werkzeuge, das dauert einige Minuten.

## Einrichtung

1. Nach dem ersten Start öffnet OMG das WLAN **OMG…**. Verbinden, im Portal WLAN und MQTT-Broker (z. B. der RUT956) eintragen.
2. Oder vorher fest einbauen: `camper_local_env.ini.example` nach `camper_local_env.ini` kopieren und ausfüllen. Die Datei geht nicht nach GitHub; `setup.py` kopiert sie mit.
3. Am Display unter *Einstellungen → 📡 Sensoren* erscheinen gefundene Thermometer unter „Gefunden, aber noch nicht eingetragen“ → *+ übernehmen*, Namen geben, *Sensoren speichern*. Oder im Topic-Finder `camper/OMG/#` ansehen.

Home Assistant bekommt die Sensoren wie bei OMG üblich per MQTT-Discovery mit.

## Andere Version von OMG

```bash
OMG_REF=v1.8.0 python3 setup.py build camper-ble
```

Die Umgebungen in `camper_env.ini` bauen auf `com-esp32`, `libraries.ble` und `libraries.decoder` aus OMG auf. Ändern sich diese Namen in einer neuen OMG-Version, `camper_env.ini` anpassen.

## Lizenz

OpenMQTTGateway: GPL-3.0 (wird nur heruntergeladen, nicht mitgeliefert). Die Dateien in diesem Ordner: MIT.
