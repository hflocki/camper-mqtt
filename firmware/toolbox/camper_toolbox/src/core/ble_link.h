#ifndef CT_BLE_LINK_H
#define CT_BLE_LINK_H

#include "../../config.h"

#if CT_BLE_LINKS

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ============================================================
// BleLink: one persistent GATT connection to a BLE device
// ------------------------------------------------------------
// Shared by all connecting plugins (pump, roof hatch, fridge):
//   - non-blocking connect with back-off while the device is out of range
//   - pauses the shared scan while connecting (NimBLE cannot do both)
//   - optional PIN / passkey pairing (bonding is stored by NimBLE)
//   - subscribes to one characteristic, or to every notifying one
//   - received notifications are queued for loop() (receive())
//   - "release for the app": stays disconnected so a phone app can connect
// All methods must be called from the loop task.
// ============================================================

#define BLE_PACKET_MAX 128
#define BLE_NO_PIN     (-1)
// Own reason codes (below 0x100, NimBLE codes start at 0x100)
#define BLE_REASON_SILENT     1   // Connected, but the device sent nothing
#define BLE_REASON_PAIRING    2   // Pairing failed, no detailed code from the stack
#define BLE_REASON_NEED_PIN   3   // Subscriptions refused: device needs pairing
#define BLE_REASON_NO_SERVICE 4   // Expected service / characteristic missing

// Short German text for a NimBLE connect / disconnect reason (HCI or host error code)
const char* bleReasonText(int reason);

// Parses a PIN from the web UI: 1-6 digits. Returns BLE_NO_PIN for an empty or invalid
// string. "000000" is a valid PIN (value 0) and must not be mistaken for "no PIN".
int32_t blePinParse(const char* s);

struct BlePacket {
    uint16_t len;
    uint8_t data[BLE_PACKET_MAX];
};

class BleLink : public NimBLEClientCallbacks {
public:
    // tag: log prefix. Empty notify UUID = subscribe to every characteristic that can notify
    // (in the given service, or in all services if service is empty). Empty write UUID = read only.
    void begin(const char* tag, const char* service, const char* notifyUuid, const char* writeUuid);
    void setUuids(const char* service, const char* notifyUuid, const char* writeUuid);

    // Device address ("" = none) and optional PIN (BLE_NO_PIN = no pairing, 0 = PIN "000000").
    // Reconnects when changed; a changed PIN also deletes the old bond.
    void setTarget(const char* mac, int32_t pin = BLE_NO_PIN);

    // Connected but nothing received for that long: drop the link and reconnect (0 = never)
    void setSilenceTimeout(uint32_t ms) { silenceMs = ms; }

    void loop();

    bool connected() const { return linkUp; }
    bool connecting() const { return isConnecting; }
    bool hasTarget() const { return mac[0] != '\0'; }
    const char* state() const { return stateText; }
    int rssi() const { return lastRssiValue; }
    uint16_t mtu() const { return mtuValue; }
    unsigned long lastReceive() const { return lastRx; }   // millis() of the last notification
    uint8_t subscriptions() const { return subscribed; }
    int lastError() const { return lastReason; }   // Last connect / disconnect reason, 0 = none
    const char* lastErrorText() const { return lastReason ? bleReasonText(lastReason) : ""; }

    bool write(const uint8_t* d, size_t n, bool response);
    bool write(const char* s, bool response) { return write((const uint8_t*)s, strlen(s), response); }
    bool canWrite() const { return linkUp && writeChr != nullptr; }

    bool receive(BlePacket& p);   // Next received notification, false if none

    void setReleased(bool r);
    bool released() const { return isReleased; }

    void disconnect();

    uint32_t changes = 0;   // Incremented on every state change (plugins publish then)

    // NimBLE callbacks (host task)
    void onConnect(NimBLEClient* c) override;
    void onConnectFail(NimBLEClient* c, int reason) override;
    void onDisconnect(NimBLEClient* c, int reason) override;
    void onPassKeyEntry(NimBLEConnInfo& info) override;
    void onConfirmPasskey(NimBLEConnInfo& info, uint32_t pin) override;
    void onAuthenticationComplete(NimBLEConnInfo& info) override;

private:
    const char* tag = "LINK";
    char service[40] = "";
    char notifyUuid[40] = "";
    char writeUuid[40] = "";
    char mac[18] = "";
    int32_t pin = BLE_NO_PIN;
    uint32_t silenceMs = 0;

    NimBLEClient* client = nullptr;
    NimBLERemoteCharacteristic* writeChr = nullptr;
    QueueHandle_t queue = nullptr;

    volatile int connectResult = 0;   // 0 = pending, 1 = connected, -1 = failed
    volatile bool linkLost = false;
    volatile int lastReason = 0;      // Reason of the last failed connect / disconnect
    volatile int pendingReason = 0;   // Set in the host task, logged from loop()
    volatile bool authDone = false;   // Pairing / encryption result waiting to be logged
    volatile bool authEncrypted = false, authAuthenticated = false, authBonded = false;
    uint8_t rxLogged = 0;             // Packets logged as hex since the last connect
    bool isConnecting = false;
    bool linkUp = false;
    bool isReleased = false;
    uint8_t tryType = 0;              // Address type to try if the scan has not seen the device
    uint8_t subscribed = 0;
    uint8_t subscribeFailed = 0;     // Subscriptions refused in the last attempt
    unsigned long connectStarted = 0;
    unsigned long lastAttempt = 0;
    unsigned long retryDelay = 0;
    unsigned long releasedAt = 0;
    unsigned long lastRx = 0;
    unsigned long lastRssiPoll = 0;
    int lastRssiValue = 0;
    uint16_t mtuValue = 0;
    char stateText[24] = "Kein Gerät";

    void setState(const char* s);
    bool startConnect();
    bool finishConnect();
    void connectFailed();
    void markDisconnected();
    bool subscribeChr(NimBLERemoteCharacteristic* c);
    void forgetBond();
    void dumpGatt();
    void push(const uint8_t* d, size_t n);
};

#endif // CT_BLE_LINKS
#endif // CT_BLE_LINK_H
