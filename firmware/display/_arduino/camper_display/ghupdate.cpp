#include "ghupdate.h"
#include "config.h"
#include "settings.h"
#include "wifi_manager.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <NetworkClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// Root certificates of the ESP32 core (Mozilla bundle), also used by notify.cpp and weather.cpp
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");

namespace ghupdate {
namespace {

const char* const TAG_PREFIX = "display-v";

SemaphoreHandle_t lock = nullptr;
TaskHandle_t task = nullptr;
volatile uint8_t job = 0;            // 1 = check, 2 = install
String state = "idle", latest, assetUrl, msg;
int pct = 0;
volatile int otaPct = -1;
const char* volatile otaSrc = "";

void set(const char* st, const String& m = "") {
  xSemaphoreTake(lock, portMAX_DELAY);
  state = st;
  msg = m;
  xSemaphoreGive(lock);
  Serial.printf("[ghupd] %s %s\n", st, m.c_str());
}

// "2.10.1" > "2.9.3"
int cmpVersion(const String& a, const String& b) {
  int ia = 0, ib = 0;
  for (int part = 0; part < 4; part++) {
    long x = a.substring(ia).toInt(), y = b.substring(ib).toInt();
    if (x != y) return x < y ? -1 : 1;
    ia = a.indexOf('.', ia);
    ib = b.indexOf('.', ib);
    if (ia < 0 && ib < 0) return 0;
    ia = ia < 0 ? a.length() : ia + 1;
    ib = ib < 0 ? b.length() : ib + 1;
  }
  return 0;
}

void doCheck() {
  String repo = settings.updRepo;
  repo.trim();
  if (repo.indexOf('/') <= 0) { set("error", "Kein GitHub-Repo eingetragen (benutzer/repo)"); return; }
  if (!wifimgr::staConnected()) { set("error", "Keine Internetverbindung"); return; }
  set("checking");
  NetworkClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
  HTTPClient http;
  http.setTimeout(15000);
  http.setUserAgent(FW_NAME "/" FW_VERSION);
  // Only the tags starting with display-v: a small answer, unlike the full release list
  String url = "https://api.github.com/repos/" + repo + "/git/matching-refs/tags/" + TAG_PREFIX;
  if (!http.begin(client, url)) { set("error", "Keine Verbindung zu GitHub"); return; }
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != 200) {
    set("error", code == 404 ? String("Repo nicht gefunden oder privat")
               : code == 403 ? String("GitHub: zu viele Anfragen, später erneut versuchen")
               : "GitHub: Fehler " + String(code));
    http.end();
    return;
  }
  String body = http.getString();   // reads the whole answer (chunked or not)
  http.end();
  JsonDocument filter;
  filter[0]["ref"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) { set("error", String("GitHub-Antwort: ") + err.c_str() + " (" + body.length() + " Bytes)"); return; }

  String bestVer;
  for (JsonObjectConst r : doc.as<JsonArrayConst>()) {
    String ref = r["ref"] | "";                       // "refs/tags/display-v2.5.1"
    int i = ref.indexOf(TAG_PREFIX);
    if (i < 0) continue;
    String ver = ref.substring(i + strlen(TAG_PREFIX));
    if (ver.isEmpty() || !isdigit((unsigned char)ver[0])) continue;
    if (bestVer.isEmpty() || cmpVersion(ver, bestVer) > 0) bestVer = ver;
  }
  // file name as built by .github/workflows/build.yml
  String bestUrl = bestVer.isEmpty() ? String("")
                 : "https://github.com/" + repo + "/releases/download/" + TAG_PREFIX + bestVer + "/" UPDATE_ASSET;
  xSemaphoreTake(lock, portMAX_DELAY);
  latest = bestVer;
  assetUrl = bestUrl;
  xSemaphoreGive(lock);
  if (bestVer.isEmpty()) set("none", "Kein Tag display-v… im Repo gefunden");
  else if (cmpVersion(bestVer, FW_VERSION) > 0) set("available", "Version " + bestVer + " verfügbar");
  else set("none", "Aktuell (neueste Version " + bestVer + ")");
}

void doInstall() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String url = assetUrl;
  xSemaphoreGive(lock);
  if (url.isEmpty()) { set("error", "Erst nach Update suchen"); return; }
  set("downloading");
  pct = 0;
  NetworkClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
  client.setTimeout(20);
  // GitHub sends release files through a redirect to its download servers
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);
  setProgress(0, "GitHub");
  httpUpdate.onProgress([](int cur, int total) {
    if (total > 0) pct = (int)((int64_t)cur * 100 / total);
    setProgress(pct, "GitHub");
  });
  HTTPUpdateResult r = httpUpdate.update(client, url);
  if (r != HTTP_UPDATE_OK) setProgress(-1, "");
  if (r == HTTP_UPDATE_OK) {
    set("done", "Update installiert, Neustart ...");
    delay(1500);
    ESP.restart();
  } else {
    String e = httpUpdate.getLastErrorString();
    if (httpUpdate.getLastError() == HTTP_UE_SERVER_FILE_NOT_FOUND || e.indexOf("404") >= 0)
      e = "Release-Datei fehlt (Build in GitHub Actions noch nicht fertig?)";
    set("error", "Update fehlgeschlagen: " + e);
  }
}

void run(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(200));
    uint8_t j = job;
    if (!j) continue;
    job = 0;
    if (j == 1) doCheck();
    else doInstall();
  }
}

}  // namespace

void begin() {
  if (!lock) lock = xSemaphoreCreateMutex();
  if (!task) xTaskCreatePinnedToCore(run, "ghupd", 12288, nullptr, 1, &task, 0);
}

void setProgress(int p, const char* source) { otaSrc = source; otaPct = p; }
int progress() { return otaPct; }
const char* progressSource() { return otaSrc; }

void check() { if (state != "downloading") job = 1; }
void install() { if (state != "downloading") job = 2; }

String json() {
  JsonDocument d;
  xSemaphoreTake(lock, portMAX_DELAY);
  d["state"] = state;
  d["latest"] = latest;
  d["msg"] = msg;
  xSemaphoreGive(lock);
  d["cur"] = FW_VERSION;
  d["pct"] = pct;
  d["repo"] = settings.updRepo;
  String out;
  serializeJson(d, out);
  return out;
}

}  // namespace ghupdate
