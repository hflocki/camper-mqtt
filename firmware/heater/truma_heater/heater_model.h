// Heater model selection: which controls a Truma heater actually offers.
// Selected in the web UI (or MQTT / HA), stored in NVS, default DEFAULT_HEATER_MODEL from config.h.
// Changes take effect immediately: web UI, MQTT checks and HA discovery follow the active model.
#pragma once

#include <Arduino.h>

#include "truma_protocol.h"

namespace heater {

struct ModelInfo {
  const char *key;    // stored in NVS and used on MQTT ("combi_d")
  const char *name;   // shown in the web UI
  bool fan_boost;     // room heating fan mode BOOST (gas Combi)
  bool vario;         // VarioHeat: fan modes night/auto/boost instead of eco/high
  bool water;         // has a boiler
  bool water_boost;   // hot water BOOST (gas Combi)
  bool electric;      // 230 V heating element: energy source gas|diesel / mix / electric
  bool diesel;        // diesel burner: decoking available, energy shown as "Diesel"
};

// Order = index used by model_index(). Keep keys stable, they are stored on the devices.
static const ModelInfo MODELS[] = {
    // key         name                         fboost vario  water  wboost elec   diesel
    {"combi",     "Truma Combi 4/6 (Gas)",       true,  false, true,  true,  false, false},
    {"combi_e",   "Truma Combi 4/6 E (Gas + 230 V)", true, false, true, true, true, false},
    {"combi_d",   "Truma Combi D 4/6 (Diesel)",  false, false, true,  false, false, true},
    {"combi_d_e", "Truma Combi D 6 E (Diesel + 230 V)", false, false, true, false, true, true},
    {"vario",     "Truma VarioHeat",             true,  true,  false, false, false, false},
};
static const size_t MODEL_COUNT = sizeof(MODELS) / sizeof(MODELS[0]);

// Index of a model key, -1 if unknown.
inline int model_index(const String &key) {
  for (size_t i = 0; i < MODEL_COUNT; i++)
    if (key.equalsIgnoreCase(MODELS[i].key)) return (int) i;
  return -1;
}

// Active model (from g_settings.heater_model, falls back to the first diesel entry).
const ModelInfo &model();
// Switch the model at runtime. Saves it, refreshes MQTT discovery. false = unknown key.
bool set_model(const String &key);

// Heater family reported by the CP Plus (device list during init), for a plausibility hint.
// Returns nullptr while unknown.
const char *detected_name(truma::Device d);
// Hint text if the selected model clearly does not match the reported heater, else nullptr.
const char *model_mismatch(truma::Device d);

}  // namespace heater
