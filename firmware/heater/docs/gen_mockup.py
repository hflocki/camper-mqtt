#!/usr/bin/env python3
"""Build a clickable mockup of the web UI (docs/mockup/truma-heater-mockup.html).

Takes the real page from truma_heater/web_ui.h and replaces the device API with simulated
data, so the UI can be shown without an ESP32. Standard library only (no venv needed).
Run after changing web_ui.h:  python3 docs/gen_mockup.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "truma_heater", "web_ui.h")
OUT = os.path.join(HERE, "mockup", "truma-heater-mockup.html")

SHIM = r'''<script>
// Mockup: simulated device API (no ESP32 needed). Buttons change the simulated state.
(function(){
const t0=Date.now();
const S={system:{fw:"0.9.0-mock",ip:"192.168.1.50",rssi:-61,ap:false,wifi:true,mqtt:true,uptime:0,heap:182000,wifi_state:"Verbunden",ssid:"MeinWLAN",wifi_drops:0,wifi_reason:"-",reset_reason:"Einschalten"},
 lin:{traffic:true,cpplus:true,init:true,fault:false,frames:0,crc_errors:0,sniffer:false,ack_errors:0,pending:false,cpplus_version:"23.70.0",heater_version:"06.00.00"},
 heater:{valid:true,room:17.4,water:14,target_room:21,water_mode:"off",heating_mode:"eco",energy_mix:"gas",status:7,status_text:"on",error:0,on:true,action:"heating",vent:0,setpoint:21,fan:"eco",boost:false,water_boost:false},
 decoke:{active:false,remaining:0,minutes:30},
 timer:"off",timer_cfg:{active:false,start:"22:00",stop:"06:30",temp:20,fan:"eco",water:"off"},cpplus_clock:"12:00"};
const CFG={ssid:"MeinWLAN",hostname:"truma-heater",static_ip:"",gateway:"",subnet:"",dns:"",mqtt_host:"192.168.1.10",mqtt_port:1883,mqtt_user:"",mqtt_base:"camper/truma",ha_discovery:true,ha_prefix:"homeassistant",ntp_server:"pool.ntp.org",tz:"CET-1CEST,M3.5.0,M10.5.0/3",web_user:"",log_level:2,mqtt_log_level:1,ap_ssid:"truma-heater-A1B2",ap_always:false,wifi_tx_power:34};
let logs=[],logN=0;function log(l,m){const d=new Date();logs.push(d.toTimeString().slice(0,8)+' ['+l+'] '+m)}
log('I','camper-heater 0.9.0 on Camper Heater (ESP32-C3 Super Mini)');log('I','CP Plus assigned node address 0x03');log('I','Heater status received');
function tick(){const h=S.heater,up=(Date.now()-t0)/1000;S.system.uptime=Math.floor(86400+up);S.lin.frames=Math.floor(4200+up*14);
 const d=new Date();S.cpplus_clock=String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0');
 if(h.on&&h.room<h.target_room){h.room=Math.min(h.target_room,h.room+0.05);h.status=7;h.action='heating'}else if(h.on){h.action='idle';h.status=5}
 else if(h.vent>0){h.action='fan';h.status=5}else{h.action='off';h.status=0;h.room=Math.max(15,h.room-0.02)}
 h.status_text=h.status===0?'off':h.status===4?'start/cool down':'on';
 if(S.decoke.active){S.decoke.remaining=Math.max(0,S.decoke.remaining-2);if(!S.decoke.remaining){S.decoke.active=false;h.target_room=S._prev||21}}
 if(S.lin.sniffer&&Math.random()<0.5)log('W','SNIFF PID 16: 00 0F '+(0x60+Math.floor(Math.random()*8)).toString(16).toUpperCase()+' 0B 87 0C 77 85')}
setInterval(tick,2000);tick();
function cmd(a,v){const h=S.heater;log('I','Command '+a+' = '+v);
 if(a==='room_on'){h.on=v==='1';h.target_room=h.on?h.setpoint:0;h.vent=0}
 else if(a==='setpoint'){h.setpoint=+v;if(h.on)h.target_room=+v}
 else if(a==='fan'){h.fan=v;h.heating_mode=v}
 else if(a==='water')h.water_mode=v;
 else if(a==='decoke'){S.decoke.active=v==='1';if(S.decoke.active){S._prev=h.target_room;h.on=true;h.target_room=30;S.decoke.remaining=S.decoke.minutes*60}else h.target_room=S._prev||21}
 else if(a==='decoke_minutes')S.decoke.minutes=+v;
 else if(a==='timer'){if(v==='off'){S.timer_cfg.active=false;S.timer='off'}else{const m=v.match(/^(\d\d:\d\d)-(\d\d:\d\d),?([\d.]*),?(\w*),?(\w*)/);if(m){Object.assign(S.timer_cfg,{active:true,start:m[1],stop:m[2],temp:+m[3]||20,fan:m[4]||'eco',water:m[5]||S.timer_cfg.water});S.timer=m[1]+'-'+m[2]}}}
 else if(a==='sniffer'){S.lin.sniffer=v==='1';log('I','Sniffer '+(S.lin.sniffer?'on':'off'));if(S.lin.sniffer){['03: 86 0B FF FF FF FF FF FF','04: AA 0A FF FF FF FF FF FF','07: 01 00 FF FF FF FF FF FF','20: 00 00 00 00 00 B0 00 00'].forEach(x=>log('W','SNIFF PID '+x))}}
 else if(a==='log_level')CFG.log_level=+v;}
const J=o=>new Response(JSON.stringify(o),{status:200,headers:{'Content-Type':'application/json'}});
window.fetch=async(url,opt)=>{const u=String(url);
 if(u.startsWith('/api/state'))return J(S);
 if(u.startsWith('/api/config')&&(!opt||opt.method!=='POST'))return J(CFG);
 if(u.startsWith('/api/log')){const since=+(u.split('since=')[1]||0);const lines=logs.slice(since);return J({next:logs.length,lines:lines.length?lines.join('\n')+'\n':''})}
 if(u.startsWith('/api/cmd')){const p=new URLSearchParams(opt.body);cmd(p.get('a'),p.get('v'));return J({ok:true})}
 return J({ok:true})};
window.confirm=()=>true;
// demo switch for the ventilation display
window.mockVent=n=>{S.heater.on=false;S.heater.target_room=0;S.heater.vent=n};
})();
</script>
'''

BANNER = r'''<div style="background:#3a2f12;color:#ffd970;padding:8px 12px;font:13px system-ui;text-align:center">Vorschau mit Beispieldaten – kein Gerät verbunden. <button onclick="mockVent(5)" style="margin-left:6px">Lüften Stufe 5 zeigen</button></div>'''


def main():
    src = open(SRC, encoding="utf-8").read()
    html = src.split('R"rawliteral(', 1)[1].rsplit(')rawliteral"', 1)[0]
    html = html.replace("<script>", SHIM + "<script>", 1).replace("<body>", "<body>" + BANNER, 1)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(html)
    print("written", os.path.relpath(OUT))


if __name__ == "__main__":
    main()
