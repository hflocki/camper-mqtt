// Tiny logger: prints to Serial and keeps the last lines in RAM for the web UI and MQTT.
// Every line gets a running sequence number, so readers can fetch only new lines.
#pragma once

#include <Arduino.h>

enum class LogLevel : uint8_t { ERROR = 0, WARN = 1, INFO = 2, DEBUG = 3 };

void log_init(LogLevel level);
void log_set_level(LogLevel level);
LogLevel log_get_level();
void log_write(LogLevel level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

// Appends all lines with sequence number >= `since` (oldest first) to `out`.
// Returns the sequence number to pass as `since` next time.
uint32_t log_read_since(uint32_t since, String &out);

// Copies the line with sequence number `seq` into `buf`. Returns false if it does not exist (yet / anymore).
// If the line was already overwritten, `seq` is moved forward to the oldest available line.
bool log_get_line(uint32_t &seq, char *buf, size_t buf_len, LogLevel &level);

// Sequence number the next written line will get.
uint32_t log_next_seq();

#define LOGE(tag, ...) log_write(LogLevel::ERROR, tag, __VA_ARGS__)
#define LOGW(tag, ...) log_write(LogLevel::WARN, tag, __VA_ARGS__)
#define LOGI(tag, ...) log_write(LogLevel::INFO, tag, __VA_ARGS__)
#define LOGD(tag, ...) log_write(LogLevel::DEBUG, tag, __VA_ARGS__)
