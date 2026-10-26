// BMS protocol decoding (no Arduino dependencies, so it can be tested on a PC).
//
// The protocol knowledge (commands, field positions, scaling) is ported from
// aiobmsble by Patrick Loschmidt (patman15), https://github.com/patman15/aiobmsble,
// licensed under the Apache License 2.0. See LICENSE in this folder.
#ifndef BMS_PROTO_H
#define BMS_PROTO_H

#include <stdint.h>
#include <stddef.h>

#ifndef BMS_MAX_CELLS
#define BMS_MAX_CELLS   16
#endif
#ifndef BMS_MAX_TEMPS
#define BMS_MAX_TEMPS   4
#endif

// Supported BMS families
enum BmsType : uint8_t {
    BMS_NONE   = 0,
    BMS_DALY   = 1,   // Daly smart BMS (BLE name "DL-...", service FFF0)
    BMS_ECTIVE = 2,   // Ective BM X / LiPower (Modbus RTU over BLE, advertises service AF30)
    BMS_BM2    = 3,   // BM2 battery monitor (voltage only, AES encrypted notifications on FFF4)
};

// Daly protocol variants
enum DalyProto : uint8_t {
    DALY_D2  = 0xD2,  // older / most common
    DALY_X81 = 0x81,  // newer modules, response header 0x51
};

// One reading. Values that the BMS does not report are NAN (or 0 for counts).
struct BmsSample {
    float voltage;        // V
    float current;        // A, positive = charging, negative = discharging
    float power;          // W
    float soc;            // %
    float charge_ah;      // remaining capacity in Ah
    float energy_wh;      // stored energy in Wh (voltage x remaining capacity)
    float temperature;    // °C, mean of all sensors
    float delta_v;        // V, highest minus lowest cell
    float runtime_s;      // remaining runtime in s while discharging, NAN otherwise
    int32_t cycles;       // charge cycles, -1 = unknown
    uint8_t cell_count;
    float cell_v[BMS_MAX_CELLS];
    uint8_t temp_count;
    float temp_c[BMS_MAX_TEMPS];
    bool problem;         // BMS reports an error / protection
    uint64_t problem_code;
    int8_t charge_fet;    // 1 on, 0 off, -1 unknown
    int8_t discharge_fet;
    int8_t bat_state;     // battery state reported by the monitor (BatState), -1 unknown
};

// Battery state of a monitor without current sensor (BM2), derived from the voltage
enum BatState : int8_t {
    BAT_STATE_UNKNOWN  = -1,
    BAT_STATE_CRITICAL = 0,
    BAT_STATE_LOW      = 1,
    BAT_STATE_NORMAL   = 2,
    BAT_STATE_CHARGING = 4,   // charging voltage (e.g. engine running)
    BAT_STATE_FLOATING = 8,   // float / maintenance charge
};
const char* batStateName(int8_t st);   // German text for UI and MQTT, nullptr if unknown

void bmsClear(BmsSample& s);

// CRC-16 MODBUS (poly 0xA001, init 0xFFFF)
uint16_t bmsCrcModbus(const uint8_t* d, size_t n);

// MODBUS "read holding registers" request with CRC (8 bytes)
size_t bmsModbusRead(uint8_t dev, uint16_t addr, uint16_t count, uint8_t out[8]);

// Expected total length of a MODBUS response that starts with `head` and function 0x03,
// 0 if the first bytes do not match (yet), -1 if the buffer does not start with a valid header.
int bmsModbusExpectedLen(const uint8_t* d, size_t n, uint8_t head);

// Complete frame with matching CRC (little endian at the end)
bool bmsModbusCrcOk(const uint8_t* d, size_t n);

// ---- Daly -------------------------------------------------------------
// Requests: D2 -> one request (addr 0, 62 registers), X81 -> two (0/64 and 0x41/62).
uint8_t dalyRequestCount(DalyProto p);
void dalyRequest(DalyProto p, uint8_t idx, uint16_t& addr, uint16_t& count);
uint8_t dalyResponseHead(DalyProto p);
// f1 = response to request 0, f2 = response to request 1 (X81 only, else nullptr)
bool dalyDecode(DalyProto p, const uint8_t* f1, size_t n1, const uint8_t* f2, size_t n2, BmsSample& s);

// ---- Ective BM X / LiPower --------------------------------------------
// Device ids tried in this order; the one that answers is kept.
extern const uint8_t ECTIVE_DEV_IDS[4];
#define ECTIVE_REG_ADDR   0x0400
#define ECTIVE_REG_COUNT  8
bool ectiveDecode(const uint8_t* f, size_t n, BmsSample& s);

// ---- BM2 battery monitor (e.g. starter battery) ----------------------
// The monitor sends a 16-byte notification about once per second on characteristic FFF4,
// AES-128 (CBC, zero IV, one block = ECB) with a key found in the vendor app.
// Protocol documented by haxrob, https://haxrob.net/bm2-reversing-the-ble-protocol-of-the-bm2-battery-monitor/
extern const uint8_t BM2_AES_KEY[16];
// plain = decrypted block; byte 0 = 0xF5, then 12 bit voltage in 10 mV, byte 3 = state of charge in %.
// The low nibble of byte 2 is a status code in some integrations, but captures disagree on its
// meaning, so the state is derived from the voltage instead (thresholds below, 12 V lead/AGM battery).
#ifndef BM2_V_CHARGING
#define BM2_V_CHARGING   13.8f   // at or above: charging (alternator / charger)
#endif
#ifndef BM2_V_FLOATING
#define BM2_V_FLOATING   13.2f   // at or above: float / maintenance charge
#endif
#ifndef BM2_V_LOW
#define BM2_V_LOW        12.2f   // at or below: low
#endif
#ifndef BM2_V_CRITICAL
#define BM2_V_CRITICAL   11.9f   // at or below: critical
#endif
bool bm2Decode(const uint8_t plain[16], BmsSample& s);
// Newer BM2 firmware also broadcasts its reading: manufacturer data of 16 bytes (company id
// included), same key; decrypted bytes 6-7 = voltage in 10 mV (big endian), byte 8 = SoC.
// Layout as in DJBenson/bm2-esphome and andystewart999/ha_bm2monitor (MIT).
bool bm2DecodeAdv(const uint8_t plain[16], BmsSample& s);

// Fills power, energy, delta, mean temperature and runtime from the raw values
void bmsFinish(BmsSample& s);

const char* bmsTypeName(uint8_t t);

#endif // BMS_PROTO_H
