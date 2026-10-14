#include "wifi_manager.h"
#include <WiFi.h>

static WiFiState wifiState = WIFI_STATE_INIT;
static unsigned long lastReconnectAttempt = 0;
static bool apStarted = false;

#define RECONNECT_INTERVAL     30000  // Retry every 30s in fallback
#define SINGLE_ATTEMPT_TIMEOUT 10000  // 10s per attempt
#define MAX_ATTEMPTS           5

// Forward declarations
static bool blockingConnect();

void wifiManagerInit() {
    Serial.println("[WiFi] Init...");
    
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
            
            // Add AP on same channel as router
            WiFi.mode(WIFI_AP_STA);
            delay(100);
            WiFi.begin(config.wifi_ssid, config.wifi_pass);
            unsigned long w = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - w < 5000) {
                delay(50);
            }
            startAP(ch);
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
    WiFi.setTxPower(WIFI_TX_POWER);  // Reduced TX power is more stable on ESP32-C3 boards
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
            }
            sensorData.rssi = WiFi.RSSI();
            break;
            
        case WIFI_STATE_RECONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFi] Reconnected! RSSI: %d\n", WiFi.RSSI());
                WiFi.setSleep(WIFI_PS_MIN_MODEM);  // Keep modem sleep for WiFi/Bluetooth coexistence
                wifiState = WIFI_STATE_CONNECTED;
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
        strlcpy(config.ap_ssid, DEFAULT_AP_SSID, sizeof(config.ap_ssid));
    }
    if (strlen(config.ap_pass) < 8) {
        strlcpy(config.ap_pass, DEFAULT_AP_PASSWORD, sizeof(config.ap_pass));
    }
    
    WiFi.setTxPower(WIFI_TX_POWER);
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
