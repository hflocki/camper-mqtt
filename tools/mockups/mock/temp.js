// camper-temp: DS18B20 sensors on a 1-wire bus
const M = {
  id: 'temp',
  config: {
    version: '1.2.0', chip: 'ESP8266', pin: 4, reset_reason: 'Einschalten', wifi_ssid: 'Camper-WLAN',
    ap_ssid: 'Camper-Temp-A1B2C3', wifi_tx_power: 70, ap_always: false, mqtt_server: '192.168.1.1', mqtt_port: 1883,
    mqtt_user: '', mqtt_prefix: 'camper/temp', web_user: '', interval: 10, resolution: 12, max_sensors: 8,
    sensors: [
      {addr: '28FF4A1B6C1603A1', name: 'Kühlschrank', key: 'temp1', offset: 0, lo: null, hi: 8, delay: 15},
      {addr: '28FF7C2D5A1604B2', name: 'Gefrierfach', key: 'temp2', offset: -0.3, lo: null, hi: -10, delay: 15},
      {addr: '28FF11E09B1605C3', name: 'Innenraum', key: 'temp3', offset: 0, lo: 3, hi: null, delay: 30}]
  },
  bus: {'28FF4A1B6C1603A1': 5.2, '28FF7C2D5A1604B2': -17.8, '28FF11E09B1605C3': 19.4, '28FF9D3344160AD4': 22.1},
  mm: {},
  route(path) {
    if (path === '/api/minmax/reset') { M.mm = {}; return {ok: true}; }
    if (path === '/api/scan') return {ok: true};
  },
  state() {
    const t = (Date.now() - M.t0) / 1000;
    const bus = Object.entries(M.bus).map(([addr, base], i) => ({addr, temp: +(base + Math.sin(t / 40 + i) * 0.6).toFixed(2)}));
    const raw = Object.fromEntries(bus.map(b => [b.addr, b.temp]));
    const alarms = [];
    const sensors = (M.cfg.sensors || []).map(s => {
      const present = s.addr in raw;
      const temp = present ? +(raw[s.addr] + (s.offset || 0)).toFixed(1) : null;
      const mm = M.mm[s.addr] = M.mm[s.addr] || {min: temp, max: temp};
      if (temp !== null) { mm.min = Math.min(mm.min ?? temp, temp); mm.max = Math.max(mm.max ?? temp, temp); }
      let alarm = 0;
      if (temp !== null && s.hi !== null && s.hi !== undefined && temp > s.hi) alarm = 2;
      if (temp !== null && s.lo !== null && s.lo !== undefined && temp < s.lo) alarm = 1;
      if (alarm) alarms.push(s.name + (alarm === 2 ? ' zu warm: ' : ' zu kalt: ') + temp.toFixed(1).replace('.', ',') + ' °C');
      return {addr: s.addr, name: s.name, key: s.key, temp, min: mm.min, max: mm.max, ok: present, present, alarm, age: present ? Math.floor(t % 10) : -1};
    });
    return {sensors, bus, alarm_text: alarms.join('; '), wifi: 'Verbunden', mqtt: true, rssi: -63,
            uptime: M.up(), heap: 23400, wifi_drops: 1, wifi_reason: 'Beacon timeout'};
  }
};
