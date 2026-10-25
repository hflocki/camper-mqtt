# RoadBuddy mit der Arduino IDE

Anleitung zum Flashen des RoadBuddy mit der Arduino IDE (1.x oder 2.x). Eine Beschreibung der Funktionen, Seiten und MQTT-Topics steht in der [Haupt-README](../README.md). Wer lieber PlatformIO nutzt, findet die Anleitung [hier](../_platformio/README.md).

---

## Voraussetzungen

1. **Board-Paket** „M5Stack“ von M5Stack über den Boardverwalter installieren (Board-Manager-URL: `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json`).
2. **Bibliotheken** über den Bibliotheksverwalter:
   - **M5Unified** (zieht **M5GFX** automatisch mit)
   - **PubSubClient** von Nick O'Leary
   - **ArduinoJson** von Benoit Blanchon (für die Temperaturliste)
3. Falls vorhanden, die alte Bibliothek **M5Core2** entfernen (Ordner `Documents/Arduino/libraries/M5Core2` löschen oder verschieben). Sie ist mit neuen ESP32-Cores nicht mehr kompatibel und führt zum Fehler `rom/miniz.h: No such file or directory`.

---

## Flashen

1. Den Ordner [`roadbuddy/`](roadbuddy/) in der Arduino IDE öffnen (`roadbuddy.ino`). Der Ordner muss genauso heißen wie die Hauptdatei, sonst findet die IDE den Sketch nicht.
2. In diesem Ordner `config.example.h` nach `config.h` kopieren und die eigenen Zugangsdaten eintragen:

   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";

   const char* mqtt_server = "192.168.1.10";
   const int mqtt_port = 1883;
   const char* mqtt_user = "YOUR_MQTT_USER";
   const char* mqtt_pass = "YOUR_MQTT_PASSWORD";

   const char* mqtt_topic = "camper/level/#";
   ```

3. Unter *Werkzeuge → Board* **M5Core2** wählen (Eintrag unter „M5Stack“).
4. Den Core2 per USB-C verbinden, unter *Werkzeuge → Port* den Anschluss wählen (auf dem Mac z. B. `/dev/cu.usbserial-…`).
5. Hochladen.

Der Serial Monitor (115200 Baud) gibt jedes eingehende MQTT-Topic mit Wert aus, das hilft bei der Fehlersuche.

---

## Fehlersuche

- **`rom/miniz.h: No such file or directory`:** Die alte `M5Core2`-Bibliothek ist noch installiert, siehe Voraussetzungen Punkt 3.
- **`config.h: No such file or directory`:** `config.example.h` wurde noch nicht nach `config.h` kopiert.
- **„Multiple libraries were found for WiFi.h“:** Nur eine Warnung. Die WiFi-Bibliothek aus dem ESP32-Core wird verwendet. Eine zusätzliche `WiFi`-Bibliothek in `Documents/Arduino/libraries` kann gelöscht werden.
- **Display zeigt falsche Pitch-/Roll-Werte:** Im Serial Monitor prüfen, welche Topics wirklich ankommen.
- **Upload bricht ab oder Port fehlt:** Anderes USB-Kabel (Datenkabel) testen, bei Bedarf den Treiber für den USB-Seriell-Chip installieren.
- **Gerät wacht nicht auf:** Display kurz antippen. Hilft das nicht, den Power-Knopf links am Gehäuse drücken, um das Gerät neu zu starten.
- **Seite zeigt `--`:** Es wurde noch kein Wert empfangen. Prüfen, ob der Sensor mit `retain` sendet, sonst erscheint der Wert erst bei der nächsten Meldung.
