#ifndef EMU_H
#define EMU_H

// Device emulator: one table of fields (values and settings of all emulated devices),
// payload builders that produce the same topics as the real firmware, commands from
// the display, scenarios and persistence (/emu.json). No networking in here: mqtt_ha.cpp
// owns the broker connections and asks this module what to send.

#include <Arduino.h>
#include <ArduinoJson.h>

enum EmuDev : uint8_t {
    D_TANK, D_LEVEL, D_HEATER, D_FAN, D_TEMP, D_BATTERY,   // own MQTT connection with last will
    D_ROUTER, D_ZIGBEE,                                     // sent over the emulator's own connection
    D_COUNT
};

// Wanted state of an emulated device (set in the web UI or by a scenario)
enum EmuMode : uint8_t {
    M_OFF = 0,       // nothing is sent, its retained topics are deleted
    M_ON = 1,        // online, values are sent
    M_OFFLINE = 2    // connection dropped like a power cut (broker sends the last will "offline");
                     // router / Zigbee: paused, the last retained values stay
};

struct EmuDevDef {
    const char* id;       // short id (web UI, emu.json)
    const char* name;     // German name for the web UI
    const char* preKey;   // field holding the base topic
    bool own;             // own connection with status + last will
};
extern const EmuDevDef EMU_DEVS[D_COUNT];

void emuInit();       // defaults, then /emu.json (LittleFS must be mounted)
void emuLoop();       // scenarios, heater dynamics, noise, deferred save

// ---- state for the MQTT part (loop task) ----
uint8_t emuMode(uint8_t dev);
String emuPrefix(uint8_t dev);           // base topic of a device ("" for router / Zigbee)
String emuZigbeeCmdFilter();             // subscription for Zigbee light commands
bool emuTakeClearAll();                  // true once after "delete all retained topics"
bool emuTakeTopicChange(uint8_t dev);    // true once after a base topic of this device changed

// Payload builder: calls emit(topic, payload) for every retained topic of the device
typedef void (*EmuEmit)(const String& topic, const String& payload, void* ctx);
void emuBuild(uint8_t dev, EmuEmit emit, void* ctx);

// Not retained one-shot messages (e.g. the awning command of the departure scenario)
typedef void (*EmuSend)(const String& topic, const String& payload);
void emuSetSender(EmuSend fn);

// A command message from the broker (display, Home Assistant, mosquitto_pub)
void emuCommand(const char* topic, const char* payload, unsigned int len);

// ---- web UI (async web task, guarded by a mutex inside) ----
void emuSchemaJson(JsonDocument& doc);                 // field and device definitions, once per page load
void emuStateJson(JsonObject obj);                     // values, modes, scenario, command log
bool emuSetValue(const char* key, const char* value, String& err);
bool emuSetModeReq(uint8_t dev, uint8_t mode);
void emuSetNoise(uint8_t dev, bool on);
bool emuStartScenario(const char* id, uint8_t dev, String& err);
void emuStopScenario(bool restore);
void emuResetValues(int dev);                          // -1 = all devices back to the example data
void emuRequestClearAll();

// Serial command "read"
void emuPrint();

#endif // EMU_H
