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

**Stopp** stellt die Werte von vorher wieder her, **Stopp & behalten** lässt den Endstand stehen.

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

Alles Gerätespezifische steht in `emu.cpp` (Feldtabelle, Builder, Befehle, Szenarien). Host-Test ohne ESP32:

```bash
firmware/emulator/tests/run.sh       # Prüfungen
firmware/emulator/tests/run.sh -v    # zusätzlich alle Payloads
```

Er sucht ArduinoJson im Bibliotheksordner der Arduino IDE, sonst `ARDUINOJSON=<…/ArduinoJson/src>` setzen.

## Versionen

| Version | Änderungen |
|---|---|
| 1.0.1 | Geräteliste lädt wieder: Schema wird stückweise gesendet statt als ein großer Block (der C3 hatte bei allen Broker-Verbindungen nicht mehr genug zusammenhängenden Speicher); Seite zeigt den Fehlergrund und versucht es erneut |
| 1.0.0 | Erste Version: 8 Geräte, Web-Oberfläche mit Werten, Zuständen und Szenarien, Befehle vom Display |

## Lizenz

MIT
