// Truma iNet Box protocol definitions (enums, frame layouts, helpers).
// Ported from https://github.com/skrebber/esphome-truma_inetbox (fork of Fabian-Schmidt/esphome-truma_inetbox),
// stripped of all ESPHome dependencies. Aircon frames are kept only so that frame lengths validate correctly.
#pragma once

#include <Arduino.h>
#include <math.h>

namespace truma {

// ---------------------------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------------------------

enum class HeatingMode : uint16_t {
  // Prefixed because Arduino defines HIGH/LOW as macros
  MODE_OFF = 0x0,
  MODE_ECO = 0x1,    // Combi
  MODE_NIGHT = 0x2,  // Vario Heat
  MODE_AUTO = 0x3,   // Vario Heat
  MODE_HIGH = 0xA,   // Combi
  MODE_BOOST = 0xB,  // Combi gas, Vario Heat (not available on diesel)
};

enum class ElectricPowerLevel : uint16_t {
  LEVEL_0 = 0,
  LEVEL_900 = 900,
  LEVEL_1800 = 1800,
};

// Temperatures are transferred in 1/10 Kelvin.
enum class TargetTemp : uint16_t {
  OFF = 0x0,
  WATER_ECO = (40 + 273) * 10,
  WATER_HIGH = (60 + 273) * 10,
  WATER_BOOST = (200 + 273) * 10,
  ROOM_MIN = (5 + 273) * 10,
  ROOM_MAX = (30 + 273) * 10,
};

enum class EnergyMix : uint8_t {
  NONE = 0b00,
  GAS = 0b01,  // also diesel
  ELECTRICITY = 0b10,
  MIX = 0b11,
};

enum class OperatingStatus : uint8_t {
  OFF = 0x0,
  WARNING = 0x1,
  START_OR_COOL_DOWN = 0x4,
  ON_5 = 0x5,
  ON_6 = 0x6,
  ON_7 = 0x7,
  ON_8 = 0x8,
  ON_9 = 0x9,
};

enum class ResponseAckResult : uint8_t {
  OKAY = 0x0,
  ERROR_INVALID_MSG = 0x2,
  ERROR_INVALID_ID = 0x3,
};

enum class ClockMode : uint8_t { H24 = 0x0, H12 = 0x1 };
enum class ClockSource : uint8_t { MANUAL = 0x1, PROG = 0x2 };
enum class TimerActive : uint8_t { OFF = 0x0, ON = 0x1 };

enum class Company : uint8_t { UNKNOWN = 0x00, TRUMA = 0x1E, ALDE = 0x1A };

enum class Device : uint8_t {
  UNKNOWN = 0x00,
  AIRCON = 0x01,
  HEATER_COMBI4 = 0x02,
  HEATER_VARIO = 0x03,
  CPPLUS_COMBI = 0x04,
  CPPLUS_VARIO = 0x05,  // also old CP6 heater
  HEATER_COMBI6D = 0x06,
};

// ---------------------------------------------------------------------------------------------
// Frame layouts
// ---------------------------------------------------------------------------------------------

#define LIN_SID_RESPONSE 0x40
#define LIN_SID_READ_STATE_BUFFER 0xBA
#define LIN_SID_FILL_STATE_BUFFER 0xBB

#define STATUS_FRAME_RESPONSE_INIT_REQUEST 0x0A
#define STATUS_FRAME_DEVICES 0x0B
#define STATUS_FRAME_RESPONSE_ACK 0x0D
#define STATUS_FRAME_CLOCK 0x15
#define STATUS_FRAME_CLOCK_RESPONSE (STATUS_FRAME_CLOCK - 1)
#define STATUS_FRAME_CONFIG 0x17
#define STATUS_FRAME_HEATER 0x33
#define STATUS_FRAME_HEATER_RESPONSE (STATUS_FRAME_HEATER - 1)
#define STATUS_FRAME_AIRCON_MANUAL 0x35
#define STATUS_FRAME_AIRCON_AUTO 0x37
#define STATUS_FRAME_TIMER 0x3D
#define STATUS_FRAME_TIMER_RESPONSE (STATUS_FRAME_TIMER - 1)
#define STATUS_FRAME_AIRCON_MANUAL_INIT 0x3F  // actually the aircon timer status (community captures)
#define STATUS_FRAME_AIRCON_AUTO_INIT 0x41

struct StatusFrameHeader {
  uint8_t service_identifier;
  uint8_t header[10];
  uint8_t header_2;  // 'T'
  uint8_t header_3;  // 0x01
  uint8_t message_length;
  uint8_t message_type;
  uint8_t command_counter;
  uint8_t checksum;
} __attribute__((packed));

// Length 20 (0x14)
struct StatusFrameHeater {
  TargetTemp target_temp_room;
  HeatingMode heating_mode;
  ElectricPowerLevel el_power_level_a;
  TargetTemp target_temp_water;
  ElectricPowerLevel el_power_level_b;
  EnergyMix energy_mix_a;
  EnergyMix energy_mix_b;
  uint16_t current_temp_water;
  uint16_t current_temp_room;
  OperatingStatus operating_status;
  uint8_t error_code_low;
  uint8_t error_code_high;
  uint8_t heater_unknown_2;
} __attribute__((packed));

// Length 12 (0x0C)
struct StatusFrameHeaterResponse {
  TargetTemp target_temp_room;
  HeatingMode heating_mode;
  ElectricPowerLevel el_power_level_a;
  TargetTemp target_temp_water;
  ElectricPowerLevel el_power_level_b;
  EnergyMix energy_mix_a;
  EnergyMix energy_mix_b;
} __attribute__((packed));

// Length 24 (0x18)
struct StatusFrameTimer {
  TargetTemp timer_target_temp_room;
  HeatingMode timer_heating_mode;
  ElectricPowerLevel timer_el_power_level_a;
  TargetTemp timer_target_temp_water;
  ElectricPowerLevel timer_el_power_level_b;
  EnergyMix timer_energy_mix_a;
  EnergyMix timer_energy_mix_b;
  uint8_t unused[5];
  uint8_t timer_unknown_3;
  uint8_t timer_unknown_4;
  TimerActive timer_active;
  uint8_t timer_start_minutes;
  uint8_t timer_start_hours;
  uint8_t timer_stop_minutes;
  uint8_t timer_stop_hours;
} __attribute__((packed));

// Length 2 (0x02)
struct StatusFrameResponseAck {
  ResponseAckResult error_code;
  uint8_t unknown;
} __attribute__((packed));

// Length 10 (0x0A)
struct StatusFrameClock {
  uint8_t clock_hour;
  uint8_t clock_minute;
  uint8_t clock_second;
  uint8_t display_1;  // must be 0x1
  uint8_t display_2;  // must be 0x1
  uint8_t display_3;
  ClockMode clock_mode;
  ClockSource clock_source;
  uint8_t display_4;
  uint8_t display_5;
} __attribute__((packed));

// Length 10 (0x0A)
struct StatusFrameConfig {
  uint8_t display_brightness;
  uint8_t language;
  TargetTemp ac_offset;
  TargetTemp temp_offset;
  uint8_t temp_units;
  uint8_t unknown_6;
  uint8_t unknown_7;
  uint8_t unknown_8;
} __attribute__((packed));

// Length 12 (0x0C)
struct StatusFrameDevice {
  uint8_t device_count;
  uint8_t device_id;
  uint8_t state;
  uint8_t unknown_1;
  uint16_t hardware_revision_major;
  uint8_t hardware_revision_minor;
  uint8_t software_revision[3];  // [0] = Device type
  uint8_t unknown_2;
  uint8_t unknown_3;
} __attribute__((packed));

// Aircon frames are only needed for their size.
struct StatusFrameAirconManual { uint8_t raw[18]; } __attribute__((packed));
struct StatusFrameAirconManualInit { uint8_t raw[22]; } __attribute__((packed));
struct StatusFrameAirconAuto { uint8_t raw[18]; } __attribute__((packed));
struct StatusFrameAirconAutoInit { uint8_t raw[20]; } __attribute__((packed));

// Length 18 (0x12). Sent to the CP Plus to set or disable the timer.
// Layout and length verified against an original iNet Box (Truma app, community capture):
// the 17 bytes known from esphome-truma_inetbox plus one trailing zero byte.
struct StatusFrameTimerResponse {
  TargetTemp timer_target_temp_room;
  HeatingMode timer_heating_mode;
  ElectricPowerLevel timer_el_power_level_a;
  TargetTemp timer_target_temp_water;
  ElectricPowerLevel timer_el_power_level_b;
  EnergyMix timer_energy_mix_a;
  EnergyMix timer_energy_mix_b;
  TimerActive timer_resp_active;
  uint8_t timer_resp_start_minutes;
  uint8_t timer_resp_start_hours;
  uint8_t timer_resp_stop_minutes;
  uint8_t timer_resp_stop_hours;
  uint8_t timer_resp_unknown;  // always 0x00 in the original iNet Box frames
} __attribute__((packed));

union StatusFrame {
  uint8_t raw[41];
  struct {
    StatusFrameHeader genericHeader;
    union {
      StatusFrameHeater heater;
      StatusFrameHeaterResponse heaterResponse;
      StatusFrameTimer timer;
      StatusFrameTimerResponse timerResponse;
      StatusFrameResponseAck responseAck;
      StatusFrameClock clock;
      StatusFrameConfig config;
      StatusFrameDevice device;
      StatusFrameAirconManual airconManual;
      StatusFrameAirconManualInit airconManualInit;
      StatusFrameAirconAuto airconAuto;
      StatusFrameAirconAutoInit airconAutoInit;
    } __attribute__((packed));
  };
} __attribute__((packed));

static_assert(sizeof(StatusFrameHeader) == 17, "header size");
static_assert(sizeof(StatusFrameHeater) == 20, "heater frame size");
static_assert(sizeof(StatusFrameHeaterResponse) == 12, "heater response size");
static_assert(sizeof(StatusFrameTimer) == 24, "timer frame size");
static_assert(sizeof(StatusFrameTimerResponse) == 18, "timer response size");
static_assert(sizeof(StatusFrameClock) == 10, "clock frame size");
static_assert(sizeof(StatusFrameDevice) == 12, "device frame size");
static_assert(sizeof(StatusFrame) == 41, "status frame size");

// First byte is the service identifier and ignored. The last three bytes may be 0xFF or 0x00.
static const uint8_t TRUMA_MESSAGE_HEADER[11] = {0x00, 0x00, 0x1F, 0x00, 0x1E, 0x00, 0x00, 0x22, 0xFF, 0xFF, 0xFF};
static const uint8_t ALDE_MESSAGE_HEADER[11] = {0x00, 0x00, 0x1F, 0x00, 0x1A, 0x00, 0x00, 0x22, 0xFF, 0xFF, 0xFF};

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

// LIN protected identifier parity bits (P0, P1) for a 6 bit frame id.
inline uint8_t addr_parity(uint8_t pid) {
  uint8_t p0 = ((pid >> 0) + (pid >> 1) + (pid >> 2) + (pid >> 4)) & 1;
  uint8_t p1 = ~((pid >> 1) + (pid >> 3) + (pid >> 4) + (pid >> 5)) & 1;
  return (p0 | (p1 << 1));
}

// sum = 0: classic LIN 1.x checksum, sum = protected id: enhanced LIN 2.x checksum.
inline uint8_t data_checksum(const uint8_t *message, uint8_t length, uint16_t sum) {
  for (uint8_t i = 0; i < length; i++) {
    sum += message[i];
    if (sum >= 256) sum -= 255;
  }
  return (uint8_t) (~sum);
}

inline float temp_code_to_decimal(uint16_t val, float zero = NAN) {
  if (val == 0) return zero;
  return ((float) val) / 10.0f - 273.0f;
}
inline float temp_code_to_decimal(TargetTemp val, float zero = NAN) {
  return temp_code_to_decimal((uint16_t) val, zero);
}

inline TargetTemp decimal_to_temp(float val) { return (TargetTemp) (uint16_t) lroundf((val + 273.0f) * 10.0f); }

inline TargetTemp decimal_to_room_temp(float val) {
  if (isnan(val) || val < 5) return TargetTemp::OFF;
  if (val >= 30) return TargetTemp::ROOM_MAX;
  return decimal_to_temp(val);
}

inline TargetTemp decimal_to_water_temp(float val) {
  if (isnan(val) || val < 40) return TargetTemp::OFF;
  if (val < 60) return TargetTemp::WATER_ECO;
  if (val < 80) return TargetTemp::WATER_HIGH;
  return TargetTemp::WATER_BOOST;
}

inline ElectricPowerLevel decimal_to_el_power_level(uint16_t val) {
  if (val >= 1800) return ElectricPowerLevel::LEVEL_1800;
  if (val >= 900) return ElectricPowerLevel::LEVEL_900;
  return ElectricPowerLevel::LEVEL_0;
}

inline const char *heating_mode_to_str(HeatingMode m) {
  switch (m) {
    case HeatingMode::MODE_ECO: return "eco";
    case HeatingMode::MODE_HIGH: return "high";
    case HeatingMode::MODE_BOOST: return "boost";
    case HeatingMode::MODE_NIGHT: return "night";
    case HeatingMode::MODE_AUTO: return "auto";
    default: return "off";
  }
}

inline const char *water_mode_to_str(TargetTemp t) {
  switch (t) {
    case TargetTemp::WATER_ECO: return "eco";
    case TargetTemp::WATER_HIGH: return "high";
    case TargetTemp::WATER_BOOST: return "boost";
    default: return "off";
  }
}

inline const char *energy_mix_to_str(EnergyMix e) {
  switch (e) {
    case EnergyMix::GAS: return "gas";
    case EnergyMix::ELECTRICITY: return "electricity";
    case EnergyMix::MIX: return "mix";
    default: return "none";
  }
}

// Fan / ventilation nibble of the CP Plus -> heater frame PID 0x20 (byte 5, high nibble), as used by
// inetbox2mqtt and esphome-truma_inetbox (VENT_MODE). Only some heaters use PID 0x20 (seen on Combi 4 and
// a Combi gas heater; the Combi D6 E in the WomoLIN captures uses PIDs 0x03-0x07 instead).
// 0 = off, 1..10 = ventilation level, 0x0B = eco, 0x0D = high (heating).
inline int vent_level_from_raw(uint8_t raw) { return (raw >= 1 && raw <= 10) ? raw : 0; }
inline const char *vent_mode_to_str(uint8_t raw) {
  static const char *levels[] = {"off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
  if (raw <= 10) return levels[raw];
  if (raw == 0x0B) return "eco";
  if (raw == 0x0D) return "high";
  return "unknown";
}

inline const char *operating_status_to_str(OperatingStatus s) {
  switch (s) {
    case OperatingStatus::OFF: return "off";
    case OperatingStatus::WARNING: return "warning";
    case OperatingStatus::START_OR_COOL_DOWN: return "start/cool down";
    default: return "on";
  }
}

}  // namespace truma
