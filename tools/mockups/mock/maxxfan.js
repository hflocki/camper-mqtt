// camper-maxxfan: VanTurtle board, buttons of the MaxxFan remote via port expander
const M = {
  id: 'maxxfan',
  config: {hostname: 'camper-maxxfan', ssid: 'Camper-WLAN', static_ip: false, ip: '', gw: '', sn: '255.255.255.0', dns: '',
           mhost: '192.168.1.1', mport: 1883, muser: '', prefix: 'camper/maxxfan', disc: true, wuser: '',
           p0: false, p1: true, pulse: 150, syncport: 1, apssid: 'Camper-MaxxFan-A1B2C3'},
  ports: [{label: 'RJ11', hold: false, led: false}, {label: 'RJ45', hold: false, led: true}],
  sync: false,
  route(path, method, b) {
    const pt = M.ports[+b.port || 0];
    if (path === '/api/state') {
      const on = v => v === true || v === '1' || v === 1;
      return {fw: '1.5.0', expander: true, sync: M.sync, syncPort: +M.cfg.syncport,
              ports: M.ports.map((p, i) => Object.assign({enabled: on(M.cfg['p' + i])}, p)),
              sta: true, ap: false, ip: '192.168.1.50', rssi: -58, mqtt: true, uptime: M.up()};
    }
    if (path === '/api/press' && b.btn === 'auto') pt.hold = !pt.hold;
    if (path === '/api/led') pt.led = b.on === '1';
    if (path === '/api/close' || path === '/api/speed') {
      M.sync = true;
      setTimeout(() => { M.sync = false; }, 3000);
    }
    if (path === '/api/state' || path === '/api/press' || path === '/api/led' || path === '/api/close' || path === '/api/speed') return {ok: true};
  }
};
