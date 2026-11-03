// Shared source – edit only in firmware/_common, then run tools/common/sync_common.py
#include "devinfo.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#include <esp_system.h>
#endif
#include <ArduinoJson.h>

static const char* resetReason() {
#if defined(ESP8266)
    switch (ESP.getResetInfoPtr()->reason) {
        case REASON_DEFAULT_RST:       return "power on";
        case REASON_WDT_RST:           return "watchdog";
        case REASON_EXCEPTION_RST:     return "crash";
        case REASON_SOFT_WDT_RST:      return "watchdog";
        case REASON_SOFT_RESTART:      return "restart";
        case REASON_DEEP_SLEEP_AWAKE:  return "deep sleep";
        case REASON_EXT_SYS_RST:       return "reset pin";
        default:                       return "unknown";
    }
#else
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "power on";
        case ESP_RST_EXT:       return "reset pin";
        case ESP_RST_SW:        return "restart";
        case ESP_RST_PANIC:     return "crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:       return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_USB:       return "usb";
        default:                return "unknown";
    }
#endif
}

String devinfoJson(const char* version) {
    JsonDocument doc;
    doc["version"] = version;
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000UL;   // seconds
    doc["reset"] = resetReason();
    doc["heap"] = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    return out;
}

bool devinfoDue(bool connected) {
    static bool wasConnected = false;
    static uint32_t last = 0;
    if (!connected) {
        wasConnected = false;
        return false;
    }
    uint32_t now = millis();
    if (!wasConnected || now - last >= DEVINFO_INTERVAL_MS) {
        wasConnected = true;
        last = now;
        return true;
    }
    return false;
}
