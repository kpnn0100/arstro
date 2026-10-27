/*
 * ntwb_fake.js - a FAKE of the NTWB page SDK (/ntwb/ntwb.js), served by fixture_host.mjs in its place,
 * so an app's REAL web face draws EXACTLY the model a native reference shot was drawn from.
 *
 * Same public API as server/arstro_remote/web/static/ntwb/ntwb.js (NTWB 1.1.0):
 *   const app = NTWB.connect([{app, session}]);
 *   app.onState(key, fn)  app.onEvent(name|"*", fn)  app.onBlob(stream, fn)  app.onStatus(fn)
 *   app.onReady(fn)  app.onError(fn)  app.onPresence(fn)  app.call(method, params, {timeout})
 *   app.notify(method, params)  app.close()
 *   fields: appId session clients status statusDetail client info state
 *   NTWB.VERSION  NTWB.MESSAGES  NTWB.connect  NTWB.Bridge   (+ NTWB.fake = true, bridge.fake = true)
 *
 * What it does instead of a WebSocket:
 *   - reads `?fixture=<name>` from the page URL and fetches /fixtures/<name>.bundle.json - fixture_host's
 *     composition of <name>.model.json (required), .view.json, .transport.json, .peaks.json, _api.json
 *     and the installed manifest (all optional);
 *   - then behaves as the host does for a client joining a running session: a `status` (running,
 *     session "main" or the URL's, 1 client) and a `ready` carrying the retained state {model,
 *     transport} - so every onState("model"/"transport") fires with the fixture, then every onReady.
 *     With `?fxstate=late` the ready carries no state and each key follows as its own `state` message
 *     (what a page sees when it connected before the app published);
 *   - answers calls the way the apps' adapters do (apps/<app>/cli/NtwbAdapter.cpp - check there when
 *     a method changes):
 *       command {line}   recorded and logged; resolves {output: "", revision} - nothing happens (the
 *                        model moves only by __ntwbPush). `api --json` answers with the app's real API
 *                        document (fixtures/_api.json), as the service prints it
 *       model            the fixture's model (or the last one pushed)
 *       commands         [{name, usage, summary}] from _api.json
 *       frame {t, source, edge}       blob `frame` (image/jpeg), meta {seq, t, source, w, h, edge, frameSeq,
 *                        ms}; resolves {seq, w, h, bytes, ms}, or {empty: true, lastError} when the native
 *                        monitor showed nothing for that very request. The picture: the fixture's
 *                        <name>.frame.png when the request is the one the native monitor made (same
 *                        `source`); else painted (below)
 *       thumb {path, t, edge}         blob `thumb`, meta {path, t, edge, epoch, w, h}; {epoch, ready, w, h, bytes}
 *       still {id, edge}              blob `still`, meta {id, edge, w, h}; {w, h, bytes}
 *       capture {bind}                blob `capture` (image/png), meta {seq, bind, w, h}; {seq, w, h, bytes}
 *       peaks {media, perSecond}      {ready, perSecond, peaks} from <name>.peaks.json (the native hook's
 *                        envelope; a coarser perSecond takes the peak of each group, as the adapter does);
 *                        {ready: false, peaksEpoch} for a file the fixture has none for
 *       browse {path, kinds}          {path, parent, entries: []} - an empty folder
 *       any other method named picture/image/cover/preview/thumbnail, or whose params carry `edge`,
 *                        `picture`, `thumb` or `image`: one blob on stream params.stream || method
 *       anything else    recorded, resolves {}
 *     Painted pictures: for Interstellar, the shot harness's own FakeService painting (tests/
 *     FakeService.h gradient: renderFrame / renderSource / thumbnail / stillPicture, the same arithmetic),
 *     so a monitor, a cover or a filmstrip cell compares 1:1 with the native PNG; for any other app a
 *     16:9 placeholder gradient of the requested edge, its hue keyed by the request.
 *   Every call and notify is recorded in window.__ntwbCalls as {kind, method, params, at} and logged
 *   to the console as "[ntwb-fake] call <method> <params>".
 *
 * Test hooks on window:
 *   __ntwbFixture   {name, model, view, transport, peaks, api}  (the page may read `view` to put its
 *                   view-model where the native shot was: the tab, open panels ... - see .view.json)
 *   __ntwbCalls     the record above;  __ntwbBooted  true once `ready` was delivered
 *   __ntwbPush(key, value)        a `state` message from the host (value null = the key removed);
 *                                 pushing "model" also changes what call("model") returns
 *   __ntwbEvent(name, data)       an `event` message ({line, fields})
 *   __ntwbStatus(state, detail)   a `status` message (starting | running | stopped | failed)
 *   __ntwbRefuseNext = "why"      the next `command` call rejects with that reason (and the
 *                                 command.rejected event fires with {line, why}), as a refused line does
 *   window "ntwb-fixture" event   dispatched with detail = __ntwbFixture once it is loaded
 *
 * What it does NOT do: no socket, no host, no app process - nothing a command line says happens; no
 * sessions beyond one; no other client's presence; no real render, decode or folder listing.
 */
(function (global) {
  "use strict";

  const VERSION = "1.1.0";
  const MESSAGES = {
    c2h: ["call", "notify"],
    h2c: ["ready", "status", "result", "event", "state", "error"],
  };
  const OTHER_PICTURES = /^(thumbnail|picture|image|cover|preview)$/i;

  global.__ntwbCalls = global.__ntwbCalls || [];
  global.__ntwbBooted = false;

  function log(...a) { try { console.log("[ntwb-fake]", ...a); } catch (e) { /* ignore */ } }

  async function getJson(url, required) {
    const r = await fetch(url, { cache: "no-store" });
    if (!r.ok) {
      if (required) throw new Error("fixture " + url + ": HTTP " + r.status);
      return null;
    }
    return r.json();
  }

  /** FNV-1a over the UTF-8 bytes, 32-bit unsigned - FakeService::thumbnail's hash. */
  function fnv(s) {
    let h = 2166136261;
    for (const b of new TextEncoder().encode(s)) h = Math.imul(h ^ b, 16777619) >>> 0;
    return h;
  }

  /** tests/FakeService.h `gradient`: dusk sky, a sun at `phase`, teal water with a glint. RGBA8. */
  function fakeGradient(w, h, phase, light, hueShift) {
    const px = new Uint8ClampedArray(w * h * 4);
    const sunX = 0.25 + 0.5 * (phase % 1.0), sunY = 0.42;
    const q = (x) => Math.trunc(Math.min(255, Math.max(0, x)));
    for (let y = 0; y < h; ++y)
      for (let x = 0; x < w; ++x) {
        const u = x / w, v = y / h;
        let r, g, b;
        if (v < 0.58) {
          const k = v / 0.58;
          r = 0.18 + 0.75 * k; g = 0.16 + 0.42 * k; b = 0.42 - 0.12 * k;
        } else {
          const k = (v - 0.58) / 0.42;
          r = 0.10 - 0.06 * k; g = 0.22 - 0.10 * k; b = 0.30 - 0.12 * k;
          const glint = Math.exp(-Math.pow((u - sunX) * 9.0, 2.0)) * (1.0 - k);
          r += 0.25 * glint; g += 0.16 * glint;
        }
        const d = Math.hypot((u - sunX) * 1.78, v - sunY);
        const sun = Math.exp(-d * d * 260.0);
        r += sun * 0.9; g += sun * 0.7; b += sun * 0.35;
        const rr = r * (1 - hueShift) + b * hueShift, bb = b * (1 - hueShift) + r * hueShift;
        const i = (y * w + x) * 4;
        px[i] = q(rr * light * 255.0); px[i + 1] = q(g * light * 255.0); px[i + 2] = q(bb * light * 255.0); px[i + 3] = 255;
      }
    return { px, w, h };
  }

  /** A placeholder for an app without a faithful painter: 16:9, long edge `edge`, hue keyed by `key`. */
  function placeholder(edge, key) {
    const w = Math.max(16, Math.min(3840, Math.round(edge) || 960)), h = Math.max(9, Math.round((w * 9) / 16));
    const hue = fnv(key) % 360;
    const c = new OffscreenCanvas(w, h);
    const g = c.getContext("2d");
    const lin = g.createLinearGradient(0, 0, 0, h);
    lin.addColorStop(0, `hsl(${hue}, 45%, 28%)`);
    lin.addColorStop(0.58, `hsl(${(hue + 40) % 360}, 70%, 62%)`);
    lin.addColorStop(0.581, `hsl(${(hue + 180) % 360}, 35%, 22%)`);
    lin.addColorStop(1, `hsl(${(hue + 200) % 360}, 40%, 10%)`);
    g.fillStyle = lin;
    g.fillRect(0, 0, w, h);
    return c;
  }

  function canvasOf(pic) {
    const c = new OffscreenCanvas(pic.w, pic.h);
    c.getContext("2d").putImageData(new ImageData(pic.px, pic.w, pic.h), 0, 0);
    return c;
  }
  async function encode(canvas, mime) {
    const blob = await canvas.convertToBlob(mime === "image/png" ? { type: "image/png" } : { type: "image/jpeg", quality: 0.82 });
    return { blob, w: canvas.width, h: canvas.height, mime };
  }
  async function fixtureCanvas(file) {
    const r = await fetch("/fixtures/" + encodeURIComponent(file), { cache: "no-store" });
    if (!r.ok) throw new Error("fixture picture " + file + ": HTTP " + r.status);
    const bmp = await createImageBitmap(await r.blob());
    const c = new OffscreenCanvas(bmp.width, bmp.height);
    c.getContext("2d").drawImage(bmp, 0, 0);
    return c;
  }

  class Bridge {
    constructor(opts) {
      opts = opts || {};
      const m = location.pathname.match(/^\/apps\/([^/]+)/);
      this.appId = opts.app || (m && decodeURIComponent(m[1]));
      if (!this.appId) throw new Error("NTWB.connect: no app id (not under /apps/<id>/)");
      const q = new URLSearchParams(location.search);
      this._wantSession = opts.session || q.get("session") || null;
      this._lateState = q.get("fxstate") === "late";
      this.fixtureName = opts.fixture || q.get("fixture") || null;
      this.fake = true;
      this.session = null;
      this.clients = 0;
      this.status = "connecting";
      this.statusDetail = null;
      this.client = null;
      this.info = null;
      this.state = {};
      this._fx = null;
      this._seq = 0;
      this._closed = false;
      this._subs = { event: new Map(), state: new Map(), blob: new Map(), status: new Set(), ready: new Set(), error: new Set(), presence: new Set() };
      this._booted = new Promise((res) => { this._bootDone = res; });
      setTimeout(() => this._boot(), 0);   // a socket is never synchronous: the page subscribes first
    }

    // ------------------------------------------------------------ subscriptions (as the real SDK)
    _sub(kind, key, fn) {
      const map = this._subs[kind];
      if (!map.has(key)) map.set(key, new Set());
      map.get(key).add(fn);
      return () => map.get(key).delete(fn);
    }
    onEvent(name, fn) { return this._sub("event", name, fn); }
    onBlob(stream, fn) { return this._sub("blob", stream, fn); }
    onState(key, fn) {
      const off = this._sub("state", key, fn);
      if (key in this.state) fn(this.state[key]);
      return off;
    }
    onStatus(fn) { this._subs.status.add(fn); fn(this.status); return () => this._subs.status.delete(fn); }
    onReady(fn) { this._subs.ready.add(fn); if (this.status === "running" && this.info) fn(this.info); return () => this._subs.ready.delete(fn); }
    onError(fn) { this._subs.error.add(fn); return () => this._subs.error.delete(fn); }
    onPresence(fn) {
      this._subs.presence.add(fn);
      if (this.session) fn({ session: this.session, clients: this.clients });
      return () => this._subs.presence.delete(fn);
    }
    _emit(set, ...args) {
      for (const fn of [...(set || [])]) {
        try { fn(...args); } catch (e) { console.error(e); }
      }
    }
    _setStatus(s, detail) {
      if (this.status === s && !detail) return;
      this.status = s;
      this.statusDetail = detail || null;
      this._emit(this._subs.status, s, detail);
    }
    close() { this._closed = true; }

    // ------------------------------------------------------------ the host's messages, faked
    _status(state, detail, session, clients) {
      if (session !== undefined || clients !== undefined) this._presence(session, clients);
      this._setStatus(state === "running" && !this.client ? "starting" : state, detail);
      if (state !== "running") this.client = null;
    }
    _presence(session, clients) {
      const changed = (session !== undefined && session !== this.session) || (clients !== undefined && clients !== this.clients);
      if (session !== undefined) this.session = session;
      if (clients !== undefined) this.clients = clients;
      if (changed) this._emit(this._subs.presence, { session: this.session, clients: this.clients });
    }
    _ready(state) {
      this.client = "fx1";
      this.state = state;
      this._setStatus("running");
      for (const [key, fns] of this._subs.state) if (key in this.state) this._emit(fns, this.state[key]);
      this._emit(this._subs.ready, this.info);
    }
    _stateMsg(key, data) {
      if (data === null || data === undefined) delete this.state[key]; else this.state[key] = data;
      this._emit(this._subs.state.get(key), data === undefined ? null : data);
    }
    _event(name, data) {
      this._emit(this._subs.event.get(name), data);
      this._emit(this._subs.event.get("*"), name, data);
    }

    async _boot() {
      let session = this._wantSession || "main";
      if (session === "new") session = "s1";
      try {
        if (!this.fixtureName) throw new Error("no ?fixture=<name> in the page URL (fixture_host.mjs lists them at / and /fixtures/index.json)");
        // one request: fixture_host composes the fixture's files and the installed manifest
        const b = await getJson("/fixtures/" + encodeURIComponent(this.fixtureName) + ".bundle.json", true);
        const { model, view = null, transport = null, peaks = null, api = null, manifest = null } = b;
        if (!model) throw new Error("fixture " + this.fixtureName + " has no model");
        this._fx = { name: this.fixtureName, model, view, transport, peaks, api };
        global.__ntwbFixture = this._fx;
        try { global.dispatchEvent(new CustomEvent("ntwb-fixture", { detail: this._fx })); } catch (e) { console.error(e); }
        this.info = { id: this.appId, name: (manifest && manifest.name) || this.appId, version: (manifest && manifest.version) || "fixture" };
        log("fixture", this.fixtureName, "- model revision", model.revision, view ? "+ view" : "", transport ? "+ transport" : "", peaks ? "+ peaks" : "");
        this._status("running", undefined, session, 1);
        const state = { model };
        if (transport) state.transport = transport;
        if (this._lateState) {
          this._ready({});
          for (const k of Object.keys(state)) this._stateMsg(k, state[k]);
        } else {
          this._ready(state);
        }
        global.__ntwbBooted = true;
        this._bootDone(true);
      } catch (e) {
        const why = String(e && e.message ? e.message : e);
        console.error("[ntwb-fake] " + why);
        this._status("failed", why, session, 1);
        this._bootDone(false);
      }
    }

    // ------------------------------------------------------------------ requests
    _record(kind, method, params) {
      const rec = { kind, method, params: params || {}, at: Math.round(performance.now()) };
      global.__ntwbCalls.push(rec);
      log(kind, method, JSON.stringify(rec.params));
      return rec;
    }
    call(method, params, { timeout = 60000 } = {}) {
      this._record("call", method, params);
      const work = this._booted.then((ok) => {
        if (!ok || this._closed) throw new Error("not connected");
        return this._answer(method, params || {});
      });
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error(method + " timed out")), timeout);
        work.then((v) => { clearTimeout(timer); resolve(v); }, (e) => { clearTimeout(timer); reject(e); });
      });
    }
    notify(method, params) { this._record("notify", method, params); }

    _model() { return this.state.model || (this._fx && this._fx.model) || {}; }

    async _answer(method, p) {
      const fx = this._fx || {};
      const model = this._model();
      switch (method) {
        case "command": {
          const line = typeof p.line === "string" ? p.line : "";
          if (global.__ntwbRefuseNext) {
            const why = String(global.__ntwbRefuseNext);
            global.__ntwbRefuseNext = null;
            this._event("command.rejected", { line: "[evt] command.rejected line=" + JSON.stringify(line) + " why=" + JSON.stringify(why), fields: { line, why } });
            throw new Error(why);
          }
          if (/^\s*api\s+--json\s*$/.test(line) && fx.api) return { output: JSON.stringify(fx.api), revision: model.revision || 0 };
          return { output: "", revision: model.revision || 0 };
        }
        case "model": return model;
        case "commands": {
          const cmds = fx.api && Array.isArray(fx.api.commands) ? fx.api.commands : [];
          return cmds.map((c) => ({ name: c.verb || c.name, usage: c.usage, summary: c.summary }));
        }
        case "frame": return this._frame(p, model, fx);
        case "thumb": return this._thumb(p);
        case "still": return this._still(p);
        case "capture": return this._capture(p, model, fx);
        case "peaks": return this._peaks(p, model, fx);
        case "browse": return { path: typeof p.path === "string" && p.path ? p.path : "/", parent: null, entries: [] };
      }
      if (OTHER_PICTURES.test(method) || p.edge != null || p.picture || p.thumb || p.image) {
        const pic = await encode(placeholder(p.edge != null ? +p.edge : 960, method + "|" + JSON.stringify(p)), "image/jpeg");
        const seq = ++this._seq;
        this._blob(p.stream || method, pic, { seq, w: pic.w, h: pic.h, edge: p.edge });
        return { seq, w: pic.w, h: pic.h, bytes: pic.blob.size };
      }
      return {};
    }

    _blob(stream, pic, meta) {
      const header = { stream, mime: pic.mime, meta, coalesce: false };
      this._emit(this._subs.blob.get(stream), pic.blob, header);
    }

    /** FakeService::renderFrame / renderSource, for Interstellar; a placeholder otherwise. */
    _painted(p, model, source, t, edge) {
      if (this.appId !== "interstellar") return placeholder(edge, "frame|" + source + "|" + t);
      const exposure = (model.gradeParams && +model.gradeParams.exposure) || 0;
      const light = Math.pow(2.0, exposure * 0.6);
      const w = Math.min(edge, 640), h = Math.trunc((w * 9) / 16);
      if (!source) return canvasOf(fakeGradient(w, h, t / Math.max(1.0, model.duration || 0), light, model.currentTimeline === "main" ? 0.0 : 0.08));
      let at = t;
      if (at < 0) for (const n of model.rack || []) if (n.bindName === source) at = n.frame;
      return canvasOf(fakeGradient(w, h, at / 12.0, light, 0.04));
    }

    async _frame(p, model, fx) {
      const t0 = performance.now();
      const source = typeof p.source === "string" ? p.source : "";
      const tr = this.state.transport;
      const t = typeof p.t === "number" ? p.t : source ? -1 : (tr && typeof tr.playhead === "number" ? tr.playhead : model.playhead || 0);
      const edge = Math.max(64, Math.min(3840, p.edge != null ? Math.round(+p.edge) : 960));
      const vf = fx.view && fx.view.frame;
      let canvas;
      if (vf && (vf.source || "") === source) {
        if (!vf.file) return { empty: true, lastError: model.lastError || "" };   // the native monitor had none
        canvas = await fixtureCanvas(vf.file);
      } else {
        canvas = this._painted(p, model, source, t, edge);
      }
      const pic = await encode(canvas, "image/jpeg");
      const ms = Math.round((performance.now() - t0) * 10) / 10;
      const seq = ++this._seq;
      this._blob("frame", pic, { seq, t, source, w: pic.w, h: pic.h, edge, frameSeq: model.frameSeq || 0, ms });
      return { seq, w: pic.w, h: pic.h, bytes: pic.blob.size, ms };
    }

    /** FakeService::thumbnail: 160x90 whatever the edge, keyed by the path's hash, the sun moving with t. */
    _thumbCanvas(path, t, edge) {
      if (this.appId !== "interstellar") return placeholder(edge, "thumb|" + path + "|" + t);
      const hs = fnv(path);
      return canvasOf(fakeGradient(160, 90, (hs % 100) / 100.0 + t * 0.06, 0.85 + (hs % 7) * 0.04, (hs % 5) * 0.12));
    }
    async _thumb(p) {
      if (typeof p.path !== "string" || !p.path) throw new Error("thumb needs params.path (a media file as the model names it)");
      const t = typeof p.t === "number" ? p.t : 0;
      const edge = Math.max(16, Math.min(1024, p.edge != null ? Math.round(+p.edge) : 160));
      const pic = await encode(this._thumbCanvas(p.path, t, edge), "image/jpeg");
      this._blob("thumb", pic, { path: p.path, t, edge, epoch: 0, w: pic.w, h: pic.h });
      return { epoch: 0, ready: true, w: pic.w, h: pic.h, bytes: pic.blob.size };
    }
    async _still(p) {
      if (typeof p.id !== "string" || !p.id) throw new Error("still needs params.id (a still's id, model.stills[].id)");
      const edge = p.edge != null ? Math.round(+p.edge) : 320;
      const pic = await encode(this._thumbCanvas("/stills/" + p.id + ".png", 0, edge || 172), "image/jpeg");   // FakeService stillPicture
      this._blob("still", pic, { id: p.id, edge, w: pic.w, h: pic.h });
      return { w: pic.w, h: pic.h, bytes: pic.blob.size };
    }
    async _capture(p, model, fx) {
      const bind = typeof p.bind === "string" ? p.bind : "";
      const vf = fx.view && fx.view.frame;
      const canvas = vf && vf.file && (vf.source || "") === bind ? await fixtureCanvas(vf.file)
        : this._painted(p, model, bind, bind ? -1 : model.playhead || 0, 640);
      const pic = await encode(canvas, "image/png");
      const seq = ++this._seq;
      this._blob("capture", pic, { seq, bind, w: pic.w, h: pic.h });
      return { seq, w: pic.w, h: pic.h, bytes: pic.blob.size };
    }
    _peaks(p, model, fx) {
      if (typeof p.media !== "string" || !p.media) throw new Error("peaks needs params.media (a clip's media as the model names it)");
      const env = fx.peaks && fx.peaks[p.media];
      if (!env || !(env.perSecond > 0)) return { ready: false, peaksEpoch: model.peaksEpoch || 0 };
      const want = typeof p.perSecond === "number" ? p.perSecond : 0;
      const k = want > 0 && want < env.perSecond ? Math.max(1, Math.ceil(env.perSecond / want)) : 1;
      const out = [];
      for (let i = 0; i < env.peaks.length; i += k) {
        let m = 0;
        for (let j = i; j < Math.min(env.peaks.length, i + k); ++j) m = Math.max(m, env.peaks[j]);
        out.push(m);
      }
      return { ready: true, perSecond: env.perSecond / k, peaks: out };
    }
  }

  let current = null;
  const need = () => { if (!current) throw new Error("NTWB.connect() not called yet"); return current; };
  global.__ntwbPush = (key, value) => {
    const b = need();
    b._stateMsg(key, value);
    if (key === "model" && b._fx && value) b._fx.model = value;
  };
  global.__ntwbEvent = (name, data) => need()._event(name, data || {});
  global.__ntwbStatus = (state, detail) => need()._status(state, detail);

  global.NTWB = { VERSION, MESSAGES, connect: (opts) => (current = new Bridge(opts)), Bridge, fake: true };
})(window);
