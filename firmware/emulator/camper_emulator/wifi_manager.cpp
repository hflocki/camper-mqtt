#include "wifi_manager.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>
#endif

static WifiMgrState wifiState = WIFI_STATE_INIT;
static unsigned long lastReconnectAttempt = 0;
static bool apStarted = false;
static unsigned long lostSince = 0;     // millis() when the WiFi connection was lost

static const int8_t TX_POWER_OPTIONS[] = {34, 44, 52, 60, 70, 78};   // 8.5 ... 19.5 dBm

#define RECONNECT_INTERVAL     30000  // Retry every 30s in fallback
#define SINGLE_ATTEMPT_TIMEOUT 10000  // 10s per attempt
#define MAX_ATTEMPTS           5

// Forward declarations
static bool blockingConnect();

static int8_t txPowerQdbm() {
    return isValidTxPower(config.wifi_tx_power) ? config.wifi_tx_power : DEFAULT_TX_POWER;
}

// ------------------------------------------------------------
// Platform helpers (ESP8266 and ESP32 use different WiFi APIs)
// ------------------------------------------------------------
static void applyTxPower() {
#if defined(ESP8266)
    WiFi.setOutputPower(txPowerQdbm() / 4.0f);     // dBm, 0 ... 20.5
#else
    WiFi.setTxPower((wifi_power_t)txPowerQdbm());  // 0.25 dBm steps
#endif
}

static void applyHostname() {
#if defined(ESP8266)
    WiFi.hostname(OTA_HOSTNAME);
#else
    WiFi.setHostname(OTA_HOSTNAME);
#endif
}

// No Bluetooth on this device: power saving off gives the most stable connection and web UI
static void applySleep() {
#if defined(ESP8266)
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
    WiFi.setSleep(false);
#endif
}

static void fullDisconnect() {
#if defined(ESP8266)
    WiFi.disconnect(true);
#else
    WiFi.disconnect(true, true);
#endif
}

// Logs why the router dropped the connection (beacon timeout, auth expired, AP not found ...)
static void recordDrop(unsigned reason, const char* name) {
    if (wifiState != WIFI_STATE_CONNECTED) return;   // Only count drops of an established connection
    sensorData.wifi_drops++;
    snprintf(sensorData.wifi_reason, sizeof(sensorData.wifi_reason), "%s (%u)", name ? name : "Grund", reason);
    Serial.printf("[WiFi] Disconnected: %s, RSSI before: %d dBm\n", sensorData.wifi_reason, sensorData.rssi);
}

#if defined(ESP8266)
static WiFiEventHandler disconnectHandler;   // Must stay alive, otherwise the handler is removed

static const char* reasonName8266(unsigned r) {
    switch (r) {
        case 2:   return "AUTH_EXPIRE";
        case 4:   return "ASSOC_EXPIRE";
        case 8:   return "ASSOC_LEAVE";
        case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
        case 200: return "BEACON_TIMEOUT";
        case 201: return "NO_AP_FOUND";
        case 202: return "AUTH_FAIL";
        case 203: return "ASSOC_FAIL";
        case 204: return "HANDSHAKE_TIMEOUT";
        default:  return "Grund";
    }
}
#else
static void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
    uint8_t reason = info.wifi_sta_disconnected.reason;
    recordDrop(reason, WiFi.disconnectReasonName((wifi_err_reason_t)reason));
}
#endif

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
#if defined(ESP8266)
    disconnectHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
        recordDrop((unsigned)e.reason, reasonName8266((unsigned)e.reason));
    });
#else
    WiFi.onEvent(onWiFiEvent);
#endif
    strlcpy(sensorData.wifi_reason, "-", sizeof(sensorData.wifi_reason));
    Serial.printf("[WiFi] Last reset: %s | TX power: %.1f dBm | AP: %s\n", resetReasonString(),
        txPowerQdbm() / 4.0f, config.ap_always ? "always" : "fallback only");
    fullDisconnect();
    WiFi.mode(WIFI_OFF);
    delay(500);

    if (strlen(config.wifi_ssid) > 0) {
        Serial.printf("[WiFi] SSID: %s - trying to connect...\n", config.wifi_ssid);

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
                if (WiFi.status() != WL_CONNECTED) WiFi.begin(config.wifi_ssid, config.wifi_pass);
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
            Serial.println("[WiFi] All attempts failed - AP fallback");
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
        applyHostname();
        startAP(1);
        wifiState = WIFI_STATE_AP;
        Serial.println("[WiFi] No SSID - AP only");
    }
}

static bool blockingConnect() {
    WiFi.mode(WIFI_STA);
    delay(500);
    applyTxPower();
    applyHostname();
    applySleep();

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        Serial.printf("[WiFi] Attempt %d/%d...\n", attempt, MAX_ATTEMPTS);

        WiFi.disconnect(true);
        delay(500);

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
                Serial.println("[WiFi] Lost - reconnecting...");
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
                applySleep();
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
                Serial.println("[WiFi] Reconnect timeout - retry...");
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

WifiMgrState getWiFiState() {
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

    applyTxPower();
    IPAddress apIP(192, 168, 4, 1);
#if defined(ESP8266)
    // ESP8266: set the AP address before starting the AP
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    bool ok = WiFi.softAP(config.ap_ssid, config.ap_pass, channel, false, 4);
#else
    bool ok = WiFi.softAP(config.ap_ssid, config.ap_pass, channel, false, 4);
    delay(100);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
#endif

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
#if defined(ESP8266)
    switch (ESP.getResetInfoPtr()->reason) {
        case REASON_DEFAULT_RST:      return "Einschalten";
        case REASON_WDT_RST:          return "Watchdog (Hardware)";
        case REASON_EXCEPTION_RST:    return "Absturz";
        case REASON_SOFT_WDT_RST:     return "Watchdog (Software)";
        case REASON_SOFT_RESTART:     return "Neustart (Software)";
        case REASON_DEEP_SLEEP_AWAKE: return "Deep Sleep";
        case REASON_EXT_SYS_RST:      return "Reset-Pin";
        default:                      return "Unbekannt";
    }
#else
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
#endif
}

const char* defaultApSsid() {
    static char ssid[32] = "";
    if (ssid[0] == '\0') {
        // Station MAC (works before WiFi is started); same address the router shows
        uint8_t mac[6] = {0};
#if defined(ESP8266)
        WiFi.macAddress(mac);
#else
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
        snprintf(ssid, sizeof(ssid), "%s-%02X%02X%02X", DEFAULT_AP_SSID, mac[3], mac[4], mac[5]);
    }
    return ssid;
}
