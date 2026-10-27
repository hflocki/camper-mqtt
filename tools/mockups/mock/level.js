// camper-level: BNO085 spirit level with wedge heights
const M = {
  id: 'level',
  config: {
    version: '2.1.0', reset_reason: 'Einschalten', pins: 'SDA GPIO8 · SCL GPIO9', track_width: 165, wheelbase: 345,
    mount: 0, tolerance: 0.5, filter_active: true, mqtt_fast: false, tare_pitch: 0, tare_roll: 0,
    mqtt_server: '192.168.1.1', mqtt_port: 1883, mqtt_user: '', mqtt_prefix: 'camper/level', web_user: '',
    wifi_ssid: 'Camper-WLAN', ap_ssid: 'Camper-Level-A1B2C3', wifi_tx_power: 70, ap_always: false
  },
  wsInterval: 500,
  route(path, method, body, q) {
    if (path === '/api/tare') {
      if (q.get('action') === 'set') { const r = M.raw(); M.save({tare_pitch: +r.p.toFixed(2), tare_roll: +r.r.toFixed(2)}); }
      else M.save({tare_pitch: 0, tare_roll: 0});
      return {ok: true};
    }
  },
  // Raw angles drift slowly, as if someone moves the van onto wedges
  raw() {
    const t = (Date.now() - M.t0) / 1000;
    return {p: 1.4 * Math.cos(t / 25) + 0.3, r: -0.9 * Math.sin(t / 18)};
  },
  state() {
    const c = M.cfg, r = M.raw();
    const pitch = r.p - c.tare_pitch, roll = r.r - c.tare_roll;
    const tol = c.tolerance;
    const rad = Math.PI / 180;
    // height of each corner relative to the plane, lift the low corners up to the highest one
    const h = {fl: Math.tan(pitch * rad) * c.wheelbase / 2 + Math.tan(roll * rad) * c.track_width / 2,
               fr: Math.tan(pitch * rad) * c.wheelbase / 2 - Math.tan(roll * rad) * c.track_width / 2,
               rl: -Math.tan(pitch * rad) * c.wheelbase / 2 + Math.tan(roll * rad) * c.track_width / 2,
               rr: -Math.tan(pitch * rad) * c.wheelbase / 2 - Math.tan(roll * rad) * c.track_width / 2};
    const top = Math.max(h.fl, h.fr, h.rl, h.rr);
    const level = Math.abs(pitch) <= tol && Math.abs(roll) <= tol;
    return {ok: true, pitch, roll, level, tol, fl: top - h.fl, fr: top - h.fr, rl: top - h.rl, rr: top - h.rr,
            fast: !!c.mqtt_fast, tare: !!(c.tare_pitch || c.tare_roll), tare_pitch: c.tare_pitch, tare_roll: c.tare_roll,
            raw_pitch: r.p, raw_roll: r.r, wifi: 'Verbunden', mqtt: true, rssi: -55, uptime: M.up(), heap: 182000,
            wifi_drops: 0, wifi_reason: ''};
  }
};
