#include "gas_base.h"

#if CT_BLE_PASSIVE

#include "../core/util.h"
#include <NimBLEDevice.h>

static const Entity GAS_ENTITIES[] = {
    {E_SENSOR, "level",       "Füllstand",       "%",   nullptr,           "measurement", "mdi:gas-cylinder", 0, nullptr},
    {E_SENSOR, "content",     "Inhalt",          "L",   "volume_storage",  "measurement", "mdi:gas-cylinder", 0, nullptr},
    {E_SENSOR, "battery",     "Batterie",        "%",   "battery",         "measurement", nullptr, EF_DIAG, nullptr},
    {E_SENSOR, "raw",         "Füllstand roh",   "%",   nullptr,           "measurement", "mdi:gauge", EF_DIAG | EF_HIDDEN, nullptr},
    {E_SENSOR, "ble_rssi",    "Bluetooth Signal","dBm", "signal_strength", "measurement", "mdi:bluetooth", EF_DIAG | EF_HIDDEN, nullptr},
    {E_SENSOR, "state",       "Status",          nullptr, nullptr,         nullptr,       "mdi:bluetooth-connect", EF_DIAG | EF_ALWAYS, nullptr},
    // Mopeka only
    {E_SENSOR, "distance",    "Füllhöhe",        "mm",  "distance",        "measurement", "mdi:arrow-expand-vertical", 0, nullptr},
    {E_SENSOR, "temperature", "Temperatur",      "°C",  "temperature",     "measurement", nullptr, EF_DIAG, nullptr},
    {E_SENSOR, "quality",     "Messqualität",    nullptr, nullptr,         "measurement", "mdi:signal", EF_DIAG, nullptr},
};

GasPlugin::GasPlugin(const char* type_, const char* typeName_, uint8_t index_)
    : Plugin(type_, typeName_, "Gas", index_) {
    portMUX_INITIALIZE(&mux);
}

void GasPlugin::resetValues() {
    portENTER_CRITICAL(&mux);
    pending = false;
    rawPending = false;
    portEXIT_CRITICAL(&mux);
    hasData = false;
    battery = NAN;
    rawLevel = 0;
    distance = NAN;
    temperature = NAN;
    quality = -1;
    level = 0;
    content = 0;
    bleRssi = 0;
    rawHex[0] = '\0';
    updates++;
}

void GasPlugin::begin() {
    resetValues();
    Serial.printf("[%s] %s  MAC: %s\n", id, typeName, common().mac[0] ? common().mac : "(not set)");
}

void GasPlugin::onConfigChanged() {
    if (strcmp(lastMac, common().mac) != 0) {
        // Other sensor: forget the old readings
        strlcpy(lastMac, common().mac, sizeof(lastMac));
        resetValues();
        return;
    }
    // Same sensor: recalculate from the last raw value (age stays unchanged)
    if (hasData) {
        float l = levelFromDistance(distance);
        if (!isnan(l)) rawLevel = l;
        computeDerived();
        updates++;
    }
}

// Level and content like the Home Assistant integration (fill stop optional)
void GasPlugin::computeDerived() {
    const GasCommon& c = common();
    float fillStop = c.fill_stop_on ? c.fill_stop : 100.0f;
    if (fillStop <= 0.0f) fillStop = 100.0f;
    float l = levelIsUsable() ? rawLevel : rawLevel / fillStop * 100.0f;
    level = constrain(l, 0.0f, 100.0f);
    content = level / 100.0f * c.capacity * fillStop / 100.0f;
}

void GasPlugin::apply(const GasReading& r) {
    if (!isnan(r.battery)) battery = r.battery;
    temperature = r.temperature;
    quality = r.quality;
    bleRssi = r.rssi;
    if (!r.hasLevel) {
        // Mopeka with poor read quality: keep the last level, only show the quality
        updates++;
        Serial.printf("[%s] Read quality %d too low - level ignored\n", id, quality);
        return;
    }
    rawLevel = r.raw;
    distance = r.distance_mm;
    computeDerived();
    lastUpdate = millis();
    hasData = true;
    updates++;
    Serial.printf("[%s] Battery: %.0f%% | Raw: %.1f%% | Level: %.1f%% | Content: %.1f L | RSSI: %d\n",
        id, battery, rawLevel, level, content, bleRssi);
}

// NimBLE host task
void GasPlugin::onAdvertisement(const NimBLEAdvertisedDevice* dev, const char* addr) {
    const char* mac = common().mac;
    if (mac[0] == '\0' || strcmp(addr, mac) != 0) return;
    GasReading r = {false, NAN, 0, NAN, NAN, -1, dev->getRSSI()};
    uint8_t raw[BLE_RAW_MAX];
    size_t n = 0;
    bool ok = decode(dev, r, raw, n);
    portENTER_CRITICAL(&mux);
    if (n) {
        memcpy(rawBuf, raw, n);
        rawLen = n;
        rawPending = true;
    }
    if (ok) {
        pendingReading = r;
        pending = true;
    }
    portEXIT_CRITICAL(&mux);
}

void GasPlugin::loop() {
    GasReading r;
    bool have = false;
    uint8_t raw[BLE_RAW_MAX];
    size_t n = 0;
    portENTER_CRITICAL(&mux);
    if (pending) { r = pendingReading; pending = false; have = true; }
    if (rawPending) { memcpy(raw, rawBuf, rawLen); n = rawLen; rawPending = false; }
    portEXIT_CRITICAL(&mux);

    if (n) bytesToHex(raw, n, rawHex, sizeof(rawHex));
    if (have) apply(r);

    const char* s;
    if (common().mac[0] == '\0')                          s = "Kein Sensor";
    else if (!hasData)                                    s = "Warte auf Daten";
    else if (millis() - lastUpdate > GAS_DATA_TIMEOUT_MS) s = "Keine Daten";
    else                                                  s = "OK";
    if (strcmp(s, state) != 0) {
        state = s;
        updates++;
    }
}

bool GasPlugin::available() {
    return hasData && millis() - lastUpdate <= GAS_DATA_TIMEOUT_MS;
}

void GasPlugin::live(JsonObject o) {
    o["state"] = state;
    o["ok"] = available();
    JsonObject g = o["gauge"].to<JsonObject>();
    if (hasData) g["v"] = roundf(level * 10.0f) / 10.0f;
    else g["v"] = nullptr;
    g["unit"] = "%";
    g["label"] = "Füllstand";
    liveRowNum(o, "Inhalt", hasData ? content : NAN, 1, "L");
    liveRowNum(o, "Rohwert", hasData ? rawLevel : NAN, 1, "%");
    if (hasMopekaValues()) {
        liveRowNum(o, "Füllhöhe", distance, 0, "mm");
        liveRowNum(o, "Temperatur", temperature, 0, "°C");
        liveRowNum(o, "Messqualität", quality < 0 ? NAN : quality, 0, "/ 3");
    }
    liveRowNum(o, "Batterie", battery, 0, "%");
    liveRowNum(o, "Bluetooth", bleRssi ? (float)bleRssi : NAN, 0, "dBm");
    liveRowNum(o, "Alter", hasData ? (millis() - lastUpdate) / 1000.0f : NAN, 0, "s");
    o["raw"] = (const char*)rawHex;
}

size_t GasPlugin::entities(const Entity*& out) {
    out = GAS_ENTITIES;
    return sizeof(GAS_ENTITIES) / sizeof(GAS_ENTITIES[0]);
}

bool GasPlugin::entityActive(const Entity& e) {
    if (!strcmp(e.key, "distance") || !strcmp(e.key, "temperature") || !strcmp(e.key, "quality")) {
        return hasMopekaValues();
    }
    return true;
}

void GasPlugin::publish(MqttOut& out) {
    out.text("state", state);
    out.integer("ble_rssi", bleRssi);
    out.num("battery", battery, 0);
    // Values only once a real reading exists (HA keeps the last value meanwhile)
    if (!hasData) return;
    out.num("level", level, 1);
    out.num("content", content, 1);
    out.num("raw", rawLevel, 1);
    if (hasMopekaValues()) {
        out.num("distance", distance, 0);
        out.num("temperature", temperature, 0);
        if (quality >= 0) out.integer("quality", quality);
    }
}

#endif // CT_BLE_PASSIVE
