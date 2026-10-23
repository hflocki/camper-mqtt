# Stückliste (BOM) – eigener Aufbau

ESP32-S3 als LIN-Slave mit TJA1021, fahrzeugfeste 12-V-Versorgung, RJ12- und JST-Durchschleifung.
Preise sind grobe Richtwerte (Stand 2026), ohne Versand.

## Kernbauteile

| Pos | Menge | Bauteil | Wert / Typ | Bauform | Funktion | ca. € |
|---|---|---|---|---|---|---|
| U1 | 1 | ESP32-S3 Board | Waveshare ESP32-S3-Zero oder ESP32-S3-DevKitC-1 | Modul | Controller, natives USB (kein CH340) | 8–12 |
| U2 | 1 | LIN-Transceiver | **TJA1021T/20** (NXP) | SOIC-8 | LIN-Slave | 1–2 |
| U3 | 1 | DC/DC-Wandler | **Traco TSR 1-2450** (6,5–36 V → 5 V / 1 A) | SIP-3 | 12 V → 5 V | 5–7 |
| D1 | 1 | Schottky-Diode | **SS34** (3 A, 40 V) | SMA | Verpolschutz 12 V | 0,20 |
| D2 | 1 | TVS-Diode | **SMBJ24A** (unidirektional) | SMB | Schutz vor Spannungsspitzen | 0,30 |
| F1 | 1 | Polyfuse | 500 mA, ≥ 30 V | 1812 / THT | Absicherung Eingang | 0,30 |
| C1 | 1 | Elko / Keramik | 10 µF, **50 V** | 1206 / THT | Eingang Wandler | 0,20 |
| C2 | 1 | Keramik | 10 µF, 16 V | 0805 | Ausgang 5 V | 0,10 |
| C3 | 1 | Keramik | 100 nF, 50 V | 0805 | VBAT TJA1021 | 0,05 |
| C4 | 1 | Keramik | **220 pF**, 50 V | 0805 | LIN gegen GND (Slave) | 0,05 |
| C5 | 1 | Keramik | 100 nF, 16 V | 0805 | VCC 3,3 V am TJA1021 | 0,05 |
| R1 | 1 | Widerstand | 10 kΩ | 0805 | Pull-up RXD → 3,3 V | 0,02 |
| R2 | 1 | Widerstand | 10 kΩ | 0805 | SLP_N → 3,3 V (oder an GPIO) | 0,02 |
| R3 | 1 | Widerstand | 1 kΩ | 0805 | Vorwiderstand LED | 0,02 |
| LED1 | 1 | LED | grün oder blau | 0805 / 3 mm | Status (LIN/MQTT) | 0,10 |

**Nicht bestücken:** 1-kΩ-Master-Pull-up auf LIN. Den hat das CP Plus schon.

## Anschlüsse

| Pos | Menge | Bauteil | Typ | Funktion | ca. € |
|---|---|---|---|---|---|
| J1, J2 | 2 | RJ12-Buchse | 6P6C, Leiterplatte, liegend | LIN IN/OUT, alle 6 Pins 1:1 durchverbunden | 0,50–1 |
| J3, J4 | 2 | JST-PH Stiftleiste | 2-polig, 2,0 mm (vorher am CP-Plus-Stecker nachmessen) | 12 V vom Kabelbaum IN/OUT zum CP Plus | 0,20 |
| – | 2 | JST-PH Gehäuse + Crimpkontakte | 2-polig | Adapterkabel | 0,30 |
| – | 1 | RJ12-Kabel | 6-polig, 1:1, 1–2 m | zur zweiten LIN-Buchse oder als Verlängerung | 3–5 |

## Gehäuse und Montage

| Menge | Bauteil | Hinweis | ca. € |
|---|---|---|---|
| 1 | Gehäuse | Hammond 1551 (klein) oder 3D-Druck mit Ausschnitten für RJ12/USB-C | 0–6 |
| 4 | Abstandsbolzen M2/M2,5 | Platine im Gehäuse | 1 |
| 1 | Schutzlack (optional) | gegen Feuchtigkeit/Kondenswasser | 8 |

## Nur für Variante A (Lochraster)

| Menge | Bauteil | Hinweis | ca. € |
|---|---|---|---|
| 1 | Lochrasterplatine | 2,54 mm, doppelseitig, ca. 50 × 70 mm | 2 |
| 1 | SOIC-8 → DIP-Adapter | für den TJA1021 | 0,50 |
| 1 | Stift-/Buchsenleisten | ESP-Board steckbar | 1 |
| 1 | Kupferlackdraht / Silberdraht | Verdrahtung | 2 |

Bei Variante A lassen sich statt SMD-Kondensatoren und -Widerständen auch bedrahtete Typen mit gleichen Werten verwenden.

## Verbindungen (Kurzfassung)

| Von | Nach |
|---|---|
| 12 V (JST IN) → F1 → D1 (Anode) | D1 (Kathode) = „12 V geschützt“ |
| 12 V geschützt | D2 nach GND, C1 nach GND, U3 Eingang, TJA1021 **VBAT** (+ C3) |
| U3 Ausgang 5 V (+ C2) | ESP32-S3 **5V**-Pin |
| ESP32-S3 3,3 V | TJA1021 **VCC** (+ C5), R1, R2 |
| ESP GPIO (LIN TX) | TJA1021 **TXD** |
| ESP GPIO (LIN RX) | TJA1021 **RXD** (+ R1 nach 3,3 V) |
| 3,3 V über R2 | TJA1021 **SLP_N** |
| TJA1021 **LIN** | LIN-Ader J1/J2 (+ C4 nach GND) |
| GND | TJA1021 GND, U3 GND, ESP GND, Masse-Ader J1/J2, JST GND |

Die LIN-TX/RX-Pins trägst du im Profil `BOARD_ESP32S3_GENERIC` in `config.h` ein.

**Gesamt:** ca. **25–40 €**, je nach Board und Gehäuse.
