#ifndef CT_PLUGIN_H
#define CT_PLUGIN_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stddef.h>
#include "../../config.h"

// ============================================================
// Plugin interface
// ------------------------------------------------------------
// A plugin describes itself with three tables, the core does the rest:
//   Field  : its settings (stored in /config.json, shown in the web UI, validated by the core)
//   Entity : its Home Assistant entities (MQTT discovery, topics and commands)
//   live() : its current values for the web UI (gauge, rows, controls)
// New plugin = one .cpp file in src/plugins/ + one line in registry.cpp + a switch in config.h.
// See README.md, section "Eigenes Plugin".
//
// Threads: begin(), loop(), command(), publish(), live() and onConfigChanged() run in the
// Arduino loop task. onAdvertisement() runs in the NimBLE host task - copy the data there
// and process it in loop().
// ============================================================

class NimBLEAdvertisedDevice;

// ---- Settings --------------------------------------------------------------
enum FieldType : uint8_t {
    F_TEXT,     // char[size]; max = max. length (0 = size - 1)
    F_MAC,      // char[18], normalized to "AA:BB:CC:DD:EE:FF", empty allowed
    F_FLOAT,    // float, min..max
    F_INT,      // int32_t, min..max
    F_BOOL,     // bool
    F_SELECT,   // uint8_t index into options "Label 0|Label 1|..."
};

struct Field {
    const char* key;       // JSON key
    const char* label;     // Web UI label (German)
    FieldType type;
    uint16_t offset;       // offsetof() in the plugin's config struct
    uint16_t size;         // sizeof() of that member
    float min, max, step;
    const char* unit;      // Web UI only (nullptr = none)
    const char* options;   // F_SELECT: "A|B|C"
    const char* help;      // Hint below the input (nullptr = none)
};

#define FLD_TEXT(S, m, key, label, help)  {key, label, F_TEXT, (uint16_t)offsetof(S, m), (uint16_t)sizeof(((S*)0)->m), 0, 0, 0, nullptr, nullptr, help}
#define FLD_MAC(S, m, key, label, help)   {key, label, F_MAC, (uint16_t)offsetof(S, m), (uint16_t)sizeof(((S*)0)->m), 0, 0, 0, nullptr, nullptr, help}
#define FLD_FLOAT(S, m, key, label, unit, mn, mx, st, help) {key, label, F_FLOAT, (uint16_t)offsetof(S, m), (uint16_t)sizeof(float), mn, mx, st, unit, nullptr, help}
#define FLD_INT(S, m, key, label, unit, mn, mx, help) {key, label, F_INT, (uint16_t)offsetof(S, m), (uint16_t)sizeof(int32_t), mn, mx, 1, unit, nullptr, help}
#define FLD_BOOL(S, m, key, label, help)  {key, label, F_BOOL, (uint16_t)offsetof(S, m), (uint16_t)sizeof(bool), 0, 1, 1, nullptr, nullptr, help}
#define FLD_SELECT(S, m, key, label, opts, help) {key, label, F_SELECT, (uint16_t)offsetof(S, m), (uint16_t)sizeof(uint8_t), 0, 0, 1, nullptr, opts, help}

// ---- Home Assistant entities --------------------------------------------
enum EntityKind : uint8_t {
    E_SENSOR,   // state <prefix>/<id>/<key>
    E_BINARY,   // state ON / OFF
    E_SWITCH,   // state ON / OFF, command <prefix>/cmd/<id>/<key>/set
    E_SELECT,   // options "A|B|C", state = option text, command = option text
    E_BUTTON,   // command payload "PRESS"
    E_COVER,    // state <key> (open/closed/opening/closing), position <key>_position,
                // commands OPEN / CLOSE on <key>, position on <key>_position
};

// Entity flags
#define EF_DIAG      0x01   // entity_category diagnostic
#define EF_HIDDEN    0x02   // enabled_by_default false
#define EF_ALWAYS    0x04   // available while the ESP is online, even without the plugin's device
#define EF_CONFIG    0x08   // entity_category config (switches / selects that change settings)

struct Entity {
    EntityKind kind;
    const char* key;
    const char* name;          // German, shown in Home Assistant
    const char* unit;          // nullptr = none
    const char* devClass;      // nullptr = none
    const char* stateClass;    // nullptr = none
    const char* icon;          // nullptr = HA default
    uint8_t flags;
    const char* options;       // E_SELECT only
};

// Output used by publish(): topics are <prefix>/<plugin id>/<key>, retained
class MqttOut {
public:
    virtual ~MqttOut() {}
    virtual void text(const char* key, const char* value) = 0;
    virtual void num(const char* key, float value, int decimals) = 0;   // NAN = not published
    virtual void integer(const char* key, long value) = 0;
    void onoff(const char* key, bool on) { text(key, on ? "ON" : "OFF"); }
};

// ---- BLE usage --------------------------------------------------------------
#define BLE_NONE     0
#define BLE_PASSIVE  1   // listens to advertisements
#define BLE_LINK     2   // keeps a GATT connection

// ============================================================
// Plugin base class
// ============================================================
class Plugin {
public:
    // type     : short id of the plugin type ("rvtech"); instance ids are "rvtech", "rvtech2" ...
    // typeName : human readable type ("RV Tech roof hatch")
    // title    : default display name ("Gas"); instances 2+ get " 2", " 3" appended
    Plugin(const char* type, const char* typeName, const char* title, uint8_t index);
    virtual ~Plugin() {}

    const char* type;
    const char* typeName;
    uint8_t index;
    char id[16];
    char name[32];             // Display name (web UI, HA device), editable
    bool enabled = true;       // Switched in the web UI, applied at boot
    uint32_t updates = 0;      // Increment on relevant changes: MQTT publishes right away

    // ---- Settings ----
    virtual size_t fields(const Field*& out) { out = nullptr; return 0; }
    virtual void* cfg() { return nullptr; }          // Base address for Field::offset
    virtual void setDefaults() {}                     // Factory values of cfg()
    virtual void onConfigChanged() {}                 // After load and after every save from the web UI

    // ---- Lifecycle (only called when enabled) ----
    virtual uint8_t bleUse() const { return BLE_NONE; }
    virtual void begin() {}
    virtual void loop() {}

    // ---- BLE (NimBLE host task!) ----
    // addr is the upper case MAC "AA:BB:CC:DD:EE:FF"
    virtual void onAdvertisement(const NimBLEAdvertisedDevice* dev, const char* addr) {}
    // Configured device address (keeps it on top of the "nearby" list)
    virtual const char* bleAddress() const { return ""; }
    // Could this advertisement be a device of this plugin? (marks it in the "nearby" list)
    virtual bool bleCandidate(int32_t company, const char* uuid, const char* name) const { return false; }

    // ---- Web UI ----
    // Fills o with: "state" (text), "ok" (bool), optional "gauge" {v, unit, label, max},
    // "rows" [[label, value, unit], ...], "ctrl" [{t: switch|select|buttons|slider, ...}], "raw"
    virtual void live(JsonObject o) = 0;

    // ---- MQTT / Home Assistant ----
    virtual size_t entities(const Entity*& out) { out = nullptr; return 0; }
    virtual bool entityActive(const Entity& e) { return true; }  // false = removed from HA
    virtual void publish(MqttOut& out) {}
    virtual uint32_t publishIntervalMs() const { return 30000; }
    virtual bool available() { return true; }        // Device reachable (HA availability)
    virtual const char* manufacturer() const { return ""; }
    virtual const char* model() const { return typeName; }

    // Command from MQTT (<prefix>/cmd/<id>/<key>/set) or from the web UI (same key / value)
    virtual void command(const char* key, const char* value) {}
};

// Helpers for live() and command()
JsonArray liveRow(JsonObject o, const char* label);   // adds [label, ...] to o["rows"]
void liveRowNum(JsonObject o, const char* label, float v, int decimals, const char* unit);
void liveRowText(JsonObject o, const char* label, const char* v);
JsonObject liveCtrl(JsonObject o, const char* t, const char* key, const char* label);
bool payloadOn(const char* v);   // "ON", "on", "true", "1"

#endif // CT_PLUGIN_H
