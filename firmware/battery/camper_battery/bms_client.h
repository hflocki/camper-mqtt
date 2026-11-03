#ifndef BMS_CLIENT_H
#define BMS_CLIENT_H

#include <Arduino.h>
#include "config.h"

// Starts NimBLE and the BLE task that connects to and polls the configured BMS
void bmsInit();

// Apply a changed battery list / interval from the web UI (reconnects all)
void bmsRequestReconfigure();

// Scan for BMS for a while (results in bmsGetFound)
void bmsRequestScan();

// Copy of the live state of configured slot i (0 ... MAX_BMS-1)
void bmsGetLive(int i, BmsLive& out);

// Copy of the scan list, returns the number of entries
uint8_t bmsGetFound(FoundDevice* out, uint8_t max);

// Incremented after every reading of any BMS (used to publish right away)
uint32_t bmsReadSeq();

bool bmsScanning();

// Upper case "AA:BB:CC:DD:EE:FF" check
bool isValidMac(const char* mac);

void bmsPrint();

#endif // BMS_CLIENT_H
