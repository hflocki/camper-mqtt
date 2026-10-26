// BMS protocol decoding. Protocol knowledge ported from aiobmsble (Apache-2.0),
// https://github.com/patman15/aiobmsble - see bms_proto.h and LICENSE.
#include "bms_proto.h"
#include <math.h>
#include <string.h>

const uint8_t ECTIVE_DEV_IDS[4] = {0x22, 0x0B, 0x08, 0x38};

static uint32_t be(const uint8_t* d, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) v = (v << 8) | d[i];
    return v;
}

void bmsClear(BmsSample& s) {
    memset(&s, 0, sizeof(s));
    s.voltage = s.current = s.power = s.soc = s.charge_ah = s.energy_wh = NAN;
    s.temperature = s.delta_v = s.runtime_s = NAN;
    s.cycles = -1;
    s.charge_fet = s.discharge_fet = -1;
}

uint16_t bmsCrcModbus(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

size_t bmsModbusRead(uint8_t dev, uint16_t addr, uint16_t count, uint8_t out[8]) {
    out[0] = dev;
    out[1] = 0x03;
    out[2] = addr >> 8;
    out[3] = addr & 0xFF;
    out[4] = count >> 8;
    out[5] = count & 0xFF;
    uint16_t crc = bmsCrcModbus(out, 6);
    out[6] = crc & 0xFF;
    out[7] = crc >> 8;
    return 8;
}

int bmsModbusExpectedLen(const uint8_t* d, size_t n, uint8_t head) {
    if (n == 0) return 0;
    if (d[0] != head) return -1;
    if (n < 2) return 0;
    if (d[1] != 0x03) return -1;
    if (n < 3) return 0;
    return d[2] + 5;   // head, function, length, data, 2 bytes CRC
}

bool bmsModbusCrcOk(const uint8_t* d, size_t n) {
    if (n < 5 || (size_t)(d[2] + 5) != n) return false;
    uint16_t crc = bmsCrcModbus(d, n - 2);
    return d[n - 2] == (crc & 0xFF) && d[n - 1] == (crc >> 8);
}

// ============================================================
// Daly
// ============================================================

uint8_t dalyRequestCount(DalyProto p) { return p == DALY_X81 ? 2 : 1; }

void dalyRequest(DalyProto p, uint8_t idx, uint16_t& addr, uint16_t& count) {
    if (p == DALY_X81) {
        addr = idx == 0 ? 0x00 : 0x41;
        count = idx == 0 ? 64 : 62;
    } else {
        addr = 0x00;
        count = 62;
    }
}

uint8_t dalyResponseHead(DalyProto p) { return p == DALY_X81 ? 0x51 : 0xD2; }

// Data positions are relative to the first data byte (after head, function and length)
static void dalyCells(const uint8_t* data, size_t len, uint8_t count, BmsSample& s) {
    s.cell_count = 0;
    for (uint8_t i = 0; i < count && s.cell_count < BMS_MAX_CELLS; i++) {
        size_t p = i * 2;
        if (p + 2 > len) break;
        uint16_t mv = be(data + p, 2);
        if (mv == 0) continue;   // unused cell
        s.cell_v[s.cell_count++] = mv / 1000.0f;
    }
}

static void dalyTemps(const uint8_t* data, size_t len, size_t start, uint8_t count, BmsSample& s) {
    s.temp_count = 0;
    for (uint8_t i = 0; i < count && s.temp_count < BMS_MAX_TEMPS; i++) {
        size_t p = start + i * 2;
        if (p + 2 > len) break;
        uint16_t raw = be(data + p, 2);
        if (raw == 0 || raw >= 0xFF) continue;   // sensor missing
        s.temp_c[s.temp_count++] = (int)raw - 40;
    }
}

bool dalyDecode(DalyProto p, const uint8_t* f1, size_t n1, const uint8_t* f2, size_t n2, BmsSample& s) {
    bmsClear(s);
    const uint8_t head = dalyResponseHead(p);
    if (!f1 || n1 < 5 || f1[0] != head || !bmsModbusCrcOk(f1, n1)) return false;
    const uint8_t* d1 = f1 + 3;
    const size_t l1 = n1 - 5;

    if (p == DALY_D2) {
        if (l1 < 124) return false;                      // 62 registers
        s.voltage   = be(d1 + 80, 2) / 10.0f;
        s.current   = ((int)be(d1 + 82, 2) - 30000) / 10.0f;
        s.soc       = be(d1 + 84, 2) / 10.0f;
        s.charge_ah = be(d1 + 96, 2) / 10.0f;
        uint8_t cells = be(d1 + 98, 2) > 32 ? 32 : be(d1 + 98, 2);
        uint8_t temps = be(d1 + 100, 2) > 8 ? 8 : be(d1 + 100, 2);
        s.cycles    = be(d1 + 102, 2);
        s.charge_fet    = be(d1 + 106, 2) ? 1 : 0;
        s.discharge_fet = be(d1 + 108, 2) ? 1 : 0;
        s.problem_code = 0;
        for (int i = 0; i < 8; i++) s.problem_code = (s.problem_code << 8) | d1[116 + i];
        dalyCells(d1, l1, cells, s);
        dalyTemps(d1, l1, 32 * 2, temps, s);
    } else {
        if (l1 < 128 || !f2 || n2 < 5 || f2[0] != head || !bmsModbusCrcOk(f2, n2)) return false;
        const uint8_t* d2 = f2 + 3;
        const size_t l2 = n2 - 5;
        if (l2 < 124) return false;
        s.voltage   = be(d1 + 112, 2) / 10.0f;
        s.current   = ((int)be(d1 + 114, 2) - 30000) / 10.0f;
        s.soc       = be(d1 + 116, 2) / 10.0f;
        uint8_t cells = be(d1 + 120, 2) > 48 ? 48 : be(d1 + 120, 2);
        uint8_t temps = be(d1 + 122, 2) > 8 ? 8 : be(d1 + 122, 2);
        s.charge_ah = be(d2 + 20, 2) / 10.0f;
        s.cycles    = be(d2 + 22, 2);
        s.charge_fet    = be(d2 + 34, 2) ? 1 : 0;
        s.discharge_fet = be(d2 + 36, 2) ? 1 : 0;
        s.problem_code  = be(d2 + 88, 4);
        dalyCells(d1, l1, cells, s);
        dalyTemps(d1, l1, 48 * 2, temps, s);
    }
    s.problem = s.problem_code != 0;
    bmsFinish(s);
    return true;
}

// ============================================================
// Ective BM X / LiPower (registers 0x0400 ... 0x0407)
// Positions below are in the whole frame (head, function, length come first).
// ============================================================

bool ectiveDecode(const uint8_t* f, size_t n, BmsSample& s) {
    bmsClear(s);
    if (n < 17 || f[1] != 0x03 || !bmsModbusCrcOk(f, n)) return false;
    bool known = false;
    for (uint8_t id : ECTIVE_DEV_IDS) known |= (f[0] == id);
    if (!known) return false;

    s.charge_ah = be(f + 3, 2);                            // Ah
    s.soc       = be(f + 5, 2);                            // %
    uint32_t rt = be(f + 7, 4);                            // hours (high word), minutes (low word)
    float runtime = (rt >> 16) * 3600.0f + (rt & 0xFFFF) * 60.0f;
    // Byte 12 = direction (1 = discharging), bytes 13-14 = current in 10 mA
    float amps = be(f + 13, 2) / 100.0f;
    s.current   = (f[12] & 0x01) ? -amps : amps;
    s.voltage   = be(f + 15, 2) / 10.0f;
    bmsFinish(s);
    // The monitor reports its own runtime estimate; use it while discharging
    if (s.current < 0 && runtime > 0) s.runtime_s = runtime;
    return true;
}

// ============================================================
// BM2
// ============================================================

const uint8_t BM2_AES_KEY[16] = {0x6C, 0x65, 0x61, 0x67, 0x65, 0x6E, 0x64, 0xFF,
                                 0xFE, 0x31, 0x38, 0x38, 0x32, 0x34, 0x36, 0x36};  // "leagend" FF FE "1882466"

bool bm2Decode(const uint8_t p[16], BmsSample& s) {
    bmsClear(s);
    if (p[0] != 0xF5) return false;
    uint16_t raw = ((uint16_t)p[1] << 4) | (p[2] >> 4);   // 12 bit, 10 mV steps
    if (raw == 0 || raw > 2000) return false;             // 0 ... 20 V plausible
    s.voltage = raw / 100.0f;
    s.soc = p[3] > 100 ? 100 : p[3];
    bmsFinish(s);
    return true;
}

// ============================================================
// Derived values
// ============================================================

void bmsFinish(BmsSample& s) {
    if (!isnan(s.voltage) && !isnan(s.current)) s.power = s.voltage * s.current;
    if (!isnan(s.voltage) && !isnan(s.charge_ah)) s.energy_wh = s.voltage * s.charge_ah;

    if (s.cell_count > 1) {
        float lo = s.cell_v[0], hi = s.cell_v[0];
        for (uint8_t i = 1; i < s.cell_count; i++) {
            if (s.cell_v[i] < lo) lo = s.cell_v[i];
            if (s.cell_v[i] > hi) hi = s.cell_v[i];
        }
        s.delta_v = hi - lo;
    }
    if (s.temp_count > 0) {
        float sum = 0;
        for (uint8_t i = 0; i < s.temp_count; i++) sum += s.temp_c[i];
        s.temperature = sum / s.temp_count;
    }
    // Runtime from remaining capacity while discharging
    if (!isnan(s.current) && s.current < -0.01f && !isnan(s.charge_ah) && s.charge_ah > 0) {
        s.runtime_s = s.charge_ah / -s.current * 3600.0f;
    }
}

const char* bmsTypeName(uint8_t t) {
    switch (t) {
        case BMS_DALY:   return "Daly";
        case BMS_ECTIVE: return "Ective BM X";
        case BMS_BM2:    return "BM2";
        default:         return "-";
    }
}
