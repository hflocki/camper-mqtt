#include "registry.h"

// ============================================================
// Plugin factories (one per plugin type, implemented in src/plugins/)
// To add a plugin type: declare its factory here, add a PLUGIN_xxx switch to
// config.h and one ADD_PLUGINS line in registryCreate().
// ============================================================
#if PLUGIN_DIMES
Plugin* createDimes(uint8_t index);
#endif
#if PLUGIN_MOPEKA_PRO
Plugin* createMopekaPro(uint8_t index);
#endif
#if PLUGIN_MOPEKA_STD
Plugin* createMopekaStd(uint8_t index);
#endif
#if PLUGIN_CARAWATER
Plugin* createCaraWater(uint8_t index);
#endif
#if PLUGIN_RVTECH
Plugin* createRvTech(uint8_t index);
#endif
#if PLUGIN_WCS_FRIDGE
Plugin* createWcsFridge(uint8_t index);
#endif

#define MAX_PLUGINS 8
static Plugin* plugins[MAX_PLUGINS];
static uint8_t count = 0;

static void add(Plugin* p) {
    if (p && count < MAX_PLUGINS) plugins[count++] = p;
}

#define ADD_PLUGINS(n, factory) \
    for (uint8_t i = 0; i < (n); i++) add(factory(i))

void registryCreate() {
    if (count) return;
#if PLUGIN_DIMES
    ADD_PLUGINS(PLUGIN_DIMES, createDimes);
#endif
#if PLUGIN_MOPEKA_PRO
    ADD_PLUGINS(PLUGIN_MOPEKA_PRO, createMopekaPro);
#endif
#if PLUGIN_MOPEKA_STD
    ADD_PLUGINS(PLUGIN_MOPEKA_STD, createMopekaStd);
#endif
#if PLUGIN_CARAWATER
    ADD_PLUGINS(PLUGIN_CARAWATER, createCaraWater);
#endif
#if PLUGIN_RVTECH
    ADD_PLUGINS(PLUGIN_RVTECH, createRvTech);
#endif
#if PLUGIN_WCS_FRIDGE
    ADD_PLUGINS(PLUGIN_WCS_FRIDGE, createWcsFridge);
#endif
    for (uint8_t i = 0; i < count; i++) plugins[i]->setDefaults();
}

uint8_t pluginCount() {
    return count;
}

Plugin* pluginAt(uint8_t i) {
    return i < count ? plugins[i] : nullptr;
}

Plugin* pluginById(const char* id) {
    for (uint8_t i = 0; i < count; i++) {
        if (strcmp(plugins[i]->id, id) == 0) return plugins[i];
    }
    return nullptr;
}

bool registryNeedsBle() {
    for (uint8_t i = 0; i < count; i++) {
        if (plugins[i]->enabled && plugins[i]->bleUse() != BLE_NONE) return true;
    }
    return false;
}

// ============================================================
// Plugin base class + helpers
// ============================================================
Plugin::Plugin(const char* type_, const char* typeName_, const char* title, uint8_t index_)
    : type(type_), typeName(typeName_), index(index_) {
    if (index == 0) {
        strlcpy(id, type, sizeof(id));
        strlcpy(name, title, sizeof(name));
    } else {
        snprintf(id, sizeof(id), "%s%u", type, (unsigned)(index + 1));
        snprintf(name, sizeof(name), "%s %u", title, (unsigned)(index + 1));
    }
}

JsonArray liveRow(JsonObject o, const char* label) {
    JsonArray rows = o["rows"].is<JsonArray>() ? o["rows"].as<JsonArray>() : o["rows"].to<JsonArray>();
    JsonArray r = rows.add<JsonArray>();
    r.add(label);
    return r;
}

void liveRowNum(JsonObject o, const char* label, float v, int decimals, const char* unit) {
    JsonArray r = liveRow(o, label);
    if (isnan(v)) {
        r.add(nullptr);
    } else {
        float f = powf(10.0f, (float)decimals);
        r.add(roundf(v * f) / f);
    }
    r.add(unit ? unit : "");
}

void liveRowText(JsonObject o, const char* label, const char* v) {
    JsonArray r = liveRow(o, label);
    r.add(v);
    r.add("");
}

JsonObject liveCtrl(JsonObject o, const char* t, const char* key, const char* label) {
    JsonArray c = o["ctrl"].is<JsonArray>() ? o["ctrl"].as<JsonArray>() : o["ctrl"].to<JsonArray>();
    JsonObject x = c.add<JsonObject>();
    x["t"] = t;
    x["k"] = key;
    if (label) x["l"] = label;
    return x;
}

bool payloadOn(const char* v) {
    return v && (strcasecmp(v, "on") == 0 || strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0);
}
