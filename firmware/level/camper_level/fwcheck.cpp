// Shared source – edit only in firmware/_common, then run tools/common/sync_common.py
#include "fwcheck.h"
#include "config.h"   // FW_DEVICE, FW_VERSION
#include "evlog.h"

#ifndef FW_DEVICE
#error "fwcheck needs FW_DEVICE in config.h (short device id, e.g. \"maxxfan\")"
#endif

#if defined(ESP32)
#include <Update.h>
#include <esp_ota_ops.h>
#include <sdkconfig.h>
#include <algorithm>

// The marker. Referenced by id(), so the linker keeps it in the image.
static const char FW_ID[] = "CAMPER-FW:" FW_DEVICE ":" FW_VERSION ":" CONFIG_IDF_TARGET ";";

namespace {

const char PREFIX[] = {'C', 'A', 'M', 'P', 'E', 'R', '-', 'F', 'W', ':'};   // no string literal: not a second marker
const size_t TAIL = 96;                        // longer than any marker, kept across chunks
const size_t MERGED_PT = 0x8000;               // partition table offset inside a .merged.bin (C3 / S3)

bool active = false, failed = false, done = false, force = false, found = false;
size_t seen = 0;
uint8_t hdr[16];
uint8_t pt[2];
std::string tail;
String err, fileDev, fileVer, fileChip;

const char* chipName(uint16_t id) {
    switch (id) {
        case 0x0000: return "ESP32";
        case 0x0002: return "ESP32-S2";
        case 0x0005: return "ESP32-C3";
        case 0x0009: return "ESP32-S3";
        case 0x000C: return "ESP32-C2";
        case 0x000D: return "ESP32-C6";
        case 0x0010: return "ESP32-H2";
        default:     return "unbekannter Chip";
    }
}

size_t maxSize() {
    const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
    return p ? p->size : 0;
}

void fail(const String& why) {
    if (failed) return;
    failed = true;
    err = why;
    if (active) Update.abort();
    active = false;
    evlog::add("Update abgelehnt: %s", err.c_str());
}

bool validPart(const std::string& s) {
    if (s.empty() || s.size() > 24) return false;
    for (char c : s)
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false;
    return true;
}

// Looks for a valid marker in the data seen so far (tail + this chunk)
void scan(const uint8_t* data, size_t len) {
    std::string buf = tail;
    buf.append((const char*)data, len);
    size_t from = 0;
    while (!found) {
        auto it = std::search(buf.begin() + from, buf.end(), PREFIX, PREFIX + sizeof(PREFIX));
        if (it == buf.end()) break;
        size_t at = it - buf.begin();
        size_t end = buf.find(';', at);
        if (end == std::string::npos) break;   // marker cut off: complete with the next chunk
        std::string body = buf.substr(at + sizeof(PREFIX), end - at - sizeof(PREFIX));
        size_t c1 = body.find(':'), c2 = body.find(':', c1 == std::string::npos ? 0 : c1 + 1);
        if (c1 != std::string::npos && c2 != std::string::npos && end - at < 80) {
            std::string d = body.substr(0, c1), v = body.substr(c1 + 1, c2 - c1 - 1), c = body.substr(c2 + 1);
            if (validPart(d) && validPart(v) && validPart(c)) {
                found = true;
                fileDev = d.c_str();
                fileVer = v.c_str();
                fileChip = c.c_str();
                break;
            }
        }
        from = at + 1;
    }
    tail = buf.size() > TAIL ? buf.substr(buf.size() - TAIL) : buf;
}

} // namespace

namespace fwcheck {

const char* id() { return FW_ID; }

void info(JsonObject o) {
    o["dev"] = FW_DEVICE;
    o["ver"] = FW_VERSION;
    o["chip"] = CONFIG_IDF_TARGET;
    o["chip_id"] = CONFIG_IDF_FIRMWARE_CHIP_ID;
    o["max"] = (uint32_t)maxSize();
    o["id"] = id();
}

void begin(size_t total, bool forceIt) {
    if (active) Update.abort();
    active = failed = done = found = false;
    force = forceIt;
    seen = 0;
    tail.clear();
    err = fileDev = fileVer = fileChip = "";
    size_t max = maxSize();
    evlog::add("Firmware-Upload gestartet (%u kB)", (unsigned)(total / 1024));
    if (max == 0) { fail("kein Update-Bereich im Flash (Partitionsschema?)"); return; }
    if (total > max + 4096) {
        fail(String("Datei zu groß (") + String((unsigned)(total / 1024)) + " kB, Platz " +
             String((unsigned)(max / 1024)) + " kB) – evtl. die .merged.bin statt der .bin?");
        return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { fail(String("Update-Start: ") + Update.errorString()); return; }
    active = true;
}

void chunk(const uint8_t* data, size_t len, bool final) {
    if (failed || done) return;
    // header bytes and the partition table position of a .merged.bin, wherever the chunks end
    for (size_t i = 0; i < len; i++) {
        size_t at = seen + i;
        if (at < sizeof(hdr)) hdr[at] = data[i];
        else if (at == MERGED_PT || at == MERGED_PT + 1) pt[at - MERGED_PT] = data[i];
        else if (at > MERGED_PT + 1 && at >= sizeof(hdr)) break;
    }
    size_t before = seen;
    seen += len;
    if (before < sizeof(hdr) && seen >= sizeof(hdr)) {
        if (hdr[0] != 0xE9) { fail("keine ESP32-Firmware (.bin aus „Kompilierte Binärdatei exportieren“ oder aus dem Release)"); return; }
        uint16_t chip = hdr[12] | (hdr[13] << 8);
        if (chip != CONFIG_IDF_FIRMWARE_CHIP_ID) {
            fail(String("Firmware für ") + chipName(chip) + ", dieses Gerät ist ein " + chipName(CONFIG_IDF_FIRMWARE_CHIP_ID));
            return;
        }
    }
    if (before <= MERGED_PT + 1 && seen > MERGED_PT + 1 && pt[0] == 0xAA && pt[1] == 0x50) {
        fail("das ist die .merged.bin (komplettes Flash-Abbild für USB) – bitte die .bin ohne „merged“ nehmen");
        return;
    }
    scan(data, len);
    if (found && fileDev != FW_DEVICE) {
        fail(String("Firmware für „") + fileDev + "“, dieses Gerät ist „" FW_DEVICE "“");
        return;
    }
    if (len && Update.write((uint8_t*)data, len) != len) { fail(String("Schreiben: ") + Update.errorString()); return; }
    if (!final) return;

    if (seen < sizeof(hdr)) { fail("Datei zu klein"); return; }
    if (!found && !force) {
        fail("keine Gerätekennung in der Datei (fremde oder ältere Firmware) – zum Erzwingen „Ohne Kennung hochladen“ wählen");
        return;
    }
    if (!Update.end(true)) { fail(String("Abschluss: ") + Update.errorString()); return; }
    active = false;
    done = true;
    if (found) evlog::add("Update auf v%s ok (%u kB), Neustart", fileVer.c_str(), (unsigned)(seen / 1024));
    else evlog::add("Update ohne Kennung ok (%u kB), Neustart", (unsigned)(seen / 1024));
}

bool ok() { return done && !failed; }

const String& error() {
    static const String unknown = "Upload unvollständig";
    if (!failed && !done) return unknown;
    return err;
}

} // namespace fwcheck

#else   // ESP8266: no checks here

namespace fwcheck {
const char* id() { return ""; }
void info(JsonObject) {}
void begin(size_t, bool) {}
void chunk(const uint8_t*, size_t, bool) {}
bool ok() { return false; }
const String& error() { static const String e = "nicht unterstützt"; return e; }
} // namespace fwcheck

#endif
