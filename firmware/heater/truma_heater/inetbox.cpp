#include "inetbox.h"

#include <driver/gpio.h>
#include <time.h>

#include "config.h"
#include "logger.h"

namespace truma {

static const char *const TAG = "lin";

/* LIN frame:
    _______ __________ _________ ____________ __________
   | Break | Sync 0x55 | PID byte | Data bytes | Checksum |

   Break: >= 13 dominant bits. The ESP32 UART sees it as a 0x00 byte (framing error).
   PID: 6 bit frame id + 2 parity bits.
   Checksum: inverted sum with carry. Classic (data only) for diagnostic frames 0x3C/0x3D,
   enhanced (data + PID) otherwise.
*/

#define LIN_BREAK 0x00
#define LIN_SYNC 0x55
#define PID_DIAG_MASTER 0x3C
#define PID_DIAG_SLAVE 0x3D
#define PID_TRUMA_INET_BOX 0x18

#define LIN_NAD_BROADCAST 0x7F
#define LIN_SID_ASSIGN_NAD 0xB0
#define LIN_SID_READ_BY_IDENTIFIER 0xB2
#define LIN_SID_HEARTBEAT 0xB9

static const uint8_t LIN_EMPTY_RESPONSE[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Supplier id 0x4617 (Truma), function id 0x1F00 = iNet Box (shown as T23.70.0 in the CP Plus INDEX menu).
static const uint8_t LIN_IDENTIFIER[4] = {0x17, 0x46, 0x00, 0x1F};

// ===============================================================================================
// Setup / task
// ===============================================================================================

bool InetBox::begin(uart_port_t uart_num, int tx_pin, int rx_pin, int cs_pin, int fault_pin, bool observer_mode) {
  uart_num_ = uart_num;
  cs_pin_ = cs_pin;
  fault_pin_ = fault_pin;
  observer_mode_ = observer_mode;

  const float us_per_bit = 1000000.0f / LIN_BAUD;
  // Allow up to 5 byte times of silence inside a frame (response space of a slave).
  time_per_first_byte_us_ = (uint32_t) (us_per_bit * 11 * 5);

  if (cs_pin_ >= 0) {
    // Keep the transceiver awake in observer mode too: with SLP_N/EN low a TJA1020/1021 sleeps and
    // RXD no longer follows the bus. Observer mode only suppresses our answers (see finish_frame_).
    pinMode(cs_pin_, OUTPUT);
    digitalWrite(cs_pin_, HIGH);
  }
  if (fault_pin_ >= 0) {
    pinMode(fault_pin_, INPUT_PULLUP);
  }

  uart_config_t cfg = {};
  cfg.baud_rate = LIN_BAUD;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_2;  // same as the proven ESPHome setup
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;

  esp_err_t err = uart_driver_install(uart_num_, 512, 0, 32, &uart_queue_, 0);
  if (err != ESP_OK) {
    LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
    return false;
  }
  uart_param_config(uart_num_, &cfg);
  uart_set_pin(uart_num_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  // TJA1020/1021 RXD is open drain: make sure the line is pulled up even if the module has no resistor.
  gpio_pullup_en((gpio_num_t) rx_pin);
  // Wake up on every received byte; LIN answers must start within a few ms after the PID.
  uart_set_rx_full_threshold(uart_num_, 1);
  uart_set_rx_timeout(uart_num_, 2);

  cmd_queue_ = xQueueCreate(8, sizeof(Command));

  // Dual core: core 1 next to the Arduino loop, but with a much higher priority (WiFi runs on core 0).
#if CONFIG_FREERTOS_UNICORE
  const BaseType_t core = 0;
#else
  const BaseType_t core = 1;
#endif
  BaseType_t ok = xTaskCreatePinnedToCore(task_entry_, "lin", 6144, this, configMAX_PRIORITIES - 4, &task_, core);
  if (ok != pdPASS) {
    LOGE(TAG, "Cannot create LIN task");
    return false;
  }
  LOGI(TAG, "LIN slave started (UART%d TX=%d RX=%d%s)", (int) uart_num_, tx_pin, rx_pin,
       observer_mode_ ? ", observer mode" : "");
  return true;
}

void InetBox::task_entry_(void *arg) { static_cast<InetBox *>(arg)->task_loop_(); }

void InetBox::task_loop_() {
  uart_event_t event;
  uint8_t buf[64];
  uint32_t last_periodic = 0;

  for (;;) {
    if (xQueueReceive(uart_queue_, &event, pdMS_TO_TICKS(10)) == pdTRUE) {
      switch (event.type) {
        case UART_DATA: {
          if (check_lin_fault_()) break;
          size_t avail = 0;
          uart_get_buffered_data_len(uart_num_, &avail);
          while (avail > 0) {
            int n = uart_read_bytes(uart_num_, buf, avail > sizeof(buf) ? sizeof(buf) : avail, 0);
            if (n <= 0) break;
            for (int i = 0; i < n; i++) {
              on_byte_(buf[i]);
              last_byte_us_ = micros();
            }
            uart_get_buffered_data_len(uart_num_, &avail);
          }
          break;
        }
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
          LOGW(TAG, "UART overflow, flushing");
          uart_flush_input(uart_num_);
          xQueueReset(uart_queue_);
          reset_read_state_();
          break;
        default:
          // UART_BREAK / UART_FRAME_ERR are expected on every LIN break; the 0x00 byte is handled by the parser.
          break;
      }
    }

    uint32_t now = millis();
    if (now - last_periodic >= 20) {
      last_periodic = now;
      periodic_();
    }
  }
}

bool InetBox::check_lin_fault_() {
  if (fault_pin_ < 0) return false;
  if (digitalRead(fault_pin_) == LOW) {
    if (fault_counter_ < 0xFF) fault_counter_++;
    if (fault_counter_ == 4) LOGE(TAG, "Fault on LIN bus detected");
  } else {
    if (fault_counter_ > 3) LOGI(TAG, "Fault on LIN bus fixed");
    fault_counter_ = 0;
  }
  if (fault_counter_ > 3) {
    reset_read_state_();
    uart_flush_input(uart_num_);
    return true;
  }
  return false;
}

// ===============================================================================================
// LIN frame parser (runs in LIN task)
// ===============================================================================================

void InetBox::reset_read_state_() {
  read_state_ = READ_BREAK;
  pid_with_parity_ = 0;
  pid_ = 0;
  data_valid_ = true;
  order_answered_ = false;
  data_count_ = 0;
  memset(data_, 0, sizeof(data_));
}

void InetBox::on_byte_(uint8_t b) {
  switch (read_state_) {
    case READ_BREAK:
    case READ_DONE:
      reset_read_state_();
      // Depending on timing we either see the break as 0x00 or only the following sync byte.
      if (b == LIN_BREAK) {
        read_state_ = READ_SYNC;
      } else if (b == LIN_SYNC) {
        read_state_ = READ_PID;
      }
      break;

    case READ_SYNC:
      if (b == LIN_SYNC) {
        read_state_ = READ_PID;
      } else if (b != LIN_BREAK) {
        read_state_ = READ_BREAK;
      }
      break;

    case READ_PID:
      pid_with_parity_ = b;
      pid_ = b & 0x3F;
      if (pid_with_parity_ != (pid_ | (addr_parity(pid_) << 6))) {
        data_valid_ = false;
        crc_errors_++;
      }
      if (data_valid_) {
        can_write_answer_ = true;
        answer_order_(pid_);
        can_write_answer_ = false;
      }
      read_state_ = READ_DATA;
      break;

    case READ_DATA:
      if ((micros() - last_byte_us_) > time_per_first_byte_us_) {
        // Nobody answered this header. The byte belongs to the next frame.
        read_state_ = READ_BREAK;
        on_byte_(b);
        return;
      }
      data_[data_count_++] = b;
      if (data_count_ >= sizeof(data_)) {
        read_state_ = READ_DONE;
        finish_frame_();
      }
      break;
  }
}

void InetBox::finish_frame_() {
  const uint8_t len = data_count_ - 1;
  const uint8_t crc = data_[data_count_ - 1];
  bool from_master = true;

  if (pid_ == PID_DIAG_MASTER || pid_ == PID_DIAG_SLAVE) {
    if (crc != data_checksum(data_, len, 0)) data_valid_ = false;
    from_master = (pid_ == PID_DIAG_MASTER);
  } else {
    // Same trick as upstream: master frames match with the plain id, slave answers with the protected id.
    uint8_t crc_master = data_checksum(data_, len, pid_);
    uint8_t crc_slave = data_checksum(data_, len, pid_with_parity_);
    if (crc != crc_master && crc != crc_slave) data_valid_ = false;
    if (crc == crc_slave) from_master = false;
  }

  if (!data_valid_) {
    crc_errors_++;
    return;
  }
  frames_ok_++;
  last_lin_frame_ms_ = millis();
  if (last_lin_frame_ms_ == 0) last_lin_frame_ms_ = 1;

  if (sniffer_) sniff_frame_(len);

  // CP Plus -> heater command frame (only on some heaters): ventilation level in byte 5, high nibble.
  // The sender cannot be told apart by checksum here (protected id 0x20 == plain id), so no from_master check.
  if (pid_ == 0x20 && len >= 6) {
    const uint8_t v = data_[5] >> 4;
    if (v != vent_raw_) {
      if (vent_raw_ == 0xFF) LOGI(TAG, "Heater frame 0x20 seen, ventilation level available");
      vent_raw_ = v;
      state_dirty_ = true;
    }
    last_vent_ms_ = millis();
    if (last_vent_ms_ == 0) last_vent_ms_ = 1;
  }

  if (from_master) {
    on_master_message_(pid_, data_, len);
  }
}

// ---- Sniffer ------------------------------------------------------------------------------------

void InetBox::set_sniffer(bool on) {
  if (on && sniff_queue_ == nullptr) sniff_queue_ = xQueueCreate(32, sizeof(SniffFrame));
  sniffer_reset_ = true;  // report every PID once after (re)enabling
  sniffer_ = on && sniff_queue_ != nullptr;
  LOGI(TAG, "Sniffer %s", sniffer_ ? "on" : "off");
}

void InetBox::sniff_frame_(uint8_t len) {
  if (pid_ == PID_TRUMA_INET_BOX || pid_ == PID_DIAG_MASTER || pid_ == PID_DIAG_SLAVE) return;
  if (sniffer_reset_) {
    memset(sniff_last_, 0, sizeof(sniff_last_));
    sniffer_reset_ = false;
  }
  if (len > 8) len = 8;
  uint8_t *last = sniff_last_[pid_ & 0x3F];
  if (last[0] == len + 1 && memcmp(&last[1], data_, len) == 0) return;  // unchanged
  last[0] = len + 1;
  memcpy(&last[1], data_, len);

  SniffFrame f;
  f.ms = millis();
  f.pid = pid_;
  f.len = len;
  memset(f.data, 0, sizeof(f.data));
  memcpy(f.data, data_, len);
  if (xQueueSend(sniff_queue_, &f, 0) != pdTRUE) sniff_dropped_++;
}

bool InetBox::sniff_pop(SniffFrame &out) {
  if (sniff_queue_ == nullptr) return false;
  return xQueueReceive(sniff_queue_, &out, 0) == pdTRUE;
}

void InetBox::write_answer_(const uint8_t *data, uint8_t len) {
  if (!can_write_answer_ || len > 8) return;
  can_write_answer_ = false;

  uint8_t out[9];
  memcpy(out, data, len);
  out[len] = (pid_ == PID_DIAG_SLAVE) ? data_checksum(data, len, 0) : data_checksum(data, len, pid_with_parity_);

  if (!observer_mode_) {
    order_answered_ = true;
    uart_write_bytes(uart_num_, (const char *) out, len + 1);
  }
}

// ===============================================================================================
// LIN diagnostic transport
// ===============================================================================================

void InetBox::tx_push_(const uint8_t *msg) {
  if (tx_count_ >= TX_QUEUE_LEN) {
    LOGW(TAG, "Diagnostic TX queue full");
    return;
  }
  uint8_t idx = (tx_head_ + tx_count_) % TX_QUEUE_LEN;
  memcpy(tx_queue_[idx], msg, 8);
  tx_count_++;
}

bool InetBox::tx_pop_(uint8_t *msg) {
  if (tx_count_ == 0) return false;
  memcpy(msg, tx_queue_[tx_head_], 8);
  tx_head_ = (tx_head_ + 1) % TX_QUEUE_LEN;
  tx_count_--;
  return true;
}

bool InetBox::answer_order_(uint8_t pid) {
  if (pid == PID_TRUMA_INET_BOX) {
    // "Alive" poll of the CP Plus. 0xFE = nothing to tell, 0xFF = please read my state buffer.
    last_cpplus_poll_ms_ = millis();
    if (last_cpplus_poll_ms_ == 0) last_cpplus_poll_ms_ = 1;
    uint8_t response[8];
    memcpy(response, LIN_EMPTY_RESPONSE, 8);
    if (tx_count_ == 0 && !has_update_to_submit_()) {
      response[0] = 0xFE;
    }
    write_answer_(response, 8);
    return true;
  }
  if (pid == PID_DIAG_SLAVE) {
    uint8_t msg[8];
    if (tx_pop_(msg)) {
      write_answer_(msg, 8);
      return true;
    }
  }
  return false;
}

void InetBox::on_master_message_(uint8_t pid, const uint8_t *msg, uint8_t len) {
  if (pid != PID_DIAG_MASTER) return;

  if (msg[0] != node_address_ && msg[0] != LIN_NAD_BROADCAST) return;

  const uint8_t pci = msg[1];
  if ((pci & 0xF0) == 0x00) {
    multi_expected_ = 0;
    multi_len_ = 0;
    multi_counter_ = 0;
    diag_single_(msg, len);
  } else if ((pci & 0xF0) == 0x10) {
    diag_first_(msg);
  } else if ((pci & 0xF0) == 0x20) {
    if (diag_consecutive_(msg)) diag_multi_();
  }
}

bool InetBox::matches_identifier_(const uint8_t *msg) const { return memcmp(msg, LIN_IDENTIFIER, 4) == 0; }

bool InetBox::read_by_identifier_(uint8_t identifier, uint8_t *response5) const {
  if (identifier == 0x00) {  // LIN product identification
    memcpy(response5, LIN_IDENTIFIER, 4);
    response5[4] = 0x01;  // variant
    return true;
  }
  if (identifier == 0x20) {  // product details shown in CP Plus
    memcpy(response5, LIN_IDENTIFIER, 3);
    return true;
  }
  if (identifier == 0x22) {  // unknown, INIT fails without an answer
    return true;
  }
  return false;
}

void InetBox::diag_single_(const uint8_t *msg, uint8_t len) {
  const bool my_address = msg[0] == node_address_;
  const bool broadcast = msg[0] == LIN_NAD_BROADCAST;
  const uint8_t length = msg[1];
  const uint8_t sid = msg[2];
  if (length > 6) return;

  uint8_t response[8];
  memcpy(response, LIN_EMPTY_RESPONSE, 8);

  if (sid == LIN_SID_READ_BY_IDENTIFIER && length == 6) {
    if (!matches_identifier_(&msg[4])) return;
    response[0] = node_address_;
    uint8_t id_response[5] = {0, 0, 0, 0, 0};
    if (read_by_identifier_(msg[3], id_response)) {
      response[1] = 6;
      response[2] = LIN_SID_READ_BY_IDENTIFIER | LIN_SID_RESPONSE;
      memcpy(&response[3], id_response, 5);
    } else {
      // Negative response: service not supported for this identifier
      response[1] = 3;
      response[2] = 0x7F;
      response[3] = LIN_SID_READ_BY_IDENTIFIER;
      response[4] = 0x12;
    }
    tx_push_(response);
  } else if (my_address && sid == LIN_SID_HEARTBEAT && length >= 5) {
    response[0] = node_address_;
    response[1] = 2;
    response[2] = LIN_SID_HEARTBEAT | LIN_SID_RESPONSE;
    response[3] = 0x00;
    tx_push_(response);
  } else if (broadcast && sid == LIN_SID_ASSIGN_NAD && length == 6) {
    if (!matches_identifier_(&msg[3])) return;
    // Answer with the old address, then switch.
    response[0] = node_address_;
    response[1] = 1;
    response[2] = LIN_SID_ASSIGN_NAD | LIN_SID_RESPONSE;
    tx_push_(response);
    node_address_ = msg[7];
    LOGI(TAG, "CP Plus assigned node address 0x%02X", node_address_);
  } else if (my_address) {
    LOGD(TAG, "Unhandled diagnostic SID 0x%02X", sid);
  }
}

void InetBox::diag_first_(const uint8_t *msg) {
  const uint16_t length = ((uint16_t) (msg[1] & 0x0F) << 8) + msg[2];
  if (length < 7 || length > sizeof(multi_)) {
    LOGW(TAG, "Invalid multi frame length %u", length);
    multi_expected_ = 0;
    return;
  }
  memset(multi_, 0, sizeof(multi_));
  multi_expected_ = length;
  multi_len_ = 0;
  multi_counter_ = 1;
  for (uint8_t i = 3; i < 8; i++) multi_[multi_len_++] = msg[i];
}

bool InetBox::diag_consecutive_(const uint8_t *msg) {
  if (multi_len_ >= multi_expected_) return false;
  if ((msg[1] & 0x0F) != multi_counter_) return false;
  multi_counter_ = (multi_counter_ + 1) & 0x0F;
  for (uint8_t i = 2; i < 8; i++) {
    if (multi_len_ < multi_expected_) multi_[multi_len_++] = msg[i];
  }
  return multi_len_ == multi_expected_;
}

void InetBox::diag_multi_() {
  uint8_t answer_len = 0;
  const uint8_t *answer = on_multiframe_(multi_, multi_len_, &answer_len);
  if (answer == nullptr || answer_len == 0) return;

  uint8_t response[8];
  memcpy(response, LIN_EMPTY_RESPONSE, 8);
  if (answer_len <= 6) {
    response[0] = node_address_;
    response[1] = answer_len;
    response[2] = answer[0] | LIN_SID_RESPONSE;
    for (uint8_t i = 1; i < answer_len; i++) response[i + 2] = answer[i];
    tx_push_(response);
    return;
  }

  // First frame
  response[0] = node_address_;
  response[1] = 0x10 | ((answer_len >> 8) & 0x0F);
  response[2] = answer_len & 0xFF;
  response[3] = answer[0] | LIN_SID_RESPONSE;
  for (uint8_t i = 1; i < 5; i++) response[i + 3] = answer[i];
  tx_push_(response);

  // Consecutive frames, 6 bytes each
  uint16_t pos = 5;
  uint8_t counter = 0;
  while (pos < answer_len) {
    memcpy(response, LIN_EMPTY_RESPONSE, 8);
    response[0] = node_address_;
    response[1] = ((counter + 1) & 0x0F) | 0x20;
    for (uint8_t i = 0; i < 6 && pos < answer_len; i++) response[i + 2] = answer[pos++];
    tx_push_(response);
    counter++;
  }
}

// ===============================================================================================
// iNet Box application
// ===============================================================================================

void InetBox::reset_device_() {
  tx_clear_();
  init_received_us_ = 0;
  update_time_us_ = 0;
  heater_valid_ = false;
  heater_update_prepared_ = heater_update_unsubmitted_ = heater_update_stale_ = false;
  timer_valid_ = false;
  timer_update_prepared_ = timer_update_unsubmitted_ = timer_update_stale_ = false;
  clock_valid_ = false;
  clock_update_unsubmitted_ = false;
  state_dirty_ = true;
}

bool InetBox::has_update_to_submit_() {
  // Called from the PID 0x18 answer path: keep it fast, no logging.
  if (init_requested_us_ == 0) {
    init_requested_us_ = micros();
    return true;
  }
  if (init_received_us_ == 0) {
    if (micros() - init_requested_us_ > 5UL * 1000 * 1000) {
      init_requested_us_ = micros();
      return true;
    }
    return false;
  }
  if (heater_update_unsubmitted_ || timer_update_unsubmitted_ || clock_update_unsubmitted_) {
    if (update_time_us_ == 0) {
      update_time_us_ = micros();
      return true;
    }
    if (micros() - update_time_us_ > 5UL * 1000 * 1000) {
      update_time_us_ = micros();
      return true;
    }
  }
  return false;
}

void InetBox::frame_create_empty_(StatusFrame *f, uint8_t type, uint8_t length) {
  memset(f, 0, sizeof(StatusFrame));
  f->genericHeader.service_identifier = LIN_SID_READ_STATE_BUFFER | LIN_SID_RESPONSE;
  for (size_t i = 1; i < sizeof(TRUMA_MESSAGE_HEADER); i++) f->raw[i] = TRUMA_MESSAGE_HEADER[i];
  f->genericHeader.header_2 = 'T';
  f->genericHeader.header_3 = 0x01;
  f->genericHeader.message_type = type;
  f->genericHeader.message_length = length;
  f->genericHeader.command_counter = message_counter_++;
}

void InetBox::frame_finalize_(StatusFrame *f) {
  f->genericHeader.checksum = 0;
  f->genericHeader.checksum = data_checksum(&f->raw[10], sizeof(StatusFrame) - 10, 0);
}

const uint8_t *InetBox::on_multiframe_(const uint8_t *msg, uint8_t len, uint8_t *return_len) {
  static uint8_t response[48];
  *return_len = 0;

  if (len < sizeof(TRUMA_MESSAGE_HEADER)) return nullptr;
  for (uint8_t i = 1; i < sizeof(TRUMA_MESSAGE_HEADER) - 3; i++) {
    if (msg[i] != TRUMA_MESSAGE_HEADER[i] && msg[i] != ALDE_MESSAGE_HEADER[i]) return nullptr;
  }
  if (msg[4] != (uint8_t) company_) {
    company_ = (Company) msg[4];
    LOGI(TAG, "Company switched to 0x%02X", msg[4]);
  }

  // ---- CP Plus asks for our state buffer --------------------------------------------------------
  if (msg[0] == LIN_SID_READ_STATE_BUFFER) {
    memset(response, 0, sizeof(response));
    auto *frame = reinterpret_cast<StatusFrame *>(response);

    if (init_received_us_ == 0) {
      LOGD(TAG, "CP Plus reads state: sending init request");
      frame_create_empty_(frame, STATUS_FRAME_RESPONSE_INIT_REQUEST, 0);
      frame_finalize_(frame);
      *return_len = sizeof(StatusFrameHeader);
      return response;
    }
    if (heater_update_unsubmitted_) {
      LOGI(TAG, "CP Plus reads state: sending heater update");
      heater_create_update_(frame, return_len);
      update_time_us_ = 0;
      return response;
    }
    if (timer_update_unsubmitted_) {
      LOGI(TAG, "CP Plus reads state: sending timer update");
      timer_create_update_(frame, return_len);
      update_time_us_ = 0;
      return response;
    }
    if (clock_update_unsubmitted_) {
      LOGI(TAG, "CP Plus reads state: sending clock update");
      clock_create_update_(frame, return_len);
      update_time_us_ = 0;
      return response;
    }
    LOGD(TAG, "CP Plus reads state, but nothing to send");
    return nullptr;
  }

  // ---- CP Plus fills our state buffer (status frames) ------------------------------------------
  if (msg[0] != LIN_SID_FILL_STATE_BUFFER || len < sizeof(StatusFrameHeader)) return nullptr;

  // `multi_` is zero padded up to sizeof(StatusFrame), so short frames checksum correctly as well.
  auto *sf = reinterpret_cast<const StatusFrame *>(msg);
  auto *hdr = &sf->genericHeader;
  if (len < sizeof(StatusFrameHeader) + hdr->message_length) return nullptr;
  if (hdr->checksum != data_checksum(&sf->raw[10], sizeof(StatusFrame) - 10, 0xFF - hdr->checksum) ||
      hdr->header_2 != 'T' || hdr->header_3 != 0x01) {
    LOGW(TAG, "Truma frame checksum fail");
    return nullptr;
  }

  // Acknowledge
  response[0] = hdr->service_identifier | LIN_SID_RESPONSE;
  *return_len = 1;

  const uint8_t type = hdr->message_type;
  const uint8_t mlen = hdr->message_length;

  if (type == STATUS_FRAME_HEATER && mlen == sizeof(StatusFrameHeater)) {
    if (!heater_valid_ || memcmp(&heater_, &sf->heater, sizeof(heater_)) != 0) state_dirty_ = true;
    heater_ = sf->heater;
    heater_valid_ = true;
    heater_update_stale_ = false;
    LOGD(TAG, "Heater status received");
  } else if (type == STATUS_FRAME_TIMER && mlen == sizeof(StatusFrameTimer)) {
    if (!timer_valid_ || memcmp(&timer_, &sf->timer, sizeof(timer_)) != 0) state_dirty_ = true;
    timer_ = sf->timer;
    timer_valid_ = true;
    timer_update_stale_ = false;
    LOGD(TAG, "Timer status received (%s %02u:%02u-%02u:%02u)", sf->timer.timer_active == TimerActive::ON ? "on" : "off",
         sf->timer.timer_start_hours, sf->timer.timer_start_minutes, sf->timer.timer_stop_hours,
         sf->timer.timer_stop_minutes);
  } else if (type == STATUS_FRAME_CLOCK && mlen == sizeof(StatusFrameClock)) {
    clock_ = sf->clock;
    clock_valid_ = true;
    state_dirty_ = true;
  } else if (type == STATUS_FRAME_CONFIG && mlen == sizeof(StatusFrameConfig)) {
    // display settings, not used
  } else if (type == STATUS_FRAME_RESPONSE_ACK && mlen == sizeof(StatusFrameResponseAck)) {
    if (sf->responseAck.error_code != ResponseAckResult::OKAY) {
      ack_errors_++;
      LOGW(TAG, "CP Plus rejected update (code %u), re-reading state", (unsigned) sf->responseAck.error_code);
      // Read current state again and drop pending updates.
      reset_device_();
    } else {
      LOGI(TAG, "CP Plus accepted update");
    }
  } else if (type == STATUS_FRAME_DEVICES && mlen == sizeof(StatusFrameDevice)) {
    const auto &dev = sf->device;
    // state: 1 = online, 0 = offline (e.g. a 230 V aircon without shore power, see tests/README.md)
    LOGI(TAG, "Device %u/%u: %u.%02u.%02u hw %04X.%02X %s", dev.device_id + 1, dev.device_count,
         dev.software_revision[0], dev.software_revision[1], dev.software_revision[2], dev.hardware_revision_major,
         dev.hardware_revision_minor, dev.state ? "online" : "offline");
    if (dev.device_id == 0) {
      snprintf(cpplus_version_, sizeof(cpplus_version_), "C%u.%02u.%02u", dev.software_revision[0],
               dev.software_revision[1], dev.software_revision[2]);
    } else if (dev.device_id == 1) {
      heater_device_ = (Device) dev.software_revision[0];
      snprintf(heater_version_, sizeof(heater_version_), "H%u.%02u.%02u", dev.software_revision[0],
               dev.software_revision[1], dev.software_revision[2]);
    }
    // The last device of the list completes the init sequence.
    if (dev.device_id + 1 == dev.device_count && heater_device_ != Device::UNKNOWN) {
      init_received_us_ = micros();
      if (init_received_us_ == 0) init_received_us_ = 1;
      LOGI(TAG, "Init data received from CP Plus");
    }
    state_dirty_ = true;
  } else if (type == STATUS_FRAME_AIRCON_MANUAL || type == STATUS_FRAME_AIRCON_AUTO ||
             type == STATUS_FRAME_AIRCON_MANUAL_INIT || type == STATUS_FRAME_AIRCON_AUTO_INIT) {
    // Aircon not supported, just acknowledge.
  } else {
    LOGW(TAG, "Unknown status frame type 0x%02X len %u", type, mlen);
    *return_len = 0;
    return nullptr;
  }
  return response;
}

// ---- Heater -----------------------------------------------------------------------------------

StatusFrameHeaterResponse *InetBox::heater_prepare_() {
  // An update is already being built or waiting for confirmation: keep modifying that one.
  if (heater_update_prepared_ || heater_update_stale_) return &heater_update_;

  heater_update_ = {};
  heater_update_.target_temp_room = heater_.target_temp_room;
  heater_update_.heating_mode = heater_.heating_mode;
  heater_update_.el_power_level_a = heater_.el_power_level_a;
  heater_update_.target_temp_water = heater_.target_temp_water;
  heater_update_.el_power_level_b = heater_.el_power_level_b;
  heater_update_.energy_mix_a = heater_.energy_mix_a;
  heater_update_.energy_mix_b = heater_.energy_mix_b;
  heater_update_prepared_ = true;
  return &heater_update_;
}

void InetBox::heater_create_update_(StatusFrame *f, uint8_t *len) {
  frame_create_empty_(f, STATUS_FRAME_HEATER_RESPONSE, sizeof(StatusFrameHeaterResponse));
  f->heaterResponse.target_temp_room = heater_update_.target_temp_room;
  f->heaterResponse.heating_mode = heater_update_.heating_mode;
  f->heaterResponse.target_temp_water = heater_update_.target_temp_water;
  f->heaterResponse.energy_mix_a = heater_update_.energy_mix_a;
  f->heaterResponse.energy_mix_b = heater_update_.energy_mix_a;
  f->heaterResponse.el_power_level_a = heater_update_.el_power_level_a;
  f->heaterResponse.el_power_level_b = heater_update_.el_power_level_a;
  frame_finalize_(f);
  *len = sizeof(StatusFrameHeader) + sizeof(StatusFrameHeaterResponse);

  heater_update_prepared_ = false;
  heater_update_unsubmitted_ = false;
  heater_update_stale_ = true;
  state_dirty_ = true;
}

bool InetBox::cmd_room_(float temperature, HeatingMode mode) {
  if (!heater_can_update_()) {
    LOGW(TAG, "Cannot update heater yet (no status from CP Plus)");
    return false;
  }
  auto *h = heater_prepare_();
  h->target_temp_room = decimal_to_room_temp(temperature);

  if (h->target_temp_room == TargetTemp::OFF) {
    h->heating_mode = HeatingMode::MODE_OFF;
  } else if (heater_device_ == Device::HEATER_VARIO) {
    if (mode == HeatingMode::MODE_NIGHT || mode == HeatingMode::MODE_AUTO || mode == HeatingMode::MODE_BOOST) {
      h->heating_mode = mode;
    } else if (h->heating_mode == HeatingMode::MODE_OFF) {
      h->heating_mode = HeatingMode::MODE_AUTO;
    }
  } else {
    // Combi
    if (mode == HeatingMode::MODE_ECO || mode == HeatingMode::MODE_HIGH || mode == HeatingMode::MODE_BOOST) {
      h->heating_mode = mode;
    } else if (h->heating_mode == HeatingMode::MODE_OFF) {
      h->heating_mode = HeatingMode::MODE_ECO;
    }
  }
  if (h->energy_mix_a == EnergyMix::NONE) h->energy_mix_a = EnergyMix::GAS;

  heater_submit_();
  return true;
}

bool InetBox::cmd_fan_mode_(HeatingMode mode) {
  if (!heater_can_update_()) {
    LOGW(TAG, "Cannot update heater yet (no status from CP Plus)");
    return false;
  }
  if (mode == HeatingMode::MODE_OFF) return cmd_room_(0, HeatingMode::MODE_OFF);

  // Use the temperature of a pending update if there is one, so "set temp" + "set fan" in a row both stick.
  TargetTemp base = (heater_update_prepared_ || heater_update_stale_) ? heater_update_.target_temp_room
                                                                        : heater_.target_temp_room;
  float temp = temp_code_to_decimal(base, 0);
  if (temp < 5) temp = 5;
  return cmd_room_(temp, mode);
}

bool InetBox::cmd_water_(float temperature) {
  if (!heater_can_update_()) {
    LOGW(TAG, "Cannot update heater yet (no status from CP Plus)");
    return false;
  }
  auto *h = heater_prepare_();
  h->target_temp_water = decimal_to_water_temp(temperature);
  if (h->target_temp_water != TargetTemp::OFF && h->energy_mix_a == EnergyMix::NONE) h->energy_mix_a = EnergyMix::GAS;
  heater_submit_();
  return true;
}

bool InetBox::cmd_energy_mix_(EnergyMix mix, ElectricPowerLevel level) {
  if (!heater_can_update_()) {
    LOGW(TAG, "Cannot update heater yet (no status from CP Plus)");
    return false;
  }
  auto *h = heater_prepare_();
  h->el_power_level_a = level;
  if (mix == EnergyMix::GAS) {
    h->energy_mix_a = mix;
    h->el_power_level_a = ElectricPowerLevel::LEVEL_0;
  } else if (mix == EnergyMix::MIX || mix == EnergyMix::ELECTRICITY) {
    h->energy_mix_a = mix;
    if (h->el_power_level_a == ElectricPowerLevel::LEVEL_0) h->el_power_level_a = ElectricPowerLevel::LEVEL_900;
  }
  if (h->el_power_level_a != ElectricPowerLevel::LEVEL_0) {
    if (h->energy_mix_a != EnergyMix::MIX && h->energy_mix_a != EnergyMix::ELECTRICITY) h->energy_mix_a = EnergyMix::MIX;
  } else {
    h->energy_mix_a = EnergyMix::GAS;
  }
  heater_submit_();
  return true;
}

// ---- Timer ------------------------------------------------------------------------------------
// Ported from TrumaiNetBoxAppTimer (Fabian-Schmidt / havanti esphome-truma_inetbox, GPL-3.0).

StatusFrameTimerResponse *InetBox::timer_prepare_() {
  if (timer_update_prepared_ || timer_update_stale_) return &timer_update_;

  timer_update_ = {};
  timer_update_.timer_target_temp_room = timer_.timer_target_temp_room;
  timer_update_.timer_heating_mode = timer_.timer_heating_mode;
  timer_update_.timer_el_power_level_a = timer_.timer_el_power_level_a;
  timer_update_.timer_target_temp_water = timer_.timer_target_temp_water;
  timer_update_.timer_el_power_level_b = timer_.timer_el_power_level_b;
  timer_update_.timer_energy_mix_a = timer_.timer_energy_mix_a;
  timer_update_.timer_energy_mix_b = timer_.timer_energy_mix_b;
  timer_update_.timer_resp_active = timer_.timer_active;
  timer_update_.timer_resp_start_minutes = timer_.timer_start_minutes;
  timer_update_.timer_resp_start_hours = timer_.timer_start_hours;
  timer_update_.timer_resp_stop_minutes = timer_.timer_stop_minutes;
  timer_update_.timer_resp_stop_hours = timer_.timer_stop_hours;
  timer_update_prepared_ = true;
  return &timer_update_;
}

void InetBox::timer_create_update_(StatusFrame *f, uint8_t *len) {
  frame_create_empty_(f, STATUS_FRAME_TIMER_RESPONSE, sizeof(StatusFrameTimerResponse));
  f->timerResponse = timer_update_;
  // Same as for the heater update: the B fields mirror the A fields.
  f->timerResponse.timer_energy_mix_b = timer_update_.timer_energy_mix_a;
  f->timerResponse.timer_el_power_level_b = timer_update_.timer_el_power_level_a;
  frame_finalize_(f);
  *len = sizeof(StatusFrameHeader) + sizeof(StatusFrameTimerResponse);

  timer_update_prepared_ = false;
  timer_update_unsubmitted_ = false;
  timer_update_stale_ = true;
  state_dirty_ = true;
}

bool InetBox::cmd_timer_(uint16_t start, uint16_t stop, float room_temp, HeatingMode mode, float water_temp) {
  if (!timer_can_update_()) {
    LOGW(TAG, "Cannot set timer yet (no timer status from CP Plus)");
    return false;
  }
  if (start >= 24 * 60 || stop >= 24 * 60) {
    LOGW(TAG, "Timer: invalid start/stop");
    return false;
  }
  auto *t = timer_prepare_();
  t->timer_resp_active = TimerActive::ON;
  t->timer_resp_start_hours = start / 60;
  t->timer_resp_start_minutes = start % 60;
  t->timer_resp_stop_hours = stop / 60;
  t->timer_resp_stop_minutes = stop % 60;
  t->timer_target_temp_room = decimal_to_room_temp(room_temp);

  if (t->timer_target_temp_room == TargetTemp::OFF) {
    t->timer_heating_mode = HeatingMode::MODE_OFF;
  } else if (heater_device_ == Device::HEATER_VARIO) {
    if (mode == HeatingMode::MODE_NIGHT || mode == HeatingMode::MODE_AUTO || mode == HeatingMode::MODE_BOOST) {
      t->timer_heating_mode = mode;
    } else if (t->timer_heating_mode == HeatingMode::MODE_OFF) {
      t->timer_heating_mode = HeatingMode::MODE_AUTO;
    }
  } else {
    // Combi
    if (mode == HeatingMode::MODE_ECO || mode == HeatingMode::MODE_HIGH || mode == HeatingMode::MODE_BOOST) {
      t->timer_heating_mode = mode;
    } else if (t->timer_heating_mode == HeatingMode::MODE_OFF) {
      t->timer_heating_mode = HeatingMode::MODE_ECO;
    }
  }

  if (!isnan(water_temp)) t->timer_target_temp_water = decimal_to_water_temp(water_temp);
  if (t->timer_energy_mix_a == EnergyMix::NONE) t->timer_energy_mix_a = EnergyMix::GAS;
  if (t->timer_energy_mix_a == EnergyMix::GAS) t->timer_el_power_level_a = ElectricPowerLevel::LEVEL_0;

  timer_update_unsubmitted_ = true;
  LOGI(TAG, "Timer set %02u:%02u-%02u:%02u", start / 60, start % 60, stop / 60, stop % 60);
  return true;
}

bool InetBox::cmd_timer_off_() {
  if (!timer_can_update_()) {
    LOGW(TAG, "Cannot change timer yet (no timer status from CP Plus)");
    return false;
  }
  auto *t = timer_prepare_();
  t->timer_resp_active = TimerActive::OFF;
  timer_update_unsubmitted_ = true;
  LOGI(TAG, "Timer off");
  return true;
}

// ---- Clock ------------------------------------------------------------------------------------

bool InetBox::system_time_valid() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return t.tm_year + 1900 >= 2025;
}

bool InetBox::cmd_sync_clock_() {
  if (!clock_valid_) {
    LOGW(TAG, "Clock sync: no clock status from CP Plus yet");
    return false;
  }
  if (!system_time_valid()) {
    LOGW(TAG, "Clock sync: system time not valid (NTP)");
    return false;
  }
  // The frame is built with the current time when the CP Plus fetches it.
  clock_update_unsubmitted_ = true;
  last_clock_sync_ms_ = millis();
  clock_synced_once_ = true;
  return true;
}

void InetBox::clock_create_update_(StatusFrame *f, uint8_t *len) {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  frame_create_empty_(f, STATUS_FRAME_CLOCK_RESPONSE, sizeof(StatusFrameClock));
  f->clock.clock_hour = t.tm_hour;
  f->clock.clock_minute = t.tm_min;
  f->clock.clock_second = t.tm_sec;
  f->clock.display_1 = 0x1;
  f->clock.display_2 = 0x1;
  f->clock.clock_mode = clock_.clock_mode;
  frame_finalize_(f);
  *len = sizeof(StatusFrameHeader) + sizeof(StatusFrameClock);
  clock_update_unsubmitted_ = false;
}

// ---- Commands / periodic --------------------------------------------------------------------

bool InetBox::send_command(const Command &cmd) {
  if (cmd_queue_ == nullptr) return false;
  return xQueueSend(cmd_queue_, &cmd, 0) == pdTRUE;
}

void InetBox::process_command_(const Command &cmd) {
  bool ok = false;
  switch (cmd.type) {
    case CommandType::SET_ROOM_TEMP: ok = cmd_room_(cmd.value, HeatingMode::MODE_OFF); break;
    case CommandType::SET_FAN_MODE: ok = cmd_fan_mode_(cmd.mode); break;
    case CommandType::SET_ROOM: ok = cmd_room_(cmd.value, cmd.mode); break;
    case CommandType::SET_WATER: ok = cmd_water_(cmd.value); break;
    case CommandType::SET_ENERGY_MIX: ok = cmd_energy_mix_(cmd.mix, cmd.level); break;
    case CommandType::SYNC_CLOCK: ok = cmd_sync_clock_(); break;
    case CommandType::SET_TIMER: ok = cmd_timer_(cmd.start, cmd.stop, cmd.value, cmd.mode, cmd.water); break;
    case CommandType::TIMER_OFF: ok = cmd_timer_off_(); break;
  }
  if (ok) state_dirty_ = true;
}

void InetBox::periodic_() {
  Command cmd;
  while (xQueueReceive(cmd_queue_, &cmd, 0) == pdTRUE) process_command_(cmd);

  // Write system time to the CP Plus 30 s after init, then once a day.
  if (init_received_us_ != 0 && clock_valid_ && !clock_update_unsubmitted_ && system_time_valid()) {
    if (!clock_synced_once_) {
      if (micros() - init_received_us_ > 30UL * 1000 * 1000) cmd_sync_clock_();
    } else if (millis() - last_clock_sync_ms_ > CLOCK_RESYNC_INTERVAL_MS) {
      cmd_sync_clock_();
    }
  }

  publish_state_(false);
}

void InetBox::publish_state_(bool changed) {
  const uint32_t now = millis();
  TrumaState s;
  s.heater_valid = heater_valid_;
  s.heater = heater_;
  s.timer_valid = timer_valid_;
  s.timer = timer_;
  s.clock_valid = clock_valid_;
  s.clock = clock_;
  s.last_cpplus_poll_ms = last_cpplus_poll_ms_;
  s.cpplus_connected = last_cpplus_poll_ms_ != 0 && (now - last_cpplus_poll_ms_) < CPPLUS_TIMEOUT_MS;
  s.last_lin_frame_ms = last_lin_frame_ms_;
  s.lin_traffic = last_lin_frame_ms_ != 0 && (now - last_lin_frame_ms_) < 10000;
  s.lin_fault = fault_counter_ > 3;
  s.init_received = init_received_us_ != 0;
  s.heater_device = heater_device_;
  memcpy(s.cpplus_version, cpplus_version_, sizeof(s.cpplus_version));
  memcpy(s.heater_version, heater_version_, sizeof(s.heater_version));
  s.update_pending = heater_update_unsubmitted_ || heater_update_stale_ || timer_update_unsubmitted_ ||
                     timer_update_stale_ || clock_update_unsubmitted_;
  s.ack_errors = ack_errors_;
  s.frames_ok = frames_ok_;
  s.crc_errors = crc_errors_;
  s.vent_valid = last_vent_ms_ != 0 && (now - last_vent_ms_) < 10000;
  s.vent_raw = s.vent_valid ? vent_raw_ : 0;

  portENTER_CRITICAL(&state_mux_);
  bool flags_changed = s.cpplus_connected != state_.cpplus_connected || s.lin_traffic != state_.lin_traffic ||
                       s.lin_fault != state_.lin_fault || s.init_received != state_.init_received ||
                       s.update_pending != state_.update_pending || s.vent_valid != state_.vent_valid;
  s.change_counter = state_.change_counter + ((changed || state_dirty_ || flags_changed) ? 1 : 0);
  state_ = s;
  portEXIT_CRITICAL(&state_mux_);
  state_dirty_ = false;
}

void InetBox::get_state(TrumaState &out) {
  portENTER_CRITICAL(&state_mux_);
  out = state_;
  portEXIT_CRITICAL(&state_mux_);
}

}  // namespace truma
