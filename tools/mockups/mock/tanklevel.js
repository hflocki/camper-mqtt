// camper-tanklevel: fresh/grey water (ADC) and gas bottle (BLE)
const M = {
  id: 'tanklevel',
  config: {
    version: '1.6.0', reset_reason: 'Einschalten', wifi_ssid: 'Camper-WLAN', ap_ssid: 'Camper-Tank-A1B2C3',
    wifi_tx_power: 70, ap_always: false, mqtt_server: '192.168.1.1', mqtt_port: 1883, mqtt_user: '',
    mqtt_prefix: 'camper/tanklevel', web_user: '',
    water_enabled: true, grey_enabled: true, gas_enabled: true,
    tank_size: 100, calib: [{v: 0.12, l: 0}, {v: 0.9, l: 30}, {v: 1.7, l: 65}, {v: 2.45, l: 100}],
    grey_tank_size: 80, grey_calib: [{v: 0.1, l: 0}, {v: 2.4, l: 100}], grey_type: 1,
    sensor_mac: 'AA:BB:CC:11:22:33', capacity: 11, fill_stop_on: true, fill_stop: 80,
    gas_type: 0, lpg_propane: 100, mopeka_empty_mm: 38, mopeka_full_mm: 366,
    sensor_def: {model: 'Beispiel-Gassensor', source: 0, company: 'FFFF', uuid: '', level_offset: 2, level_len: 1,
                 battery: true, battery_offset: 1, battery_len: 1}
  },
  filter: {water: true, grey: false},
  route(path, method, body, q) {
    if (path === '/api/filter') {
      const k = q.get('tank') === 'grey' ? 'grey' : 'water';
      M.filter[k] = !M.filter[k];
      return {filter: M.filter[k] ? 'ON' : 'OFF'};
    }
    if (path === '/api/scan') return {devices: [
      {mac: 'AA:BB:CC:11:22:33', name: 'Gassensor', mfg: 'FFFF', rssi: -71},
      {mac: 'C4:7C:8D:6A:12:34', name: '', mfg: '0059', rssi: -80},
      {mac: '5A:01:23:45:67:89', name: 'Kühlbox', svc: '0000fe95', rssi: -88}]};
  },
  tank(pct, size, calib) {
    const l = pct / 100 * size;
    // voltage from the calibration table (percent of size -> volt), linear between points
    const pts = calib.slice().sort((a, b) => a.l - b.l);
    let v = pts.length ? pts[0].v : 0;
    for (let i = 1; i < pts.length; i++) {
      const a = pts[i - 1], b = pts[i];
      const la = a.l / 100 * size, lb = b.l / 100 * size;
      if (l >= la && l <= lb) { v = a.v + (b.v - a.v) * (l - la) / Math.max(0.001, lb - la); break; }
      if (l > lb) v = b.v;
    }
    return {percent: pct, liters: l, voltage: v};
  },
  state() {
    const t = (Date.now() - M.t0) / 1000, c = M.cfg;
    const wp = Math.max(0, 68 - t / 60);           // fresh water slowly used
    const gp = Math.min(95, 31 + t / 120);          // grey water slowly fills
    const w = Object.assign(M.tank(wp, c.tank_size, c.calib), {filter: M.filter.water});
    const g = Object.assign(M.tank(gp, c.grey_tank_size, c.grey_calib), {filter: M.filter.grey});
    const level = 57;
    return {
      water: w, grey: g,
      gas: {ble_state: 'OK', has_data: true, level, content: level / 100 * c.capacity, raw: level, ble_rssi: -71,
            battery: 88, mm: null, quality: null, temp: null, age: Math.floor(t % 30), scanning: true,
            adv_count: 1200 + Math.floor(t * 2), raw_hex: '0158390000'},
      wifi: 'Verbunden', mqtt: true, rssi: -58, uptime: M.up(), wifi_drops: 0, wifi_reason: '-'
    };
  }
};
