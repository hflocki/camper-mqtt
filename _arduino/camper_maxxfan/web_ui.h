#pragma once
// Web UI served from flash. Edit the HTML between the R"HTML( ... )HTML" markers.

static const char WEB_UI[] = R"HTML(<!doctype html>
<html lang="de"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Camper MaxxFan</title>
<style>
:root{--bg:#0f1418;--card:#182129;--fg:#e6edf2;--mut:#8b9aa6;--acc:#2f9be0;--ok:#2fbf71;--bad:#e05252;--bd:#26323c}
@media (prefers-color-scheme:light){:root{--bg:#f2f5f7;--card:#fff;--fg:#17212a;--mut:#5d6b77;--bd:#dde3e8}}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:14px 16px 0;max-width:640px;margin:auto}h1{font-size:19px;margin:0 0 10px}
nav{display:flex;gap:4px;border-bottom:1px solid var(--bd)}
nav button{flex:1;background:none;border:0;color:var(--mut);padding:10px 4px;font:inherit;cursor:pointer;border-bottom:2px solid transparent}
nav button.on{color:var(--fg);border-color:var(--acc)}
main{max-width:640px;margin:auto;padding:14px 16px 40px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;margin-bottom:12px}
.card h2{font-size:15px;margin:0 0 10px;display:flex;justify-content:space-between;align-items:center}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.btn{background:var(--acc);color:#fff;border:0;border-radius:9px;padding:12px 6px;font:inherit;font-weight:600;cursor:pointer}
.btn.sec{background:transparent;color:var(--fg);border:1px solid var(--bd)}
.btn.bad{background:var(--bad)}.btn:active{transform:scale(.97)}
.pill{font-size:12px;font-weight:500;padding:2px 8px;border-radius:99px;background:var(--bd);color:var(--mut)}
.pill.ok{background:var(--ok);color:#fff}.pill.bad{background:var(--bad);color:#fff}
.row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid var(--bd)}.row:last-child{border:0}
label{display:block;margin:10px 0 4px;color:var(--mut);font-size:13px}
input,select{width:100%;padding:9px;border-radius:8px;border:1px solid var(--bd);background:var(--bg);color:var(--fg);font:inherit}
.chk{display:flex;gap:8px;align-items:center;margin-top:10px;color:var(--fg)}.chk input{width:auto}
.hide{display:none}.mut{color:var(--mut);font-size:13px}#msg{margin-top:8px}
progress{width:100%}
</style></head><body>
<header><h1>Camper MaxxFan</h1>
<nav><button data-t="live" class="on">Live</button><button data-t="cfg">Einstellungen</button><button data-t="sys">System</button></nav></header>
<main>
<section id="live">
  <div class="card"><h2>Status <span id="st" class="pill">…</span></h2>
    <button class="btn sec" style="width:100%" onclick="post('/api/sync')">Lüfter auf AUS synchronisieren</button>
    <div class="mut" style="margin-top:6px">Drückt Auto, wartet auf „Auto Hold“, dann Ein/Aus.</div></div>
  <div id="ports"></div>
</section>

<section id="cfg" class="hide"><div id="ctl"></div><form id="f">
  <div class="card"><h2>WLAN</h2>
    <label>Gerätename (Hostname)</label><input name="hostname">
    <label>SSID</label><input name="ssid">
    <label>Passwort (leer = unverändert)</label><input name="wpass" type="password">
    <div class="chk"><input type="checkbox" name="static_ip" id="sip"><span>Feste IP</span></div>
    <div id="ipbox"><label>IP</label><input name="ip"><label>Gateway</label><input name="gw">
    <label>Subnetz</label><input name="sn"><label>DNS (leer = Gateway)</label><input name="dns"></div>
    <label>Setup-AP-Passwort (min. 8 Zeichen, leer = unverändert)</label><input name="appass" type="password">
  </div>
  <div class="card"><h2>MQTT</h2>
    <label>Broker</label><input name="mhost" placeholder="192.168.x.x">
    <label>Port</label><input name="mport" type="number">
    <label>Benutzer</label><input name="muser"><label>Passwort (leer = unverändert)</label><input name="mpass" type="password">
    <label>Topic-Prefix</label><input name="prefix">
    <div class="chk"><input type="checkbox" name="disc"><span>Home-Assistant-Discovery</span></div>
  </div>
  <div class="card"><h2>MaxxFan</h2>
    <div class="chk"><input type="checkbox" name="p0"><span>Port 0 (RJ11) aktiv</span></div>
    <div class="chk"><input type="checkbox" name="p1"><span>Port 1 (RJ45) aktiv</span></div>
    <label>Tastendruck-Dauer (ms)</label><input name="pulse" type="number" min="50" max="2000">
    <label>Port für Synchronisation</label><select name="syncport"><option value="0">Port 0 (RJ11)</option><option value="1">Port 1 (RJ45)</option></select>
  </div>
  <div class="card"><h2>Web-Login</h2>
    <label>Benutzer (leer = kein Login)</label><input name="wuser">
    <label>Passwort (leer = unverändert)</label><input name="wpw" type="password">
  </div>
  <button class="btn" style="width:100%" type="submit">Speichern &amp; Neustart</button><div id="msg" class="mut"></div>
</form></section>

<section id="sys" class="hide">
  <div class="card"><h2>Info</h2><div id="info"></div></div>
  <div class="card"><h2>Firmware-Update</h2>
    <div class="mut">Arduino IDE: Sketch → Kompilierte Binärdatei exportieren, dann die .bin hier hochladen.</div>
    <label>Datei</label><input type="file" id="fw" accept=".bin">
    <progress id="pr" value="0" max="100" class="hide"></progress>
    <button class="btn" style="width:100%;margin-top:10px" onclick="upload()">Hochladen</button><div id="umsg" class="mut"></div></div>
  <div class="card"><h2>Gerät</h2><div class="grid" style="grid-template-columns:1fr 1fr">
    <button class="btn sec" onclick="post('/api/reboot')">Neustart</button>
    <button class="btn bad" onclick="if(confirm('Alle Einstellungen löschen?'))post('/api/factory')">Werkseinstellungen</button></div></div>
</section>
</main>
<script>
const $=s=>document.querySelector(s);
const BTN=[["auto","Auto"],["direction","Richtung"],["slower","Langsamer"],["faster","Schneller"]];
let built="";
document.querySelectorAll("nav button").forEach(b=>b.onclick=()=>{
  document.querySelectorAll("nav button").forEach(x=>x.classList.toggle("on",x===b));
  ["live","cfg","sys"].forEach(id=>$("#"+id).classList.toggle("hide",id!==b.dataset.t));
  if(b.dataset.t==="cfg")loadCfg();
});
function post(url,data){return fetch(url,{method:"POST",body:new URLSearchParams(data||{})}).then(()=>setTimeout(poll,300))}
function buildPorts(s){
  const key=s.ports.map(p=>p.enabled).join();
  if(key===built)return; built=key;
  $("#ports").innerHTML=s.ports.map((p,i)=>!p.enabled?"":`<div class="card"><h2>${p.label} · Port ${i}
    <span class="pill" id="h${i}">Auto Hold</span></h2>
    <button class="btn" style="width:100%;margin-bottom:8px" onclick="post('/api/press',{port:${i},btn:'on_off'})">Ein / Aus</button>
    <div class="grid" style="grid-template-columns:1fr 1fr">${BTN.map(b=>`<button class="btn" onclick="post('/api/press',{port:${i},btn:'${b[0]}'})">${b[1]}</button>`).join("")}</div>
    <div class="row" style="margin-top:8px"><span>Stufe setzen</span>
    <span><select id="s${i}" style="width:auto">${[1,2,3,4,5,6,7,8,9,10].map(n=>`<option>${n}</option>`).join("")}</select>
    <button class="btn sec" onclick="post('/api/speed',{port:${i},level:$('#s${i}').value})">Setzen</button></span></div>
    </div>`).join("")
    || '<div class="card mut">Kein Port aktiv – unter Einstellungen einschalten.</div>';
  $("#ctl").innerHTML=s.ports.map((p,i)=>!p.enabled?"":`<div class="card"><h2>Bedienteil ${p.label} · Port ${i}</h2>
    <div class="row"><span>Controller-LED</span>
    <button class="btn sec" type="button" id="l${i}" onclick="post('/api/led',{port:${i},on:this.dataset.on==='1'?0:1})">–</button></div>
    <div class="row"><span>Piep (Verbindungstest)</span>
    <button class="btn sec" type="button" onclick="post('/api/press',{port:${i},btn:'beep'})">Piep</button></div></div>`).join("");
}
function poll(){fetch("/api/state").then(r=>r.json()).then(s=>{
  buildPorts(s);
  s.ports.forEach((p,i)=>{if(!p.enabled)return;
    const h=$("#h"+i);h.className="pill"+(p.hold?" ok":"");h.textContent="Auto Hold "+(p.hold?"an":"aus");
    const l=$("#l"+i);l.dataset.on=p.led?"1":"0";l.textContent=p.led?"An":"Aus";});
  const st=$("#st");
  if(!s.expander){st.className="pill bad";st.textContent="Port-Expander fehlt"}
  else if(s.sync){st.className="pill";st.textContent="Synchronisiere…"}
  else{st.className="pill "+(s.mqtt?"ok":"");st.textContent=s.mqtt?"MQTT verbunden":"MQTT getrennt"}
  $("#info").innerHTML=[["Firmware",s.fw],["IP",s.ip],["WLAN",s.sta?s.rssi+" dBm":"nicht verbunden"],
    ["Setup-AP",s.ap?"aktiv":"aus"],["MQTT",s.mqtt?"verbunden":"getrennt"],["Port-Expander",s.expander?"OK":"nicht gefunden"],
    ["Laufzeit",Math.floor(s.uptime/3600)+" h "+Math.floor(s.uptime%3600/60)+" min"]]
    .map(r=>`<div class="row"><span class="mut">${r[0]}</span><span>${r[1]}</span></div>`).join("");
}).catch(()=>{$("#st").className="pill bad";$("#st").textContent="offline"})}
function loadCfg(){fetch("/api/config").then(r=>r.json()).then(c=>{
  const f=$("#f");for(const k in c){const el=f.elements[k];if(!el)continue;
    if(el.type==="checkbox")el.checked=!!c[k];else el.value=c[k];}
  f.elements.appass.placeholder="AP: "+c.apssid;sipToggle();})}
function sipToggle(){$("#ipbox").classList.toggle("hide",!$("#sip").checked)}
$("#sip").onchange=sipToggle;
$("#f").onsubmit=e=>{e.preventDefault();const f=e.target,d={};
  for(const el of f.elements){if(!el.name)continue;d[el.name]=el.type==="checkbox"?(el.checked?"1":"0"):el.value;}
  fetch("/api/config",{method:"POST",body:new URLSearchParams(d)}).then(r=>{
    $("#msg").textContent=r.ok?"Gespeichert – Neustart…":"Fehler beim Speichern";});};
function upload(){const file=$("#fw").files[0];if(!file)return;
  const fd=new FormData();fd.append("update",file);const x=new XMLHttpRequest();
  $("#pr").classList.remove("hide");
  x.upload.onprogress=e=>{if(e.lengthComputable)$("#pr").value=e.loaded/e.total*100};
  x.onload=()=>{$("#umsg").textContent=x.status===200?"Erfolgreich – Neustart…":"Fehler: "+x.responseText};
  x.open("POST","/api/update");x.send(fd);}
poll();setInterval(poll,2000);
</script></body></html>)HTML";
