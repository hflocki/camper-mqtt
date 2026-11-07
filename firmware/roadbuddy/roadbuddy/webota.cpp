#include "webota.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "safeguard.h"
#include "fwcheck.h"
#include "evlog.h"

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
/* same look as all VanNode pages (firmware/_common/web/camper-theme.css) */
:root{--bg:#0D1115;--card:#151B21;--hi:#1C242C;--line:#222B34;--line2:#2F3A45;--text:#E7ECF0;--text2:#B4BEC8;--muted:#8B98A5;
 --ok:#46B97C;--warn:#E5A23B;--bad:#E5534B;--accent:#4DA3F0;--acx:#06131F;color-scheme:dark}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif}
svg.i{width:20px;height:20px;flex:none;vertical-align:-4px}
header{display:flex;align-items:center;gap:10px;padding:14px 16px;border-bottom:1px solid var(--line)}
header h1{flex:1;display:flex;align-items:center;gap:10px;font-size:1.15rem;font-weight:600;margin:0}header h1 svg.i{color:var(--accent)}
main{max-width:520px;margin:0 auto;padding:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:12px}
.card>b{display:block;font-size:.85rem;font-weight:600;color:var(--text2);margin-bottom:10px}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:10px 0;border-bottom:1px solid var(--line)}.row:last-child{border-bottom:0}
.row span:first-child{color:var(--muted)}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;background:var(--line2)}
input[type=file]{width:100%;color:var(--muted);margin:10px 0;padding:10px;border:1px dashed var(--line2);border-radius:10px;background:var(--bg)}
button{min-height:44px;background:var(--accent);color:var(--acx);border:1px solid var(--accent);border-radius:10px;padding:10px 18px;font:inherit;font-weight:600;cursor:pointer;width:100%}
.bar{height:6px;background:var(--line);border-radius:999px;overflow:hidden;margin-top:12px}
.bar div{height:100%;width:0;background:var(--accent);transition:width .2s}
#msg{margin-top:8px;color:var(--muted)}
.hint{color:var(--muted);font-size:.85rem}
:root{--fwu-ok:var(--ok);--fwu-warn:var(--warn);--fwu-bad:var(--bad);--fwu-mute:var(--muted);--fwu-line:var(--line)}
.fwu-cur{font-size:.8rem;margin:0 0 10px}
.fwu-chk{font-size:.8rem;margin:8px 0;line-height:1.4}
.fwu-chk.ok{color:var(--fwu-ok)}.fwu-chk.warn{color:var(--fwu-warn)}.fwu-chk.bad{color:var(--fwu-bad)}
.fwu-force{display:flex;gap:8px;align-items:center;font-size:.8rem;margin:0 0 8px}
.fwu-log{font-size:.75rem;line-height:1.4;max-height:320px;overflow-y:auto}
.fwu-ev{padding:4px 0;border-bottom:1px solid var(--fwu-line)}.fwu-ev:last-child{border-bottom:0}
.fwu-ev span{color:var(--fwu-mute);margin-right:8px;white-space:nowrap}.fwu-ev.bad{color:var(--fwu-bad)}
button:disabled{opacity:.45;cursor:not-allowed}
</style></head><body>
<header><h1><svg class="i" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4 5h16v11H4zM9 20h6M12 16v4"/></svg><span>Road Buddy</span></h1></header>
<main>
<div class="card" id="st"><div class="row"><span>Status</span><span>...</span></div></div>
<div class="card">
<b>Firmware-Update</b>
<div class="fwu-cur" id="fwu-cur" style="margin-top:6px">Installiert: …</div>
<div class="hint">Die .bin aus dem Release (roadbuddy-node-core2.bin) oder aus der Arduino IDE (roadbuddy.ino.bin, nicht die .merged.bin). Die Datei wird vor dem Hochladen geprüft.</div>
<input type="file" id="fwu-file" accept=".bin" onchange="fwChosen(false)">
<div class="fwu-chk" id="fwu-chk"></div>
<label class="fwu-force" id="fwu-force-row" style="display:none"><input type="checkbox" id="fwu-force" onchange="fwForce()">Ohne Kennung hochladen</label>
<button id="fwu-btn" disabled>Hochladen</button>
<div class="bar"><div id="p"></div></div>
<div id="msg"></div>
</div>
<div class="card"><b>Ereignisse</b><div class="fwu-log" id="fwu-log" style="margin-top:6px">Lade …</div>
<div class="hint" style="font-size:12px;margin-top:8px">Bleibt bei Neustart, Absturz und Update erhalten, nicht bei Stromausfall. Aufwachen aus dem Schlaf wird nicht eingetragen.</div></div>
</main>
<script>
const $=id=>document.getElementById(id);
function dot(ok){return '<span class="dot" style="background:var(--'+(ok?'ok':'bad')+')"></span>'}
function row(k,v){return '<div class="row"><span>'+k+'</span><span>'+v+'</span></div>'}
async function load(){
 try{const s=await (await fetch('/status')).json();
  $('st').innerHTML=row('Version',s.version)+row('WLAN',dot(s.wifi)+s.rssi+' dBm')+
   row('MQTT',dot(s.mqtt)+(s.mqtt?'verbunden':'getrennt'))+row('Broker',s.broker)+
   row('Akku',s.battery+' %'+(s.charging?' (lädt)':''))+row('Laufzeit',s.uptime)+
   row('Wach bleibt er noch',s.awake);
 }catch(e){}
}
load();setInterval(load,5000);
$('fwu-btn').onclick=()=>{
 const f=$('fwu-file').files[0];if(!f||!fwPick){$('msg').textContent='Bitte zuerst eine .bin auswählen.';return}
 const force=!fwPick.ok&&fwPick.force&&$('fwu-force').checked;
 if(fwPick.warn&&!confirm(fwPick.msg+'\nTrotzdem installieren?'))return;
 const fd=new FormData();fd.append('firmware',f,f.name);
 const x=new XMLHttpRequest();x.open('POST','/update'+(force?'?force=1':''));
 x.upload.onprogress=e=>{if(e.lengthComputable)$('p').style.width=(e.loaded*100/e.total)+'%'};
 x.onload=()=>{$('msg').textContent=x.status==200?'Update ok, RoadBuddy startet neu ...':'Fehler: '+x.responseText;
  if(x.status==200)setTimeout(()=>location.reload(),15000);else{$('fwu-btn').disabled=false;fwLoad()}};
 x.onerror=()=>{$('msg').textContent='Verbindung abgebrochen'};
 $('fwu-btn').disabled=true;$('msg').textContent='Lade hoch ...';x.send(fd);
};
// ---- Firmware update with checks, event log (same checks as fwcheck.cpp on the device) ----
// Elements: fwu-cur, fwu-file, fwu-chk, fwu-force-row, fwu-force, fwu-btn, fwu-pr, fwu-log
let FWI = null;   // running firmware: {dev, ver, chip, chip_id, max}
const FW_CHIPS = {'-1': 'ESP8266', 0: 'ESP32', 2: 'ESP32-S2', 5: 'ESP32-C3', 9: 'ESP32-S3', 12: 'ESP32-C2', 13: 'ESP32-C6', 16: 'ESP32-H2'};
const fwEl = id => document.getElementById(id);
const fwEsc = s => String(s).replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
function fwVerCmp(a, b) {
    const x = String(a).split('.').map(n => parseInt(n) || 0), y = String(b).split('.').map(n => parseInt(n) || 0);
    for (let i = 0; i < Math.max(x.length, y.length); i++) { const d = (x[i] || 0) - (y[i] || 0); if (d) return d; }
    return 0;
}
function fwAgo(s) { return s < 90 ? s + ' s' : s < 5400 ? Math.round(s / 60) + ' min' : s < 172800 ? Math.round(s / 3600) + ' h' : Math.round(s / 86400) + ' d'; }
async function fwLoad() {
    try {
        const r = await fetch('/api/system', {cache: 'no-store'});
        if (!r.ok) return;
        const d = await r.json();
        FWI = d.fw || null;
        if (FWI && fwEl('fwu-cur')) fwEl('fwu-cur').innerHTML = `Installiert: <b>${fwEsc(FWI.dev)} v${fwEsc(FWI.ver)}</b> · ${FW_CHIPS[FWI.chip_id] || fwEsc(FWI.chip)} · Platz ${Math.round(FWI.max / 1024)} kB`;
        const box = fwEl('fwu-log');
        if (box) box.innerHTML = (d.log || []).length ? d.log.map(e => {
            const when = e.b === 0 ? 'vor ' + fwAgo(Math.max(0, (d.up || 0) - e.t)) : (e.b === 1 ? 'vorheriger Start' : e.b + ' Starts zuvor') + ', nach ' + fwAgo(e.t);
            const bad = /abgelehnt|fehlgeschlagen|nicht gespeichert|zurück auf|Absturz|Watchdog|Spannungseinbruch/.test(e.m);
            return `<div class="fwu-ev${bad ? ' bad' : ''}"><span>${fwEsc(when)}</span>${fwEsc(e.m)}</div>`;
        }).join('') : '<div class="fwu-ev">Noch keine Einträge.</div>';
    } catch (e) {}
}
// Checks the chosen file in the browser: {ok, kind, msg, warn, force}
async function fwInspect(file, fsImages) {
    const n = file.name.toLowerCase(), kb = Math.round(file.size / 1024) + ' kB';
    if (n.endsWith('.gz')) return {ok: true, kind: 'web', msg: 'Web-Oberfläche (' + kb + '), ersetzt nur die Seite, ohne Neustart'};
    if (fsImages && /littlefs|spiffs/.test(n)) return {ok: true, kind: 'fs', warn: true, msg: 'Dateisystem-Abbild (' + kb + ') – ersetzt die Web-Oberfläche, danach Neustart'};
    if (!n.endsWith('.bin')) return {ok: false, msg: 'Nur .bin (Firmware) oder .gz (Web-Oberfläche)'};
    const merged = 'Das ist die .merged.bin (komplettes Flash-Abbild für USB). Bitte die .bin ohne „merged“ nehmen.';
    if (n.includes('merged')) return {ok: false, msg: merged};
    const u = new Uint8Array(await file.arrayBuffer());
    if (u.length < 64 || u[0] !== 0xE9) return {ok: false, msg: 'Keine ESP-Firmware'};
    const esp32 = !FWI || FWI.chip_id >= 0;   // ESP8266 images have no chip id and no .merged.bin
    if (esp32 && u.length > 0x8001 && u[0x8000] === 0xAA && u[0x8001] === 0x50) return {ok: false, msg: merged};
    const chip = u[12] | (u[13] << 8);
    if (FWI && esp32 && chip !== FWI.chip_id) return {ok: false, msg: `Firmware für ${FW_CHIPS[chip] || 'einen unbekannten Chip'}, dieses Gerät ist ein ${FW_CHIPS[FWI.chip_id] || FWI.chip}`};
    if (FWI && FWI.max && u.length > FWI.max) return {ok: false, msg: `Datei zu groß (${kb}, Platz ${Math.round(FWI.max / 1024)} kB)`};
    const m = new TextDecoder('latin1').decode(u).match(/CAMPER-FW:([A-Za-z0-9_.-]{1,24}):([A-Za-z0-9_.-]{1,24}):([A-Za-z0-9_.-]{1,24});/);
    if (!m) return {ok: false, force: true, msg: 'Keine Gerätekennung in der Datei: fremde Firmware oder eine Version vor der Kennung. Nur hochladen, wenn sicher ist, dass sie für dieses Gerät ist.'};
    if (FWI && m[1] !== FWI.dev) return {ok: false, msg: `Firmware für „${m[1]}“, dieses Gerät ist „${FWI.dev}“`};
    if (FWI && m[3] !== FWI.chip) return {ok: false, msg: `Firmware für ${m[3]}, dieses Gerät ist ein ${FWI.chip}`};
    const c = FWI ? fwVerCmp(m[2], FWI.ver) : 1;
    return {ok: true, kind: 'fw', warn: c <= 0, msg: `Firmware ${m[1]} v${m[2]} (${kb}) – ` +
        (c > 0 ? 'neuer als die installierte' : c === 0 ? 'gleiche Version wie installiert' : 'ÄLTER als die installierte (Downgrade)')};
}
let fwPick = null;
async function fwChosen(fsImages) {
    const f = fwEl('fwu-file').files[0], chk = fwEl('fwu-chk');
    fwPick = null;
    fwEl('fwu-force-row').style.display = 'none';
    fwEl('fwu-force').checked = false;
    if (!f) { chk.textContent = ''; fwEl('fwu-btn').disabled = true; return; }
    chk.className = 'fwu-chk'; chk.textContent = 'Prüfe …';
    const r = await fwInspect(f, fsImages);
    fwPick = r;
    chk.textContent = (r.ok ? (r.warn ? '⚠ ' : '✓ ') : '✕ ') + r.msg;
    chk.className = 'fwu-chk ' + (r.ok ? (r.warn ? 'warn' : 'ok') : 'bad');
    if (r.force) fwEl('fwu-force-row').style.display = '';
    fwEl('fwu-btn').disabled = !r.ok;
}
function fwForce() { fwEl('fwu-btn').disabled = !(fwPick && (fwPick.ok || (fwPick.force && fwEl('fwu-force').checked))); }
fwLoad();
setInterval(fwLoad, 15000);
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
  bool ok = uploadOk && fwcheck::ok();
  if (ok) guard::otaDone();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 400, "text/plain", ok ? "OK" : "Update abgelehnt: " + fwcheck::error());
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
      fwcheck::begin(uploadTotal, server.hasArg("force"));
      if (progressFn) progressFn(0);
      break;
    case UPLOAD_FILE_WRITE:
      if (!uploadAuthorized || !uploading) return;
      fwcheck::chunk(up.buf, up.currentSize, false);
      uploadWritten += up.currentSize;
      if (progressFn) progressFn(progress());
      break;
    case UPLOAD_FILE_END:
      if (!uploadAuthorized || !uploading) return;
      { static const uint8_t none = 0; fwcheck::chunk(&none, 0, true); }   // last checks, activates the new firmware
      uploadOk = fwcheck::ok();
      Serial.printf("[OTA] %s\n", uploadOk ? "ok" : fwcheck::error().c_str());
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
  // Firmware info for the update check in the browser, and the event log
  server.on("/api/system", HTTP_GET, []() {
    if (!auth()) return;
    JsonDocument d;
    fwcheck::info(d["fw"].to<JsonObject>());
    evlog::json(d["log"].to<JsonArray>());
    d["up"] = millis() / 1000;
    String out;
    serializeJson(d, out);
    server.send(200, "application/json", out);
  });
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
