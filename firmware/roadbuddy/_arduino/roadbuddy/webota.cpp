#include "webota.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

// Note: config.h is not included here, it defines variables and may only be included once (roadbuddy.ino).

namespace webota {
int progress();

namespace {

WebServer server(80);
const char* hostName = "roadbuddy";
const char* fwVersion = "";
String webUser, webPass;
StatusFn statusFn = nullptr;
ProgressFn progressFn = nullptr;

bool mdnsStarted = false;
bool uploading = false;
bool uploadOk = false;
bool uploadAuthorized = false;
size_t uploadTotal = 0;     // expected size from Content-Length (0 = unknown)
size_t uploadWritten = 0;
unsigned long restartAt = 0;

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RoadBuddy</title>
<style>
:root{--bg:#0B1015;--card:#1A232D;--line:#263241;--text:#E8EDF2;--muted:#8696A6;--ok:#4CC27A;--warn:#F2C94C;--bad:#E5484D;--accent:#5AB0F0}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,sans-serif;padding:16px}
main{max-width:480px;margin:0 auto}h1{font-size:20px;margin:0 0 12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px;margin-bottom:12px}
.row{display:flex;justify-content:space-between;gap:12px;padding:4px 0}.row span:first-child{color:var(--muted)}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;background:var(--muted)}
input[type=file]{width:100%;color:var(--muted);margin:8px 0}
button{background:var(--accent);color:#0B1015;border:0;border-radius:999px;padding:9px 18px;font-weight:600;cursor:pointer}
button:disabled{opacity:.5}
.bar{height:8px;background:var(--line);border-radius:4px;overflow:hidden;margin-top:10px}
.bar div{height:100%;width:0;background:var(--accent);transition:width .2s}
#msg{margin-top:8px;color:var(--muted)}
</style></head><body><main>
<h1>RoadBuddy</h1>
<div class="card" id="st"><div class="row"><span>Status</span><span>...</span></div></div>
<div class="card">
<b>Firmware-Update</b>
<div style="color:var(--muted)">Die .bin aus den GitHub Actions (camper-roadbuddy-core2.bin) auswählen.</div>
<input type="file" id="f" accept=".bin">
<button id="b">Hochladen</button>
<div class="bar"><div id="p"></div></div>
<div id="msg"></div>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
function dot(ok){return '<span class="dot" style="background:var(--'+(ok?'ok':'bad')+')"></span>'}
function row(k,v){return '<div class="row"><span>'+k+'</span><span>'+v+'</span></div>'}
async function load(){
 try{const s=await (await fetch('/status')).json();
  $('st').innerHTML=row('Version',s.version)+row('WLAN',dot(s.wifi)+s.rssi+' dBm')+
   row('MQTT',dot(s.mqtt)+(s.mqtt?'verbunden':'getrennt'))+row('Broker',s.broker)+
   row('Akku',s.battery+' %'+(s.charging?' (laedt)':''))+row('Laufzeit',s.uptime)+
   row('Wach bleibt er noch',s.awake);
 }catch(e){}
}
load();setInterval(load,5000);
$('b').onclick=()=>{
 const f=$('f').files[0];if(!f){$('msg').textContent='Bitte zuerst eine .bin auswaehlen.';return}
 const fd=new FormData();fd.append('firmware',f,f.name);
 const x=new XMLHttpRequest();x.open('POST','/update');
 x.upload.onprogress=e=>{if(e.lengthComputable)$('p').style.width=(e.loaded*100/e.total)+'%'};
 x.onload=()=>{$('msg').textContent=x.status==200?'Update ok, RoadBuddy startet neu ...':'Fehler: '+x.responseText;
  if(x.status==200)setTimeout(()=>location.reload(),15000)};
 x.onerror=()=>{$('msg').textContent='Verbindung abgebrochen'};
 $('b').disabled=true;$('msg').textContent='Lade hoch ...';x.send(fd);
};
</script></body></html>)HTML";

bool authOk() {
  if (webUser.isEmpty()) return true;
  return server.authenticate(webUser.c_str(), webPass.c_str());
}

bool auth() {
  if (authOk()) return true;
  server.requestAuthentication();
  return false;
}

void handleRoot() {
  if (!auth()) return;
  server.send_P(200, "text/html", PAGE);
}

void handleStatus() {
  if (!auth()) return;
  server.send(200, "application/json", statusFn ? statusFn() : String("{}"));
}

// Called once the upload is complete
void handleUpdateDone() {
  if (!uploadAuthorized) {
    server.requestAuthentication();
    return;
  }
  bool ok = uploadOk && !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : (String("Update fehlgeschlagen: ") + Update.errorString()));
  if (ok) restartAt = millis() + 1000;   // give the response time to leave
  if (ok && progressFn) progressFn(100);   // shows "restarting"
}

// Called for every chunk of the uploaded file
void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      uploadAuthorized = authOk();
      uploadOk = false;
      uploadWritten = 0;
      if (!uploadAuthorized) return;
      uploadTotal = max(server.clientContentLength(), 0);   // whole multipart body, close enough for a progress bar
      Serial.printf("[OTA] start: %s\n", up.filename.c_str());
      uploading = true;
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
      if (progressFn) progressFn(0);
      break;
    case UPLOAD_FILE_WRITE:
      if (!uploadAuthorized || !uploading) return;
      if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
      uploadWritten += up.currentSize;
      if (progressFn) progressFn(progress());
      break;
    case UPLOAD_FILE_END:
      if (!uploadAuthorized || !uploading) return;
      uploadOk = Update.end(true);
      if (uploadOk) Serial.printf("[OTA] ok, %u bytes\n", (unsigned)up.totalSize);
      else Update.printError(Serial);
      if (!uploadOk) uploading = false;
      if (progressFn) progressFn(uploadOk ? 100 : -1);
      break;
    case UPLOAD_FILE_ABORTED:
      Update.abort();
      uploading = false;
      Serial.println("[OTA] aborted");
      break;
  }
}

}  // namespace

void begin(const char* host, const char* version, const char* user, const char* pass,
           StatusFn status, ProgressFn progressCb) {
  hostName = host;
  fwVersion = version;
  webUser = user ? user : "";
  webPass = pass ? pass : "";
  statusFn = status;
  progressFn = progressCb;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void loop() {
  if (!mdnsStarted && WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(hostName)) MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
  }
  server.handleClient();
  if (restartAt && (long)(millis() - restartAt) > 0) ESP.restart();
}

bool busy() { return uploading; }

int progress() {
  if (!uploading) return -1;
  if (restartAt) return 100;
  if (uploadTotal == 0) return 0;
  return (int)constrain((long)(uploadWritten * 100 / uploadTotal), 0L, 100L);
}

bool finished() { return restartAt != 0; }

}  // namespace webota
