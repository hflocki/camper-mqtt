#!/usr/bin/env python3
"""Build docs/mockup.html from the web UI in web/index.html.

The mockup is the real page plus a fake fetch() with sample data, so the UI can be
viewed in any browser without an ESP. Run after editing web/index.html:
    python3 tools/make_mockup.py
Uses only the Python standard library.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "_arduino" / "camper_maxxfan" / "web" / "index.html"
DST = ROOT / "docs" / "mockup.html"

MOCK = """<script>
// Mock API for preview - simulates the ESP with sample data
(()=>{const S={fw:"1.2.0",expander:true,sync:false,syncPort:1,ports:[{label:"RJ11",enabled:false,hold:false,led:false},{label:"RJ45",enabled:true,hold:false,led:true}],sta:true,ap:false,ip:"192.168.1.50",rssi:-58,mqtt:true,uptime:15840};
const C={hostname:"camper-maxxfan",ssid:"MyWiFi",static_ip:false,ip:"",gw:"",sn:"255.255.255.0",dns:"",mhost:"192.168.1.10",mport:1883,muser:"",prefix:"camper/maxxfan",disc:true,wuser:"",p0:false,p1:true,pulse:150,syncport:1,apssid:"Camper-MaxxFan-A1B2C3"};
const J=o=>Promise.resolve(new Response(JSON.stringify(o),{headers:{"Content-Type":"application/json"}}));
window.fetch=(url,opt={})=>{const p=Object.fromEntries(opt.body instanceof URLSearchParams?opt.body:[]);
 if(url==="/api/state"){S.uptime+=2;return J(S)}
 if(url==="/api/config"&&opt.method!=="POST")return J(C);
 if(url==="/api/press"&&p.btn==="auto"){const pt=S.ports[p.port];pt.hold=!pt.hold}
 if(url==="/api/led")S.ports[p.port].led=p.on==="1";
 if(url==="/api/sync"){S.sync=true;S.ports[1].hold=true;setTimeout(()=>{S.sync=false;S.ports[1].hold=false},3000)}
 if(url==="/api/config"&&opt.method==="POST"){S.ports[0].enabled=p.p0==="1";S.ports[1].enabled=p.p1==="1";C.p0=S.ports[0].enabled;C.p1=S.ports[1].enabled}
 return J({ok:true})};})();
</script>
<div style="background:#d4a017;color:#000;text-align:center;font:13px system-ui;padding:4px">Mockup mit Beispieldaten – keine Verbindung zum ESP</div>
"""


def main():
    html = SRC.read_text(encoding="utf-8")
    html = html.replace("<body>", "<body>\n" + MOCK, 1)
    DST.write_text(html, encoding="utf-8")
    print(f"{DST.relative_to(ROOT)}: {len(html)} bytes")


if __name__ == "__main__":
    main()
