# Camper Emulator

Python-Werkzeug mit lokaler Webseite für den Verbund [camper-mqtt](../../README.md): Es sendet die MQTT-Topics der fest im Camper verbauten Geräte mit Beispielwerten. So lassen sich das Display, der Road Buddy und Home Assistant zu Hause testen, während Tanks, Heizung & Co. im Fahrzeug bleiben. Werte, Geräte und Szenarien stellst du im Browser ein. Gedacht zum Testen, nicht als Dauerbetrieb.

Nachgebildet werden:

| Gerät | Topics | Reagiert auf Befehle |
| --- | --- | --- |
| Tanks | `camper/tanklevel/…` Frischwasser, Grauwasser, Gas | Glättung an/aus |
| Levelsensor | `camper/level/…` Neigung, Keilhöhen, `is_level` | Nullpunkt setzen/zurücksetzen, Toleranz |
| Heizung | `camper/heater/control_status/…` | `set/target_temp_room`, `hvac_mode`, `room_setpoint`, `fan_mode`, `heating_mode`, `water_mode`, `target_temp_water`, `decoke` |
| MaxxFan | `camper/maxxfan/p1/…`, `p2/…` | Stufe, Schließen, LED, Tasten |
| Temperaturen | `camper/temp/<id>`, `alarm`, `alarm_text`, `sensors` (4 Sensoren mit Alarmgrenzen) | – |
| Batterien | `camper/battery/<id>/…`, `batteries` (2 Batterien, Daly / Ective / BM2) | – |
| Router | `camper/rut` (GPS-JSON wie der RUT956), `camper/door`, `camper/router/mobile` | – |
| Zigbee | `camper/zigbee/data/<ieee>/1/…`: 3 Tür-/Fensterkontakte, 2 Klimasensoren, LED-Leiste | LED an/aus, Helligkeit, Farbe |
| Toolbox | `camper/toolbox/rvtech/…`: Dachhaube (Heki) mit `cover`, `cover_position`, `reported`, `connected` | Auf, Zu, Regen, Lüften, Position, `OPEN`/`CLOSE`; die Haube fährt mit einstellbarer Geschwindigkeit (Standard 5 %/s) |

Die Formate sind dieselben wie bei der echten Firmware bzw. beim Router und SMLIGHT-Gateway (siehe [`docs/TOPICS.md`](../../docs/TOPICS.md)). Jedes Gerät hat eine eigene Broker-Verbindung mit eigenem Last Will, `…/status` und `…/info` (Version `emu-py-…`), daher erscheint es in der Geräteliste des Displays wie das echte. Home-Assistant-Discovery sendet der Emulator nicht, damit keine doppelten Entitäten entstehen.

## Einrichten und starten

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r tools/emulator/requirements.txt

python3 tools/emulator/emulator.py --host 192.168.8.1
```

Dann `http://localhost:8080` im Browser öffnen.

| Option | Bedeutung |
| --- | --- |
| `--host`, `--port`, `--user` | Broker (auch als `MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`). Das Passwort nur als Umgebungsvariable `MQTT_PASSWORD`, damit es nicht in der Shell-History landet |
| `--web-port 8080` | Port der Webseite |
| `--listen 0.0.0.0` | Webseite auch im Netz erreichbar (z. B. vom Handy). Vorgabe: nur dieser Rechner. Es gibt keinen Login, also nur im eigenen Netz nutzen |
| `--prefix camper/emulator` | Topic des Emulators selbst (`…/status`, `…/info`) |
| `--data <ordner>` | wo Werte und eigene Szenarien liegen (Vorgabe `tools/emulator/data/`, nicht im Repo) |
| `--keep` | beim Beenden die Werte am Broker stehen lassen, nur `…/status` = `offline` |
| `-v` | jede Nachricht ausgeben |

**Beenden mit Strg+C:** Der Emulator löscht dabei alle retained Topics, die er gesendet hat. Display und Home Assistant sehen danach keine Beispielwerte mehr. Mit `--keep` bleiben die Werte stehen und die Geräte melden `offline`.

## Bedienung

**Geräte:** eine Karte je Gerät mit drei Zuständen:

| Zustand | Wirkung |
| --- | --- |
| **An** | online, Werte gehen raus (Änderungen sofort, alles jede Minute) |
| **Offline** | Verbindung reißt ab wie bei Stromausfall: Der Broker setzt `…/status` per Last Will auf `offline`, die letzten Werte bleiben stehen. Bei Router und Zigbee heißt das **Pause** (nichts Neues senden) |
| **Aus** | nichts senden, die retained Topics des Geräts werden gelöscht – für ein echtes Gerät am selben Broker |

Aufgeklappt zeigt die Karte alle Werte mit Schiebereglern, Schaltern und Auswahllisten. Ungültige Eingaben werden mit einer Meldung abgelehnt. **Mehr** zeigt Einstellungen wie Basis-Topics, IDs und IEEE-Adressen, **Rauschen** lässt Messwerte leicht schwanken (wie echte Sensoren), **Beispielwerte** setzt das Gerät zurück. Werte und Zustände bleiben für den nächsten Start erhalten (`data/emu_state.json`).

Wenn sich etwas Abgeleitetes ändert, rechnet der Emulator wie das Gerät: Literangabe aus Prozent und Tankgröße, Keilhöhen aus Neigung, Rad- und Spurweite, Alarmtext der Temperaturen, Leistung, Restlaufzeit und Laden der Batterie, `hvac_action` und Betriebstext der Heizung. Die Raumtemperatur läuft bei eingeschalteter Heizung langsam auf die Solltemperatur zu (abschaltbar).

**Eigene Zigbee-Adressen:** Trägst du unter *Zigbee → Mehr* die IEEE-Adressen deiner echten Kontakte und Klimasensoren ein, passt die Sensor-Liste des Displays ohne Änderung.

**Szenarien:**

| Szenario | Ablauf |
| --- | --- |
| Tanks leer | Frischwasser 30 → 5 %, Gas 25 → 8 % in 3 Minuten |
| Kühlschrank warm | Sensor 1 und Zigbee-Klimasensor 1 von 5 auf 11 °C |
| Batterie leer | Batterie 1 von 40 auf 12 % |
| Frost | Sensor 4 und Zigbee-Klimasensor 2 von 4 auf −2 °C |
| Abfahrt | Markise draußen (Befehl `camper/display/cmd/awning`), Tür und Kontakt 1 offen, Fahrt ab 20 s, Tür zu nach 60 s, Markise rein nach 90 s, Kontakt zu nach 120 s, Stillstand ab 150 s |
| Fahrt | 0 → 80 km/h, Neigung schwankt, Kontakt 1 bleibt offen |
| Schräg geparkt | 3° / 2,2°, nach 90 s eben |
| Aufheizen | Heizung an auf 22 °C bei 12 °C, Warmwasser Eco |
| Gerät offline | gewähltes Gerät fällt nach 30 s aus und kommt nach 2 Minuten wieder |

**Stopp** stellt die Werte von vorher wieder her, **Stopp & behalten** lässt den Endstand stehen. Bei Szenarien mit Gerätewahl (z. B. *Gerät offline*) steht die Auswahl direkt am Szenario.

**Eigene Szenarien:** ⬇ lädt ein Szenario oder unter *Eigene Szenarien* alle als JSON herunter. Datei ändern, hochladen, und neue Szenarien stehen mit dem Zeichen *eigenes* in der Liste. Die Standard-Szenarien bleiben immer erhalten: Einträge mit deren `id` werden beim Hochladen übersprungen, für eine geänderte Fassung also eine neue `id` vergeben. Ein eigenes Szenario mit gleicher `id` wird ersetzt, 🗑 löscht es. Fehlerhafte Dateien werden ganz abgelehnt, die Meldung nennt Szenario und Stelle. Gespeichert wird je Szenario als `data/scenarios/<id>.json`; du kannst die Dateien dort auch direkt ablegen (gelesen beim Start).

### Eigene Szenarien (JSON)

```json
{
  "format": "camper-emulator-scenarios",
  "version": 1,
  "scenarios": [
    {
      "id": "grey-full",
      "name": "Grauwasser voll",
      "desc": "Grauwasser 50 → 100 % in 1 Minute, das gewählte Gerät fällt nach 20 s aus",
      "start":   { "g_on": "ON" },
      "tracks":  { "g_pct": [[0, 50], [60, 100]],
                   "door":  [[0, "open"], [30, "close"]] },
      "modes":   { "$dev": [[20, "offline"], [80, "on"]] },
      "publish": [[5, "test/ping", "1"], [10, "@awn_t", "ON"]],
      "on_stop": [["@awn_t", "OFF"]],
      "end": 90
    }
  ]
}
```

| Schlüssel | Bedeutung |
| --- | --- |
| `id` | Pflicht, 1–24 Zeichen `a–z 0–9 - _`. Gleiche `id` wie ein Standard-Szenario → übersprungen |
| `name`, `desc` | Name (sonst die `id`) und Beschreibung für die Liste |
| `start` | Werte, die beim Start einmal gesetzt werden |
| `tracks` | Verlauf je Feld: `[[Sekunde, Wert], …]`. Zahlen laufen gleichmäßig zwischen den Punkten, Texte, ON/OFF und Auswahlfelder springen am Punkt. Vor dem ersten Punkt bleibt das Feld unverändert, nach dem letzten bleibt der letzte Wert. Zwei Punkte zur selben Sekunde = Sprung. `{"points": [...], "loop": 13}` wiederholt nach dem letzten Punkt den Teil ab Sekunde 13 |
| `modes` | Zustand je Gerät über die Zeit: `on`, `offline` (Router/Zigbee: Pause), `off`. Geräte: `tanklevel`, `level`, `heater`, `maxxfan`, `temp`, `battery`, `router`, `zigbee`, `toolbox`; `$dev` = das in der Liste gewählte Gerät |
| `publish` | einmalige Nachrichten `[Sekunde, Topic, Payload]`, nicht retained. `@feld` nimmt das Topic aus dem Feld (z. B. `@awn_t` = Markisen-Befehl) |
| `on_stop` | Nachrichten beim Stoppen `[Topic, Payload]` |
| `end`, `keep` | optional: Szenario stoppt nach `end` Sekunden von selbst; `"keep": true` lässt dann den Endstand stehen |

Feldnamen sind die Schlüssel aus der Feldtabelle in `emu.py` (z. B. `w_pct`, `gas_pct`, `pitch`, `roll`, `room`, `setp`, `spd`, `door`, `t1_t`, `b1_soc`, `c1_open`, `k1_t`). Am einfachsten ein Standard-Szenario herunterladen und als Vorlage nehmen. Werte werden wie bei der Eingabe in der Webseite geprüft (Bereich, Auswahl). Topic-Felder (Basis-Topics, IDs, IEEE-Adressen) kann ein Szenario nicht ändern. Weitere Schlüssel (z. B. `"_kommentar"`) werden ignoriert und bleiben in der Datei erhalten.

**Empfangene Befehle:** die letzten 16 Befehle vom Display (oder `mosquitto_pub`) mit Topic und Inhalt – so siehst du, ob ein Knopf am Display das Richtige sendet.

**Aufräumen:** *Retained Topics löschen* entfernt alles, was der Emulator gesendet hat, vom Broker, und sendet die eingeschalteten Geräte neu.

Läuft ein echtes Gerät am selben Broker, das nachgebildete auf **Aus** stellen – sonst überschreiben sich beide.

## Entwickeln

| Datei | Inhalt |
| --- | --- |
| `emu.py` | Feldtabelle, Builder je Gerät (dieselben Topics und Formate wie die echte Firmware), Befehle, Szenarien |
| `emulator.py` | Broker-Verbindungen (eine je Gerät mit Last Will), Webserver, Selbsttest |
| `web/index.html` | Webseite; der Design-Block kommt aus `firmware/_common/web/camper-theme.css` (`tools/common/sync_common.py`) |

```bash
python3 tools/emulator/emulator.py --selftest     # ohne Broker, nur Standardbibliothek, läuft auch in CI
```

Ändert sich ein Topic oder Format an einem echten Gerät, den Builder in `emu.py` mitziehen.

Bis November 2026 war der Emulator ein ESP32-C3 mit eigener Firmware (`firmware/emulator`, letzte Version 1.3.0). Die Python-Fassung sendet dieselben Topics und Formate.

## Lizenz

MIT
