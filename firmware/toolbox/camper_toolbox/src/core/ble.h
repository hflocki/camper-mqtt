#ifndef CT_BLE_H
#define CT_BLE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../../config.h"

// Shared Bluetooth LE: one NimBLE stack, one passive scan for all plugins.
// Every advertisement is handed to the enabled plugins (Plugin::onAdvertisement) and
// remembered in a "nearby" list for picking device addresses in the web UI.

// Default BLE security (bonding, no forced MITM, secure connections). BleLink switches
// MITM on only while pairing with a PIN and restores these values afterwards.
#define BLE_SEC_BOND true
#define BLE_SEC_MITM false
#define BLE_SEC_SC   true

void bleInit();            // Only called when an enabled plugin uses BLE
void bleLoop();
bool bleActive();

// NimBLE cannot scan and initiate a connection at the same time: connecting plugins
// pause the scan while they connect (counted, every pause needs a resume)
void bleScanPause();
void bleScanResume();

// Only one connection attempt at a time (owner = any unique pointer)
bool bleConnectLock(const void* owner);
void bleConnectUnlock(const void* owner);

// Address type (public / random) as seen in the scan; false if the device was not seen yet
bool bleFindAddrType(const char* mac, uint8_t* type);
// Last RSSI of an advertisement of this device (0 = not seen)
int bleLastRssi(const char* mac);

uint32_t bleAdvCount();
bool bleScanning();

// Nearby devices for the web UI (address, name, rssi, company ID, UUID, matching plugins)
void bleFillSeenList(JsonArray arr);
void blePrintSeenList();

#endif // CT_BLE_H
