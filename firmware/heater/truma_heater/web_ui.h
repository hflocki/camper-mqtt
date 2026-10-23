// Single page web UI (served from flash, no filesystem upload needed).
// Look and tab layout follow camper-tanklevel.
#pragma once

static const char INDEX_HTML[] = R"rawliteral(<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Camper Heater</title>
<style>
:root{--bg:#0f1419;--bg2:#1a2332;--card:#1e2d3d;--input:#0d1b2a;--heat:#ff7a3d;--heat-glow:rgba(255,122,61,.3);--water:#00d4aa;
--warn:#ffb800;--danger:#ff4757;--ok:#00d4aa;--ink:#e8f0f8;--mute:#8899aa;--dim:#556677;--line:#2a3a4a;--r:12px}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);color:var(--ink);min-height:100vh;overflow-x:hidden}
.header{background:var(--bg2);padding:16px 20px;border-bottom:1px solid var(--line);display:flex;align-items:center;justify-content:space-between;gap:8px}
.header h1{font-size:1.15rem;font-weight:600;color:var(--heat);white-space:nowrap}
.dots{display:flex;gap:8px;align-items:center;font-size:.65rem;color:var(--dim)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--dim);display:inline-block;margin-right:4px}
.dot.on{background:var(--ok);box-shadow:0 0 8px rgba(0,212,170,.3)}
.dot.bad{background:var(--danger)}
.tabs{display:flex;background:var(--bg2);border-bottom:1px solid var(--line);overflow-x:auto}
.tab{flex:1;min-width:0;padding:8px 1px 7px;text-align:center;font-size:.68rem;font-weight:500;line-height:1.2;display:flex;flex-direction:column;align-items:center;gap:3px;
color:var(--mute);cursor:pointer;border-bottom:2px solid transparent;transition:all .2s;white-space:nowrap}
.tab .ico{font-size:1.05rem;line-height:1}
.tab.on{color:var(--heat);border-bottom-color:var(--heat);background:rgba(255,122,61,.06)}
.page{display:none;padding:16px}.page.on{display:block}
.card{background:var(--card);border-radius:var(--r);padding:16px;margin-bottom:12px;border:1px solid var(--line)}
.card.warn{border-color:var(--danger)}
.ct{font-size:.75rem;text-transform:uppercase;letter-spacing:.5px;color:var(--mute);margin-bottom:12px}
.ct.heat{color:var(--heat)}.ct.water{color:var(--water)}.ct.bad{color:var(--danger)}
.big{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;flex-wrap:wrap}
.temp{font-size:3rem;font-weight:700;line-height:1;font-variant-numeric:tabular-nums}
.temp small{font-size:1.1rem;color:var(--dim);font-weight:500}
.sub{color:var(--mute);font-size:.85rem}
.sub b{color:var(--ink)}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:14px}
.row .lbl{font-size:.8rem;color:var(--mute);min-width:52px}
.seg{display:inline-flex;background:var(--input);border:1px solid var(--line);border-radius:8px;overflow:hidden}
.seg button{border:0;background:transparent;color:var(--ink);padding:9px 14px;font:inherit;font-size:.85rem;cursor:pointer;border-right:1px solid var(--line)}
.seg button:last-child{border-right:0}
.seg button.on{background:var(--heat);color:var(--bg);font-weight:600}
.seg.water button.on{background:var(--water)}
.stepper{display:inline-flex;align-items:center;gap:10px;margin-left:auto}
.stepper .btn{width:42px;padding:8px 0;font-size:1.1rem}
.stepper b{font-size:1.3rem;min-width:64px;text-align:center;font-variant-numeric:tabular-nums}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:10px 16px;border-radius:8px;font-size:.85rem;font-weight:600;cursor:pointer;border:1px solid var(--line);background:var(--input);color:var(--ink)}
.btn.primary{background:var(--heat);border-color:var(--heat);color:var(--bg)}
.btn.save{background:var(--ok);border-color:var(--ok);color:var(--bg);width:100%}
.btn.danger{background:rgba(255,71,87,.15);color:var(--danger);border-color:var(--danger);width:100%}
.btn:disabled{opacity:.4;cursor:not-allowed}
.btns{display:flex;flex-direction:column;gap:8px;margin-top:4px}
.info{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid var(--line);font-size:.85rem}
.info:last-child{border-bottom:none}
.info span:first-child{color:var(--mute)}
.info span:last-child{text-align:right;word-break:break-word}
.fg{margin-bottom:12px}
.fg label{display:block;font-size:.75rem;color:var(--mute);margin-bottom:4px}
.fg input,.fg select,select.inl{width:100%;padding:10px 12px;background:var(--input);border:1px solid var(--line);border-radius:8px;color:var(--ink);font-size:.9rem;outline:none}
.fg input:focus,.fg select:focus{border-color:var(--heat)}
select.inl{width:auto;padding:8px 10px}
.hint{font-size:.7rem;color:var(--dim);margin-top:4px;line-height:1.4}
.cols{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}
.sw{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:4px 0 8px}
.sw .t{font-size:.85rem}
.toggle{position:relative;width:46px;height:26px;flex-shrink:0}
.toggle input{opacity:0;width:0;height:0}
.toggle span{position:absolute;inset:0;background:var(--line);border-radius:13px;cursor:pointer;transition:.2s}
.toggle span::before{content:'';position:absolute;width:20px;height:20px;left:3px;top:3px;background:var(--ink);border-radius:50%;transition:.2s}
.toggle input:checked+span{background:var(--ok)}
.toggle.heat input:checked+span{background:var(--heat)}
.toggle input:checked+span::before{transform:translateX(20px)}
.toggle input:disabled+span{opacity:.4;cursor:not-allowed}
details summary{cursor:pointer;font-size:.8rem;color:var(--mute);margin:4px 0 10px}
pre{white-space:pre-wrap;word-break:break-word;font:12px ui-monospace,Menlo,monospace;max-height:340px;overflow:auto;background:var(--input);padding:10px;border-radius:8px;margin-top:10px}
code{font-family:ui-monospace,Menlo,monospace}
input[type=file]{font-size:.8rem;color:var(--mute);width:100%}
.hide{display:none!important}
.toast{position:fixed;bottom:80px;left:50%;transform:translateX(-50%) translateY(100px);background:var(--card);border:1px solid var(--ok);border-radius:8px;padding:10px 20px;font-size:.85rem;color:var(--ok);opacity:0;transition:all .3s;z-index:999;white-space:nowrap}
.toast.show{transform:translateX(-50%) translateY(0);opacity:1}
@media (max-width:374px){.header{padding:14px 12px}.header h1{font-size:1rem}.dots{gap:4px;font-size:.6rem}.cols{grid-template-columns:1fr}}
@media (min-width:600px){body{display:flex;justify-content:center}.wrap{width:100%;max-width:520px}}
</style>
</head>
<body>
<div class="wrap">
<div class="header">
  <h1>🚐 Camper Heater</h1>
  <div class="dots"><span><span class="dot" id="d-lin"></span>CP Plus</span><span><span class="dot" id="d-wifi"></span>WiFi</span><span><span class="dot" id="d-mqtt"></span>MQTT</span></div>
</div>
<div class="tabs">
  <div class="tab on" data-tab="heat"><span class="ico">🔥</span><span>Heizung</span></div>
  <div class="tab" data-tab="timer"><span class="ico">⏱️</span><span>Timer</span></div>
  <div class="tab" data-tab="mqtt"><span class="ico">⚙️</span><span>MQTT</span></div>
  <div class="tab" data-tab="wifi"><span class="ico">📶</span><span>WiFi</span></div>
  <div class="tab" data-tab="sys"><span class="ico">🛠️</span><span>System</span></div>
</div>

<div class="page on" id="tab-heat">
  <div id="nolink" class="card warn hide"><div class="ct bad">Keine Verbindung zum CP Plus</div><div id="nolink-text" class="sub"></div></div>
  <div class="card">
    <div class="ct heat">Raum</div>
    <div class="big">
      <div class="temp"><span id="room">--</span><small> °C</small></div>
      <div class="sub" style="text-align:right">Ziel <b id="target">--</b><br><span id="status">--</span></div>
    </div>
    <div class="row">
      <span class="lbl">Heizen</span><label class="toggle heat"><input type="checkbox" id="room-on"><span></span></label>
      <div class="stepper"><button class="btn" id="minus">−</button><b id="setp">--</b><button class="btn" id="plus">+</button></div>
    </div>
    <div class="row"><span class="lbl">Lüfter</span><div class="seg" id="seg-fan"><button data-v="eco">Eco</button><button data-v="high">High</button><button data-v="boost" class="hide" id="fan-boost">Boost</button></div></div>
  </div>
  <div class="card">
    <div class="ct water">Warmwasser</div>
    <div class="temp"><span id="water">--</span><small> °C</small></div>
    <div class="row"><div class="seg water" id="seg-water"><button data-v="off">Aus</button><button data-v="eco">Eco 40°</button><button data-v="high">Hot 60°</button><button data-v="boost" class="hide" id="water-boost">Boost</button></div></div>
  </div>
  <div class="card">
    <div class="ct">Freibrennen</div>
    <div class="sub" id="decoke-text">Maximale Temperatur mit Lüfter High, danach werden die vorherigen Einstellungen wiederhergestellt.</div>
    <div class="row"><button class="btn primary" id="decoke">Starten</button><span class="sub">Dauer</span><select class="inl" id="decoke-min"></select></div>
  </div>
  <div class="card">
    <div class="ct">Details</div>
    <div class="info"><span>Betriebsstatus</span><span id="opstat">--</span></div>
    <div class="info"><span>Fehlercode</span><span id="err">--</span></div>
    <div class="info"><span>Timer</span><span id="timer">--</span></div>
    <div class="info"><span>Uhr CP Plus</span><span id="clock">--</span></div>
    <div class="row"><button class="btn" id="sync">🕒 Uhrzeit an CP Plus senden</button></div>
  </div>
</div>

<div class="page" id="tab-timer">
  <div class="card">
    <div class="ct heat">Timer des CP Plus</div>
    <div class="sub" id="timer-text">--</div>
    <div class="hint">Das CP Plus führt den Timer selbst aus, der ESP muss dafür nicht laufen.</div>
  </div>
  <div class="card">
    <div class="ct">Neuer Timer</div>
    <div class="cols">
      <div class="fg"><label>Start</label><input type="time" id="t-start"></div>
      <div class="fg"><label>Ende</label><input type="time" id="t-stop"></div>
      <div class="fg"><label>Temperatur °C</label><input type="number" id="t-temp" min="5" max="30" step="1"></div>
      <div class="fg"><label>Lüfter</label><select id="t-fan"><option value="eco">Eco</option><option value="high">High</option><option value="boost" id="t-boost">Boost</option></select></div>
    </div>
    <div class="fg"><label>Warmwasser</label><select id="t-water"><option value="">unverändert</option><option value="off">Aus</option><option value="eco">Eco 40°</option><option value="high">Hot 60°</option></select></div>
    <div class="btns"><button class="btn save" id="t-set">⏱️ Timer setzen</button><button class="btn danger" id="t-off">Timer aus</button></div>
  </div>
</div>

<div class="page" id="tab-mqtt">
  <div class="card" id="f-mqtt">
    <div class="ct">MQTT-Broker</div>
    <div class="cols">
      <div class="fg"><label>Broker</label><input data-cfg="mqtt_host" placeholder="192.168.x.y"></div>
      <div class="fg"><label>Port</label><input data-cfg="mqtt_port" type="number"></div>
      <div class="fg"><label>Benutzer</label><input data-cfg="mqtt_user"></div>
      <div class="fg"><label>Passwort</label><input data-cfg="mqtt_pass" type="password" placeholder="(unverändert)" autocomplete="new-password"></div>
    </div>
    <div class="fg"><label>Basis-Topic</label><input data-cfg="mqtt_base"></div>
    <div class="sw"><span class="t">MQTT-Passwort löschen</span><label class="toggle"><input type="checkbox" data-cfg="clear_mqtt_pass"><span></span></label></div>
    <div class="ct" style="margin-top:16px">Home Assistant</div>
    <div class="sw"><span class="t">Auto-Discovery</span><label class="toggle"><input type="checkbox" data-cfg="ha_discovery"><span></span></label></div>
    <div class="fg"><label>Discovery-Prefix</label><input data-cfg="ha_prefix"></div>
    <div class="fg"><label>Log per MQTT (Topic &lt;Basis-Topic&gt;/log)</label>
      <select data-cfg="mqtt_log_level"><option value="4">Aus</option><option value="0">Nur Fehler</option><option value="1">Warnungen + Fehler</option><option value="2">Info</option><option value="3">Debug (viel Verkehr)</option></select></div>
  </div>
  <div class="btns"><button class="btn save" data-save="f-mqtt">💾 Speichern</button></div>
</div>

<div class="page" id="tab-wifi">
  <div class="card">
    <div class="ct">Verbindungsstatus</div>
    <div class="info"><span>Status</span><span id="w-state">--</span></div>
    <div class="info"><span>IP</span><span id="w-ip">--</span></div>
    <div class="info"><span>RSSI</span><span id="w-rssi">--</span></div>
    <div class="info"><span>Firmware</span><span id="w-fw">--</span></div>
    <div class="info"><span>Laufzeit</span><span id="w-up">--</span></div>
    <div class="info"><span>Letzter Neustart</span><span id="w-reset">--</span></div>
    <div class="info"><span>WLAN-Abbrüche</span><span id="w-drops">--</span></div>
    <div class="info"><span>Letzter Abbruchgrund</span><span id="w-reason">--</span></div>
  </div>
  <div id="f-wifi">
  <div class="card">
    <div class="ct">WLAN-Verbindung</div>
    <div class="fg"><label>SSID</label><input data-cfg="ssid" autocapitalize="off" autocorrect="off"></div>
    <div class="fg"><label>Passwort</label><input data-cfg="wifi_pass" type="password" placeholder="(unverändert)" autocomplete="new-password"></div>
    <div class="fg"><label>Sendeleistung</label>
      <select data-cfg="wifi_tx_power"><option value="34">8,5 dBm</option><option value="44">11 dBm</option><option value="52">13 dBm</option><option value="60">15 dBm</option><option value="70">17,5 dBm (Standard)</option><option value="78">19,5 dBm (Maximum)</option></select>
      <div class="hint">Bricht die Verbindung oft ab, zuerst 8,5 dBm probieren. Der ESP32-C3 Super Mini läuft mit weniger Leistung stabiler. Nur 2,4-GHz-WLAN.</div></div>
    <div class="fg"><label>Hostname</label><input data-cfg="hostname"></div>
    <details><summary>Feste IP (optional, leer = DHCP)</summary>
      <div class="cols">
        <div class="fg"><label>IP</label><input data-cfg="static_ip" placeholder="192.168.x.y"></div>
        <div class="fg"><label>Gateway</label><input data-cfg="gateway"></div>
        <div class="fg"><label>Subnetz</label><input data-cfg="subnet" placeholder="255.255.255.0"></div>
        <div class="fg"><label>DNS</label><input data-cfg="dns"></div>
      </div>
    </details>
  </div>
  <div class="card">
    <div class="ct">Access Point</div>
    <div class="fg"><label>Name (SSID)</label><input data-cfg="ap_ssid"></div>
    <div class="fg"><label>Passwort (min. 8 Zeichen)</label><input data-cfg="ap_pass" type="password" placeholder="(unverändert)" autocomplete="new-password"></div>
    <div class="sw"><div><div class="t">Access Point immer an</div>
      <div class="hint">Aus = AP nur als Notfall, wenn das WLAN 60 s nicht erreichbar ist. Stabilere WLAN-Verbindung; das Gerät ist dann über seine IP im WLAN oder <code id="w-mdns">truma-heater.local</code> erreichbar.</div></div>
      <label class="toggle"><input type="checkbox" data-cfg="ap_always"><span></span></label></div>
  </div>
  </div>
  <div class="btns"><button class="btn save" data-save="f-wifi">💾 Speichern &amp; Verbinden</button><button class="btn danger" id="reboot">🔄 Neustart</button></div>
</div>

<div class="page" id="tab-sys">
  <div class="card">
    <div class="ct">LIN-Bus</div>
    <div class="info"><span>LIN-Verkehr</span><span id="s-traffic">--</span></div>
    <div class="info"><span>CP Plus angemeldet</span><span id="s-cp">--</span></div>
    <div class="info"><span>CP Plus</span><span id="s-cpv">--</span></div>
    <div class="info"><span>Heizung</span><span id="s-hv">--</span></div>
    <div class="info"><span>Frames ok / CRC-Fehler</span><span id="s-frames">--</span></div>
    <div class="info"><span>Abgelehnte Befehle</span><span id="s-ack">--</span></div>
    <div class="info"><span>Freier Speicher</span><span id="s-heap">--</span></div>
    <div class="sw" style="margin-top:12px"><div><div class="t">LIN-Sniffer</div><div class="hint">Schreibt alle geänderten Frames ins Log (Fehlersuche).</div></div>
      <label class="toggle"><input type="checkbox" id="sniff"><span></span></label></div>
    <div class="hint">Anmeldung am CP Plus: Einstellungen → RESET → PR SET. Danach steht im INDEX-Menü ein dritter Eintrag T23.70.0. Eine echte iNet Box darf nicht gleichzeitig am Bus hängen.</div>
  </div>
  <div class="card" id="f-sys">
    <div class="ct">Zeit &amp; Zugriff</div>
    <div class="cols">
      <div class="fg"><label>NTP-Server</label><input data-cfg="ntp_server"></div>
      <div class="fg"><label>Zeitzone (POSIX)</label><input data-cfg="tz"></div>
      <div class="fg"><label>Web-Benutzer (leer = offen)</label><input data-cfg="web_user"></div>
      <div class="fg"><label>Web-Passwort</label><input data-cfg="web_pass" type="password" placeholder="(unverändert)" autocomplete="new-password"></div>
    </div>
    <div class="sw"><span class="t">Web-Login entfernen</span><label class="toggle"><input type="checkbox" data-cfg="clear_web_pass"><span></span></label></div>
    <div class="btns"><button class="btn save" data-save="f-sys">💾 Speichern</button></div>
  </div>
  <div class="card">
    <div class="ct">Firmware-Update</div>
    <input type="file" id="fw" accept=".bin">
    <div class="row"><button class="btn primary" id="upload">⬆️ Hochladen</button><span class="sub" id="up-status"></span></div>
  </div>
  <div class="card">
    <div class="ct">Log</div>
    <div class="row" style="margin-top:0">
      <select class="inl" id="loglvl"><option value="0">Fehler</option><option value="1">Warnung</option><option value="2">Info</option><option value="3">Debug</option></select>
      <span class="sub">Live</span><label class="toggle"><input type="checkbox" id="loglive" checked><span></span></label>
      <button class="btn" id="logclear">Leeren</button><button class="btn" id="logsave">Speichern</button>
    </div>
    <pre id="log"></pre>
    <div class="hint">Läuft mit, solange dieser Reiter offen ist. Ohne USB nutzbar – auch per MQTT unter <code id="logtopic">camper/heater/log</code>.</div>
  </div>
</div>
<div class="toast" id="toast"></div>
</div>
<script>
const $=id=>document.getElementById(id);
let st=null,localSetp=null,setpTimer=null,timerEdit=false;
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('show');setTimeout(()=>e.classList.remove('show'),2500)}
// Two-tap confirmation. confirm() is blocked silently in the captive portal windows of phones.
function ask(el,txt,fn){if(el.dataset.arm){delete el.dataset.arm;clearTimeout(el._t);el.textContent=el.dataset.orig;fn();return}
  el.dataset.orig=el.textContent;el.dataset.arm='1';el.textContent=txt;
  el._t=setTimeout(()=>{if(el.dataset.arm){delete el.dataset.arm;el.textContent=el.dataset.orig}},4000)}
async function cmd(a,v){const b=new URLSearchParams({a,v:v??''});try{const r=await fetch('/api/cmd',{method:'POST',body:b});if(!r.ok)throw 0;setTimeout(load,400)}catch(e){toast('Befehl fehlgeschlagen')}}
function seg(id,val){document.querySelectorAll('#'+id+' button').forEach(b=>b.classList.toggle('on',b.dataset.v===val))}
function setTxt(id,t){$(id).textContent=t}
function dot(id,s){$(id).className='dot'+(s===true?' on':s===false?' bad':'')}
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+' d ':'')+h+' h '+m+' min'}
function render(){
  const s=st,h=s.heater,l=s.lin,y=s.system;
  dot('d-lin',l.cpplus);dot('d-wifi',y.wifi);dot('d-mqtt',y.mqtt);
  const nl=$('nolink');
  if(!l.cpplus||!h.valid){nl.classList.remove('hide');
    setTxt('nolink-text',!l.traffic?'Kein LIN-Verkehr erkannt. Verkabelung, 12 V Versorgung und Masse prüfen.':(!l.cpplus?'LIN-Verkehr ist da, aber das CP Plus fragt dieses Gerät nicht ab. Einmal RESET → PR SET am CP Plus ausführen.':'Warte auf Status der Heizung …'));
  }else nl.classList.add('hide');
  setTxt('room',h.room!==undefined?h.room.toFixed(1):'--');
  setTxt('water',h.water!==undefined?h.water.toFixed(0):'--');
  setTxt('target',h.on?h.target_room+' °C':'aus');
  setTxt('status',h.valid?(h.action==='heating'?'heizt':h.vent>0?'Lüften Stufe '+h.vent:h.action==='fan'?'Lüfter läuft':(h.on?'Temperatur erreicht':'aus')):'--');
  if(document.activeElement!==$('room-on'))$('room-on').checked=!!h.on;
  if(localSetp===null)setTxt('setp',(+h.setpoint).toFixed(0)+' °C');
  seg('seg-fan',h.fan);seg('seg-water',h.water_mode||'');
  $('fan-boost').classList.toggle('hide',!h.boost);$('water-boost').classList.toggle('hide',!h.water_boost);$('t-boost').classList.toggle('hide',!h.boost);
  const d=s.decoke;if(!$('decoke').dataset.arm)$('decoke').textContent=d.active?'Beenden':'Starten';
  setTxt('decoke-text',d.active?('Läuft noch '+Math.ceil(d.remaining/60)+' min.'):'Maximale Temperatur mit Lüfter High, danach werden die vorherigen Einstellungen wiederhergestellt.');
  if(document.activeElement!==$('decoke-min'))$('decoke-min').value=d.minutes;
  setTxt('opstat',h.valid?h.status+' ('+h.status_text+')':'--');
  setTxt('err',h.valid?(h.error?h.error:'keiner'):'--');
  setTxt('timer',s.timer??'--');setTxt('clock',s.cpplus_clock??'--');
  const tc=s.timer_cfg;
  setTxt('timer-text',tc?(tc.active?'Aktiv '+tc.start+'–'+tc.stop+', '+tc.temp+' °C, Lüfter '+tc.fan:'Aus (gespeichert '+tc.start+'–'+tc.stop+')'):'Noch kein Timer-Status vom CP Plus.');
  if(tc&&!timerEdit){$('t-start').value=tc.start;$('t-stop').value=tc.stop;$('t-temp').value=tc.temp>=5?tc.temp:Math.round(+h.setpoint);
    $('t-fan').value=(tc.fan==='off'?h.fan:tc.fan)||'eco'}
  setTxt('s-traffic',l.traffic?'ja':'nein');setTxt('s-cp',l.cpplus?'ja':'nein');
  setTxt('s-cpv',l.cpplus_version||'--');setTxt('s-hv',l.heater_version||'--');
  setTxt('s-frames',l.frames+' / '+l.crc_errors);setTxt('s-ack',l.ack_errors);setTxt('s-heap',Math.round(y.heap/1024)+' kB');
  if(document.activeElement!==$('sniff'))$('sniff').checked=!!l.sniffer;
  setTxt('w-state',y.wifi_state+(y.wifi&&y.ssid?' ('+y.ssid+')':''));setTxt('w-ip',y.ip+(y.ap?' (Access Point)':''));
  setTxt('w-rssi',y.wifi?y.rssi+' dBm':'--');setTxt('w-fw',y.fw);setTxt('w-up',fmtUp(y.uptime));
  setTxt('w-reset',y.reset_reason);setTxt('w-drops',y.wifi_drops);setTxt('w-reason',y.wifi_reason);
  const ctl=h.valid&&l.cpplus;document.querySelectorAll('#tab-heat .btn,#tab-heat .seg button,#tab-timer .btn').forEach(b=>b.disabled=!ctl);$('room-on').disabled=!ctl;
}
async function load(){try{const r=await fetch('/api/state');st=await r.json();render()}catch(e){}}
let logNext=0,logLines=[],logTimer=null;
async function loadLog(){try{const r=await fetch('/api/log?since='+logNext);const j=await r.json();logNext=j.next;
  if(j.lines){const el=$('log'),atEnd=el.scrollTop+el.clientHeight>=el.scrollHeight-20;
    logLines=logLines.concat(j.lines.replace(/\n$/,'').split('\n')).slice(-400);el.textContent=logLines.join('\n');if(atEnd)el.scrollTop=1e9}}catch(e){}}
function logLive(){clearInterval(logTimer);logTimer=null;if($('loglive').checked&&$('tab-sys').classList.contains('on'))logTimer=setInterval(loadLog,2000)}
async function loadCfg(){const r=await fetch('/api/config');const c=await r.json();
  document.querySelectorAll('[data-cfg]').forEach(e=>{const v=c[e.dataset.cfg];if(e.type==='password')return;
    if(e.type==='checkbox')e.checked=!!v;else if(v!==undefined)e.value=v});
  $('loglvl').value=c.log_level;setTxt('logtopic',c.mqtt_base+'/log');setTxt('w-mdns',(c.hostname||'truma-heater')+'.local')}
// Each tab saves only its own fields. Empty password fields keep the stored value. The device restarts.
async function save(id,btn){const p=new URLSearchParams();
  for(const e of $(id).querySelectorAll('[data-cfg]')){const k=e.dataset.cfg;
    if(e.type==='checkbox')p.append(k,e.checked?'1':'0');else if(e.type==='password'){if(e.value)p.append(k,e.value)}else p.append(k,e.value.trim())}
  const ap=p.get('ap_pass');if(ap&&ap.length<8){toast('AP-Passwort: mindestens 8 Zeichen');return}
  btn.disabled=true;
  try{const r=await fetch('/api/config',{method:'POST',body:p});if(!r.ok)throw 0;toast('✓ Gespeichert – Neustart …');btn.textContent='✓ Gespeichert – Gerät startet neu'}
  catch(e){toast('Fehler beim Speichern');btn.disabled=false}}
document.querySelectorAll('[data-save]').forEach(b=>b.onclick=()=>save(b.dataset.save,b));
document.querySelectorAll('.tab').forEach(t=>t.onclick=()=>{document.querySelectorAll('.tab').forEach(x=>x.classList.toggle('on',x===t));
  document.querySelectorAll('.page').forEach(p=>p.classList.toggle('on',p.id==='tab-'+t.dataset.tab));if(t.dataset.tab==='sys')loadLog();logLive()});
$('room-on').onchange=e=>cmd('room_on',e.target.checked?'1':'0');
document.querySelectorAll('#seg-fan button').forEach(b=>b.onclick=()=>cmd('fan',b.dataset.v));
document.querySelectorAll('#seg-water button').forEach(b=>b.onclick=()=>cmd('water',b.dataset.v));
function step(d){const base=localSetp??Math.round(+st.heater.setpoint);localSetp=Math.min(30,Math.max(5,base+d));setTxt('setp',localSetp+' °C');
  clearTimeout(setpTimer);setpTimer=setTimeout(()=>{cmd('setpoint',localSetp);setTimeout(()=>localSetp=null,1500)},900)}
$('minus').onclick=()=>step(-1);$('plus').onclick=()=>step(1);
$('decoke').onclick=e=>{const on=!st.decoke.active;if(on)ask(e.target,'Wirklich starten?',()=>cmd('decoke','1'));else cmd('decoke','0')};
for(let m=5;m<=120;m+=5){const o=document.createElement('option');o.value=m;o.textContent=m+' min';$('decoke-min').appendChild(o)}
$('decoke-min').onchange=e=>cmd('decoke_minutes',e.target.value);
['t-start','t-stop','t-temp','t-fan','t-water'].forEach(id=>$(id).oninput=()=>timerEdit=true);
$('t-set').onclick=()=>{const a=$('t-start').value,b=$('t-stop').value;if(!a||!b||a===b){toast('Start und Ende prüfen');return}
  cmd('timer',a+'-'+b+','+$('t-temp').value+','+$('t-fan').value+($('t-water').value?','+$('t-water').value:''));timerEdit=false;toast('Timer wird gesendet')};
$('t-off').onclick=()=>{cmd('timer','off');timerEdit=false};
$('sync').onclick=()=>{cmd('sync_clock');toast('Uhrzeit wird gesendet')};
$('reboot').onclick=e=>ask(e.target,'Wirklich neu starten? Nochmal tippen',async()=>{await fetch('/api/reboot',{method:'POST'});toast('Neustart …')});
$('loglive').onchange=logLive;
$('logclear').onclick=()=>{logLines=[];$('log').textContent=''};
$('logsave').onclick=()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([logLines.join('\n')],{type:'text/plain'}));a.download='camper-heater-log.txt';a.click()};
$('loglvl').onchange=e=>cmd('log_level',e.target.value);
$('sniff').onchange=e=>cmd('sniffer',e.target.checked?'1':'0');
$('upload').onclick=async()=>{const f=$('fw').files[0];if(!f)return;const fd=new FormData();fd.append('firmware',f,f.name);setTxt('up-status','Lade hoch …');
  try{const r=await fetch('/update',{method:'POST',body:fd});setTxt('up-status',r.ok?'Fertig, Neustart …':'Fehlgeschlagen')}catch(e){setTxt('up-status','Fehlgeschlagen')}};
load();setInterval(load,2000);loadCfg().catch(()=>{});
</script>
</body>
</html>)rawliteral";
