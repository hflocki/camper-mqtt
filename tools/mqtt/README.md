# MQTT-Werkzeuge: Simulator und Recorder

Zum Testen von Display, Road Buddy und Home Assistant ohne Fahrzeug, z. B. im Testnetz zu Hause.

## Einrichten

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r tools/mqtt/requirements.txt
```

Broker-Zugang per Option oder Umgebungsvariable: `--host`/`MQTT_HOST`, `--port`/`MQTT_PORT`, `--user`/`MQTT_USER`. Das Passwort nur als `MQTT_PASSWORD`, damit es nicht in der Shell-History landet.

## Simulator `sim.py`

Sendet dieselben Topics wie die echte Firmware (retained): Tanks, Level, Heizung, Dachlüfter, Temperatur, Batterie, Router (Mobilfunk und GPS) sowie `…/status` und `…/info` jedes Geräts.

```bash
python3 tools/mqtt/sim.py --list                         # alle Szenarien
python3 tools/mqtt/sim.py --host 192.168.8.1             # "normal"
python3 tools/mqtt/sim.py --host 192.168.8.1 empty       # Tanks laufen leer
python3 tools/mqtt/sim.py --host 192.168.8.1 offline --device heater
python3 tools/mqtt/sim.py --host 192.168.8.1 drive --contact 0x00158d0000000001
```

| Szenario | Ablauf |
| --- | --- |
| `normal` | alle Geräte online, gleichmäßige Werte mit etwas Rauschen |
| `drive` | Fahrt: GPS-Geschwindigkeit 0 → 80 km/h, Level-Werte schwanken; mit `--contact` bleibt ein Fenster offen |
| `tilt` | schräg geparkt, nach 90 s eben |
| `empty` | Frischwasser 30 → 5 %, Gas 25 → 8 % in 3 Minuten |
| `offline` | `--device` geht nach 30 s offline und kommt nach 2 Minuten wieder |
| `fridge` | Kühlschrank erwärmt sich von 5 auf 11 °C (Alarm von camper-temp, mit `--fridge` auch ein Zigbee-Sensor) |
| `battery` | Batterie entlädt von 40 auf 12 % |
| `frost` | Außentemperatur fällt unter 0 °C (braucht `--outdoor`: Zigbee-Sensor, der am Display für die Wetter-Seite markiert ist) |
| `replay DATEI` | spielt einen Mitschnitt von `record.py` ab, `--speed 10` zehnmal schneller |

Weitere Optionen: `--skip <gerät>` lässt ein Gerät weg (z. B. wenn das echte läuft), `--duration` beendet nach Sekunden, `--cleanup` löscht beim Beenden alle retained Topics, die der Simulator gesendet hat, `-v` zeigt jede Nachricht.

Echte Geräte am selben Broker überschreiben die simulierten Werte. Entweder ausschalten oder mit `--skip` weglassen.

## Recorder `record.py`

Schneidet Nachrichten als JSON-Lines-Datei mit (eine Zeile je Nachricht: Zeit, Topic, Inhalt, retain).

```bash
python3 tools/mqtt/record.py --host 192.168.8.2 -o camper.jsonl                    # alles unter camper/#
python3 tools/mqtt/record.py -t 'camper/zigbee/data/#' --duration 600 -o pj1203.jsonl
```

`--no-retained` lässt die retained Nachrichten beim Start weg, `-q` gibt nichts auf der Konsole aus. Beenden mit Strg+C.

Mitschnitte können Positionen (GPS) und Gerätenamen enthalten. Sie gehören nicht ins Repo, `*.jsonl` im Hauptordner fängt `.gitignore` ab.
