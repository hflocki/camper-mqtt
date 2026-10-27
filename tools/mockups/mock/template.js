// _template: example device with an analog input
const M = {
  id: 'template',
  config: {version: '1.0.0', reset_reason: 'Einschalten', wifi_ssid: 'Camper-WLAN', ap_ssid: 'Camper-Example-A1B2C3',
           wifi_tx_power: 70, ap_always: false, mqtt_server: '192.168.1.1', mqtt_port: 1883, mqtt_user: '',
           mqtt_prefix: 'camper/example', web_user: '', interval: 10, scale: 1, offset: 0},
  reads: 120,
  route(path) { if (path === '/api/measure') { M.reads++; return {ok: true}; } },
  state() {
    const t = (Date.now() - M.t0) / 1000, c = M.cfg;
    const raw = 1.62 + Math.sin(t / 15) * 0.08;
    return {values: [{key: 'voltage', name: 'Spannung', unit: 'V', dec: 2, v: raw * c.scale + c.offset},
                     {key: 'raw', name: 'ADC roh', unit: '', dec: 0, v: Math.round(raw / 3.3 * 4095)}],
            ok: true, age: Math.floor(t % c.interval), reads: M.reads + Math.floor(t / c.interval), errors: 0,
            wifi: 'Verbunden', mqtt: true, rssi: -57, uptime: M.up(), heap: 210000, wifi_drops: 0, wifi_reason: '-'};
  }
};
