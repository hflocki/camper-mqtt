// camper-battery: BLE gateway for Daly, Ective and BM2 battery monitors
const M = {
  id: 'battery',
  config: {
    version: '1.3.0', chip: 'ESP32-C3', reset_reason: 'Einschalten', wifi_ssid: 'Camper-WLAN', ap_ssid: 'Camper-Battery-A1B2C3',
    wifi_tx_power: 70, ap_always: false, mqtt_server: '192.168.1.1', mqtt_port: 1883, mqtt_user: '',
    mqtt_prefix: 'camper/battery', web_user: '', interval: 10, max_batteries: 3,
    batteries: [{mac: 'C8:47:80:00:00:01', type: 2, name: 'Aufbau', key: 'battery1'},
                {mac: '3C:A5:51:00:00:02', type: 3, name: 'Starter', key: 'starter'}]
  },
  scanUntil: 0,
  route(path) {
    if (path === '/api/scan') { M.scanUntil = Date.now() + 8000; return {ok: true}; }
  },
  bat(b, t) {
    const age = Math.floor(t % 10);
    if (b.type === 3) {  // BM2: voltage only
      const v = 12.62 + Math.sin(t / 60) * 0.05;
      return {mac: b.mac, name: b.name, type: 3, ok: true, soc: 82, v, a: null, w: null, ah: null, wh: null, rt: null,
              t: null, cyc: -1, dv: null, cells: [], bst: 2, state: 'OK', age, rssi: -77, prob: false, cfet: 1, dfet: 1};
    }
    // solar during the first half of every 4 minutes, then consumption
    const charging = Math.floor(t / 120) % 2 === 0;
    const a = charging ? 6.8 : -3.4;
    const soc = 74 + Math.sin(t / 200) * 3;
    const v = 13.28 + (charging ? 0.25 : -0.05);
    const ah = 200 * soc / 100;
    const cells = [3.321, 3.318, 3.326, 3.315].map(c => +(c + (charging ? 0.06 : 0)).toFixed(3));
    return {mac: b.mac, name: b.name, type: b.type, ok: true, soc, v, a, w: v * a, ah, wh: ah * v, rt: charging ? null : ah / 3.4 * 3600,
            t: 17.5, cyc: 42, dv: Math.max(...cells) - Math.min(...cells), cells, bst: null, state: 'OK', age, rssi: -69,
            prob: false, cfet: 1, dfet: 1};
  },
  state() {
    const t = (Date.now() - M.t0) / 1000;
    const scanning = Date.now() < M.scanUntil;
    const found = M.scanUntil ? [
      {mac: 'C8:47:80:00:00:01', name: 'BMX-200', type: 2, rssi: -69},
      {mac: '3C:A5:51:00:00:02', name: 'Battery Monitor', type: 3, rssi: -77},
      {mac: 'A4:C1:38:00:00:03', name: 'DL-40A1B2C3', type: 1, rssi: -84}] : [];
    return {batteries: (M.cfg.batteries || []).map(b => M.bat(b, t)), found, scanning, wifi: 'Verbunden', mqtt: true,
            rssi: -60, uptime: M.up(), heap: 91000, wifi_drops: 0, wifi_reason: '-'};
  }
};
