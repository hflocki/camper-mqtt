#include "safeguard.h"
#include "config.h"   // FW_VERSION

#if defined(ESP32)

#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// NVS keys (namespace "safeguard"):
//   ota   1 = new firmware on probation      new  app slot label of the new firmware
//   old   app slot label to go back to       oldv version of the previous firmware
//   boots starts on probation so far
//   wtr   1 = WiFi trial running             wss/wpw/wex  previous WiFi values
//   note  last event for the web UI
static const char* NS = "safeguard";

static bool     probation = false;
static uint32_t healthySince = 0;
static bool     wifiTrial = false;     // backup stored
static bool     trialAtBoot = false;   // ... already before this start: the new values are live now
static String   note;          // cached copy of the "note" key

static String readStr(const char* key) {
    Preferences p;
    p.begin(NS, true);
    String v = p.getString(key, "");
    p.end();
    return v;
}

static void setNote(const String& text) {
    note = text;
    Preferences p;
    p.begin(NS, false);
    p.putString("note", text);
    p.end();
    Serial.printf("[Guard] %s\n", text.c_str());
}

static void endProbation() {
    Preferences p;
    p.begin(NS, false);
    p.putUChar("ota", 0);
    p.putUChar("boots", 0);
    p.end();
    probation = false;
}

// Boots the previous firmware. Returns only if that is not possible.
static void rollback(const char* reason) {
    String oldLabel = readStr("old"), oldVer = readStr("oldv");
    const esp_partition_t* old = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY,
                                                          oldLabel.c_str());
    endProbation();
    if (old && esp_ota_set_boot_partition(old) == ESP_OK) {
        setNote(String("Firmware " FW_VERSION " ") + reason + " – zurück auf " +
                (oldVer.length() ? oldVer : String("vorherige Version")));
        delay(200);
        ESP.restart();
    }
    setNote(String("Firmware " FW_VERSION " ") + reason + ", vorherige Firmware nicht startbar");
}

namespace guard {

void begin() {
    Preferences p;
    p.begin(NS, false);
    wifiTrial = trialAtBoot = p.getUChar("wtr", 0) == 1;
    note = p.getString("note", "");
    if (p.getUChar("ota", 0) != 1) { p.end(); return; }

    const esp_partition_t* run = esp_ota_get_running_partition();
    if (!run || p.getString("new", "") != run->label) {
        // Not the firmware that was updated (flashed over USB meanwhile): nothing to guard
        p.putUChar("ota", 0);
        p.end();
        return;
    }
    uint8_t boots = p.getUChar("boots", 0) + 1;
    p.putUChar("boots", boots);
    p.end();
    probation = true;
    Serial.printf("[Guard] New firmware on probation, start %u of %u\n", boots, GUARD_MAX_BOOTS);
    if (boots > GUARD_MAX_BOOTS) rollback("startet immer wieder neu");
}

void otaDone() {
    const esp_partition_t* run = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_boot_partition();   // already switched by Update.end()
    if (!run || !next || run == next) return;
    Preferences p;
    p.begin(NS, false);
    p.putUChar("ota", 1);
    p.putUChar("boots", 0);
    p.putString("new", next->label);
    p.putString("old", run->label);
    p.putString("oldv", FW_VERSION);
    p.putString("note", "");
    p.end();
    note = "";
    Serial.printf("[Guard] Update %s -> %s, previous firmware kept as fallback\n", run->label, next->label);
}

void loop(bool healthy) {
    if (!probation) return;
    uint32_t now = millis();
    if (healthy) {
        if (!healthySince) healthySince = now ? now : 1;
        if (now - healthySince >= GUARD_CONFIRM_MS) {
            endProbation();
            Serial.println("[Guard] New firmware confirmed");
        }
        return;
    }
    healthySince = 0;
    if (now >= GUARD_TIMEOUT_MS) rollback("ohne WLAN-Verbindung");
}

bool onProbation() { return probation; }

void wifiTrialStart(const String& ssid, const String& pass, const String& extra) {
    if (wifiTrial || ssid.isEmpty()) return;   // keep the older, known good values
    Preferences p;
    p.begin(NS, false);
    p.putUChar("wtr", 1);
    p.putString("wss", ssid);
    p.putString("wpw", pass);
    p.putString("wex", extra);
    p.end();
    wifiTrial = true;
    Serial.printf("[Guard] WiFi change, '%s' kept as fallback\n", ssid.c_str());
}

bool wifiTrialPending() { return wifiTrial && trialAtBoot; }

bool wifiTrialRestore(String& ssid, String& pass, String& extra) {
    if (!wifiTrialPending()) return false;
    Preferences p;
    p.begin(NS, false);
    ssid = p.getString("wss", "");
    pass = p.getString("wpw", "");
    extra = p.getString("wex", "");
    p.putUChar("wtr", 0);
    p.remove("wpw");
    p.end();
    wifiTrial = trialAtBoot = false;
    if (ssid.isEmpty()) return false;
    setNote("Neues WLAN nicht erreichbar – zurück auf „" + ssid + "“");
    return true;
}

void wifiTrialOk() {
    if (!wifiTrialPending()) return;
    Preferences p;
    p.begin(NS, false);
    p.putUChar("wtr", 0);
    p.remove("wpw");
    p.end();
    wifiTrial = trialAtBoot = false;
    Serial.println("[Guard] New WiFi works");
}

String status() {
    if (probation) return "Neue Firmware " FW_VERSION " im Probelauf";
    if (wifiTrial) return "Neues WLAN wird getestet";
    return note;
}

} // namespace guard

#else   // no app slots / NVS: nothing to guard

namespace guard {
void begin() {}
void otaDone() {}
void loop(bool) {}
bool onProbation() { return false; }
void wifiTrialStart(const String&, const String&, const String&) {}
bool wifiTrialPending() { return false; }
bool wifiTrialRestore(String&, String&, String&) { return false; }
void wifiTrialOk() {}
String status() { return ""; }
} // namespace guard

#endif
