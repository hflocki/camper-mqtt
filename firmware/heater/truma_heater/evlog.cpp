// Shared source – edit only in firmware/_common, then run tools/common/sync_common.py
#include "evlog.h"
#include <stdarg.h>

#if defined(ESP32) || defined(ESP8266)
#if defined(ESP32)
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_system.h>
#define EVLOG_KEEP RTC_NOINIT_ATTR        // survives restart, crash and update
#define EVLOG_LOCK() portENTER_CRITICAL(&mux)
#define EVLOG_UNLOCK() portEXIT_CRITICAL(&mux)
#else
#include <ESP8266WiFi.h>
#define EVLOG_KEEP                        // ESP8266: plain RAM, lost on every restart
#define EVLOG_LOCK()
#define EVLOG_UNLOCK()
#endif

namespace {

const uint32_t MAGIC = 0x45564C31;   // "EVL1"; anything else = power on or another firmware's data

struct Entry {
    uint16_t boot;
    uint32_t sec;
    char text[72];
};

struct Store {
    uint32_t magic;
    uint16_t boot;
    uint8_t head, count;
    Entry e[EVLOG_SIZE];
};

EVLOG_KEEP Store rtc;
#if defined(ESP32)
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#endif

} // namespace

namespace evlog {

void begin() {
    if (rtc.magic != MAGIC || rtc.head >= EVLOG_SIZE || rtc.count > EVLOG_SIZE) {
        memset(&rtc, 0, sizeof(rtc));
        rtc.magic = MAGIC;
    }
    rtc.boot++;
}

void add(const char* fmt, ...) {
    char buf[sizeof(Entry::text)];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    Serial.printf("[log] %s\n", buf);
    EVLOG_LOCK();
    Entry& e = rtc.e[rtc.head];
    e.boot = rtc.boot;
    e.sec = millis() / 1000;
    memcpy(e.text, buf, sizeof(buf));
    rtc.head = (rtc.head + 1) % EVLOG_SIZE;
    if (rtc.count < EVLOG_SIZE) rtc.count++;
    EVLOG_UNLOCK();
}

void json(JsonArray out) {
    EVLOG_LOCK();
    uint8_t head = rtc.head, count = rtc.count;
    uint16_t boot = rtc.boot;
    EVLOG_UNLOCK();
    for (int k = 0; k < count; k++) {
        Entry e;   // one entry at a time: little stack (ESP8266)
        EVLOG_LOCK();
        e = rtc.e[(head - 1 - k + 2 * EVLOG_SIZE) % EVLOG_SIZE];
        EVLOG_UNLOCK();
        JsonObject o = out.add<JsonObject>();
        o["b"] = (uint16_t)(boot - e.boot);
        o["t"] = e.sec;
        o["m"] = (const char*)e.text;
    }
}

const char* resetReason() {
#if defined(ESP8266)
    switch (ESP.getResetInfoPtr()->reason) {
        case REASON_DEFAULT_RST:      return "Einschalten";
        case REASON_WDT_RST:          return "Watchdog (Hardware)";
        case REASON_EXCEPTION_RST:    return "Absturz";
        case REASON_SOFT_WDT_RST:     return "Watchdog (Software)";
        case REASON_SOFT_RESTART:     return "Neustart (Software)";
        case REASON_DEEP_SLEEP_AWAKE: return "Deep Sleep";
        case REASON_EXT_SYS_RST:      return "Reset-Pin";
        default:                      return "unbekannt";
    }
#else
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "Einschalten";
        case ESP_RST_SW:        return "Neustart (Software)";
        case ESP_RST_PANIC:     return "Absturz";
        case ESP_RST_INT_WDT:   return "Watchdog (Interrupt)";
        case ESP_RST_TASK_WDT:  return "Watchdog (Task)";
        case ESP_RST_WDT:       return "Watchdog";
        case ESP_RST_BROWNOUT:  return "Spannungseinbruch";
        case ESP_RST_DEEPSLEEP: return "Deep Sleep";
        case ESP_RST_EXT:       return "Reset-Pin";
        case ESP_RST_USB:       return "USB";
        default:                return "unbekannt";
    }
#endif
}

void watch(bool wifi, bool mqtt) {
    static int8_t lastWifi = -1, lastMqtt = -1;
    if (wifi != (lastWifi == 1)) {
        if (wifi) add("WLAN verbunden (%s, %d dBm)", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
        else if (lastWifi >= 0) add("WLAN getrennt");
        lastWifi = wifi;
    }
    if (mqtt != (lastMqtt == 1)) {
        if (mqtt) add("MQTT verbunden");
        else if (lastMqtt >= 0) add("MQTT getrennt");
        lastMqtt = mqtt;
    }
}

} // namespace evlog

#else   // other platforms: nothing is kept

namespace evlog {
void begin() {}
void add(const char*, ...) {}
void json(JsonArray) {}
const char* resetReason() { return ""; }
void watch(bool, bool) {}
} // namespace evlog

#endif
