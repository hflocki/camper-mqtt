#include "wifi_manager.h"
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>

static WiFiState wifiState = WIFI_STATE_INIT;
static unsigned long lastReconnectAttempt = 0;
static bool apStarted = false;
static unsigned long lostSince = 0;     // millis() when the WiFi connection was lost

static const int8_t TX_POWER_OPTIONS[] = {34, 44, 52, 60, 70, 78};   // 8.5 ... 19.5 dBm

#define RECONNECT_INTERVAL     30000  // Retry every 30s in fallback
#define SINGLE_ATTEMPT_TIMEOUT 10000  // 10s per attempt
#define MAX_ATTEMPTS           5

// Forward declarations
static bool blockingConnect();

static wifi_power_t txPower() {
    return isValidTxPower(config.wifi_tx_power) ? (wifi_power_t)config.wifi_tx_power : (wifi_power_t)DEFAULT_TX_POWER;
}

// Logs why the router dropped the connection (beacon timeout, auth expired, AP not found ...)
static void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
    if (wifiState != WIFI_STATE_CONNECTED) return;   // Only count drops of an established connection
    uint8_t reason = info.wifi_sta_disconnected.reason;
    const char* name = WiFi.disconnectReasonName((wifi_err_reason_t)reason);
    sensorData.wifi_drops++;
    snprintf(sensorData.wifi_reason, sizeof(sensorData.wifi_reason), "%s (%u)", name ? name : "?", reason);
    Serial.printf("[WiFi] Disconnected: %s, RSSI before: %d dBm\n", sensorData.wifi_reason, sensorData.rssi);
}

// AP-only-as-fallback: switch the own access point off while the WiFi works
static void stopApIfNotNeeded() {
    if (config.ap_always || !apStarted) return;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apStarted = false;
    Serial.println("[WiFi] AP off (fallback only)");
}

void wifiManagerInit() {
    Serial.println("[WiFi] Init...");
    
    WiFi.persistent(false);
    WiFi.onEvent(onWiFiEvent);
    strlcpy(sensorData.wifi_reason, "-", sizeof(sensorData.wifi_reason));
    Serial.printf("[WiFi] Last reset: %s | TX power: %.1f dBm | AP: %s\n", resetReasonString(),
        (int)txPower() / 4.0f, config.ap_always ? "always" : "fallback only");
    WiFi.persistent(false);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(500);
    
    // WiFi and Bluetooth share one radio: modem sleep is REQUIRED for coexistence,
    // otherwise Bluetooth gets almost no airtime and the scan finds nothing
    WiFi.setSleep(WIFI_PS_MIN_MODEM);
    
    if (strlen(config.wifi_ssid) > 0) {
        Serial.printf("[WiFi] SSID: %s — trying to connect...\n", config.wifi_ssid);
        
        bool connected = blockingConnect();
        
        if (connected) {
            int ch = WiFi.channel();
            if (ch < 1 || ch > 13) ch = 1;
            Serial.printf("[WiFi] Connected! IP: %s CH: %d RSSI: %d\n",
                WiFi.localIP().toString().c_str(), ch, WiFi.RSSI());
            
            if (config.ap_always) {
                // Add AP on same channel as router
                WiFi.mode(WIFI_AP_STA);
                delay(100);
                WiFi.begin(config.wifi_ssid, config.wifi_pass);
                unsigned long w = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - w < 5000) {
                    delay(50);
                }
                startAP(ch);
            } else {
                Serial.println("[WiFi] AP off while connected (fallback only)");
            }
            wifiState = WIFI_STATE_CONNECTED;
        } else {
            Serial.println("[WiFi] All attempts failed — AP fallback");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_AP);
            delay(100);
            startAP(1);
            wifiState = WIFI_STATE_FALLBACK_AP;
            lastReconnectAttempt = millis();
        }
    } else {
        WiFi.mode(WIFI_AP);
        delay(100);
        WiFi.setHostname(OTA_HOSTNAME);
        startAP(1);
        wifiState = WIFI_STATE_AP;
        Serial.println("[WiFi] No SSID — AP only");
    }
}

static bool blockingConnect() {
    WiFi.mode(WIFI_STA);
    delay(500);  // ESP32-C3 needs time for WiFi stack to fully init
    WiFi.setTxPower(txPower());  // Reduced TX power is more stable on some boards (web UI)
    WiFi.setHostname(OTA_HOSTNAME);
    WiFi.setSleep(WIFI_PS_MIN_MODEM);  // Keep modem sleep for WiFi/Bluetooth coexistence
    
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        Serial.printf("[WiFi] Attempt %d/%d...\n", attempt, MAX_ATTEMPTS);
        
        WiFi.disconnect(true);
        delay(500);  // More time between attempts
        
        WiFi.begin(config.wifi_ssid, config.wifi_pass);
        
        unsigned long start = millis();
        while (millis() - start < SINGLE_ATTEMPT_TIMEOUT) {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFi] OK on attempt %d\n", attempt);
                return true;
            }
            delay(500);
            yield();
        }
        
        Serial.printf("[WiFi] Attempt %d failed (status %d)\n", attempt, (int)WiFi.status());
    }
    
    return false;
}

void wifiManagerLoop() {
    switch (wifiState) {
        case WIFI_STATE_CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi] Lost — reconnecting...");
                WiFi.disconnect(true);
                delay(100);
                WiFi.begin(config.wifi_ssid, config.wifi_pass);
                wifiState = WIFI_STATE_RECONNECTING;
                lastReconnectAttempt = millis();
                lostSince = millis();
            }
            sensorData.rssi = WiFi.RSSI();
            break;
            
        case WIFI_STATE_RECONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFi] Reconnected! RSSI: %d\n", WiFi.RSSI());
                WiFi.setSleep(WIFI_PS_MIN_MODEM);  // Keep modem sleep for WiFi/Bluetooth coexistence
                wifiState = WIFI_STATE_CONNECTED;
                stopApIfNotNeeded();
            } else if (!config.ap_always && !apStarted && millis() - lostSince > AP_FALLBACK_MS) {
                // WiFi gone for a while: open the AP so the device stays reachable, keep reconnecting
                Serial.println("[WiFi] No WiFi for 60 s - starting fallback AP");
                WiFi.mode(WIFI_AP_STA);
                delay(100);
                startAP(WiFi.channel() >= 1 && WiFi.channel() <= 13 ? WiFi.channel() : 1);
                WiFi.begin(config.wifi_ssid, config.wifi_pass);
                lastReconnectAttempt = millis();
            } else if (millis() - lastReconnectAttempt > SINGLE_ATTEMPT_TIMEOUT) {
                Serial.println("[WiFi] Reconnect timeout — retry...");
                WiFi.disconnect(true);
                delay(100);
                WiFi.begin(config.wifi_ssid, config.wifi_pass);
                lastReconnectAttempt = millis();
            }
            sensorData.rssi = 0;
            break;
            
        case WIFI_STATE_FALLBACK_AP:
            sensorData.rssi = 0;
            if (strlen(config.wifi_ssid) > 0 && 
                millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
                lastReconnectAttempt = millis();
                Serial.println("[WiFi] Fallback retry...");
                WiFi.mode(WIFI_AP_STA);
                delay(100);
                WiFi.begin(config.wifi_ssid, config.wifi_pass);
                unsigned long start = millis();
                while (millis() - start < SINGLE_ATTEMPT_TIMEOUT) {
                    if (WiFi.status() == WL_CONNECTED) {
                        Serial.printf("[WiFi] Reconnected from fallback! CH: %d\n", (int)WiFi.channel());
                        wifiState = WIFI_STATE_CONNECTED;
                        stopApIfNotNeeded();
                        return;
                    }
                    delay(250);
                    yield();
                }
                WiFi.disconnect(true);
                WiFi.mode(WIFI_AP);
                delay(100);
                if (!apStarted) startAP(1);
            }
            break;

        case WIFI_STATE_AP:
            sensorData.rssi = 0;
            break;
            
        default:
            break;
    }
}

WiFiState getWiFiState() {
    return wifiState;
}

String getWiFiStateString() {
    switch (wifiState) {
        case WIFI_STATE_INIT:         return "Initialisierung";
        case WIFI_STATE_AP:           return "Access Point";
        case WIFI_STATE_CONNECTING:   return "Verbinde...";
        case WIFI_STATE_CONNECTED:    return "Verbunden";
        case WIFI_STATE_RECONNECTING: return "Reconnecting...";
        case WIFI_STATE_FALLBACK_AP:  return "AP (Fallback)";
        default:                      return "Unbekannt";
    }
}

bool isConnected() {
    return (wifiState == WIFI_STATE_CONNECTED && WiFi.status() == WL_CONNECTED);
}

void startAP(int channel) {
    Serial.printf("[WiFi] Starting AP CH %d...\n", channel);
    
    if (strlen(config.ap_ssid) == 0) {
        strlcpy(config.ap_ssid, defaultApSsid(), sizeof(config.ap_ssid));
    }
    if (strlen(config.ap_pass) < 8) {
        strlcpy(config.ap_pass, DEFAULT_AP_PASSWORD, sizeof(config.ap_pass));
    }
    
    WiFi.setTxPower(txPower());
    bool ok = WiFi.softAP(config.ap_ssid, config.ap_pass, channel, false, 4);
    delay(100);
    
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    
    apStarted = ok;
    delay(300);
    
    Serial.printf("[WiFi] AP %s: %s IP: %s CH: %d\n",
        ok ? "OK" : "FAIL", config.ap_ssid,
        WiFi.softAPIP().toString().c_str(), (int)WiFi.channel());
}

void connectToWiFi() {
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(config.wifi_ssid, config.wifi_pass);
    lastReconnectAttempt = millis();
    wifiState = WIFI_STATE_RECONNECTING;
}

bool isValidTxPower(int v) {
    for (int8_t o : TX_POWER_OPTIONS) if (o == v) return true;
    return false;
}

const char* resetReasonString() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "Einschalten";
        case ESP_RST_SW:        return "Neustart (Software)";
        case ESP_RST_PANIC:     return "Absturz";
        case ESP_RST_INT_WDT:   return "Watchdog (Interrupt)";
        case ESP_RST_TASK_WDT:  return "Watchdog (Task)";
        case ESP_RST_WDT:       return "Watchdog";
        case ESP_RST_BROWNOUT:  return "Brownout (Spannung zu niedrig)";
        case ESP_RST_DEEPSLEEP: return "Deep Sleep";
        case ESP_RST_EXT:       return "Reset-Pin";
        case ESP_RST_USB:       return "USB";
        default:                return "Unbekannt";
    }
}

const char* defaultApSsid() {
    static char ssid[32] = "";
    if (ssid[0] == '\0') {
        // Station MAC (works before WiFi is started); same address the router shows
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(ssid, sizeof(ssid), "%s-%02X%02X%02X", DEFAULT_AP_SSID, mac[3], mac[4], mac[5]);
    }
    return ssid;
}
