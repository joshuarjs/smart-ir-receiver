// Smart IR Receiver simulator: a JS copy of the firmware's IR / sensor logic behind a fake
// /api, so the real console page runs unmodified in a browser. The amber bar sends fake
// remote presses.
(function () {
  const VER = "__VER__";
  const t0 = Date.now();
  const now = () => Date.now() - t0;
  const NUM = 8, MAXC = 4, LOGN = 8, LEARN_MS = 10000, HIT_MS = 700, MSG_MS = 6000;
  const NEC = (v) => ({ t: "NEC", b: 32, v });
  const slots = [
    { label: "Home theatre on", mode: 0, codes: [NEC("FFA25D")] },
    { label: "Home theatre off", mode: 0, codes: [NEC("FF629D")] },
    { label: "Lights on", mode: 0, codes: [NEC("F7C03F")] },
    { label: "Lights off", mode: 0, codes: [NEC("F740BF")] },
    { label: "Subwoofer", mode: 1, codes: [NEC("FF02FD")] },
    { label: "Sensor 6", mode: 0, codes: [] },
    { label: "Sensor 7", mode: 0, codes: [] },
    { label: "Sensor 8", mode: 0, codes: [] },
  ];
  const S = { hold: 2000, release: 500, txPower: 8, wifiSleep: 0 };
  let rev = 1, learn = -1, learnAt = 0, msg = "", mt = "info", msgUntil = 0;
  const closed = Array(NUM).fill(false), closedAt = Array(NUM).fill(0), hitAt = Array(NUM).fill(-1e9);
  const log = []; let logId = 1;
  let last = null, lastAt = -1e9, held = -1;
  let offline = false, confirming = false, ssid = "NETGEAR-2G", wt = 0, wtSsid = "", wtMsg = "", wtEnd = 0;

  const eq = (a, b) => a.t === b.t && a.v === b.v && (a.t === "UNKNOWN" || a.b === b.b);
  const name = (c) => c.t + " 0x" + c.v;
  const find = (c) => slots.findIndex((s) => s.codes.some((x) => eq(x, c)));
  function setMsg(type, text) { mt = type; msg = text; msgUntil = now() + MSG_MS; }
  function report() {}   // RainMaker publish in the firmware

  function press(s) {
    hitAt[s] = now();
    if (slots[s].mode === 1) { closed[s] = !closed[s]; report(); setMsg("ok", slots[s].label + ": " + (closed[s] ? "closed" : "opened")); }
    else { closed[s] = true; closedAt[s] = now(); setMsg("ok", slots[s].label + " triggered"); }
  }
  function tryAdd(s, c) {
    const o = find(c);
    if (o === s) { setMsg("warn", name(c) + " is already learned for " + slots[s].label); return false; }
    if (o >= 0) { setMsg("warn", name(c) + " is already used by " + slots[o].label + ". Remove it there first."); return false; }
    if (slots[s].codes.length >= MAXC) { setMsg("warn", slots[s].label + " already has " + MAXC + " buttons. Remove one first."); return false; }
    slots[s].codes.push({ ...c }); rev++;
    setMsg("ok", "Learned " + name(c) + " for " + slots[s].label);
    return true;
  }
  function ir(c, repeat) {
    const t = now(), within = t - lastAt <= S.release;
    if (repeat) { if (within) { lastAt = t; if (held >= 0) hitAt[held] = t; } return; }
    if (within && last && eq(c, last)) { lastAt = t; if (held >= 0) hitAt[held] = t; return; }
    last = c; lastAt = t;
    log.unshift({ id: logId++, at: t, c: { ...c } }); if (log.length > LOGN) log.pop();
    if (learn >= 0) { tryAdd(learn, c); learn = -1; held = -1; }
    else { const s = find(c); held = s; if (s >= 0) press(s); }
  }
  function tick() {
    const t = now();
    for (let s = 0; s < NUM; s++) if (closed[s] && slots[s].mode === 0 && t - closedAt[s] >= S.hold) { closed[s] = false; report(); }
    if (held >= 0 && t - lastAt > S.release) held = -1;
    if (learn >= 0 && t - learnAt > LEARN_MS) { setMsg("warn", "No button received for " + slots[learn].label + ". Try again."); learn = -1; }
    if (wt === 1 && t > wtEnd) {
      if (/wrong/i.test(wtSsid)) { wt = 3; wtMsg = "wrong password"; } else { wt = 2; ssid = wtSsid; }
    }
    if (confirming && t > 30000) confirming = false;
  }
  setInterval(tick, 50);

  function status() {
    const t = now();
    let m = msg, type = mt;
    if (learn >= 0) { type = "learn"; m = "Learning for " + slots[learn].label + ": press a button on the remote (" + Math.ceil((LEARN_MS - (t - learnAt)) / 1000) + " s)"; }
    else if (t >= msgUntil) { type = "info"; m = "Ready"; }
    const up = Math.floor(t / 1000) + 3 * 3600 + 17 * 60;
    return {
      fw: VER, fwConfirming: confirming, host: "irremote", ip: "192.168.1.47", ssid, rssi: -58, cloud: "Connected",
      hotspot: false, heapFree: 63500 - (t % 7000), heapMax: 57344, heapMin: 46200, uptime: up,
      drops: 1, dropAgo: up - 2400, dropReason: "Beacon timeout", restart: "Power on", wdBoots: 0, sendErrs: 0,
      wt, wtSsid, wtMsg, rev, learn, mt: type, msg: m,
      closed: closed.map((x) => (x ? 1 : 0)), hit: hitAt.map((h) => (t - h < HIT_MS ? 1 : 0)),
      log: log.map((e) => ({ id: e.id, age: t - e.at, t: e.c.t, v: e.c.v, b: e.c.b, slot: find(e.c) })),
    };
  }
  function config() { return { rev, ...S, slots: slots.map((s) => ({ label: s.label, mode: s.mode, codes: s.codes })) }; }
  const ok = (m) => ({ ok: true, msg: m || "" });

  function api(path, q, body) {
    const n = (k, d) => (q.has(k) ? parseInt(q.get(k), 10) : d);
    const s = n("slot", -1), valid = s >= 0 && s < NUM;
    switch (path) {
      case "/api/status": return status();
      case "/api/config": return config();
      case "/api/learn": if (valid) { learn = s; learnAt = now(); } return ok();
      case "/api/cancel": if (learn >= 0) { learn = -1; setMsg("info", "Learning cancelled"); } return ok();
      case "/api/clear": if (valid) { slots[s].codes = []; rev++; setMsg("info", "Cleared all buttons from " + slots[s].label); } return ok();
      case "/api/delcode": {
        const i = n("i", -1);
        if (valid && i >= 0 && i < slots[s].codes.length) { const c = slots[s].codes.splice(i, 1)[0]; rev++; setMsg("info", "Removed " + name(c) + " from " + slots[s].label); }
        return ok();
      }
      case "/api/slot":
        if (valid) {
          if (q.has("label")) { let l = q.get("label").trim().slice(0, 23); slots[s].label = l || "Sensor " + (s + 1); }
          if (q.has("mode")) slots[s].mode = q.get("mode") === "1" ? 1 : 0;
          rev++; setMsg("ok", "Saved sensor " + (s + 1));
        }
        return ok();
      case "/api/assign": {
        const e = log.find((x) => x.id === n("id", 0));
        if (valid && e) tryAdd(s, e.c);
        return ok();
      }
      case "/api/settings":
        S.hold = Math.min(30000, Math.max(200, n("hold", S.hold)));
        S.release = Math.min(3000, Math.max(150, n("release", S.release)));
        S.txPower = Math.min(8, Math.max(0, n("txPower", S.txPower)));
        S.wifiSleep = n("wifiSleep", S.wifiSleep) ? 1 : 0;
        rev++; setMsg("ok", "Settings saved");
        return ok();
      case "/api/wifi/scan":
        return { nets: [{ ssid: "NETGEAR-2G", rssi: -58 }, { ssid: "NETGEAR-5G", rssi: -63 }, { ssid: "Neighbour wrong-pass test", rssi: -77 }, { ssid: "xfinitywifi", rssi: -82, open: true }] };
      case "/api/wifi": wt = 1; wtSsid = q.get("ssid") || ""; wtEnd = now() + 4000; return ok();
      case "/api/cmd": setMsg("info", "Restarting… (simulated)"); return ok("Restarting");
      case "/api/backup":
        if (body !== undefined) {
          try { const j = JSON.parse(body); j.slots.forEach((x, i) => { if (i < NUM) { slots[i].label = x.label; slots[i].mode = x.mode; slots[i].codes = x.codes.slice(0, MAXC); } }); rev++; return ok(); }
          catch (e) { return { ok: false, msg: "That file isn't a Smart IR Receiver backup." }; }
        }
        return { app: "SmartIRReceiver", format: 1, fw: VER, savedAt: Math.floor(Date.now() / 1000), settings: { ...S }, slots: config().slots };
    }
    return null;
  }

  const realFetch = window.fetch.bind(window);
  window.fetch = async (url, opt) => {
    const u = new URL(url, location.href);
    if (!u.pathname.startsWith("/api/")) return realFetch(url, opt);
    await new Promise((r) => setTimeout(r, 40 + Math.random() * 60));
    if (offline) throw new TypeError("offline");
    let q = u.searchParams, body;
    if (opt && opt.body) {
      if (/json/.test((opt.headers || {})["Content-Type"] || "")) body = opt.body;
      else q = new URLSearchParams(opt.body);
    }
    const j = api(u.pathname, q, body);
    if (j === null) return new Response("Not found", { status: 404 });
    return new Response(JSON.stringify(j), { status: 200, headers: { "Content-Type": "application/json" } });
  };

  // Firmware upload: fake progress, then a "restart" that shows confirming again.
  const RealXHR = window.XMLHttpRequest;
  window.XMLHttpRequest = function () {
    const x = { upload: {}, status: 0, responseText: "" };
    x.open = (m, u) => { x._u = u; };
    x.setRequestHeader = () => {};
    x.send = (file) => {
      const total = (file && file.size) || 1600000; let sent = 0;
      const step = () => {
        if (offline) { x.onerror && x.onerror(); return; }
        sent = Math.min(total, sent + total / 25);
        x.upload.onprogress && x.upload.onprogress({ lengthComputable: true, loaded: sent, total });
        if (sent < total) setTimeout(step, 120);
        else { x.status = 200; x.responseText = "ok"; x.onload && x.onload(); offline = true; setTimeout(() => { offline = false; confirming = true; }, 6000); }
      };
      setTimeout(step, 120);
    };
    return x;
  };
  window.XMLHttpRequest.prototype = RealXHR.prototype;

  // Published previews can't save files: show what would be saved instead.
  const realClick = HTMLAnchorElement.prototype.click;
  HTMLAnchorElement.prototype.click = function () {
    if (this.download) { alert("Preview: on the receiver this saves " + this.download + "."); return; }
    return realClick.call(this);
  };

  window.SIM = {
    press(t, v, b) { ir({ t, v, b }, false); },
    hold(t, v, b) { ir({ t, v, b }, false); let n = 0; const id = setInterval(() => { ir(null, true); if (++n >= 18) clearInterval(id); }, 110); },
    toggleNet(btn) { offline = !offline; btn.textContent = "Wi-Fi: " + (offline ? "off" : "on"); btn.classList.toggle("on", offline); },
  };
})();
