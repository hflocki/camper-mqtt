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
  http.useHTTP10(true);
  http.setUserAgent(FW_NAME "/" FW_VERSION);
  String url = "https://api.github.com/repos/" + repo + "/releases?per_page=30";
  if (!http.begin(client, url)) { set("error", "Keine Verbindung zu GitHub"); return; }
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != 200) {
    set("error", code == 404 ? String("Repo nicht gefunden oder privat") : "GitHub: Fehler " + String(code));
    http.end();
    return;
  }
  // only the fields we need; the full list can be large
  JsonDocument filter;
  filter[0]["tag_name"] = true;
  filter[0]["draft"] = true;
  filter[0]["prerelease"] = true;
  filter[0]["assets"][0]["name"] = true;
  filter[0]["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { set("error", String("GitHub-Antwort: ") + err.c_str()); return; }

  String bestVer, bestUrl;
  for (JsonObjectConst r : doc.as<JsonArrayConst>()) {
    if (r["draft"] | false || r["prerelease"] | false) continue;
    String tag = r["tag_name"] | "";
    if (!tag.startsWith(TAG_PREFIX)) continue;
    String ver = tag.substring(strlen(TAG_PREFIX));
    String bin;
    for (JsonObjectConst a : r["assets"].as<JsonArrayConst>()) {
      String n = a["name"] | "";
      if (n.endsWith(".bin") && n.indexOf("display") >= 0) { bin = a["browser_download_url"] | ""; break; }
    }
    if (bin.isEmpty()) continue;
    if (bestVer.isEmpty() || cmpVersion(ver, bestVer) > 0) { bestVer = ver; bestUrl = bin; }
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  latest = bestVer;
  assetUrl = bestUrl;
  xSemaphoreGive(lock);
  if (bestVer.isEmpty()) set("none", "Kein Release mit Tag display-v… gefunden");
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
  httpUpdate.onProgress([](int cur, int total) { if (total > 0) pct = (int)((int64_t)cur * 100 / total); });
  HTTPUpdateResult r = httpUpdate.update(client, url);
  if (r == HTTP_UPDATE_OK) {
    set("done", "Update installiert, Neustart ...");
    delay(1500);
    ESP.restart();
  } else {
    set("error", "Update fehlgeschlagen: " + httpUpdate.getLastErrorString());
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
