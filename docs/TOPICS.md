# MQTT-Topics

Alle Topics der Camper-Geräte auf einen Blick. Die Tabellen nennen die Vorgaben ab Werk; das Basis-Topic jedes Geräts (`<prefix>`) lässt sich in seiner Web-Oberfläche ändern. Einzelheiten zu Werten und Einheiten stehen in der README des jeweiligen Geräts.

## Gemeinsames Schema

| Topic | Inhalt |
| --- | --- |
| `camper/<gerät>/status` | `online` / `offline`, Last Will, retained |
| `camper/<gerät>/info` | Diagnose-JSON, retained, beim Verbinden und jede Minute: `version`, `ip`, `mac`, `rssi`, `uptime` (s), `reset`, `heap` (`devinfo.*` aus `firmware/_common`) |
| `camper/<gerät>/rssi` | WLAN-Signal in dBm, retained |
| `camper/<gerät>/…` | Messwerte und Zustände, retained |
| `camper/<gerät>/cmd/…` bzw. `…/set/…` | Befehle, **nicht** retained |
| `homeassistant/<komponente>/<node>/<id>/config` | Home-Assistant-Discovery, retained (wer HA nicht nutzt, kann sie ignorieren) |

Das Display liest `camper/+/status` und `camper/+/info` für die Geräteliste auf der System-Seite.

## Übersicht

| Gerät | Basis-Topic | Firmware | README |
| --- | --- | --- | --- |
| Tanks (Frischwasser, Grauwasser, Gas) | `camper/tanklevel` | tanklevel | [firmware/tanklevel](../firmware/tanklevel/README.md) |
| Levelsensor | `camper/level` | level | [firmware/level](../firmware/level/README.md) |
| Heizung (Truma) | `camper/heater` | heater | [firmware/heater](../firmware/heater/README.md) |
| Dachlüfter (MaxxFan) | `camper/maxxfan` | maxxfan | [firmware/maxxfan](../firmware/maxxfan/README.md) |
| Temperaturen (Kühlschrank …) | `camper/temp` | temp | [firmware/temp](../firmware/temp/README.md) |
| Batterien (BMS) | `camper/battery` | battery | [firmware/battery](../firmware/battery/README.md) |
| Toolbox (Bluetooth-Geräte als Plugins: Dachhaube, Pumpe, Kühlschrank) | `camper/toolbox` | toolbox | [firmware/toolbox](../firmware/toolbox/README.md) |
| Display (Wandpanel) | `camper/display` | display | [firmware/display](../firmware/display/README.md) |
| Road Buddy (M5Stack Core2) | `camper/roadbuddy` | roadbuddy | [firmware/roadbuddy](../firmware/roadbuddy/README.md) |
| Emulator (Testgerät) | `camper/emulator` (+ die Topics der nachgebildeten Geräte) | emulator | [firmware/emulator](../firmware/emulator/README.md) |
| Muster-Gerät | `camper/example` | template | [firmware/_template](../firmware/_template/README.md) |
| Router RUT956 | `camper/rut`, `camper/door` | Teltonika „Data to Server“ | – |
| Zigbee-Gateway (SMLIGHT) | `camper/zigbee/data`, `camper/zigbee/cmd` | Gateway | [firmware/display](../firmware/display/README.md#sensoren-zigbee--bluetooth) |
| Bluetooth-Gateway (OpenMQTTGateway) | `camper/OMG` | omg | [firmware/omg](../firmware/omg/README.md) |

## Tanks `camper/tanklevel`

| Topic | Inhalt |
| --- | --- |
| `…/water/percent`, `liters`, `voltage` | Frischwasser |
| `…/water/filter_active` | `ON` / `OFF` |
| `…/grey/percent`, `liters`, `voltage` | Grauwasser (nur wenn eingeschaltet) |
| `…/grey/filter_active` | `ON` / `OFF` |
| `…/gas/level`, `raw_level`, `content` | Gasflasche in %, Rohwert, Restmenge in kg (erst nach dem ersten gültigen Wert) |
| `…/gas/battery`, `ble_rssi`, `ble_state` | Gassensor: Akku, Bluetooth-Signal, `OK` oder Fehler |
| `…/gas/distance`, `temperature`, `quality` | nur Mopeka: Füllhöhe in mm, Sensortemperatur, Messqualität 0–3 |
| `…/cmd/water/filter/set`, `…/cmd/grey/filter/set` | `ON` / `OFF` / `TOGGLE` |

## Levelsensor `camper/level`

| Topic | Inhalt |
| --- | --- |
| `…/pitch`, `…/roll` | Längs- und Querneigung in ° (vorne höher bzw. links höher = positiv) |
| `…/wedge_fl`, `wedge_fr`, `wedge_rl`, `wedge_rr` | Keilhöhe in cm je Rad |
| `…/is_level` | `ON`, wenn innerhalb der Toleranz |
| `…/sensor` | `OK` / `ERROR` |
| `…/tolerance`, `track_width`, `wheelbase`, `mount`, `tare_active`, `tare_pitch`, `tare_roll`, `filter_active`, `mqtt_fast` | Einstellungen |
| `…/cmd/tare/set`, `…/cmd/tare_reset/set`, `…/cmd/sensor_restart/set` | `PRESS` |
| `…/cmd/tolerance/set`, `track_width/set`, `wheelbase/set`, `mount/set`, `tare_pitch/set`, `tare_roll/set` | Wert |
| `…/cmd/filter/set`, `…/cmd/mqtt_speed/set` | `ON` / `OFF` / `TOGGLE` |

## Heizung `camper/heater`

Unterstruktur wie bei inetbox2mqtt. Vollständige Liste der Keys in der [README der Heizung](../firmware/heater/README.md#mqtt).

| Topic | Inhalt |
| --- | --- |
| `…/control_status/<key>` | Zustand, z. B. `current_temp_room`, `target_temp_room`, `heating_mode`, `current_temp_water`, `water_mode`, `operating_status_text`, `error_code`, `heater_error`, `hvac_mode`, `hvac_action`, `room_setpoint`, `fan_mode`, `timer`, `decoke` |
| `…/set/<key>` | Befehle, z. B. `target_temp_room`, `heating_mode`, `water_mode`, `hvac_mode`, `room_setpoint`, `fan_mode`, `timer`, `decoke`, `heater_model`, `sniffer`, `reboot` |
| `…/log` | Logzeilen (nicht retained), Stufe in der Web-Oberfläche einstellbar |

## Dachlüfter `camper/maxxfan`

`p<n>` ist der Anschluss am VanTurtle-Board (`p1`, `p2`).

| Topic | Inhalt |
| --- | --- |
| `…/p<n>/auto_hold`, `…/p<n>/led` | `ON` / `OFF` |
| `…/expander` | `ON` / `OFF` (TCA9555 erreichbar) |
| `…/cmd/p<n>/<taste>` | Taste drücken: `auto`, `direction`, `on_off`, `faster`, `slower`, `beep` |
| `…/cmd/p<n>/press` | Tastenname im Payload |
| `…/cmd/p<n>/speed/set` | `0` … `10`, `0` = Haube schließen |
| `…/cmd/p<n>/close` | Haube schließen |
| `…/cmd/p<n>/led/set` | `ON` / `OFF` / `TOGGLE` |
| `…/cmd/sync_off`, `…/cmd/reboot` | beliebig |

## Temperaturen `camper/temp`

| Topic | Inhalt |
| --- | --- |
| `…/<id>` | Temperatur in °C |
| `…/<id>/available` | `online` / `offline` (Sensor am Bus) |
| `…/<id>/alarm`, `…/alarm` | `ON` / `OFF` je Sensor bzw. für alle |
| `…/alarm_text` | lesbarer Text aller Alarme, leer = keiner |
| `…/sensors` | alle Sensoren als JSON-Liste für Displays: `[{"id","name","t","alarm"}, …]` |

## Batterien `camper/battery`

| Topic | Inhalt |
| --- | --- |
| `…/<id>/available` | `online`, solange das BMS antwortet |
| `…/<id>/soc`, `voltage`, `current`, `power`, `charge`, `energy`, `runtime`, `charging` | Messwerte (Strom positiv = Laden) |
| `…/<id>/problem`, `…/<id>/problem_text` | Fehler des BMS |
| `…/<id>/temperature`, `cycles`, `delta`, `cell_1` … | nur Daly |
| `…/batteries` | alle Batterien als JSON-Liste für Displays |

## Toolbox `camper/toolbox`

Jedes Plugin hat eine eigene ID: `rvtech` (Dachhaube), `pump` (CaraWater), `fridge` (WCS). Gas liest camper-tanklevel. Die zweite Instanz desselben Plugins bekommt eine `2` angehängt (`rvtech2`). Befehle haben die Form `…/cmd/<plugin>/<key>/set`.

| Topic | Inhalt |
| --- | --- |
| `…/<plugin>/available` | `online`, solange das Bluetooth-Gerät erreichbar ist |
| `…/<plugin>/state` | Verbindungsstatus als Text („Verbunden“, „Nicht erreichbar“ …) |
| `…/<plugin>/connected`, `…/<plugin>/release` | `ON` / `OFF`: Verbindung steht, Bluetooth für die Hersteller-App freigegeben |
| `…/rvtech/cover` | Dachhaube `open` / `closed` / `opening` / `closing` |
| `…/rvtech/cover_position` | Position 0 … 100 % |
| `…/rvtech/reported` | gemeldeter Rohwert (0 … 100, `102` = Lüften) |
| `…/cmd/rvtech/cover/set` | `OPEN` / `CLOSE` |
| `…/cmd/rvtech/cover_position/set` | `0` … `100` |
| `…/cmd/rvtech/<preset>/set` | Voreinstellung `auf`, `zu`, `regen`, `lueften` (Payload beliebig) |
| `…/cmd/<plugin>/release/set` | `ON` / `OFF`: Bluetooth für die App freigeben |
| `…/pump/running`, `pressure`, `flow`, `voltage`, `current`, `temperature`, `daily_liters`, `total_liters`, `leak` … | Wasserpumpe; Befehle `enable`, `leak`, `pressure_level` |
| `…/fridge/temp1`, `set1`, `temp2`, `set2`, `voltage`, `battery`, `message` | Kühlschrank |

## Display `camper/display`

| Topic | Inhalt |
| --- | --- |
| `…/ip`, `…/brightness`, `…/screen` | Zustand |
| `…/awning` | Markise `ON` / `OFF` / `unknown` |
| `…/driving` | Abfahrtscheck: `ON` während der Fahrt |
| `…/depart` | Abfahrtscheck: `OK` oder die offenen Teile |
| `…/cmd/brightness` | `5` … `100` |
| `…/cmd/screen` | `ON` / `OFF` |
| `…/cmd/page` | Seitenname oder Nummer |
| `…/cmd/awning` | `ON` / `OFF` / `TOGGLE` (nur ohne Kontakt-Sensor) |
| `…/cmd/reboot` | beliebig |

Das Display sendet die Befehle der Dachhaube an `camper/toolbox/cmd/rvtech/…` (Voreinstellungen, Position). Es liest außerdem alle Geräte oben, die Router-Topics, die Gateways und die Topics der eigenen Kacheln und Automationen (frei eingestellt, z. B. `zigbee2mqtt/…`).

## Road Buddy `camper/roadbuddy`

Sendet nur `status` (Last Will) und `info`. Liest `camper/level/…`, `camper/tanklevel/…`, `camper/temp/sensors`, `camper/battery/batteries`, die `status`-Topics dieser Geräte und das Mobilfunk-Topic des Routers (`ROUTER_TOPIC`).

## Router RUT956

Kommt vom Router selbst („Data to Server“), nicht aus diesem Repo.

| Topic | Inhalt | Wer liest |
| --- | --- | --- |
| `camper/rut` | JSON, retained: `TIME` und `GPS` mit `latitude`, `longitude`, `altitude`, `speed` (km/h), `accuracy`, `satellites`, `fix_status`, `timestamp`; Werte teils als Text | Display: Abfahrtscheck; lässt sich auch als GPS-Topic fürs Wetter eintragen |
| `camper/door` | `{"Door":{"name":"din2","type":"Digital In","state":"open"\|"close","custom_name":"Door"}}` | Display: Abfahrtscheck |
| `camper/router/mobile` | Mobilfunk-Signal als JSON (`rsrp`, `rssi`, Netztyp) | Display (Balken in der Kopfzeile), Road Buddy |

`camper/router/mobile` ist die Vorgabe in Display und Road Buddy. Ob und unter welchem Topic der Router die Mobilfunkdaten sendet, ist noch nicht bestätigt; bis dahin kommen sie nur vom Simulator.

## Gateways

| Topic | Inhalt |
| --- | --- |
| `camper/zigbee/data/<ieee>/1/<cluster>/<attribut>` | Zigbee-Attribute vom SMLIGHT-Gateway, z. B. `0006/0000` Kontakt, `0402/0000` Temperatur, `0405/0000` Luftfeuchte, `0001/0021` Batterie |
| `camper/zigbee/cmd/<ieee>/1/<cluster>/<befehl>` | Befehle an Zigbee-Lampen und -Schalter (vom Display) |
| `camper/OMG/BTtoMQTT/<MAC>` | Bluetooth-Thermometer über OpenMQTTGateway (`tempc`, `hum`, `batt` …) |

## Werkzeuge

Der **Emulator** (`firmware/emulator`, ESP32-C3 mit Web-Oberfläche) sendet die Topics von tanklevel, level, heater, maxxfan, temp, battery, Router und Zigbee-Gateway in denselben Formaten wie die echten Geräte, jedes Gerät über eine eigene Verbindung mit Last Will. Er selbst meldet sich unter `camper/emulator/status` und `camper/emulator/info`; die `info` der nachgebildeten Geräte trägt die Version `emu-…`. Befehle (`…/cmd/…`, `…/set/…`, Zigbee `camper/zigbee/cmd/…`) setzt er wie das echte Gerät um. Home-Assistant-Discovery sendet er nicht.

Der Simulator `tools/mqtt/sim.py` sendet dieselben Topics wie die Firmware (retained), dazu `camper/rut`, `camper/door` und `camper/router/mobile` wie der Router und mit `--contact`, `--fridge`, `--outdoor` Zigbee-Werte. Beim Szenario `depart` schickt er `camper/display/cmd/awning`. Mit `--cleanup` löscht er beim Beenden alle retained Topics, die er gesendet hat.

## Alte Topics aufräumen

Retained Nachrichten bleiben am Broker, bis man sie mit einer leeren Nachricht überschreibt. Diese stammen von früheren Versionen oder älteren Simulator-Läufen und werden nicht mehr gebraucht:

| Topic | Herkunft |
| --- | --- |
| `camper/router/gps` | Simulator bis display-v2.9.15 (jetzt `camper/rut`) |
| `camper/waterlevel/#`, `camper/gaslevel/#` | Einzelprojekte vor `camper/tanklevel` |
| `camper/truma/#`, `service/truma/#` | frühere Heizungs-Firmware (bis heater 1.0 `service/truma`); nur löschen, wenn kein Gerät mehr darauf eingestellt ist |
| `homeassistant/sensor/camper_water/#`, `homeassistant/switch/camper_water/#`, `homeassistant/sensor/camper_gaslevel/#` | Discovery der Einzelprojekte |

Vorher nachsehen, was wirklich noch da ist (zeigt nur retained Nachrichten, nach 3 s Ende):

```bash
mosquitto_sub -h <broker> -t 'camper/#' -v --retained-only -W 3
```

Ein einzelnes Topic löschen:

```bash
mosquitto_pub -h <broker> -t camper/router/gps -r -n
```

Bei `#`-Einträgen jedes Topic einzeln löschen, das `mosquitto_sub` zeigt (`mosquitto_pub` kennt keine Wildcards).
