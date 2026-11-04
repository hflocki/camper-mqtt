#include "notify.h"
#include "config.h"
#include "settings.h"
#include "ui.h"
#include "datastore.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <vector>
#include <map>
#include <time.h>
#include <esp_heap_caps.h>

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
  uint16_t retry, expire;    // only used for emergency messages (prio 2)
  char tag[12];              // emergency: tag to stop the repeats later; with cancel = that request
  bool cancel;               // stop the repeats of the emergency message with this tag
};

QueueHandle_t queue = nullptr;
volatile bool testRequested = false;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
char lastStatus[112] = "";
char lastDetail[80] = "";   // reason of the last failed request (DNS, TLS error text, free RAM)

// A warning line, identified by the text before the first ':' (e.g. "Kühlschrank zu warm"),
// so a changing value in the rest of the line does not send a new message.
struct Active {
  String key;
  String text;
  uint8_t cat;               // kind of warning (WarnCat), decides the priority
  unsigned long since;
  unsigned long goneSince;   // 0 = still reported
  bool sent;
  bool quiet;                // came back within the quiet time: neither warning nor "done" message
  int8_t prio;               // priority it was sent with (2 = emergency, repeats must be stopped)
};
std::vector<Active> active;
// key -> millis() of the last message for it, for the quiet time (settings.poQuiet)
std::map<String, unsigned long> lastSent;
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
  lastDetail[0] = 0;
  // A failed name lookup also ends as -1 in HTTPClient, so check it separately
  IPAddress ip;
  if (!WiFi.hostByName(PUSHOVER_HOST, ip)) {
    strlcpy(lastDetail, "DNS: " PUSHOVER_HOST " nicht gefunden", sizeof(lastDetail));
    return -1;
  }
  // mbedTLS needs roughly 40 kB of internal RAM for the handshake
  size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  Serial.printf("[push] internal RAM free %u kB, largest block %u kB\n", (unsigned)(freeInt / 1024), (unsigned)(largest / 1024));

  NetworkClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
  client.setTimeout(10000);
  HTTPClient http;
  String body = "token=" + urlEncode(m.token);
  if (m.cancel) {
    // stop the repeats of an emergency message whose warning is over
    String url = String(PUSHOVER_CANCEL_URL) + m.tag + ".json";
    if (!http.begin(client, url)) return -1;
  } else {
    if (!http.begin(client, PUSHOVER_URL)) return -1;
    body += "&user=" + urlEncode(m.user) + "&title=" + urlEncode(m.title) +
            "&message=" + urlEncode(m.text) + "&priority=" + String((int)m.prio);
    // Emergency messages repeat until confirmed in the app; Pushover requires retry and expire
    if (m.prio == 2)
      body += "&retry=" + String(m.retry) + "&expire=" + String(m.expire) + "&tags=" + m.tag;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST(body);
  if (code < 0) {
    char err[64] = "";
    int e = client.lastError(err, sizeof(err));
    if (e != 0) snprintf(lastDetail, sizeof(lastDetail), "TLS %d: %s", e, err);
    else        snprintf(lastDetail, sizeof(lastDetail), "%s", http.errorToString(code).c_str());
    size_t n = strlen(lastDetail);
    snprintf(lastDetail + n, sizeof(lastDetail) - n, " (RAM %u kB)", (unsigned)(freeInt / 1024));
    Serial.printf("[push] failed: %s\n", lastDetail);
  }
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
        Serial.printf("[push] %s\"%s\" -> %d\n", m.cancel ? "cancel " : "", m.cancel ? m.tag : m.text, code);
        if (code == 200) { setStatus("OK"); break; }
        char s[100];
        if (lastDetail[0]) snprintf(s, sizeof(s), "Fehler %d \xE2\x80\x93 %s", code, lastDetail);
        else               snprintf(s, sizeof(s), "Fehler %d", code);
        setStatus(s);
        if (code >= 400 && code < 500) break;   // wrong token / user: retrying does not help
      } else {
        setStatus("Kein WLAN");
      }
      vTaskDelay(pdMS_TO_TICKS(PUSHOVER_RETRY_MS));
    }
  }
}

// Tag of an emergency message: short hash of the warning key (only letters and digits allowed)
void tagOf(const String& key, char* out, size_t len) {
  uint32_t h = 2166136261u;   // FNV-1a
  for (size_t i = 0; i < key.length(); i++) { h ^= (uint8_t)key[i]; h *= 16777619u; }
  snprintf(out, len, "w%08lx", (unsigned long)h);
}

void enqueue(const String& title, const String& text, int8_t prio, const String& key = String(), bool cancel = false) {
  if (!queue || settings.poToken.isEmpty() || settings.poUser.isEmpty()) return;
  Msg m = {};
  m.cancel = cancel;
  if (key.length()) tagOf(key, m.tag, sizeof(m.tag));
  strlcpy(m.token, settings.poToken.c_str(), sizeof(m.token));
  strlcpy(m.user, settings.poUser.c_str(), sizeof(m.user));
  strlcpy(m.title, title.c_str(), sizeof(m.title));
  strlcpy(m.text, text.c_str(), sizeof(m.text));
  m.prio = prio;
  m.retry = settings.poRetry;
  m.expire = settings.poExpire;
  if (xQueueSend(queue, &m, 0) != pdTRUE) Serial.println("[push] queue full, message dropped");
}

// Priority of a warning kind: its own setting, or the general one. PRIO_OFF = do not send.
int8_t prioOf(uint8_t cat) {
  int8_t p = cat < WC_COUNT ? settings.poCat[cat] : PRIO_DEFAULT;
  return p == PRIO_DEFAULT ? settings.poPrio : p;
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

  // Current warning lines and their kinds (one kind per line, empty lines included)
  std::vector<uint8_t> cats;
  String all = ui::warningText(cats);
  std::vector<String> lines;
  std::vector<uint8_t> lineCat;
  int start = 0;
  size_t idx = 0;
  while (start < (int)all.length()) {
    int nl = all.indexOf('\n', start);
    if (nl < 0) nl = all.length();
    String l = all.substring(start, nl);
    l.trim();
    if (l.length()) {
      lines.push_back(l);
      lineCat.push_back(idx < cats.size() ? cats[idx] : (uint8_t)WC_COUNT);
    }
    idx++;
    start = nl + 1;
  }

  unsigned long now = millis();
  // New or still active warnings
  for (size_t n = 0; n < lines.size(); n++) {
    const String& l = lines[n];
    String k = keyOf(l);
    bool found = false;
    for (auto& a : active) {
      if (a.key == k) { a.text = l; a.cat = lineCat[n]; a.goneSince = 0; found = true; break; }
    }
    if (!found) active.push_back({k, l, lineCat[n], now, 0, false, false, 0});
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
      if (a.sent && !a.quiet && a.prio == 2) enqueue("", "", 0, a.key, true);   // stop the repeats
      if (a.sent && !a.quiet && settings.poClear) enqueue(settings.poTitle, "\xE2\x9C\x93 Erledigt: " + a.key, -1);
      active.erase(active.begin() + i);
      continue;
    }
    if (!a.sent && now - a.since >= PUSHOVER_STABLE_MS) {
      auto it = lastSent.find(a.key);
      a.quiet = it != lastSent.end() && now - it->second < (unsigned long)settings.poQuiet * 60000UL;
      int8_t prio = prioOf(a.cat);
      if (prio == PRIO_OFF) a.quiet = true;   // this kind is not sent: no "done" message either
      if (!a.quiet) {
        a.prio = prio;
        enqueue(settings.poTitle, "\xE2\x9A\xA0 " + a.text, prio, a.key);
        lastSent[a.key] = now;
      }
      a.sent = true;
    }
    i++;
  }
}

} // namespace notify
