#include "notify.h"
#include "config.h"
#include "settings.h"
#include "ui.h"
#include "datastore.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <vector>
#include <time.h>

// Root certificates that come with the ESP32 core (Mozilla bundle), used to verify api.pushover.net
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");

namespace {

struct Msg {
  char token[40];
  char user[40];
  char title[48];
  char text[256];
  int8_t prio;
};

QueueHandle_t queue = nullptr;
volatile bool testRequested = false;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
char lastStatus[40] = "";

// A warning line, identified by the text before the first ':' (e.g. "Kühlschrank zu warm"),
// so a changing value in the rest of the line does not send a new message.
struct Active {
  String key;
  String text;
  unsigned long since;
  unsigned long goneSince;   // 0 = still reported
  bool sent;
};
std::vector<Active> active;
unsigned long lastCheck = 0;

void setStatus(const char* s) {
  char t[12] = "";
  struct tm tm;
  if (getLocalTime(&tm, 0)) strftime(t, sizeof(t), " %H:%M", &tm);
  portENTER_CRITICAL(&mux);
  snprintf(lastStatus, sizeof(lastStatus), "%s%s", s, t);
  portEXIT_CRITICAL(&mux);
}

String urlEncode(const char* s) {
  String out;
  char hex[4];
  for (const uint8_t* p = (const uint8_t*)s; *p; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') out += (char)*p;
    else { snprintf(hex, sizeof(hex), "%%%02X", *p); out += hex; }
  }
  return out;
}

int post(const Msg& m) {
  NetworkClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
  client.setTimeout(10000);
  HTTPClient http;
  if (!http.begin(client, PUSHOVER_URL)) return -1;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "token=" + urlEncode(m.token) + "&user=" + urlEncode(m.user) +
                "&title=" + urlEncode(m.title) + "&message=" + urlEncode(m.text) +
                "&priority=" + String((int)m.prio);
  int code = http.POST(body);
  http.end();
  return code;
}

void senderTask(void*) {
  Msg m;
  for (;;) {
    if (xQueueReceive(queue, &m, portMAX_DELAY) != pdTRUE) continue;
    for (int attempt = 0; attempt < PUSHOVER_RETRIES; attempt++) {
      if (WiFi.status() == WL_CONNECTED) {
        int code = post(m);
        Serial.printf("[push] \"%s\" -> %d\n", m.text, code);
        if (code == 200) { setStatus("OK"); break; }
        char s[24];
        snprintf(s, sizeof(s), "Fehler %d", code);
        setStatus(s);
        if (code >= 400 && code < 500) break;   // wrong token / user: retrying does not help
      } else {
        setStatus("Kein WLAN");
      }
      vTaskDelay(pdMS_TO_TICKS(PUSHOVER_RETRY_MS));
    }
  }
}

void enqueue(const String& title, const String& text, int8_t prio) {
  if (!queue || settings.poToken.isEmpty() || settings.poUser.isEmpty()) return;
  Msg m = {};
  strlcpy(m.token, settings.poToken.c_str(), sizeof(m.token));
  strlcpy(m.user, settings.poUser.c_str(), sizeof(m.user));
  strlcpy(m.title, title.c_str(), sizeof(m.title));
  strlcpy(m.text, text.c_str(), sizeof(m.text));
  m.prio = prio;
  if (xQueueSend(queue, &m, 0) != pdTRUE) Serial.println("[push] queue full, message dropped");
}

String keyOf(const String& line) {
  int i = line.indexOf(':');
  return i > 0 ? line.substring(0, i) : line;
}

} // namespace

namespace notify {

void begin() {
  queue = xQueueCreate(PUSHOVER_QUEUE, sizeof(Msg));
  xTaskCreate(senderTask, "push", 8192, nullptr, 1, nullptr);
}

void requestTest() { testRequested = true; }

String status() {
  portENTER_CRITICAL(&mux);
  String s = lastStatus;
  portEXIT_CRITICAL(&mux);
  return s;
}

void loop() {
  if (testRequested) {
    testRequested = false;
    if (settings.poToken.isEmpty() || settings.poUser.isEmpty()) setStatus("Token/User fehlt");
    else enqueue(settings.poTitle, "Testnachricht vom Display", 0);
  }

  if (millis() - lastCheck < 2000) return;
  lastCheck = millis();

  if (!settings.poOn) { active.clear(); return; }

  // Current warning lines
  String all = ui::warningText();
  std::vector<String> lines;
  int start = 0;
  while (start < (int)all.length()) {
    int nl = all.indexOf('\n', start);
    if (nl < 0) nl = all.length();
    String l = all.substring(start, nl);
    l.trim();
    if (l.length()) lines.push_back(l);
    start = nl + 1;
  }

  unsigned long now = millis();
  // New or still active warnings
  for (auto& l : lines) {
    String k = keyOf(l);
    bool found = false;
    for (auto& a : active) {
      if (a.key == k) { a.text = l; a.goneSince = 0; found = true; break; }
    }
    if (!found) active.push_back({k, l, now, 0, false});
  }
  // Gone warnings, and warnings that stayed long enough to be sent
  for (size_t i = 0; i < active.size();) {
    Active& a = active[i];
    bool present = false;
    for (auto& l : lines) if (keyOf(l) == a.key) { present = true; break; }
    if (!present) {
      // Warnings of offline devices are hidden on the display; that does not mean they are over
      bool tempOff = settings.prefixTemp.length() && !store::devOnline(DEV_TEMP);
      bool heatOff = settings.prefixHeater.length() && !store::devOnline(DEV_HEATER);
      bool batOff  = settings.prefixBattery.length() && !store::devOnline(DEV_BATTERY);
      if (tempOff || heatOff || batOff) {
        if (!a.sent) { active.erase(active.begin() + i); continue; }
        i++;
        continue;
      }
      if (a.goneSince == 0) a.goneSince = now;
      if (now - a.goneSince < PUSHOVER_STABLE_MS) { i++; continue; }
      if (a.sent && settings.poClear) enqueue(settings.poTitle, "\xE2\x9C\x93 Erledigt: " + a.key, -1);
      active.erase(active.begin() + i);
      continue;
    }
    if (!a.sent && now - a.since >= PUSHOVER_STABLE_MS) {
      enqueue(settings.poTitle, "\xE2\x9A\xA0 " + a.text, settings.poPrio);
      a.sent = true;
    }
    i++;
  }
}

} // namespace notify
