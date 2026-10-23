#include "logger.h"

#include <stdarg.h>

#include "config.h"
#include <time.h>

namespace {
constexpr uint32_t LOG_LINES = 80;
constexpr size_t LOG_LINE_LEN = 128;

struct Line {
  uint32_t seq;
  LogLevel level;
  char text[LOG_LINE_LEN];
};

Line g_lines[LOG_LINES];
uint32_t g_next_seq = 1;  // sequence number of the next line, 0 = "nothing"
LogLevel g_level = LogLevel::INFO;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

char level_char(LogLevel l) {
  switch (l) {
    case LogLevel::ERROR: return 'E';
    case LogLevel::WARN: return 'W';
    case LogLevel::INFO: return 'I';
    default: return 'D';
  }
}

uint32_t oldest_seq_locked() { return g_next_seq > LOG_LINES ? g_next_seq - LOG_LINES : 1; }
}  // namespace

void log_init(LogLevel level) { g_level = level; }
void log_set_level(LogLevel level) { g_level = level; }
LogLevel log_get_level() { return g_level; }

void log_write(LogLevel level, const char *tag, const char *fmt, ...) {
  if (level > g_level) return;

  char msg[LOG_LINE_LEN];
  int n;
  // Wall clock once NTP is synced, uptime before.
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_year + 1900 >= 2025) {
    n = snprintf(msg, sizeof(msg), "%02d:%02d:%02d [%c][%s] ", t.tm_hour, t.tm_min, t.tm_sec, level_char(level), tag);
  } else {
    n = snprintf(msg, sizeof(msg), "+%lus [%c][%s] ", (unsigned long) (millis() / 1000), level_char(level), tag);
  }
  if (n < 0) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg + n, sizeof(msg) - n, fmt, args);
  va_end(args);

#if SERIAL_LOG_ENABLED
  // Serial has a large TX buffer (see setup), so this does not block in normal operation.
  Serial.println(msg);
#endif

  portENTER_CRITICAL(&g_mux);
  Line &l = g_lines[g_next_seq % LOG_LINES];
  l.seq = g_next_seq++;
  l.level = level;
  memcpy(l.text, msg, sizeof(msg));
  portEXIT_CRITICAL(&g_mux);
}

uint32_t log_next_seq() {
  portENTER_CRITICAL(&g_mux);
  uint32_t s = g_next_seq;
  portEXIT_CRITICAL(&g_mux);
  return s;
}

bool log_get_line(uint32_t &seq, char *buf, size_t buf_len, LogLevel &level) {
  bool ok = false;
  portENTER_CRITICAL(&g_mux);
  uint32_t oldest = oldest_seq_locked();
  if (seq < oldest) seq = oldest;
  if (seq < g_next_seq) {
    const Line &l = g_lines[seq % LOG_LINES];
    strlcpy(buf, l.text, buf_len);
    level = l.level;
    ok = true;
  }
  portEXIT_CRITICAL(&g_mux);
  return ok;
}

uint32_t log_read_since(uint32_t since, String &out) {
  char buf[LOG_LINE_LEN];
  LogLevel lvl;
  uint32_t seq = since;
  while (log_get_line(seq, buf, sizeof(buf), lvl)) {
    out += buf;
    out += '\n';
    seq++;
  }
  return seq;
}
