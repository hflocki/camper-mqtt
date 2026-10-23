#define private public
#include "inetbox.h"
#undef private
#include "logger.h"
#include <cstdarg>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace truma;
uint32_t g_us = 1000000;
std::vector<uint8_t> g_tx;
int uart_write_bytes(uart_port_t, const char *d, size_t n) { g_tx.assign((const uint8_t *) d, (const uint8_t *) d + n); return n; }
bool g_verbose = false;
void log_write(LogLevel, const char *tag, const char *fmt, ...) {
  if (!g_verbose) return;
  va_list a; va_start(a, fmt); printf("    [%s] ", tag); vprintf(fmt, a); printf("\n"); va_end(a);
}
InetBox box;
static uint8_t pidp(uint8_t id) { return id | (addr_parity(id) << 6); }
static void feed(uint8_t b) { box.on_byte_(b); box.last_byte_us_ = micros(); g_us += 1150; }
static std::vector<uint8_t> hdr(uint8_t id) { g_us += 30000; g_tx.clear(); feed(0x00); feed(0x55); feed(pidp(id)); auto a = g_tx; for (auto b : a) feed(b); if (a.empty()) g_us += 20000; return a; }
static void mframe(uint8_t id, const std::vector<uint8_t> &d) {
  g_us += 30000; feed(0x00); feed(0x55); feed(pidp(id));
  for (auto b : d) feed(b);
  feed(id == 0x3C ? data_checksum(d.data(), 8, 0) : data_checksum(d.data(), 8, id));
}
static std::string hex(const std::vector<uint8_t> &v, size_t n = 8) { std::string s; char b[4]; for (size_t i = 0; i < v.size() && i < n; i++) { snprintf(b, 4, "%02X ", v[i]); s += b; } return s; }
int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  box.cmd_queue_ = xQueueCreate(8, sizeof(Command));
  box.time_per_first_byte_us_ = 5729;
  g_verbose = false; for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "-v") g_verbose = true;
  std::string line;
  int slots = 0, both = 0, match = 0, ours_only = 0, real_only = 0;
  for (int fi = 1; fi < argc; fi++) {
  std::string arg = argv[fi];
  if (arg.rfind("cmd:", 0) == 0) {
    Command c; c.type = CommandType::SET_ROOM; float t; unsigned m;
    sscanf(arg.c_str(), "cmd:%f,%x", &t, &m); c.value = t; c.mode = (HeatingMode) m;
    box.process_command_(c); printf(">> command room %.0f mode 0x%X queued (pending=%d)\n", t, m, box.heater_update_unsubmitted_);
    continue;
  }
  if (arg.rfind("timer:", 0) == 0) {
    // timer:HH:MM-HH:MM,room,modehex,water  (water < 0 = keep)
    Command c; c.type = CommandType::SET_TIMER; unsigned h1, m1, h2, m2, m; float t, w;
    sscanf(arg.c_str(), "timer:%u:%u-%u:%u,%f,%x,%f", &h1, &m1, &h2, &m2, &t, &m, &w);
    c.start = h1 * 60 + m1; c.stop = h2 * 60 + m2; c.value = t; c.mode = (HeatingMode) m; c.water = w < 0 ? NAN : w;
    box.process_command_(c); printf(">> command %s queued\n", arg.c_str());
    continue;
  }
  if (arg.rfind("dev:", 0) == 0) { box.heater_device_ = (Device) std::stoul(arg.substr(4), nullptr, 16); continue; }
  if (arg == "sniff") { box.set_sniffer(true); continue; }
  if (arg == "timeroff") { Command c; c.type = CommandType::TIMER_OFF; box.process_command_(c); printf(">> command timer off queued\n"); continue; }
  if (arg == "-v") continue;
  if (arg.rfind("nad:", 0) == 0) { box.node_address_ = std::stoul(arg.substr(4), nullptr, 16); box.init_received_us_ = 1; continue; }  // mid-session capture: already registered
  std::ifstream in(arg);
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string kind, pidh; ss >> kind >> pidh;
    uint8_t pid = std::stoul(pidh, nullptr, 16);
    std::vector<uint8_t> d; std::string t;
    while (ss >> t) if (t != "-") d.push_back(std::stoul(t, nullptr, 16));
    if (kind == "M") { mframe(pid, d); SniffFrame f; while (box.sniff_pop(f)) { printf("SNIFF PID %02X:", f.pid); for (int i = 0; i < f.len; i++) printf(" %02X", f.data[i]); printf("\n"); } continue; }
    auto a = hdr(pid);
    std::vector<uint8_t> ours(a.begin(), a.end() - (a.empty() ? 0 : 1));
    slots++;
    { SniffFrame f; while (box.sniff_pop(f)) { printf("SNIFF PID %02X:", f.pid); for (int i = 0; i < f.len; i++) printf(" %02X", f.data[i]); printf("\n"); } }
    if (pid == 0x3D && ours.size() == 8) {
      static std::vector<uint8_t> tb; static size_t tl = 0;
      if ((ours[1] & 0xF0) == 0x10) { tl = ((ours[1] & 0x0F) << 8) | ours[2]; tb.assign(ours.begin() + 3, ours.end()); }
      else if ((ours[1] & 0xF0) == 0x20 && tl) {
        tb.insert(tb.end(), ours.begin() + 2, ours.end());
        if (tb.size() >= tl) {
          tb.resize(tl);
          if (tb[0] == 0xFA && tl > 17) { printf("UPDATE type %02X len %02X:", tb[14], tb[13]); for (size_t i = 17; i < 17u + tb[13]; i++) printf(" %02X", tb[i]); printf("\n"); }
          tl = 0;
        }
      }
    }
    // Only compare with answers of the original iNet Box (0x18, or 0x3D frames with its node address).
    bool real_is_inetbox = !d.empty() && (pid == 0x18 || d[0] == box.node_address_);
    if (!ours.empty() && real_is_inetbox) { both++; if (ours == d) match++; else printf("DIFF pid %02X real: %s ours: %s\n", pid, hex(d).c_str(), hex(ours).c_str()); }
    else if (!ours.empty()) { ours_only++; printf("OURS pid %02X ours: %s\n", pid, hex(ours).c_str()); }
    else if (real_is_inetbox) { real_only++; printf("REAL pid %02X real: %s\n", pid, hex(d).c_str()); }
  }
  }
  printf("\nslots=%d answered_by_both=%d identical=%d ours_only=%d real_only=%d\n", slots, both, match, ours_only, real_only);
  printf("NAD=%02X init=%d heater_valid=%d device=%d timer=%d clock=%d frames=%u crc_err=%u ack_err=%u\n", box.node_address_,
         box.init_received_us_ != 0, box.heater_valid_, (int) box.heater_device_, box.timer_valid_, box.clock_valid_, box.frames_ok_, box.crc_errors_, box.ack_errors_);
  if (box.vent_raw_ != 0xFF) printf("vent: raw 0x%X (%s)\n", box.vent_raw_, vent_mode_to_str(box.vent_raw_));
  if (box.heater_valid_) {
    auto &h = box.heater_;
    printf("heater: room target %.1f, mode 0x%X, cur room %.1f, cur water %.1f, status %u, mix %u\n", temp_code_to_decimal(h.target_temp_room, 0),
           (unsigned) h.heating_mode, temp_code_to_decimal(h.current_temp_room, 0), temp_code_to_decimal(h.current_temp_water, 0), (unsigned) h.operating_status, (unsigned) h.energy_mix_a);
  }
  return box.crc_errors_ == 0 ? 0 : 1;
}
