// Mockup runtime: replaces the device API (fetch, WebSocket, upload) with a simulation.
// The device script before this one defines M = {config, state(), route(), wsCmd(), wsOpen()}.
(function () {
  const KEY = 'mock:' + (M.id || location.pathname);
  // Settings survive the "restart" reload of the page (per browser tab only).
  let saved = {};
  try { saved = JSON.parse(sessionStorage.getItem(KEY) || '{}'); } catch (e) {}
  M.cfg = Object.assign({}, M.config, saved);
  M.save = function (patch) {
    Object.assign(M.cfg, patch);
    Object.assign(saved, patch);
    try { sessionStorage.setItem(KEY, JSON.stringify(saved)); } catch (e) {}
  };
  M.t0 = Date.now();
  M.up = () => (M.uptime0 || 93780) + Math.floor((Date.now() - M.t0) / 1000);

  const json = (o, status) => Promise.resolve(new Response(typeof o === 'string' ? o : JSON.stringify(o),
    { status: status || 200, headers: { 'Content-Type': typeof o === 'string' ? 'text/plain' : 'application/json' } }));
  function bodyOf(opt) {
    const b = opt && opt.body;
    if (!b) return {};
    if (typeof b === 'string') {
      try { return JSON.parse(b); } catch (e) { return Object.fromEntries(new URLSearchParams(b)); }
    }
    if (b instanceof URLSearchParams || b instanceof FormData) return Object.fromEntries(b);
    return {};
  }
  window.fetch = function (url, opt) {
    opt = opt || {};
    const u = new URL(url, location.href);
    const method = (opt.method || 'GET').toUpperCase();
    const body = bodyOf(opt);
    const r = M.route ? M.route(u.pathname, method, body, u.searchParams) : undefined;
    if (r !== undefined) return json(r);
    if (u.pathname === '/api/config' && method === 'GET') return json(M.cfg);
    if (u.pathname === '/api/config' && method === 'POST') {
      const p = Object.assign({}, body);
      for (const k of Object.keys(p)) if (/pass|token/i.test(k)) delete p[k];  // never echo secrets
      M.save(p);
      return json({ ok: true });
    }
    return json({ ok: true });
  };

  // WebSocket: pushes M.state() every M.wsInterval ms, M.wsCmd() gets what the page sends
  class FakeWS {
    constructor() {
      this.readyState = 0;
      setTimeout(() => {
        this.readyState = 1;
        if (this.onopen) this.onopen({});
        if (M.wsOpen) M.wsOpen((o) => this._emit(o));
        this._tick();
      }, 120);
      this._timer = setInterval(() => this._tick(), M.wsInterval || 1000);
    }
    _emit(o) { if (this.onmessage) this.onmessage({ data: JSON.stringify(o) }); }
    _tick() { if (M.state) this._emit(M.state()); }
    send(d) {
      let o = d;
      try { o = JSON.parse(d); } catch (e) {}
      const reply = M.wsCmd ? M.wsCmd(o) : null;
      if (reply) this._emit(reply);
      setTimeout(() => this._tick(), 150);
    }
    close() { clearInterval(this._timer); this.readyState = 3; }
    addEventListener(ev, fn) { this['on' + ev] = fn; }
  }
  FakeWS.CONNECTING = 0; FakeWS.OPEN = 1; FakeWS.CLOSING = 2; FakeWS.CLOSED = 3;
  window.WebSocket = FakeWS;

  // File upload: simulated progress, answers like the firmware ("OK" or "OK web" for a .gz page)
  class FakeXHR {
    constructor() { this.upload = {}; this.status = 0; this.responseText = ''; }
    open(m, u) { this._u = u; }
    setRequestHeader() {}
    send(fd) {
      let name = '';
      try { for (const v of fd.values()) if (v && v.name) name = v.name; } catch (e) {}
      let p = 0;
      const iv = setInterval(() => {
        p += 25;
        if (this.upload.onprogress) this.upload.onprogress({ lengthComputable: true, loaded: p, total: 100 });
        if (p >= 100) {
          clearInterval(iv);
          this.status = 200;
          this.responseText = /\.gz$/i.test(name) ? 'OK web' : 'OK';
          if (this.onload) this.onload();
        }
      }, 250);
    }
  }
  window.XMLHttpRequest = FakeXHR;

  // Dialogs are blocked in some embedded viewers; the mockup always answers "yes"
  window.confirm = () => true;
  window.alert = (m) => console.log('[alert]', m);

  // Banner
  document.addEventListener('DOMContentLoaded', () => {
    const b = document.createElement('div');
    b.textContent = 'Mockup mit Beispieldaten – keine Verbindung zu einem Gerät';
    b.style.cssText = 'position:sticky;top:0;z-index:9999;background:#d4a017;color:#000;text-align:center;font:600 12px system-ui;padding:4px';
    document.body.prepend(b);
  });
})();
