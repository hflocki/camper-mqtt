# Camper Emulator

> **IN ENTWICKLUNG** – noch nicht am echten Testnetz ausprobiert.

Testgerät für den Verbund [camper-mqtt](../../README.md): Ein ESP32-C3 sendet die MQTT-Topics der fest im Camper verbauten Geräte mit Beispielwerten. So lassen sich das Display, der Road Buddy und Home Assistant zu Hause testen, während Tanks, Heizung & Co. im Fahrzeug bleiben. Werte, Geräte und Szenarien stellst du in der Web-Oberfläche ein.

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

Die Formate sind dieselben wie bei der echten Firmware bzw. beim Router und SMLIGHT-Gateway (siehe [`docs/TOPICS.md`](../../docs/TOPICS.md)). Jedes Gerät hat eine eigene Broker-Verbindung mit eigenem Last Will, `…/status` und `…/info` (Version `emu-<Firmware-Version>`), daher erscheint es in der Geräteliste des Displays wie das echte. Home-Assistant-Discovery sendet der Emulator nicht, damit keine doppelten Entitäten entstehen.

## Bedienung

**Geräte:** eine Karte je Gerät mit drei Zuständen:

| Zustand | Wirkung |
| --- | --- |
| **An** | online, Werte gehen raus (Änderungen sofort, alles jede Minute) |
| **Offline** | Verbindung reißt ab wie bei Stromausfall: Der Broker setzt `…/status` per Last Will auf `offline`, die letzten Werte bleiben stehen. Bei Router und Zigbee heißt das **Pause** (nichts Neues senden) |
| **Aus** | nichts senden, die retained Topics des Geräts werden gelöscht – für ein echtes Gerät am selben Broker |

Aufgeklappt zeigt die Karte alle Werte mit Schiebereglern, Schaltern und Auswahllisten. Ungültige Eingaben lehnt das Gerät mit einer Meldung ab. **Mehr** zeigt Einstellungen wie Basis-Topics, IDs und IEEE-Adressen, **Rauschen** lässt Messwerte leicht schwanken (wie echte Sensoren), **Beispielwerte** setzt das Gerät zurück. Alle Werte bleiben nach einem Neustart erhalten (`/emu.json`).

Wenn sich etwas Abgeleitetes ändert, rechnet der Emulator wie das Gerät: Literangabe aus Prozent und Tankgröße, Keilhöhen aus Neigung, Rad- und Spurweite, Alarmtext der Temperaturen, Leistung, Restlaufzeit und Laden der Batterie, `hvac_action` und Betriebstext der Heizung. Die Raumtemperatur läuft bei eingeschalteter Heizung langsam auf die Solltemperatur zu (abschaltbar).

**Eigene Zigbee-Adressen:** Trägst du unter *Zigbee → Mehr* die IEEE-Adressen deiner echten Kontakte und Klimasensoren ein, passt die Sensor-Liste des Displays ohne Änderung.

**Szenarien:** laufen auf dem Gerät, der Browser kann zu sein.

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

**Eigene Szenarien:** ⬇ lädt ein Szenario oder unter *Eigene Szenarien* alle als JSON herunter. Datei ändern, hochladen, und neue Szenarien stehen mit dem Zeichen *eigenes* in der Liste. Die Standard-Szenarien bleiben immer erhalten: Einträge mit deren `id` werden beim Hochladen übersprungen, für eine geänderte Fassung also eine neue `id` vergeben. Ein eigenes Szenario mit gleicher `id` wird ersetzt, 🗑 löscht es. Fehlerhafte Dateien werden ganz abgelehnt, die Meldung nennt Szenario und Stelle. Gespeichert wird im LittleFS (`/scen/<id>.json`), ein Firmware-Update behält sie.

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
| `modes` | Zustand je Gerät über die Zeit: `on`, `offline` (Router/Zigbee: Pause), `off`. Geräte: `tanklevel`, `level`, `heater`, `maxxfan`, `temp`, `battery`, `router`, `zigbee`; `$dev` = das in der Liste gewählte Gerät |
| `publish` | einmalige Nachrichten `[Sekunde, Topic, Payload]`, nicht retained. `@feld` nimmt das Topic aus dem Feld (z. B. `@awn_t` = Markisen-Befehl) |
| `on_stop` | Nachrichten beim Stoppen `[Topic, Payload]` |
| `end`, `keep` | optional: Szenario stoppt nach `end` Sekunden von selbst; `"keep": true` lässt dann den Endstand stehen |

Feldnamen sind die Schlüssel aus der Feldtabelle in `emu.cpp` (z. B. `w_pct`, `gas_pct`, `pitch`, `roll`, `room`, `setp`, `spd`, `door`, `t1_t`, `b1_soc`, `c1_open`, `k1_t`). Am einfachsten ein Standard-Szenario herunterladen und als Vorlage nehmen. Werte werden wie bei der Eingabe in der Web-Oberfläche geprüft (Bereich, Auswahl). Topic-Felder (Basis-Topics, IDs, IEEE-Adressen) kann ein Szenario nicht ändern. Grenzen: 20 eigene Szenarien, je Szenario 32 Verläufe mit höchstens 60 Punkten, 20 Nachrichten, 8 kB; eine Upload-Datei bis 32 kB. Weitere Schlüssel (z. B. `"_kommentar"`) werden ignoriert und bleiben in der Datei erhalten.

**Empfangene Befehle:** die letzten 16 Befehle vom Display (oder `mosquitto_pub`) mit Topic und Inhalt – so siehst du, ob ein Knopf am Display das Richtige sendet.

**Aufräumen:** *Retained Topics löschen* entfernt alles, was der Emulator gesendet hat, vom Broker. Vor dem endgültigen Abschalten nutzen, sonst stehen die Beispielwerte weiter am Broker (ein abgeschalteter Emulator meldet seine Geräte über den Last Will als `offline`).

## Hardware

Ein beliebiges ESP32-C3-Board (z. B. Super Mini), nur USB-Strom. Keine Sensoren.

## Arduino IDE

**Board-Paket:** esp32 von Espressif **3.3.x**

| Einstellung | Wert |
|---|---|
| Board | ESP32C3 Dev Module |
| USB CDC On Boot | Enabled |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** |

**Bibliotheken:**

| Bibliothek | Autor | Version |
|---|---|---|
| PubSubClient | Nick O'Leary | 2.8 |
| ArduinoJson | Benoit Blanchon | 7.4.2 |
| ESPAsyncWebServer | ESP32Async | 3.12.1 |
| AsyncTCP | ESP32Async | 3.5.0 |

**Erstes Flashen (USB):**
1. `config_local.h.example` nach `config_local.h` kopieren und WLAN und Broker des Testnetzes eintragen.
2. Sketch hochladen (oder die `.bin` aus dem Release mit einem Web-Flasher).
3. `http://<ip>/` bzw. `http://camper-emulator.local/` öffnen, beim ersten Mal `data/index.html.gz` hochladen.

Ohne `config_local.h` startet das Setup-WLAN `Camper-Emulator-XXXXXX` (192.168.4.1).

## Updates ohne USB

| Was | Wie |
|---|---|
| Firmware | `.bin` aus dem Release (`camper-emulator-esp32c3.bin`) im Web-UI unter **WiFi → Firmware- & Web-Update** hochladen |
| Web-Oberfläche | `data/index.html.gz` hochladen, ohne Neustart, Werte bleiben |

## MQTT

| Topic | Inhalt |
|---|---|
| `camper/emulator/status` | `online` / `offline` (Last Will der eigenen Verbindung) |
| `camper/emulator/info`, `…/rssi` | Diagnose wie bei allen Geräten |
| `camper/<gerät>/…` | die nachgebildeten Geräte, siehe oben |

Läuft ein echtes Gerät am selben Broker, das nachgebildete auf **Aus** stellen – sonst überschreiben sich beide.

## Serieller Monitor (115200 Baud)

`read` (alle Werte), `clear` (retained Topics löschen), `noauth` (Web-Login löschen), `help`.

## Entwickeln

Alles Gerätespezifische steht in `emu.cpp` (Feldtabelle, Builder, Befehle, Szenarien; die Standard-Szenarien als JSON in `BUILTIN_SCEN`). Host-Test ohne ESP32:

```bash
firmware/emulator/tests/run.sh       # Prüfungen
firmware/emulator/tests/run.sh -v    # zusätzlich alle Payloads
```

Er sucht ArduinoJson im Bibliotheksordner der Arduino IDE, sonst `ARDUINOJSON=<…/ArduinoJson/src>` setzen.

## Sicheres Update, Ereignisse und Rückfall

Seit 1.2.0 hat jede Firmware eine Kennung (`CAMPER-FW:emulator:<version>:<chip>;`) und dieselbe Absicherung wie alle camper-mqtt-Geräte (`fwcheck.*`, `evlog.*`, `safeguard.*` aus `firmware/_common`):

- **Prüfung beim Update** (Web-UI unter **WiFi → Firmware- & Web-Update**): Die installierte Version steht dabei. Eine gewählte Datei wird schon im Browser und beim Hochladen im Gerät noch einmal geprüft, bevor etwas aktiviert wird. Abgelehnt werden eine `.merged.bin` (komplettes Flash-Abbild für USB), Firmware für ein anderes Gerät oder einen anderen Chip und zu große Dateien. Eine Datei ohne Kennung (Version vor 1.2.0) geht nur mit „Ohne Kennung hochladen“. Fehler stehen im Klartext da statt „Update failed“.
- **Ereignisse:** Die letzten Ereignisse (Start mit Grund, WLAN und MQTT verbunden/getrennt, Uploads mit Ergebnis, Rückfall, gespeicherte Einstellungen) stehen in der Web-Oberfläche. Sie bleiben bei Neustart, Absturz und Update erhalten, nach einem Stromausfall ist das Log leer.
- **Rückfall nach Update:** Hat eine neue Firmware nach 5 Minuten kein WLAN oder startet sie mehr als dreimal neu, startet wieder die vorherige. Kam das Update über den Setup-AP (ohne WLAN), wird nur auf Neustart-Schleifen geachtet.
- **WLAN-Änderung mit Rückweg:** Werden die WLAN-Daten geändert, während das Gerät verbunden ist, merkt es sich die funktionierenden. Klappt die Verbindung mit den neuen nach dem Neustart etwa 1 Minute lang nicht, kommen die alten zurück.

## Versionen

| Version | Änderungen |
|---|---|
| 1.2.0 | Sicheres Update (Kennung, Prüfung im Browser und im Gerät, Fehler im Klartext), Ereignis-Log, Rückfall auf die vorherige Firmware und WLAN-Änderung mit Rückweg |
| 1.1.0 | Szenarien als JSON herunterladen (einzeln oder alle) und eigene hochladen; die Standard-Szenarien bleiben und sind selbst im selben Format beschrieben. Gerätewahl direkt am Szenario, optionales Ende (`end`). „Fahrt“ schwankt jetzt in Dreiecksform statt Sinus |
| 1.0.1 | Geräteliste lädt wieder: Schema wird stückweise gesendet statt als ein großer Block (der C3 hatte bei allen Broker-Verbindungen nicht mehr genug zusammenhängenden Speicher); Seite zeigt den Fehlergrund und versucht es erneut |
| 1.0.0 | Erste Version: 8 Geräte, Web-Oberfläche mit Werten, Zuständen und Szenarien, Befehle vom Display |

## Lizenz

MIT
