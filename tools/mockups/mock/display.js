// camper-display: wall panel; the web page mirrors it over a WebSocket (t: cfg / v / f / err)
const M = {
  id: 'display',
  wsInterval: 2000,
  config: {
    hostname: 'camper-display', displayName: 'Camper', apPass: '********', webUser: '', webPass: '',
    wifiSsid: 'Camper-WLAN', wifiPass: '********', staticIp: false, ip: '', gateway: '', subnet: '', dns: '',
    mqttHost: '192.168.1.1', mqttPort: 1883, mqttUser: '', mqttPass: '',
    prefixSelf: 'camper/display', prefixTank: 'camper/tanklevel', prefixLevel: 'camper/level', prefixHeater: 'camper/heater',
    prefixFan: 'camper/maxxfan', prefixTemp: 'camper/temp', prefixBattery: 'camper/battery', fanPort: 1,
    ntpServer: 'pool.ntp.org', tz: 'CET-1CEST,M3.5.0,M10.5.0/3',
    poOn: true, poClear: true, poPrio: 0, poToken: '********', poUser: '********', poTitle: 'Camper', poStatus: 'OK 14:02',
    brightness: 80, dimBrightness: 10, screenTimeout: 120,
    wxOn: true, wxLat: '47.6600', wxLon: '9.1750', wxName: 'Stellplatz am See', wxTopic: '', wxGust: 60, wxEvery: 30, ovTempSel: '', ovBatSel: '',
    builtin: [true, true, true, true, true, true, true],
    pageNames: ['Home', 'Truma', 'MaxxFan', 'Level', 'Temperatur', 'Batterie', 'Wetter', 'System'],
    ovTiles: 'water,gas,heater,level,fan,weather,temp,bat1,status',
    fw: '2.0.0', mac: '24:0A:C4:00:00:01',
    // In the mockup the device links open the other mockups
    links: [{n: 'Truma', u: 'heater.html'}, {n: 'MaxxFan', u: 'maxxfan.html'}, {n: 'Tanks', u: 'tanklevel.html'},
            {n: 'Level', u: 'level.html'}, {n: 'Temperatur', u: 'temp.html'}, {n: 'Batterie', u: 'battery.html'}]
  },
  pages: {pages: [{name: 'Licht'}],
          tiles: [{name: 'Licht innen', page: 0, type: 1, topic: 'zigbee2mqtt/licht', key: 'state', cmdTopic: 'zigbee2mqtt/licht/set', payloadOn: 'ON', payloadOff: 'OFF'},
                  {name: 'Helligkeit', page: 0, type: 3, topic: 'zigbee2mqtt/licht', key: 'brightness', cmdTopic: 'zigbee2mqtt/licht/set', min: 0, max: 254, step: 1},
                  {name: 'Landstrom', page: 0, type: 0, topic: 'zigbee2mqtt/steckdose', key: 'power', unit: 'W', decimals: 0}]},
  H: {hvac: 'heat', set: 21, fan: 'eco', wmode: 'eco', room: 18.6, decoke: false, drem: 0},
  T: {t0: 'ON', t1: '180', t2: '0'},
  awake: true, bri: 80, finderOn: false,
  cfgMsg() {
    const c = M.cfg;
    return {t: 'cfg', fw: c.fw, host: c.hostname, name: c.displayName, links: c.links, ap: false, setup: false,
            builtin: c.builtin, ovTiles: c.ovTiles, ovTempSel: c.ovTempSel, ovBatSel: c.ovBatSel, pageNames: c.pageNames, pages: M.pages.pages.map(p => p.name),
            tiles: M.pages.tiles.map(t => Object.assign({n: t.name, p: t.page, y: t.type, u: t.unit || ''},
              t.type === 1 ? {son: t.stateOn || 'ON'} : {}, t.type === 3 ? {min: t.min, max: t.max, step: t.step} : {}))};
  },
  wxMsg() {
    const c = M.cfg, now = Math.floor(Date.now() / 1000);
    if (!c.wxOn) return {t: 'wx', w: {on: false, status: 'Wetter aus', name: c.wxName, warn: ''}};
    const g = 41, warn = c.wxGust > 0 && g >= c.wxGust ? 'Wind: Böen bis ' + g + ' km/h' : '';
    return {t: 'wx', w: {on: true, status: 'Stand ' + new Date().toTimeString().slice(0, 5), name: c.wxName, warn,
      t: 13.2, f: 11.4, h: 71, w: 24, g, dir: 'W', c: 2, day: true, txt: 'Teils bewölkt', rn: 0.2, pn: 35, at: now,
      d: [{c: 2, hi: 14, lo: 7, r: 0.8, p: 40, g: 45}, {c: 63, hi: 12, lo: 6, r: 6.4, p: 90, g: 52},
          {c: 3, hi: 11, lo: 5, r: 1.2, p: 45, g: 38}, {c: 0, hi: 15, lo: 4, r: 0, p: 5, g: 20}]}};
  },
  wsOpen(emit) { emit(M.cfgMsg()); emit(M.wxMsg()); M.emit = emit; },
  state() {
    const t = (Date.now() - M.t0) / 1000, H = M.H;
    if (H.hvac === 'heat' && H.room < H.set) H.room = Math.min(H.set, H.room + 0.05);
    else if (H.hvac !== 'heat') H.room = Math.max(14, H.room - 0.02);
    if (H.decoke) { H.drem = Math.max(0, H.drem - 1 / 30); if (!H.drem) H.decoke = false; }
    const pitch = 1.2 * Math.cos(t / 25), roll = -0.7 * Math.sin(t / 18);
    const charging = Math.floor(t / 120) % 2 === 0;
    const d = {
      tank_status: 'online', water_pct: (68 - t / 60).toFixed(0), water_l: (68 - t / 60).toFixed(1), grey_pct: '', grey_l: '',
      gas_pct: '57', gas_l: '6.3', gas_bat: '88',
      lvl_status: 'online', lvl_pitch: pitch.toFixed(2), lvl_roll: roll.toFixed(2),
      lvl_ok: Math.abs(pitch) <= 0.5 && Math.abs(roll) <= 0.5 ? 'ON' : 'OFF', lvl_tol: '0.5',
      lvl_fl: Math.max(0, -pitch * 3 - roll * 1.4).toFixed(1), lvl_fr: Math.max(0, -pitch * 3 + roll * 1.4).toFixed(1),
      lvl_rl: Math.max(0, pitch * 3 - roll * 1.4).toFixed(1), lvl_rr: Math.max(0, pitch * 3 + roll * 1.4).toFixed(1),
      heat_status: 'online', heat_alive: 'ON', heat_room: H.room.toFixed(1), heat_set: String(H.set), heat_hvac: H.hvac,
      heat_action: H.hvac === 'heat' && H.room < H.set ? 'heating' : 'idle', heat_fan: H.fan, heat_wmode: H.wmode,
      heat_water: H.wmode === 'off' ? '18' : '42', heat_op: H.hvac === 'heat' ? 'Heizen' : 'Aus', heat_err: '0', heat_errflag: 'OFF',
      heat_decoke: H.decoke ? 'ON' : 'OFF', heat_drem: String(Math.ceil(H.drem)),
      fan_status: 'online', fan_hold: 'OFF', fan_led: 'ON',
      temp_status: 'online',
      temp_list: JSON.stringify([{id: 'temp1', name: 'Kühlschrank', t: 5.1 + Math.sin(t / 40) * 0.4, alarm: 0},
                                 {id: 'temp2', name: 'Gefrierfach', t: -17.9, alarm: 0},
                                 {id: 'temp3', name: 'Innenraum', t: H.room - 0.4, alarm: 0}]),
      temp_alarm: '',
      bat_status: 'online',
      bat_list: JSON.stringify([
        {name: 'Aufbau', ok: true, soc: 74, v: 13.3, w: charging ? 90 : -45, rt: charging ? null : 1960, st: '', p: false},
        {name: 'Starter', ok: true, soc: 82, v: 12.6, w: null, rt: null, st: 'Normal', p: false}]),
      t0: M.T.t0, t1: M.T.t1, t2: (charging ? 0 : 1650 + Math.round(Math.sin(t / 5) * 40)).toString()
    };
    return {t: 'v', d, s: {wifi: true, rssi: -52, mqtt: true, ip: '192.168.1.17', bri: M.bri, finder: M.finderOn, awake: M.awake}};
  },
  wsCmd(o) {
    const H = M.H;
    if (o.c === 'heat') {
      if (o.k === 'hvac_mode') H.hvac = o.v;
      else if (o.k === 'room_setpoint') H.set = +o.v;
      else if (o.k === 'fan_mode') H.fan = o.v;
      else if (o.k === 'water_mode') H.wmode = o.v;
      else if (o.k === 'decoke') { H.decoke = o.v === 'ON'; H.drem = H.decoke ? 30 : 0; }
    } else if (o.c === 'tile') {
      if (o.i === 0 && o.v === 'toggle') M.T.t0 = M.T.t0 === 'ON' ? 'OFF' : 'ON';
      else if (o.i === 1) M.T.t1 = String(o.v);
    } else if (o.c === 'bri') M.bri = +o.v;
    else if (o.c === 'screen') M.awake = o.v !== 'off';
    else if (o.c === 'finder') {
      M.finderOn = !!o.on;
      return {t: 'f', on: M.finderOn, filter: o.f || '', l: M.finderOn ? [
        ['zigbee2mqtt/licht', '{"state":"' + M.T.t0 + '","brightness":' + M.T.t1 + '}'],
        ['zigbee2mqtt/tuer', '{"contact":true,"battery":97}'],
        ['zigbee2mqtt/steckdose', '{"power":0,"energy":12.4}']] : []};
    }
    return null;
  },
  route(path, method, body) {
    if (path === '/api/settings' && method === 'GET') return M.cfg;
    if (path === '/api/settings') {
      const b = Object.assign({}, body);
      for (const k of Object.keys(b)) if (/pass|token|user/i.test(k) && k !== 'webUser' && k !== 'mqttUser') delete b[k];
      M.save(b);
      if (M.emit) setTimeout(() => { M.emit(M.cfgMsg()); M.emit(M.wxMsg()); }, 300);
      return {ok: true, msg: 'Gespeichert'}; }
    if (path === '/api/pages' && method === 'GET') return M.pages;
    if (path === '/api/pages') { M.pages = body; return {ok: true, msg: 'Seiten gespeichert'}; }
    if (path === '/api/scan') return {running: false, nets: [{ssid: 'Camper-WLAN', rssi: -48}, {ssid: 'Campingplatz-Gast', rssi: -77}]};
    if (path === '/api/pushover/test') { M.save({poStatus: 'OK ' + new Date().toTimeString().slice(0, 5)}); return {ok: true, msg: 'Testnachricht wird gesendet'}; }
    if (path === '/update') return {ok: true, msg: 'Update ok – Neustart (simuliert)'};
    if (path === '/api/reboot' || path === '/api/factory') return {ok: true, msg: 'Neustart (simuliert)'};
  }
};
