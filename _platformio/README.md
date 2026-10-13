# RoadBuddy mit PlatformIO

Anleitung zum Bauen und Flashen des RoadBuddy mit PlatformIO (VS Code oder Kommandozeile). Eine Beschreibung der Funktionen, Seiten und MQTT-Topics steht in der [Haupt-README](../README.md). Wer lieber die Arduino IDE nutzt, findet die Anleitung [hier](../_arduino/README.md).

---

## Voraussetzungen

- [PlatformIO](https://platformio.org/) als VS-Code-Erweiterung („PlatformIO IDE“) oder als Kommandozeilenwerkzeug (`pip install platformio`)
- Core2 per USB-C am Rechner

Plattform, Board und Bibliotheken (M5Unified, PubSubClient) lädt PlatformIO beim ersten Build selbst herunter, siehe [`platformio.ini`](platformio.ini).

---

## Bauen und Flashen

1. Den Ordner `_platformio/` als Projekt öffnen (in VS Code: *PlatformIO Home → Open Project*).
2. Im Ordner `include/` die Datei `config.example.h` nach `config.h` kopieren und die eigenen Zugangsdaten eintragen:

   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";

   const char* mqtt_server = "192.168.1.10";
   const int mqtt_port = 1883;
   const char* mqtt_user = "YOUR_MQTT_USER";
   const char* mqtt_pass = "YOUR_MQTT_PASSWORD";

   const char* mqtt_topic = "camper/level/#";
   ```

3. Bauen und hochladen:

   ```bash
   cd _platformio
   pio run -t upload
   ```

   In VS Code entsprechend über die Schaltflächen *Build* und *Upload* in der Statusleiste.

4. Serielle Ausgabe ansehen (115200 Baud). Dort erscheint jedes eingehende MQTT-Topic mit Wert:

   ```bash
   pio device monitor
   ```

---

## Projektstruktur

```
_platformio/
├── platformio.ini            – Board, Framework, Bibliotheken
├── src/main.cpp              – kompletter Sketch
└── include/
    ├── config.example.h      – Vorlage für WLAN- und MQTT-Daten
    └── config.h              – eigene Zugangsdaten (nicht im Git)
```

---

## Hinweise zu den Versionen

- Die Datei `platformio.ini` nutzt die offizielle Plattform `espressif32`. Sie bringt eine ältere Version des Arduino-ESP32-Cores mit als die Arduino IDE. Der Sketch ist davon unabhängig, M5Unified läuft mit beiden.
- Wer den neueren Arduino-Core 3.x möchte, kann stattdessen die Community-Plattform [pioarduino](https://github.com/pioarduino/platform-espressif32) eintragen (`platform = ` mit der dort angegebenen URL).
- Für reproduzierbare Builds lassen sich die Bibliotheken in `lib_deps` auf feste Versionen festlegen, zum Beispiel `m5stack/M5Unified@^0.2.0`.

---

## Fehlersuche

- **`config.h: No such file or directory`:** `include/config.example.h` wurde noch nicht nach `include/config.h` kopiert.
- **Upload-Port wird nicht gefunden:** Mit `pio device list` den Port prüfen und bei Bedarf in `platformio.ini` `upload_port = …` ergänzen. Außerdem ein Datenkabel verwenden.
- **Upload bricht ab:** In `platformio.ini` `upload_speed` auf `460800` oder `115200` senken.
- **Display zeigt falsche Pitch-/Roll-Werte:** Im Serial Monitor prüfen, welche Topics wirklich ankommen.
- **Gerät wacht nicht auf:** Display kurz antippen. Hilft das nicht, den Power-Knopf links am Gehäuse drücken, um das Gerät neu zu starten.
