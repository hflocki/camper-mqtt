// Truma iNet Box emulation on the LIN bus.
//
// The CP Plus stays LIN master. This device registers as an iNet Box (T23.70.0) and is polled by the CP Plus.
// All protocol work runs in a dedicated high priority FreeRTOS task, because LIN answers must be sent within a
// few milliseconds. The rest of the firmware talks to it only through `get_state()` (snapshot copy) and
// `send_command()` (queue), so WiFi/MQTT/web never block the bus.
//
// Protocol logic ported from https://github.com/skrebber/esphome-truma_inetbox (GPL-3.0).
#pragma once

#include <Arduino.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "truma_protocol.h"

namespace truma {

// Snapshot of everything the rest of the firmware may want to know.
struct TrumaState {
  bool heater_valid = false;
  StatusFrameHeater heater{};
  bool timer_valid = false;
  StatusFrameTimer timer{};
  bool clock_valid = false;
  StatusFrameClock clock{};

  // CP Plus polled the iNet Box PID (0x18) recently. Only happens after a successful INIT on the CP Plus.
  bool cpplus_connected = false;
  uint32_t last_cpplus_poll_ms = 0;
  // Any valid LIN traffic seen recently (wiring check, also works before INIT).
  bool lin_traffic = false;
  uint32_t last_lin_frame_ms = 0;
  bool lin_fault = false;

  bool init_received = false;
  Device heater_device = Device::UNKNOWN;
  char cpplus_version[16] = "";
  char heater_version[16] = "";

  bool update_pending = false;
  uint32_t ack_errors = 0;
  uint32_t frames_ok = 0;
  uint32_t crc_errors = 0;

  // Fan / ventilation nibble from PID 0x20 (CP Plus -> heater). Only valid if the heater uses this frame.
  bool vent_valid = false;
  uint8_t vent_raw = 0;

  // Incremented whenever heater/timer/clock/connection status changed. Cheap change detection for publishers.
  uint32_t change_counter = 0;
};

enum class CommandType : uint8_t {
  SET_ROOM_TEMP,  // value = °C (0 = off); keeps fan mode, defaults to ECO when switching on
  SET_FAN_MODE,   // mode = HeatingMode; OFF switches room heating off
  SET_ROOM,       // value = °C and mode = HeatingMode in one update
  SET_WATER,      // value = °C (0/40/60/200)
  SET_ENERGY_MIX, // mix = EnergyMix, level = ElectricPowerLevel
  SYNC_CLOCK,     // write system time to CP Plus
  SET_TIMER,      // start/stop = minutes of day, value = room °C, mode = HeatingMode, water = °C
  TIMER_OFF,      // disable the timer
};

// One LIN frame seen by the sniffer (any PID, any sender).
struct SniffFrame {
  uint32_t ms;
  uint8_t pid;
  uint8_t len;
  uint8_t data[8];
};

struct Command {
  CommandType type;
  float value = 0;
  HeatingMode mode = HeatingMode::MODE_OFF;
  EnergyMix mix = EnergyMix::NONE;
  ElectricPowerLevel level = ElectricPowerLevel::LEVEL_0;
  uint16_t start = 0;  // SET_TIMER: minutes of day 0..1439
  uint16_t stop = 0;   // SET_TIMER: minutes of day 0..1439
  float water = NAN;   // SET_TIMER: water °C (0/40/60/200), NAN = keep
};

class InetBox {
 public:
  bool begin(uart_port_t uart_num, int tx_pin, int rx_pin, int cs_pin, int fault_pin, bool observer_mode);

  // Thread safe copy of the current state.
  void get_state(TrumaState &out);
  // Thread safe, non-blocking. Returns false if the queue is full.
  bool send_command(const Command &cmd);

  // System time is valid (set by NTP), used for CP Plus clock sync.
  static bool system_time_valid();

  // Sniffer: report every LIN frame whose content changed (all PIDs except the iNet Box
  // diagnostic traffic 0x18/0x3C/0x3D). Used to find out where the heater ventilation level
  // or other values are transported between CP Plus and heater. Thread safe.
  void set_sniffer(bool on);
  bool sniffer() const { return sniffer_; }
  bool sniff_pop(SniffFrame &out);

 private:
  // ---- LIN listener ---------------------------------------------------------------------------
  enum ReadState : uint8_t { READ_BREAK, READ_SYNC, READ_PID, READ_DATA, READ_DONE };

  uart_port_t uart_num_ = UART_NUM_1;
  int cs_pin_ = -1;
  int fault_pin_ = -1;
  bool observer_mode_ = false;
  uint8_t vent_raw_ = 0xFF;
  uint32_t last_vent_ms_ = 0;
  volatile bool sniffer_ = false;
  volatile bool sniffer_reset_ = false;
  QueueHandle_t sniff_queue_ = nullptr;
  uint8_t sniff_last_[64][9] = {};  // [pid][0] = length + 1 (0 = not seen yet), then data
  uint32_t sniff_dropped_ = 0;
  void sniff_frame_(uint8_t len);
  QueueHandle_t uart_queue_ = nullptr;
  QueueHandle_t cmd_queue_ = nullptr;
  TaskHandle_t task_ = nullptr;

  uint32_t time_per_first_byte_us_ = 0;

  ReadState read_state_ = READ_BREAK;
  uint8_t pid_with_parity_ = 0;
  uint8_t pid_ = 0;
  bool data_valid_ = true;
  bool order_answered_ = false;
  uint8_t data_count_ = 0;
  uint8_t data_[9] = {};  // up to 8 data bytes + checksum
  uint32_t last_byte_us_ = 0;
  bool can_write_answer_ = false;
  uint8_t fault_counter_ = 0;

  static void task_entry_(void *arg);
  void task_loop_();
  void reset_read_state_();
  void on_byte_(uint8_t b);
  void finish_frame_();
  void write_answer_(const uint8_t *data, uint8_t len);
  bool check_lin_fault_();

  // ---- LIN diagnostic transport (ISO 17987 / LIN 2.x node configuration) -----------------------
  uint8_t node_address_ = 0x03;  // initial NAD, CP Plus may reassign it during INIT

  // Queue of prepared 8 byte answers for the slave diagnostic frame (0x3D).
  static constexpr uint8_t TX_QUEUE_LEN = 16;
  uint8_t tx_queue_[TX_QUEUE_LEN][8];
  uint8_t tx_head_ = 0;
  uint8_t tx_count_ = 0;
  void tx_push_(const uint8_t *msg);
  bool tx_pop_(uint8_t *msg);
  void tx_clear_() { tx_head_ = tx_count_ = 0; }

  uint16_t multi_expected_ = 0;
  uint8_t multi_len_ = 0;
  uint8_t multi_counter_ = 0;
  uint8_t multi_[64];

  bool answer_order_(uint8_t pid);
  void on_master_message_(uint8_t pid, const uint8_t *msg, uint8_t len);
  void diag_single_(const uint8_t *msg, uint8_t len);
  void diag_first_(const uint8_t *msg);
  bool diag_consecutive_(const uint8_t *msg);
  void diag_multi_();
  bool matches_identifier_(const uint8_t *msg) const;
  bool read_by_identifier_(uint8_t identifier, uint8_t *response5) const;

  // ---- iNet Box application ------------------------------------------------------------------
  Company company_ = Company::TRUMA;
  Device heater_device_ = Device::UNKNOWN;
  uint8_t message_counter_ = 1;
  uint32_t init_requested_us_ = 0;
  uint32_t init_received_us_ = 0;
  uint32_t update_time_us_ = 0;

  // Heater status and pending update
  bool heater_valid_ = false;
  StatusFrameHeater heater_{};
  StatusFrameHeaterResponse heater_update_{};
  bool heater_update_prepared_ = false;     // update_ copied from status, being modified
  bool heater_update_unsubmitted_ = false;  // waiting for CP Plus to fetch it
  bool heater_update_stale_ = false;        // sent, waiting for a fresh status frame

  bool timer_valid_ = false;
  StatusFrameTimer timer_{};
  StatusFrameTimerResponse timer_update_{};
  bool timer_update_prepared_ = false;
  bool timer_update_unsubmitted_ = false;
  bool timer_update_stale_ = false;
  bool clock_valid_ = false;
  StatusFrameClock clock_{};
  bool clock_update_unsubmitted_ = false;
  uint32_t last_clock_sync_ms_ = 0;
  bool clock_synced_once_ = false;

  uint32_t last_cpplus_poll_ms_ = 0;
  uint32_t last_lin_frame_ms_ = 0;
  uint32_t ack_errors_ = 0;
  uint32_t frames_ok_ = 0;
  uint32_t crc_errors_ = 0;
  char cpplus_version_[16] = "";
  char heater_version_[16] = "";

  // Shared snapshot
  portMUX_TYPE state_mux_ = portMUX_INITIALIZER_UNLOCKED;
  TrumaState state_{};
  bool state_dirty_ = true;
  void publish_state_(bool changed);

  void reset_device_();
  bool has_update_to_submit_();
  const uint8_t *on_multiframe_(const uint8_t *msg, uint8_t len, uint8_t *return_len);
  void frame_create_empty_(StatusFrame *f, uint8_t type, uint8_t length);
  void frame_finalize_(StatusFrame *f);

  bool heater_can_update_() const { return heater_valid_ && heater_device_ != Device::UNKNOWN; }
  StatusFrameHeaterResponse *heater_prepare_();
  void heater_submit_() { heater_update_unsubmitted_ = true; }
  void heater_create_update_(StatusFrame *f, uint8_t *len);
  void clock_create_update_(StatusFrame *f, uint8_t *len);

  bool timer_can_update_() const { return timer_valid_ && heater_device_ != Device::UNKNOWN; }
  StatusFrameTimerResponse *timer_prepare_();
  void timer_create_update_(StatusFrame *f, uint8_t *len);
  bool cmd_timer_(uint16_t start, uint16_t stop, float room_temp, HeatingMode mode, float water_temp);
  bool cmd_timer_off_();

  void process_command_(const Command &cmd);
  bool cmd_room_(float temp, HeatingMode mode);
  bool cmd_fan_mode_(HeatingMode mode);
  bool cmd_water_(float temp);
  bool cmd_energy_mix_(EnergyMix mix, ElectricPowerLevel level);
  bool cmd_sync_clock_();
  void periodic_();
};

}  // namespace truma
