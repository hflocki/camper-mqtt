// Minimal host shim of the Arduino API, just enough for emu.cpp (tests only)
#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <algorithm>
#include <vector>
using std::min; using std::max;
#define constrain(a, lo, hi) ((a) < (lo) ? (lo) : ((a) > (hi) ? (hi) : (a)))
uint32_t millis();
inline long random(long a, long b) { return a + rand() % (b - a); }
class String {
public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& x) : s(x) {}
  explicit String(char c) : s(1, c) {}
  explicit String(int v) : s(std::to_string(v)) {}
  explicit String(unsigned v) : s(std::to_string(v)) {}
  explicit String(long v) : s(std::to_string(v)) {}
  explicit String(unsigned long v) : s(std::to_string(v)) {}
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator!=(const String& o) const { return s != o.s; }
  bool operator==(const char* o) const { return s == o; }
  bool operator!=(const char* o) const { return s != o; }
  bool operator<(const String& o) const { return s < o.s; }
  char operator[](size_t i) const { return s[i]; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  bool concat(const char* c) { s += c; return true; }
  bool concat(const char* c, size_t n) { s.append(c, n); return true; }
  bool concat(char c) { s += c; return true; }
  void reserve(size_t n) { s.reserve(n); }
  bool startsWith(const String& p) const { return s.compare(0, p.s.size(), p.s) == 0; }
  bool endsWith(const String& p) const { return s.size() >= p.s.size() && s.compare(s.size() - p.s.size(), p.s.size(), p.s) == 0; }
  int indexOf(char c, size_t from = 0) const { auto r = s.find(c, from); return r == std::string::npos ? -1 : (int)r; }
  String substring(size_t a) const { return a >= s.size() ? String("") : String(s.substr(a)); }
  String substring(size_t a, size_t b) const { return String(s.substr(a, b - a)); }
  void trim() { size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b - a + 1); }
  void toUpperCase() { for (auto& c : s) c = toupper(c); }
  void toLowerCase() { for (auto& c : s) c = tolower(c); }
  void replace(char a, char b) { for (auto& c : s) if (c == a) c = b; }
  void remove(size_t i) { s.erase(i); }
  float toFloat() const { return strtof(s.c_str(), nullptr); }
  long toInt() const { return strtol(s.c_str(), nullptr, 10); }
  bool equalsIgnoreCase(const String& o) const { if (o.s.size() != s.size()) return false; for (size_t i = 0; i < s.size(); i++) if (tolower(s[i]) != tolower(o.s[i])) return false; return true; }
};
inline String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, const char* b) { String r(a); r += b; return r; }
inline String operator+(const char* a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, char b) { String r(a); r += b; return r; }
inline String operator+(const String& a, int b) { String r(a); r += String(b); return r; }
struct SerialShim { void printf(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); } void println(const char* s) { puts(s); } };
extern SerialShim Serial;
