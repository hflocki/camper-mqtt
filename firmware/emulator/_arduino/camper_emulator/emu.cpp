#include "emu.h"

#include <math.h>
#include <time.h>

#ifndef EMU_HOST_TEST
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

// ============================================================
// Field table
// ============================================================
// Every value and setting of every emulated device is one field. The web UI is built
// from this table (GET /api/emu/schema), emu.json stores the values by key, and the
// builders below read them by key. Values are kept as text: numbers as typed, bools as
// ON / OFF, enums as one of the options.

enum FType : uint8_t { T_NUM, T_TEXT, T_BOOL, T_ENUM };

enum : uint8_t {
    FL_SET   = 1,    // setting (tank size, wheelbase ...), not a measured value
    FL_TOPIC = 2,    // a topic: changing it deletes the old retained topics
    FL_OPT   = 4,    // number that may be empty (e.g. "no alarm limit")
    FL_ADV   = 8,    // shown under "more" in the web UI
};

struct FieldDef {
    const char* key;     // unique key (web UI, emu.json)
    uint8_t dev;
    const char* group;   // heading inside the device card ("" = top)
    FType type;
    uint8_t flags;
    const char* label;
    const char* unit;
    float min, max, step;
    uint8_t dec;         // decimals when published
    float noise;         // +- random amplitude while noise is on (0 = none)
    const char* def;     // example value
    const char* opts;    // enum: "a,b,c" or "value=label,..."
};

#define NUM(k, d, g, fl, lbl, u, mn, mx, st, dc, nz, df) {k, d, g, T_NUM, fl, lbl, u, mn, mx, st, dc, nz, df, ""}
#define TXT(k, d, g, fl, lbl, df)                      {k, d, g, T_TEXT, fl, lbl, "", 0, 0, 0, 0, 0, df, ""}
#define BOOLF(k, d, g, fl, lbl, df)                    {k, d, g, T_BOOL, fl, lbl, "", 0, 0, 0, 0, 0, df, ""}
#define ENUMF(k, d, g, fl, lbl, df, op)                {k, d, g, T_ENUM, fl, lbl, "", 0, 0, 0, 0, 0, df, op}

// Temperature sensor slot n of camper-temp
#define TEMP_SLOT(n, on, id, name, t, lo, hi)                                                        \
    BOOLF("t" #n "_on", D_TEMP, "Sensor " #n, 0, "Sensor vorhanden", on),                           \
    TXT("t" #n "_id", D_TEMP, "Sensor " #n, FL_SET | FL_TOPIC, "ID (Topic)", id),                   \
    TXT("t" #n "_name", D_TEMP, "Sensor " #n, FL_SET, "Name", name),                                 \
    NUM("t" #n "_t", D_TEMP, "Sensor " #n, 0, "Temperatur", "°C", -40, 60, 0.1, 2, 0.1, t),         \
    BOOLF("t" #n "_ok", D_TEMP, "Sensor " #n, 0, "Sensor antwortet", "ON"),                         \
    NUM("t" #n "_lo", D_TEMP, "Sensor " #n, FL_SET | FL_OPT, "Alarm unter", "°C", -40, 60, 0.5, 1, 0, lo), \
    NUM("t" #n "_hi", D_TEMP, "Sensor " #n, FL_SET | FL_OPT, "Alarm über", "°C", -40, 60, 0.5, 1, 0, hi)

// Battery slot n of camper-battery
#define BAT_SLOT(n, on, id, name, type, soc, v, a, cap)                                              \
    BOOLF("b" #n "_on", D_BATTERY, "Batterie " #n, 0, "Batterie vorhanden", on),                    \
    TXT("b" #n "_id", D_BATTERY, "Batterie " #n, FL_SET | FL_TOPIC, "ID (Topic)", id),              \
    TXT("b" #n "_name", D_BATTERY, "Batterie " #n, FL_SET, "Name", name),                            \
    ENUMF("b" #n "_type", D_BATTERY, "Batterie " #n, FL_SET, "Typ", type, "1=Daly,2=Ective,3=BM2 (ohne Strom)"), \
    BOOLF("b" #n "_ok", D_BATTERY, "Batterie " #n, 0, "BMS antwortet", "ON"),                       \
    NUM("b" #n "_soc", D_BATTERY, "Batterie " #n, 0, "Ladezustand", "%", 0, 100, 1, 0, 0, soc),     \
    NUM("b" #n "_v", D_BATTERY, "Batterie " #n, 0, "Spannung", "V", 10, 15, 0.01, 2, 0.01, v),      \
    NUM("b" #n "_a", D_BATTERY, "Batterie " #n, 0, "Strom (+ = Laden)", "A", -100, 100, 0.1, 2, 0.2, a), \
    NUM("b" #n "_cap", D_BATTERY, "Batterie " #n, FL_SET, "Kapazität", "Ah", 10, 600, 10, 0, 0, cap), \
    NUM("b" #n "_temp", D_BATTERY, "Batterie " #n, FL_ADV, "Temperatur (Daly)", "°C", -20, 60, 0.5, 1, 0, "18"), \
    NUM("b" #n "_cyc", D_BATTERY, "Batterie " #n, FL_ADV, "Ladezyklen (Daly)", "", 0, 9999, 1, 0, 0, "42"), \
    BOOLF("b" #n "_prob", D_BATTERY, "Batterie " #n, FL_ADV, "BMS meldet Fehler", "OFF"),           \
    TXT("b" #n "_pt", D_BATTERY, "Batterie " #n, FL_ADV, "Fehlertext", "Zellspannung zu niedrig")

// Zigbee door / window contact slot n
#define ZB_CONTACT(n, on, ieee, name, open)                                                          \
    BOOLF("c" #n "_on", D_ZIGBEE, "Kontakt " #n, 0, "Kontakt vorhanden", on),                       \
    TXT("c" #n "_ieee", D_ZIGBEE, "Kontakt " #n, FL_SET | FL_TOPIC, "IEEE-Adresse", ieee),          \
    TXT("c" #n "_name", D_ZIGBEE, "Kontakt " #n, FL_SET, "Name (nur hier)", name),                   \
    BOOLF("c" #n "_open", D_ZIGBEE, "Kontakt " #n, 0, "Offen", open),                               \
    NUM("c" #n "_bat", D_ZIGBEE, "Kontakt " #n, FL_ADV, "Batterie", "%", 0, 100, 1, 0, 0, "90")

// Zigbee climate sensor slot n
#define ZB_CLIMATE(n, on, ieee, name, t, h)                                                          \
    BOOLF("k" #n "_on", D_ZIGBEE, "Klimasensor " #n, 0, "Sensor vorhanden", on),                    \
    TXT("k" #n "_ieee", D_ZIGBEE, "Klimasensor " #n, FL_SET | FL_TOPIC, "IEEE-Adresse", ieee),      \
    TXT("k" #n "_name", D_ZIGBEE, "Klimasensor " #n, FL_SET, "Name (nur hier)", name),               \
    NUM("k" #n "_t", D_ZIGBEE, "Klimasensor " #n, 0, "Temperatur", "°C", -40, 60, 0.1, 2, 0.1, t),  \
    NUM("k" #n "_h", D_ZIGBEE, "Klimasensor " #n, 0, "Luftfeuchte", "%", 0, 100, 1, 1, 0.5, h),     \
    NUM("k" #n "_bat", D_ZIGBEE, "Klimasensor " #n, FL_ADV, "Batterie", "%", 0, 100, 1, 0, 0, "85")

static const FieldDef FIELDS[] = {
    // ---- tanks (camper-tanklevel) ----
    TXT("tk_pre", D_TANK, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/tanklevel"),
    NUM("w_pct", D_TANK, "Frischwasser", 0, "Füllstand", "%", 0, 100, 1, 0, 0.6, "62"),
    NUM("w_cap", D_TANK, "Frischwasser", FL_SET, "Tankgröße", "l", 10, 300, 5, 0, 0, "100"),
    BOOLF("w_filter", D_TANK, "Frischwasser", FL_ADV, "Glättung aktiv", "ON"),
    BOOLF("g_on", D_TANK, "Grauwasser", 0, "Grauwasser eingeschaltet", "ON"),
    NUM("g_pct", D_TANK, "Grauwasser", 0, "Füllstand", "%", 0, 100, 1, 0, 0.6, "35"),
    NUM("g_cap", D_TANK, "Grauwasser", FL_SET, "Tankgröße", "l", 10, 300, 5, 0, 0, "80"),
    BOOLF("g_filter", D_TANK, "Grauwasser", FL_ADV, "Glättung aktiv", "ON"),
    NUM("gas_pct", D_TANK, "Gas", 0, "Füllstand", "%", 0, 100, 1, 0, 0.4, "71"),
    NUM("gas_kg", D_TANK, "Gas", FL_SET, "Flasche (Füllmenge)", "kg", 1, 33, 0.5, 1, 0, "11"),
    ENUMF("gas_state", D_TANK, "Gas", 0, "Sensor", "OK", "OK,Kein Signal,Sensorfehler"),
    NUM("gas_bat", D_TANK, "Gas", FL_ADV, "Akku Sensor", "%", 0, 100, 1, 0, 0, "87"),
    NUM("gas_rssi", D_TANK, "Gas", FL_ADV, "Bluetooth-Signal", "dBm", -110, -30, 1, 0, 3, "-72"),

    // ---- level sensor (camper-level) ----
    TXT("lv_pre", D_LEVEL, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/level"),
    NUM("pitch", D_LEVEL, "", 0, "Längsneigung (vorne höher +)", "°", -15, 15, 0.1, 1, 0.05, "0.4"),
    NUM("roll", D_LEVEL, "", 0, "Querneigung (links höher +)", "°", -15, 15, 0.1, 1, 0.05, "-0.3"),
    NUM("tol", D_LEVEL, "", FL_SET, "Toleranz", "°", 0.1, 5, 0.1, 1, 0, "0.5"),
    ENUMF("lv_sensor", D_LEVEL, "", 0, "Sensor", "OK", "OK,ERROR"),
    BOOLF("lv_tare", D_LEVEL, "", FL_ADV, "Nullpunkt gesetzt", "OFF"),
    NUM("wb", D_LEVEL, "Fahrzeug", FL_SET | FL_ADV, "Radstand", "m", 2, 6, 0.05, 2, 0, "3.5"),
    NUM("tw", D_LEVEL, "Fahrzeug", FL_SET | FL_ADV, "Spurweite", "m", 1.2, 2.4, 0.05, 2, 0, "1.7"),

    // ---- heater (camper-heater, Truma) ----
    TXT("ht_pre", D_HEATER, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/heater"),
    BOOLF("alive", D_HEATER, "", 0, "CP Plus verbunden", "ON"),
    ENUMF("hvac", D_HEATER, "Raum", 0, "Raumheizung", "heat", "heat=An,off=Aus"),
    NUM("room", D_HEATER, "Raum", 0, "Raumtemperatur", "°C", -10, 40, 0.1, 1, 0.1, "19.5"),
    NUM("setp", D_HEATER, "Raum", 0, "Solltemperatur", "°C", 5, 30, 0.5, 1, 0, "21"),
    ENUMF("fan", D_HEATER, "Raum", 0, "Lüfterstufe", "eco", "eco,high,boost"),
    BOOLF("ht_dyn", D_HEATER, "Raum", FL_SET, "Raumtemperatur folgt der Heizung", "ON"),
    ENUMF("wmode", D_HEATER, "Warmwasser", 0, "Warmwasser", "off", "off=Aus,eco=Eco 40 °C,high=Hot 60 °C,boost=Boost"),
    NUM("water", D_HEATER, "Warmwasser", 0, "Wassertemperatur", "°C", 0, 80, 1, 1, 0, "38"),
    NUM("ht_err", D_HEATER, "Diagnose", 0, "Fehlercode (0 = keiner)", "", 0, 999, 1, 0, 0, "0"),
    BOOLF("decoke", D_HEATER, "Diagnose", FL_ADV, "Freibrennen", "OFF"),
    NUM("decoke_rem", D_HEATER, "Diagnose", FL_ADV, "Freibrennen Rest", "min", 0, 120, 1, 0, 0, "0"),

    // ---- roof fan (camper-maxxfan) ----
    TXT("mf_pre", D_FAN, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/maxxfan"),
    BOOLF("mf_exp", D_FAN, "", FL_ADV, "Port-Expander erreichbar", "ON"),
    BOOLF("p1_on", D_FAN, "Anschluss p1", FL_SET, "Lüfter angeschlossen", "ON"),
    NUM("p1_speed", D_FAN, "Anschluss p1", 0, "Stufe (nur hier, 0 = Haube zu)", "", 0, 10, 1, 0, 0, "0"),
    BOOLF("p1_hold", D_FAN, "Anschluss p1", 0, "Auto-Halten", "OFF"),
    BOOLF("p1_led", D_FAN, "Anschluss p1", 0, "LED", "OFF"),
    BOOLF("p2_on", D_FAN, "Anschluss p2", FL_SET, "Lüfter angeschlossen", "OFF"),
    NUM("p2_speed", D_FAN, "Anschluss p2", 0, "Stufe (nur hier, 0 = Haube zu)", "", 0, 10, 1, 0, 0, "0"),
    BOOLF("p2_hold", D_FAN, "Anschluss p2", 0, "Auto-Halten", "OFF"),
    BOOLF("p2_led", D_FAN, "Anschluss p2", 0, "LED", "OFF"),

    // ---- temperatures (camper-temp) ----
    TXT("tp_pre", D_TEMP, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/temp"),
    TEMP_SLOT(1, "ON", "fridge", "Kühlschrank", "5.0", "", "8"),
    TEMP_SLOT(2, "ON", "freezer", "Gefrierfach", "-17.0", "", "-12"),
    TEMP_SLOT(3, "OFF", "interior", "Innenraum", "20.0", "5", ""),
    TEMP_SLOT(4, "OFF", "outside", "Außen", "8.0", "", ""),

    // ---- batteries (camper-battery) ----
    TXT("bt_pre", D_BATTERY, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic", "camper/battery"),
    BAT_SLOT(1, "ON", "house", "Bordbatterie", "2", "78", "13.25", "-4.2", "100"),
    BAT_SLOT(2, "OFF", "starter", "Starterbatterie", "3", "90", "12.70", "0", "70"),

    // ---- router (RUT956 "Data to Server") ----
    TXT("rut_t", D_ROUTER, "Topics", FL_SET | FL_TOPIC | FL_ADV, "GPS (JSON)", "camper/rut"),
    TXT("door_t", D_ROUTER, "Topics", FL_SET | FL_TOPIC | FL_ADV, "Tür (Digital-Eingang)", "camper/door"),
    TXT("mob_t", D_ROUTER, "Topics", FL_SET | FL_TOPIC | FL_ADV, "Mobilfunk", "camper/router/mobile"),
    TXT("awn_t", D_ROUTER, "Topics", FL_SET | FL_ADV, "Markisen-Befehl des Displays (Szenario Abfahrt)", "camper/display/cmd/awning"),
    NUM("spd", D_ROUTER, "GPS", 0, "Geschwindigkeit", "km/h", 0, 160, 1, 1, 0, "0"),
    BOOLF("fix", D_ROUTER, "GPS", 0, "GPS-Fix", "ON"),
    NUM("lat", D_ROUTER, "GPS", FL_ADV, "Breite", "°", -90, 90, 0.0001, 6, 0, "51.000000"),
    NUM("lon", D_ROUTER, "GPS", FL_ADV, "Länge", "°", -180, 180, 0.0001, 6, 0, "10.000000"),
    NUM("alt", D_ROUTER, "GPS", FL_ADV, "Höhe", "m", -100, 4000, 1, 1, 0, "200"),
    NUM("sats", D_ROUTER, "GPS", FL_ADV, "Satelliten", "", 0, 30, 1, 0, 0, "9"),
    ENUMF("door", D_ROUTER, "Tür", 0, "Tür", "close", "close=Zu,open=Offen"),
    BOOLF("mob_on", D_ROUTER, "Mobilfunk", FL_SET, "Mobilfunk senden", "ON"),
    NUM("rsrp", D_ROUTER, "Mobilfunk", 0, "RSRP", "dBm", -140, -44, 1, 0, 3, "-92"),
    NUM("rssi", D_ROUTER, "Mobilfunk", FL_ADV, "RSSI", "dBm", -110, -40, 1, 0, 2, "-65"),
    ENUMF("net", D_ROUTER, "Mobilfunk", 0, "Netz", "LTE", "5G,LTE,3G,2G"),

    // ---- Zigbee hub (SMLIGHT cluster topics) ----
    TXT("zb_t", D_ZIGBEE, "", FL_SET | FL_TOPIC | FL_ADV, "Basis-Topic (data)", "camper/zigbee/data"),
    ZB_CONTACT(1, "ON", "00158d0000000001", "Tür", "OFF"),
    ZB_CONTACT(2, "ON", "00158d0000000002", "Fenster Küche", "OFF"),
    ZB_CONTACT(3, "OFF", "00158d0000000003", "Dachhaube", "OFF"),
    ZB_CLIMATE(1, "ON", "00158d0000000011", "Kühlschrank", "5.5", "60"),
    ZB_CLIMATE(2, "OFF", "00158d0000000012", "Außen", "8.0", "75"),
    BOOLF("l1_on", D_ZIGBEE, "LED-Leiste", 0, "Lampe vorhanden", "ON"),
    TXT("l1_ieee", D_ZIGBEE, "LED-Leiste", FL_SET | FL_TOPIC, "IEEE-Adresse", "a4c1380000000021"),
    BOOLF("l1_state", D_ZIGBEE, "LED-Leiste", 0, "An", "OFF"),
    NUM("l1_level", D_ZIGBEE, "LED-Leiste", 0, "Helligkeit", "", 1, 254, 1, 0, 0, "180"),
    TXT("l1_color", D_ZIGBEE, "LED-Leiste", FL_ADV, "Farbe (letzter Befehl)", "#ffb060"),
};
static const int FIELD_COUNT = sizeof(FIELDS) / sizeof(FIELDS[0]);

const EmuDevDef EMU_DEVS[D_COUNT] = {
    {"tanklevel", "Tanks", "tk_pre", true},
    {"level",     "Levelsensor",         "lv_pre", true},
    {"heater",    "Heizung",     "ht_pre", true},
    {"maxxfan",   "MaxxFan", "mf_pre", true},
    {"temp",      "Temperaturen",        "tp_pre", true},
    {"battery",   "Batterien",     "bt_pre", true},
    {"router",    "Router", "", false},
    {"zigbee",    "Zigbee", "", false},
};

// ============================================================
// Scenarios (same ideas as tools/mqtt/sim.py)
// ============================================================

struct ScenarioDef { const char* id; const char* name; const char* desc; };
static const ScenarioDef SCENARIOS[] = {
    {"empty",   "Tanks leer",       "Frischwasser 30 → 5 %, Gas 25 → 8 % in 3 Minuten"},
    {"fridge",  "Kühlschrank warm", "Kühlschrank (camper-temp und Zigbee-Klimasensor 1) 5 → 11 °C in 2 Minuten"},
    {"battery", "Batterie leer",    "Batterie 1 entlädt von 40 auf 12 % in 2½ Minuten"},
    {"frost",   "Frost",            "Außen (camper-temp Sensor 4 und Zigbee-Klimasensor 2) fällt von 4 auf −2 °C"},
    {"depart",  "Abfahrt",          "Markise draußen, Tür und Kontakt 1 offen; Fahrt ab 20 s, Tür zu nach 60 s, Markise rein nach 90 s, Kontakt zu nach 120 s, Stillstand ab 150 s"},
    {"drive",   "Fahrt",            "0 → 80 km/h, die Neigung schwankt, Kontakt 1 bleibt offen"},
    {"tilt",    "Schräg geparkt",   "3° nach vorne und 2,2° zur Seite, nach 90 s eben"},
    {"heat",    "Aufheizen",        "Heizung an auf 22 °C bei 12 °C Raumtemperatur, Warmwasser Eco"},
    {"offline", "Gerät offline",    "das gewählte Gerät fällt nach 30 s aus (Last Will) und kommt nach 2 Minuten wieder"},
};
static const int SCENARIO_COUNT = sizeof(SCENARIOS) / sizeof(SCENARIOS[0]);

// ============================================================
// State
// ============================================================

static String vals[FIELD_COUNT];
static float noiseOff[FIELD_COUNT];           // current random offset per field
static uint8_t modes[D_COUNT];
static bool noiseOn[D_COUNT];
static bool topicChanged[D_COUNT];
static bool clearAllReq = false;

static int scenario = -1;                     // index into SCENARIOS, -1 = none
static uint8_t scenarioDev = D_TANK;          // "offline": which device
static uint32_t scenarioStart = 0;
static String* snapshot = nullptr;            // values before the scenario (restored on stop)
static uint8_t snapModes[D_COUNT];

static bool saveDue = false;
static uint32_t saveAt = 0;
static uint32_t lastTick = 0, lastNoise = 0;

static EmuSend sender = nullptr;

struct LogEntry { uint32_t ms; String topic; String payload; };
static const int LOG_SIZE = 16;
static LogEntry cmdLog[LOG_SIZE];
static int logHead = 0, logCount = 0;

#ifndef EMU_HOST_TEST
static const char* EMU_FILE = "/emu.json";
#endif

// The web task and the loop task both touch the values
#ifndef EMU_HOST_TEST
static SemaphoreHandle_t mtx = nullptr;
struct Lock {
    Lock() { if (mtx) xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }
    ~Lock() { if (mtx) xSemaphoreGiveRecursive(mtx); }
};
#else
struct Lock { Lock() {} };
#endif

// ============================================================
// Field helpers
// ============================================================

static int fidx(const char* key) {
    for (int i = 0; i < FIELD_COUNT; i++)
        if (strcmp(FIELDS[i].key, key) == 0) return i;
    return -1;
}

static const String EMPTY_STR;

static const String& V(const char* key) {
    int i = fidx(key);
    return i < 0 ? EMPTY_STR : vals[i];
}
static float N(const char* key) {
    int i = fidx(key);
    return i < 0 || vals[i].length() == 0 ? NAN : vals[i].toFloat();
}
// Number with the current noise offset (only while noise is on for the device)
static float NN(const char* key) {
    int i = fidx(key);
    if (i < 0 || vals[i].length() == 0) return NAN;
    float v = vals[i].toFloat();
    if (noiseOn[FIELDS[i].dev]) v += noiseOff[i];
    return constrain(v, FIELDS[i].min, FIELDS[i].max);
}
static bool B(const char* key) { return V(key) == "ON"; }

static String fmt(float v, int dec) {
    if (isnan(v)) return String("");
    char buf[24];
    snprintf(buf, sizeof(buf), "%.*f", dec, (double)v);
    if (strcmp(buf, "-0") == 0 || strcmp(buf, "-0.0") == 0 || strcmp(buf, "-0.00") == 0) memmove(buf, buf + 1, strlen(buf));
    return String(buf);
}

// Value as published: the field's decimals, noise included
static String P(const char* key) {
    int i = fidx(key);
    return i < 0 ? String("") : fmt(NN(key), FIELDS[i].dec);
}

static bool optionValid(const FieldDef& f, const String& v) {
    const char* p = f.opts;
    while (*p) {
        const char* end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        const char* eq = (const char*)memchr(p, '=', n);
        size_t vn = eq ? (size_t)(eq - p) : n;
        if (v.length() == vn && strncmp(p, v.c_str(), vn) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

static bool validTopicPart(const String& v, bool slash) {
    if (v.length() == 0 || v.length() > 60) return false;
    for (size_t i = 0; i < v.length(); i++) {
        char c = v[i];
        if (c == '#' || c == '+' || c == ' ' || (c == '/' && !slash)) return false;
    }
    return !(slash && (v[0] == '/' || v[v.length() - 1] == '/'));
}

// Checks and normalises a value for field i; err gets a German message for the web UI
static bool normalize(int i, String& v, String& err) {
    const FieldDef& f = FIELDS[i];
    v.trim();
    switch (f.type) {
        case T_NUM: {
            if (v.length() == 0) {
                if (f.flags & FL_OPT) return true;
                err = String(f.label) + ": Zahl fehlt";
                return false;
            }
            v.replace(',', '.');
            char* end;
            float x = strtof(v.c_str(), &end);
            if (end == v.c_str() || *end != 0 || isnan(x)) { err = String(f.label) + ": keine Zahl"; return false; }
            if (x < f.min || x > f.max) {
                err = String(f.label) + ": erlaubt " + fmt(f.min, f.dec) + " bis " + fmt(f.max, f.dec);
                return false;
            }
            v = fmt(x, f.dec);
            return true;
        }
        case T_BOOL: {
            String u = v;
            u.toUpperCase();
            if (u == "ON" || u == "1" || u == "TRUE") { v = "ON"; return true; }
            if (u == "OFF" || u == "0" || u == "FALSE") { v = "OFF"; return true; }
            err = String(f.label) + ": ON oder OFF";
            return false;
        }
        case T_ENUM:
            if (optionValid(f, v)) return true;
            err = String(f.label) + ": unbekannte Auswahl";
            return false;
        default:
            if (v.length() > 80) { err = String(f.label) + ": zu lang"; return false; }
            if (f.flags & FL_TOPIC) {
                bool full = strstr(f.key, "_pre") || strcmp(f.key + strlen(f.key) - 2, "_t") == 0;
                if (!validTopicPart(v, full)) { err = String(f.label) + ": ungültiges Topic"; return false; }
            }
            return true;
    }
}

static void markSave() {
    saveDue = true;
    saveAt = millis() + 3000;   // collect several edits into one flash write
}

// Sets field i without checks. persist = remember in emu.json (not for dynamics)
static void setRaw(int i, const String& v, bool persist) {
    if (i < 0 || vals[i] == v) return;
    if (FIELDS[i].flags & FL_TOPIC) topicChanged[FIELDS[i].dev] = true;
    vals[i] = v;
    if (persist && scenario < 0) markSave();
}
static void setNum(const char* key, float x, bool persist) {
    int i = fidx(key);
    if (i < 0) return;
    setRaw(i, fmt(constrain(x, FIELDS[i].min, FIELDS[i].max), FIELDS[i].dec), persist);
}
static void setStr(const char* key, const char* v, bool persist) { setRaw(fidx(key), String(v), persist); }
static void setBool(const char* key, bool on, bool persist) { setStr(key, on ? "ON" : "OFF", persist); }

static void logCmd(const String& topic, const String& payload) {
    LogEntry& e = cmdLog[logHead];
    e.ms = millis();
    e.topic = topic;
    e.payload = payload.length() > 60 ? payload.substring(0, 60) + "…" : payload;
    logHead = (logHead + 1) % LOG_SIZE;
    if (logCount < LOG_SIZE) logCount++;
}

// ============================================================
// Persistence
// ============================================================

static void defaults(int dev) {
    for (int i = 0; i < FIELD_COUNT; i++)
        if (dev < 0 || FIELDS[i].dev == dev) {
            if ((FIELDS[i].flags & FL_TOPIC) && vals[i] != FIELDS[i].def) topicChanged[FIELDS[i].dev] = true;
            vals[i] = FIELDS[i].def;
        }
}

static void save() {
#ifndef EMU_HOST_TEST
    JsonDocument doc;
    JsonArray m = doc["modes"].to<JsonArray>();
    JsonArray n = doc["noise"].to<JsonArray>();
    for (int d = 0; d < D_COUNT; d++) { m.add(modes[d] == M_OFFLINE ? M_ON : modes[d]); n.add(noiseOn[d]); }
    JsonObject v = doc["values"].to<JsonObject>();
    for (int i = 0; i < FIELD_COUNT; i++)
        if (vals[i] != FIELDS[i].def) v[FIELDS[i].key] = vals[i];   // only what differs from the examples
    File f = LittleFS.open("/emu.tmp", "w");
    if (!f) return;
    size_t len = serializeJson(doc, f);
    f.close();
    if (len == 0) { LittleFS.remove("/emu.tmp"); return; }
    LittleFS.remove(EMU_FILE);
    LittleFS.rename("/emu.tmp", EMU_FILE);
    Serial.printf("[EMU] Saved (%u bytes)\n", (unsigned)len);
#endif
}

static void load() {
#ifndef EMU_HOST_TEST
    if (!LittleFS.exists(EMU_FILE)) return;
    File f = LittleFS.open(EMU_FILE, "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, f);
    f.close();
    if (e) { Serial.printf("[EMU] emu.json invalid (%s), using the examples\n", e.c_str()); return; }
    JsonArrayConst m = doc["modes"], n = doc["noise"];
    for (int d = 0; d < D_COUNT; d++) {
        if (d < (int)m.size()) { int x = m[d] | 1; modes[d] = x == M_OFF ? M_OFF : M_ON; }
        if (d < (int)n.size()) noiseOn[d] = n[d] | true;
    }
    for (JsonPairConst kv : doc["values"].as<JsonObjectConst>()) {
        int i = fidx(kv.key().c_str());
        if (i < 0) continue;
        String v = kv.value().as<String>(), err;
        if (normalize(i, v, err)) vals[i] = v;   // a stale or edited file must not break anything
    }
#endif
}

// ============================================================
// Init / loop
// ============================================================

void emuInit() {
#ifndef EMU_HOST_TEST
    if (!mtx) mtx = xSemaphoreCreateRecursiveMutex();
#endif
    for (int d = 0; d < D_COUNT; d++) { modes[d] = M_ON; noiseOn[d] = true; }
    defaults(-1);
    load();
    for (int d = 0; d < D_COUNT; d++) topicChanged[d] = false;   // nothing was sent yet
    Serial.printf("[EMU] %d fields, %d scenarios\n", FIELD_COUNT, SCENARIO_COUNT);
}

static void newNoise() {
    for (int i = 0; i < FIELD_COUNT; i++) {
        float a = FIELDS[i].noise;
        noiseOff[i] = a > 0 ? (random(-1000, 1001) / 1000.0f) * a : 0;
    }
}

static void awning(bool out) {
    if (sender && V("awn_t").length()) sender(V("awn_t"), out ? "ON" : "OFF");
}

static void scenarioStep(float t) {
    const char* id = SCENARIOS[scenario].id;
    if (!strcmp(id, "empty")) {
        float f = min(1.0f, t / 180);
        setNum("w_pct", 30 - 25 * f, false);
        setNum("gas_pct", 25 - 17 * f, false);
    } else if (!strcmp(id, "fridge")) {
        float x = 5 + min(6.0f, t / 20);
        setNum("t1_t", x, false);
        setNum("k1_t", x + 0.3f, false);
    } else if (!strcmp(id, "battery")) {
        float s = 40 - min(28.0f, t / 5);
        setNum("b1_soc", s, false);
        setNum("b1_v", 12.0f + s / 100 * 1.4f, false);
        setNum("b1_a", -8.5f, false);
    } else if (!strcmp(id, "frost")) {
        float x = 4 - min(6.0f, t / 15);
        setNum("t4_t", x, false);
        setNum("k2_t", x + 0.2f, false);
    } else if (!strcmp(id, "depart")) {
        static int awn = -1;
        if (t < 1.5f) awn = -1;
        int want = t < 90 ? 1 : 0;
        if (awn != want) { awn = want; awning(want); }
        setStr("door", t < 60 ? "open" : "close", false);
        setBool("c1_open", t < 120, false);
        setNum("spd", t < 20 || t >= 150 ? 0 : min(50.0f, (t - 20) * 2.0f), false);
    } else if (!strcmp(id, "drive")) {
        float s = min(80.0f, max(0.0f, (t - 10) * 1.5f));
        setNum("spd", s, false);
        setBool("c1_open", true, false);
        float wob = s > 5 ? 1.5f : 0;
        setNum("pitch", 0.3f + wob * sinf(t / 3), false);
        setNum("roll", -0.2f + wob * cosf(t / 4), false);
    } else if (!strcmp(id, "tilt")) {
        float f = max(0.0f, 1 - t / 90);
        setNum("pitch", 0.2f + 2.8f * f, false);
        setNum("roll", -0.3f - 1.9f * f, false);
    } else if (!strcmp(id, "offline")) {
        modes[scenarioDev] = (t >= 30 && t < 150) ? M_OFFLINE : M_ON;
    }
    // "heat" only sets its start values; the heater dynamics do the rest
}

static void heaterDynamics() {
    if (!B("ht_dyn") || !B("alive")) return;
    float room = N("room"), set = N("setp"), water = N("water");
    bool heat = V("hvac") == "heat";
    float target = heat ? set : 12.0f;                        // without heating it cools to 12 °C
    float rate = heat ? (V("fan") == "eco" ? 0.03f : 0.06f) : 0.01f;
    if (!isnan(room) && fabsf(room - target) > 0.05f) setNum("room", room + (target > room ? rate : -rate), false);
    String wm = V("wmode");
    float wt = wm == "eco" ? 40 : wm == "high" ? 60 : wm == "boost" ? 62 : 15;
    if (!isnan(water) && fabsf(water - wt) > 0.5f) setNum("water", water + (wt > water ? 0.25f : -0.05f), false);
}

void emuLoop() {
    uint32_t now = millis();
    if (now - lastTick < 1000) return;
    lastTick = now;
    Lock lock;
    if (now - lastNoise >= 10000 || lastNoise == 0) { lastNoise = now; newNoise(); }
    if (scenario >= 0) scenarioStep((now - scenarioStart) / 1000.0f);
    heaterDynamics();
    if (saveDue && (int32_t)(now - saveAt) >= 0) { saveDue = false; save(); }
}

// ============================================================
// Queries for the MQTT part
// ============================================================

uint8_t emuMode(uint8_t dev) { return dev < D_COUNT ? modes[dev] : M_OFF; }

String emuPrefix(uint8_t dev) {
    Lock lock;
    if (dev >= D_COUNT || !EMU_DEVS[dev].preKey[0]) return String("");
    return V(EMU_DEVS[dev].preKey);
}

String emuZigbeeCmdFilter() {
    Lock lock;
    String b = V("zb_t");
    if (b.endsWith("/data")) b.remove(b.length() - 5);
    return b + "/cmd/#";
}

bool emuTakeClearAll() {
    Lock lock;
    bool r = clearAllReq;
    clearAllReq = false;
    return r;
}

bool emuTakeTopicChange(uint8_t dev) {
    Lock lock;
    bool r = dev < D_COUNT && topicChanged[dev];
    if (r) topicChanged[dev] = false;
    return r;
}

void emuSetSender(EmuSend fn) { sender = fn; }

// ============================================================
// Payload builders - same topics and formats as the real firmware
// ============================================================

static void buildTank(const String& p, EmuEmit emit, void* ctx) {
    struct { const char* sub; const char* pct; const char* cap; const char* filt; } tanks[] = {
        {"water", "w_pct", "w_cap", "w_filter"}, {"grey", "g_pct", "g_cap", "g_filter"}};
    for (int k = 0; k < 2; k++) {
        if (k == 1 && !B("g_on")) continue;
        float pct = NN(tanks[k].pct);
        String b = p + "/" + tanks[k].sub + "/";
        emit(b + "percent", fmt(pct, 0), ctx);
        emit(b + "liters", fmt(pct * N(tanks[k].cap) / 100, 1), ctx);
        emit(b + "voltage", fmt(0.2f + pct / 100 * 1.8f, 2), ctx);   // sender voltage, rough
        emit(b + "filter_active", V(tanks[k].filt), ctx);
    }
    // gas: values only while the BLE sensor answers, like the firmware
    if (V("gas_state") == "OK") {
        float g = NN("gas_pct");
        emit(p + "/gas/level", fmt(g, 0), ctx);
        emit(p + "/gas/raw_level", fmt(g, 1), ctx);
        emit(p + "/gas/content", fmt(g * N("gas_kg") / 100, 2), ctx);
        emit(p + "/gas/battery", P("gas_bat"), ctx);
        emit(p + "/gas/ble_rssi", P("gas_rssi"), ctx);
    }
    emit(p + "/gas/ble_state", V("gas_state"), ctx);
}

static void buildLevel(const String& p, EmuEmit emit, void* ctx) {
    String st = V("lv_sensor");
    emit(p + "/sensor", st, ctx);
    emit(p + "/tolerance", P("tol"), ctx);
    emit(p + "/tare_active", V("lv_tare"), ctx);
    if (st != "OK") return;
    float pi = NN("pitch"), ro = NN("roll"), tol = N("tol");
    emit(p + "/pitch", fmt(pi, 1), ctx);
    emit(p + "/roll", fmt(ro, 1), ctx);
    emit(p + "/is_level", fabsf(pi) <= tol && fabsf(ro) <= tol ? "ON" : "OFF", ctx);
    // wedge under each wheel in cm: lift every wheel to the highest one
    // (pitch > 0 = front higher, roll > 0 = left higher)
    float tp = tanf(pi * (float)M_PI / 180), tr = tanf(ro * (float)M_PI / 180);
    float hb = N("wb") / 2, ht = N("tw") / 2;
    float h[4] = {hb * tp + ht * tr, hb * tp - ht * tr, -hb * tp + ht * tr, -hb * tp - ht * tr};
    float top = max(max(h[0], h[1]), max(h[2], h[3]));
    const char* names[4] = {"fl", "fr", "rl", "rr"};
    for (int k = 0; k < 4; k++) emit(p + "/wedge_" + names[k], fmt((top - h[k]) * 100, 1), ctx);
}

static void buildHeater(const String& p, EmuEmit emit, void* ctx) {
    String cs = p + "/control_status/";
    bool alive = B("alive");
    emit(cs + "alive", alive ? "ON" : "OFF", ctx);
    bool heat = V("hvac") == "heat";
    float room = NN("room"), set = N("setp"), water = N("water");
    String wm = V("wmode"), fan = V("fan");
    bool heating = heat && room < set - 0.2f;
    bool waterHeating = wm != "off" && water < (wm == "eco" ? 40 : 60) - 1;
    emit(cs + "current_temp_room", fmt(room, 1), ctx);
    emit(cs + "room_setpoint", fmt(set, 1), ctx);
    emit(cs + "target_temp_room", heat ? fmt(set, 0) : String("0"), ctx);
    emit(cs + "hvac_mode", heat ? "heat" : "off", ctx);
    emit(cs + "hvac_action", !heat && wm == "off" ? "off" : heating || waterHeating ? "heating" : "idle", ctx);
    emit(cs + "fan_mode", fan, ctx);
    emit(cs + "heating_mode", heat ? (fan == "eco" ? "eco" : "high") : "off", ctx);
    emit(cs + "water_mode", wm == "boost" ? "high" : wm, ctx);
    emit(cs + "target_temp_water", wm == "eco" ? "40" : wm == "high" ? "60" : wm == "boost" ? "200" : "0", ctx);
    emit(cs + "current_temp_water", fmt(water, 1), ctx);
    bool on = alive && (heat || wm != "off");
    emit(cs + "operating_status", on ? "5" : "0", ctx);
    emit(cs + "operating_status_text", !alive ? "Keine Verbindung" : !on ? "Aus" : heating || waterHeating ? "Heizen" : "Bereit", ctx);
    int err = (int)N("ht_err");
    emit(cs + "error_code", String(err), ctx);
    emit(cs + "heater_error", err ? "ON" : "OFF", ctx);
    emit(cs + "decoke", V("decoke"), ctx);
    emit(cs + "decoke_remaining", P("decoke_rem"), ctx);
    emit(cs + "energy_mix", "gas", ctx);
    emit(cs + "heater_model", "combi_d", ctx);
}

static void buildFan(const String& p, EmuEmit emit, void* ctx) {
    emit(p + "/expander", V("mf_exp"), ctx);
    for (int n = 1; n <= 2; n++) {
        String k = String("p") + n;
        if (!B((k + "_on").c_str())) continue;
        emit(p + "/" + k + "/auto_hold", V((k + "_hold").c_str()), ctx);
        emit(p + "/" + k + "/led", V((k + "_led").c_str()), ctx);
    }
}

static String comma(float v) {
    String s = fmt(v, 1);
    s.replace('.', ',');
    return s;
}

static void buildTemp(const String& p, EmuEmit emit, void* ctx) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    String text;
    bool any = false;
    for (int n = 1; n <= 4; n++) {
        String k = String("t") + n + "_";
        if (!B((k + "on").c_str())) continue;
        String id = V((k + "id").c_str()), name = V((k + "name").c_str());
        bool ok = B((k + "ok").c_str());
        float t = NN((k + "t").c_str()), lo = N((k + "lo").c_str()), hi = N((k + "hi").c_str());
        int alarm = !ok ? 0 : (!isnan(hi) && t > hi) ? 2 : (!isnan(lo) && t < lo) ? 1 : 0;
        if (ok) emit(p + "/" + id, fmt(t, 2), ctx);
        emit(p + "/" + id + "/available", ok ? "online" : "offline", ctx);
        emit(p + "/" + id + "/alarm", alarm ? "ON" : "OFF", ctx);
        if (alarm) {
            any = true;
            if (text.length()) text += "; ";
            text += name + (alarm == 2 ? " zu warm: " : " zu kalt: ") + comma(t) + " °C (Grenze " + comma(alarm == 2 ? hi : lo) + " °C)";
        }
        JsonObject o = arr.add<JsonObject>();
        o["id"] = id;
        o["name"] = name;
        if (ok) o["t"] = roundf(t * 100) / 100; else o["t"] = nullptr;
        o["alarm"] = alarm;
    }
    emit(p + "/alarm", any ? "ON" : "OFF", ctx);
    emit(p + "/alarm_text", text, ctx);
    String list;
    serializeJson(doc, list);
    emit(p + "/sensors", list, ctx);
}

static void buildBattery(const String& p, EmuEmit emit, void* ctx) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int n = 1; n <= 2; n++) {
        String k = String("b") + n + "_";
        if (!B((k + "on").c_str())) continue;
        String id = V((k + "id").c_str()), b = p + "/" + id + "/";
        int type = V((k + "type").c_str()).toInt();
        bool ok = B((k + "ok").c_str()), cur = type != 3;     // BM2 has no current sensor
        float soc = NN((k + "soc").c_str()), v = NN((k + "v").c_str()), a = cur ? NN((k + "a").c_str()) : NAN;
        float ah = soc / 100 * N((k + "cap").c_str());
        bool prob = B((k + "prob").c_str());
        String pt = prob ? V((k + "pt").c_str()) : String("");
        const char* st = nullptr;                              // state text, BM2 only
        if (type == 3) st = soc < 20 ? "Kritisch" : soc < 50 ? "Schwach" : "Normal";
        float rt = cur && a < -0.05f ? ah / -a * 60 : NAN;     // minutes while discharging

        emit(b + "available", ok ? "online" : "offline", ctx);
        JsonObject o = arr.add<JsonObject>();
        o["id"] = id;
        o["name"] = V((k + "name").c_str());
        o["ok"] = ok;
        o["t"] = type;
        if (!ok) continue;
        emit(b + "voltage", fmt(v, 2), ctx);
        emit(b + "soc", fmt(soc, 0), ctx);
        if (cur) {
            emit(b + "current", fmt(a, 2), ctx);
            emit(b + "power", fmt(v * a, 0), ctx);
            emit(b + "charge", fmt(ah, 1), ctx);
            emit(b + "energy", fmt(ah * v, 0), ctx);
            emit(b + "runtime", isnan(rt) ? String("None") : fmt(rt, 0), ctx);
        }
        if (st) emit(b + "state", st, ctx);
        if (type == 1) {
            emit(b + "temperature", P((k + "temp").c_str()), ctx);
            emit(b + "cycles", P((k + "cyc").c_str()), ctx);
        }
        emit(b + "charging", cur && a > 0.05f ? "ON" : "OFF", ctx);
        emit(b + "problem", prob ? "ON" : "OFF", ctx);
        emit(b + "problem_text", pt, ctx);

        o["soc"] = roundf(soc);
        o["v"] = roundf(v * 100) / 100;
        if (cur) { o["a"] = roundf(a * 100) / 100; o["w"] = roundf(v * a); }
        if (st) o["st"] = st;
        if (!isnan(rt)) o["rt"] = (long)lroundf(rt);
        if (prob) { o["p"] = true; o["pt"] = pt; }
    }
    String list;
    serializeJson(doc, list);
    emit(p + "/batteries", list, ctx);
}

static void buildRouter(EmuEmit emit, void* ctx) {
    // same shape as the RUT956 "Data to Server" JSON: values as text, speed in km/h
    time_t now = time(nullptr);
    char ts[24] = "1970-01-01 00:00:00";
    if (now > 1700000000) {
        struct tm tmv;
        localtime_r(&now, &tmv);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
    }
    float spd = N("spd");
    if (spd > 1 && noiseOn[D_ROUTER]) spd += noiseOff[fidx("rsrp")] / 3;   // a little movement while driving
    JsonDocument doc;
    doc["TIME"] = ts;
    JsonObject g = doc["GPS"].to<JsonObject>();
    g["latitude"] = V("lat");
    g["longitude"] = V("lon");
    g["altitude"] = V("alt");
    g["speed"] = fmt(max(0.0f, spd), 1);
    g["accuracy"] = B("fix") ? "0.9" : "0";
    g["satellites"] = B("fix") ? V("sats") : String("0");
    g["fix_status"] = B("fix") ? "1" : "0";
    g["timestamp"] = String((unsigned long)(now > 1700000000 ? now : millis() / 1000));
    String s;
    serializeJson(doc, s);
    emit(V("rut_t"), s, ctx);

    JsonDocument d2;
    JsonObject dr = d2["Door"].to<JsonObject>();
    dr["name"] = "din2";
    dr["type"] = "Digital In";
    dr["state"] = V("door");
    dr["custom_name"] = "Door";
    s = "";
    serializeJson(d2, s);
    emit(V("door_t"), s, ctx);

    if (B("mob_on")) {
        JsonDocument d3;
        d3["rsrp"] = (int)lroundf(NN("rsrp"));
        d3["rssi"] = (int)lroundf(NN("rssi"));
        d3["conntype"] = V("net");
        s = "";
        serializeJson(d3, s);
        emit(V("mob_t"), s, ctx);
    }
}

// SMLIGHT hub format: <base>/<ieee>/1/<cluster>/<attribute> = {"data":{"val":...,"type_sm":"report"}}
static String zbVal(const String& v, bool quote) {
    return String("{\"data\":{\"val\":") + (quote ? "\"" + v + "\"" : v) + ",\"type_sm\":\"report\"}}";
}

static void buildZigbee(EmuEmit emit, void* ctx) {
    String base = V("zb_t");
    for (int n = 1; n <= 3; n++) {
        String k = String("c") + n + "_";
        if (!B((k + "on").c_str())) continue;
        String b = base + "/" + V((k + "ieee").c_str()) + "/1/";
        emit(b + "0006/0000", zbVal(B((k + "open").c_str()) ? "1" : "0", false), ctx);
        emit(b + "0001/0021", zbVal(P((k + "bat").c_str()), false), ctx);
    }
    for (int n = 1; n <= 2; n++) {
        String k = String("k") + n + "_";
        if (!B((k + "on").c_str())) continue;
        String b = base + "/" + V((k + "ieee").c_str()) + "/1/";
        emit(b + "0402/0000", zbVal(P((k + "t").c_str()), false), ctx);
        emit(b + "0405/0000", zbVal(P((k + "h").c_str()), false), ctx);
        emit(b + "0001/0021", zbVal(P((k + "bat").c_str()), false), ctx);
    }
    if (B("l1_on")) {
        String b = base + "/" + V("l1_ieee") + "/1/";
        emit(b + "0006/0000", zbVal(V("l1_state"), true), ctx);
        emit(b + "0008/0000", zbVal(V("l1_level"), false), ctx);
    }
}

void emuBuild(uint8_t dev, EmuEmit emit, void* ctx) {
    Lock lock;
    String p = emuPrefix(dev);
    switch (dev) {
        case D_TANK:    buildTank(p, emit, ctx); break;
        case D_LEVEL:   buildLevel(p, emit, ctx); break;
        case D_HEATER:  buildHeater(p, emit, ctx); break;
        case D_FAN:     buildFan(p, emit, ctx); break;
        case D_TEMP:    buildTemp(p, emit, ctx); break;
        case D_BATTERY: buildBattery(p, emit, ctx); break;
        case D_ROUTER:  buildRouter(emit, ctx); break;
        case D_ZIGBEE:  buildZigbee(emit, ctx); break;
    }
}

// ============================================================
// Commands - react like the real device would
// ============================================================

static bool parseOnOff(const String& msg, bool current, bool& out) {
    String u = msg;
    u.trim();
    u.toUpperCase();
    if (u == "ON" || u == "1" || u == "TRUE") { out = true; return true; }
    if (u == "OFF" || u == "0" || u == "FALSE") { out = false; return true; }
    if (u == "TOGGLE") { out = !current; return true; }
    return false;
}

static void cmdTank(const String& sub, const String& msg) {
    bool on;
    if (sub == "cmd/water/filter/set" && parseOnOff(msg, B("w_filter"), on)) setBool("w_filter", on, true);
    else if (sub == "cmd/grey/filter/set" && parseOnOff(msg, B("g_filter"), on)) setBool("g_filter", on, true);
}

static void cmdLevel(const String& sub, const String& msg) {
    if (sub == "cmd/tare/set") {
        // the real sensor stores the current angles as zero point
        setNum("pitch", 0, true);
        setNum("roll", 0, true);
        setBool("lv_tare", true, true);
    } else if (sub == "cmd/tare_reset/set") {
        setBool("lv_tare", false, true);
    } else if (sub == "cmd/tolerance/set") {
        setNum("tol", msg.toFloat(), true);
    } else if (sub == "cmd/sensor_restart/set") {
        setStr("lv_sensor", "OK", true);
    }
}

static void cmdHeater(const String& sub, const String& msg) {
    if (!sub.startsWith("set/")) return;
    String key = sub.substring(4), v = msg;
    v.trim();
    v.toLowerCase();
    float x = v.toFloat();
    if (key == "target_temp_room") {
        if (x <= 0) setStr("hvac", "off", true);
        else { setNum("setp", x, true); setStr("hvac", "heat", true); }
    } else if (key == "room_setpoint") {
        if (x >= 5 && x <= 30) setNum("setp", x, true);
    } else if (key == "hvac_mode") {
        if (v == "heat" || v == "off") setStr("hvac", v.c_str(), true);
    } else if (key == "heating_mode") {
        if (v == "off") setStr("hvac", "off", true);
        else if (v == "eco" || v == "high") { setStr("fan", v.c_str(), true); setStr("hvac", "heat", true); }
    } else if (key == "fan_mode") {
        if (v == "eco" || v == "high" || v == "boost") setStr("fan", v.c_str(), true);
    } else if (key == "water_mode") {
        if (v == "off" || v == "eco" || v == "high" || v == "boost") setStr("wmode", v.c_str(), true);
    } else if (key == "target_temp_water") {
        setStr("wmode", x >= 200 ? "boost" : x >= 60 ? "high" : x >= 40 ? "eco" : "off", true);
    } else if (key == "decoke") {
        bool on;
        if (parseOnOff(msg, B("decoke"), on)) { setBool("decoke", on, true); setNum("decoke_rem", on ? 60 : 0, true); }
    }
}

static void cmdFan(const String& sub, const String& msg) {
    // cmd/p<n>/...
    if (!sub.startsWith("cmd/p") || sub.length() < 7) return;
    String k = sub.substring(4, 6), rest = sub.substring(7);   // "p1", "speed/set"
    if (k != "p1" && k != "p2") return;
    String sp = k + "_speed", led = k + "_led", hold = k + "_hold";
    float s = N(sp.c_str());
    String btn = rest == "press" ? msg : rest;
    btn.trim();
    bool on;
    if (rest == "speed/set") setNum(sp.c_str(), msg.toFloat(), true);
    else if (rest == "close") setNum(sp.c_str(), 0, true);
    else if (rest == "led/set") { if (parseOnOff(msg, B(led.c_str()), on)) setBool(led.c_str(), on, true); }
    else if (btn == "faster") setNum(sp.c_str(), s + 1, true);
    else if (btn == "slower") setNum(sp.c_str(), s - 1, true);
    else if (btn == "on_off") setNum(sp.c_str(), s > 0 ? 0 : 3, true);
    else if (btn == "auto") setBool(hold.c_str(), !B(hold.c_str()), true);
}

static void cmdZigbee(const String& rest, const String& msg) {
    // <ieee>/1/<cluster>[/<attribute>]
    int a = rest.indexOf('/');
    if (a <= 0 || !B("l1_on")) return;
    String ieee = rest.substring(0, a), path = rest.substring(a + 1);
    if (!ieee.equalsIgnoreCase(V("l1_ieee"))) return;
    bool on;
    if (path == "1/0006" && parseOnOff(msg, B("l1_state"), on)) setBool("l1_state", on, true);
    else if (path == "1/0008/0000") { setNum("l1_level", msg.toFloat(), true); setBool("l1_state", true, true); }
    else if (path == "1/0300/0007" || path == "1/0300/000a") { setStr("l1_color", msg.c_str(), true); setBool("l1_state", true, true); }
}

void emuCommand(const char* topic, const char* payload, unsigned int len) {
    String t(topic), msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += payload[i];
    Lock lock;
    logCmd(t, msg);
    String zb = V("zb_t");
    if (zb.endsWith("/data")) zb.remove(zb.length() - 5);
    zb += "/cmd/";
    if (t.startsWith(zb)) { cmdZigbee(t.substring(zb.length()), msg); return; }
    for (int d = 0; d < D_ROUTER; d++) {
        String p = emuPrefix(d) + "/";
        if (!t.startsWith(p)) continue;
        String sub = t.substring(p.length());
        switch (d) {
            case D_TANK:   cmdTank(sub, msg); break;
            case D_LEVEL:  cmdLevel(sub, msg); break;
            case D_HEATER: cmdHeater(sub, msg); break;
            case D_FAN:    cmdFan(sub, msg); break;
            default: break;
        }
        return;
    }
}

// ============================================================
// Web UI
// ============================================================

static void schemaDev(JsonObject o, int d) {
    o["id"] = EMU_DEVS[d].id;
    o["name"] = EMU_DEVS[d].name;
    o["own"] = EMU_DEVS[d].own;
}

static void schemaField(JsonObject o, int i) {
    const FieldDef& f = FIELDS[i];
    o["k"] = f.key;
    o["d"] = f.dev;
    if (f.group[0]) o["g"] = f.group;
    o["t"] = f.type == T_NUM ? "num" : f.type == T_BOOL ? "bool" : f.type == T_ENUM ? "enum" : "text";
    o["l"] = f.label;
    if (f.unit[0]) o["u"] = f.unit;
    if (f.type == T_NUM) { o["min"] = f.min; o["max"] = f.max; o["step"] = f.step; }
    if (f.opts[0]) o["o"] = f.opts;
    if (f.flags & FL_SET) o["set"] = true;
    if (f.flags & FL_ADV) o["adv"] = true;
    if (f.flags & FL_OPT) o["opt"] = true;
    o["def"] = f.def;
}

static void schemaScenario(JsonObject o, int i) {
    o["id"] = SCENARIOS[i].id;
    o["name"] = SCENARIOS[i].name;
    o["desc"] = SCENARIOS[i].desc;
}

void emuSchemaJson(JsonDocument& doc) {
    JsonArray devs = doc["devices"].to<JsonArray>();
    for (int d = 0; d < D_COUNT; d++) schemaDev(devs.add<JsonObject>(), d);
    JsonArray fs = doc["fields"].to<JsonArray>();
    for (int i = 0; i < FIELD_COUNT; i++) schemaField(fs.add<JsonObject>(), i);
    JsonArray sc = doc["scenarios"].to<JsonArray>();
    for (int i = 0; i < SCENARIO_COUNT; i++) schemaScenario(sc.add<JsonObject>(), i);
}

// The same JSON as emuSchemaJson, one entry at a time. The whole document needs
// tens of kB in one piece, which the C3 often no longer has once all broker
// connections are up; the web server streams these pieces in a chunked response.
bool emuSchemaPiece(int n, String& out) {
    out = "";
    if (n == 0) { out = "{\"devices\":["; return true; }
    n--;
    JsonDocument doc;
    if (n < D_COUNT) {
        if (n) out = ",";
        schemaDev(doc.to<JsonObject>(), n);
    } else if ((n -= D_COUNT) == 0) {
        out = "],\"fields\":[";
        return true;
    } else if (--n < FIELD_COUNT) {
        if (n) out = ",";
        schemaField(doc.to<JsonObject>(), n);
    } else if ((n -= FIELD_COUNT) == 0) {
        out = "],\"scenarios\":[";
        return true;
    } else if (--n < SCENARIO_COUNT) {
        if (n) out = ",";
        schemaScenario(doc.to<JsonObject>(), n);
    } else if (n == SCENARIO_COUNT) {
        out = "]}";
        return true;
    } else {
        return false;
    }
    String json;   // serializeJson replaces the String's content, so append afterwards
    serializeJson(doc, json);
    out += json;
    return true;
}

void emuStateJson(JsonObject obj) {
    Lock lock;
    JsonArray m = obj["modes"].to<JsonArray>();
    JsonArray n = obj["noise"].to<JsonArray>();
    for (int d = 0; d < D_COUNT; d++) { m.add(modes[d]); n.add(noiseOn[d]); }
    JsonObject v = obj["v"].to<JsonObject>();
    for (int i = 0; i < FIELD_COUNT; i++) v[FIELDS[i].key] = vals[i];
    if (scenario >= 0) {
        JsonObject s = obj["scenario"].to<JsonObject>();
        s["id"] = SCENARIOS[scenario].id;
        s["name"] = SCENARIOS[scenario].name;
        s["t"] = (millis() - scenarioStart) / 1000;
        if (!strcmp(SCENARIOS[scenario].id, "offline")) s["dev"] = scenarioDev;
    }
    JsonArray log = obj["log"].to<JsonArray>();
    uint32_t now = millis();
    for (int k = 0; k < logCount; k++) {
        const LogEntry& e = cmdLog[(logHead - 1 - k + LOG_SIZE) % LOG_SIZE];
        JsonObject o = log.add<JsonObject>();
        o["age"] = (now - e.ms) / 1000;
        o["t"] = e.topic;
        o["p"] = e.payload;
    }
}

bool emuSetValue(const char* key, const char* value, String& err) {
    Lock lock;
    int i = fidx(key);
    if (i < 0) { err = String("Unbekanntes Feld ") + key; return false; }
    String v(value ? value : "");
    if (!normalize(i, v, err)) return false;
    setRaw(i, v, true);
    if (scenario >= 0 && snapshot) snapshot[i] = v;   // a manual change survives "stop"
    return true;
}

bool emuSetModeReq(uint8_t dev, uint8_t mode) {
    if (dev >= D_COUNT || mode > M_OFFLINE) return false;
    Lock lock;
    modes[dev] = mode;
    markSave();
    return true;
}

void emuSetNoise(uint8_t dev, bool on) {
    if (dev >= D_COUNT) return;
    Lock lock;
    noiseOn[dev] = on;
    markSave();
}

bool emuStartScenario(const char* id, uint8_t dev, String& err) {
    Lock lock;
    int s = -1;
    for (int i = 0; i < SCENARIO_COUNT; i++) if (!strcmp(SCENARIOS[i].id, id)) s = i;
    if (s < 0) { err = "Unbekanntes Szenario"; return false; }
    if (scenario >= 0) emuStopScenario(true);
    if (!snapshot) snapshot = new String[FIELD_COUNT];
    for (int i = 0; i < FIELD_COUNT; i++) snapshot[i] = vals[i];
    memcpy(snapModes, modes, sizeof(modes));
    scenarioDev = dev < D_ROUTER ? dev : D_TANK;   // only devices with an own connection can drop out
    scenario = s;
    scenarioStart = millis();
    // start values the scenario needs
    if (!strcmp(id, "depart")) { setStr("door", "open", false); setNum("spd", 0, false); setBool("fix", true, false); modes[D_ROUTER] = M_ON; }
    if (!strcmp(id, "drive")) { modes[D_ROUTER] = M_ON; }
    if (!strcmp(id, "fridge")) { setBool("t1_on", true, false); setBool("t1_ok", true, false); }
    if (!strcmp(id, "battery")) { setBool("b1_on", true, false); setBool("b1_ok", true, false); }
    if (!strcmp(id, "frost")) { setBool("t4_on", true, false); setBool("t4_ok", true, false); setBool("k2_on", true, false); }
    if (!strcmp(id, "heat")) {
        setBool("alive", true, false); setStr("hvac", "heat", false); setNum("setp", 22, false);
        setNum("room", 12, false); setStr("wmode", "eco", false); setNum("water", 15, false); setBool("ht_dyn", true, false);
    }
    Serial.printf("[EMU] Scenario %s started\n", id);
    return true;
}

void emuStopScenario(bool restore) {
    Lock lock;
    if (scenario < 0) return;
    if (!strcmp(SCENARIOS[scenario].id, "depart")) awning(false);
    if (restore && snapshot) {
        for (int i = 0; i < FIELD_COUNT; i++) setRaw(i, snapshot[i], false);
        memcpy(modes, snapModes, sizeof(modes));
    }
    Serial.printf("[EMU] Scenario %s stopped%s\n", SCENARIOS[scenario].id, restore ? ", values restored" : "");
    scenario = -1;
    if (!restore) markSave();
}

void emuResetValues(int dev) {
    Lock lock;
    if (scenario >= 0) emuStopScenario(false);
    defaults(dev);
    markSave();
}

void emuRequestClearAll() {
    Lock lock;
    clearAllReq = true;
}

void emuPrint() {
    Lock lock;
    for (int d = 0; d < D_COUNT; d++)
        Serial.printf("[EMU] %-10s %s%s\n", EMU_DEVS[d].id,
                      modes[d] == M_ON ? "online" : modes[d] == M_OFF ? "aus" : "offline", noiseOn[d] ? ", Rauschen" : "");
    for (int i = 0; i < FIELD_COUNT; i++) Serial.printf("  %-12s %s\n", FIELDS[i].key, vals[i].c_str());
}
