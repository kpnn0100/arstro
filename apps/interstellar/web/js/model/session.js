/*
 * Interstellar — session.js: the MODEL side of the page (R-SVC-4; cosmo's js/model/session.js is the
 * pattern).
 *
 * The model is `interstellar-cc ntwb serve` on the machine - THE InterstellarService of this Arstro
 * Remote session, shared by every page of it. This class is its proxy in the page and nothing more:
 * it turns what the adapter publishes into signals, routes the blobs a page asked for back to the
 * request, keeps the picture caches (keyed, bounded, every object URL revoked when it leaves) and
 * wraps the adapter's methods (cli/NtwbAdapter.h, docs/ntwb-api.json). It holds no view state.
 *
 *   SIGNALS
 *   model       state `model`      the AppModel, exactly what `state print --json` prints (null until it came)
 *   transport   state `transport`  {playing, playhead, playheadFrame, duration, shuttle, frameSeq, sourceView,
 *                                   sourcePlayhead, soundPlaying, meterPeak:[l,r], meterRms:[l,r], meterClip}
 *   thumbEpoch  state `thumbs`     the thumbnailer's epoch: rises when a still asked for came back decoded
 *   thumbsVersion / stillsVersion / peaksVersion   rise whenever a picture / envelope landed in a cache -
 *               read one in an effect to redraw with the *Now() getters below
 *   status, presence ({session, clients}), lastRejection ({text, at}: the newest command.rejected/error EVENT,
 *               any page's - the page's own refusals come back from command() instead)
 *
 *   COMMANDS
 *   command(line) -> Promise<{output, revision}>   rejects with the service's sentence (a refusal)
 *   notify(line)                                    no reply (the per-frame line of a drag / scrub)
 *   commands() -> Promise<[{name, usage, summary}]>
 *   on(eventName | "*", fn) -> off                  the service's events, each {line, fields}
 *
 *   PICTURES (each to THIS page alone)
 *   requestFrame({t?, source?, edge}) -> Promise<{url, w, h, meta} | {empty: true, lastError}>
 *               the monitor's picture (adapter `frame`). The CALLER owns `url`: release(url) when the
 *               picture is replaced (vm/app.js does - the monitor's dissolve keeps two alive).
 *   thumb(path, t, edge) -> Promise<{url, w, h} | null>      a still of a media file (`thumb`): cached by
 *               path + t (ms) + edge while alive; a miss (not decoded yet) is remembered with the epoch it
 *               missed at and asked again ONLY when `thumbEpoch` rises (App::render's re-ask), and the
 *               promise resolves then. null = never (the decode failed and the epoch stopped moving).
 *   thumbNow(path, t, edge) -> {url, w, h} | null            synchronous: cached or null, asks if missing
 *   still(id, edge) / stillNow(id, edge)                      a gallery still (`still`), cached by id + edge;
 *               dropped when the still leaves model.stills
 *   peaks(media, perSecond?) -> Promise<{perSecond, peaks: Float32Array} | null>   (`peaks`), cached by
 *               media + perSecond; a miss is asked again when model.peaksEpoch rises
 *   peaksNow(media, perSecond?)                               synchronous form
 *   capture(bind?) -> Promise<Blob>   image/png at full resolution (`capture`): Copy Frame. Rejects with
 *               the refusal ("Nothing to copy: no frame under the playhead").
 *   browse(path?, kinds?) -> Promise<{path, parent, entries:[{name, path, kind, size?}]}>
 *   release(url)                      revoke an object URL this session handed out (after a grace period)
 *
 * Every cache is bounded (LRU) and revokes the URLs it drops; a project closing or opening drops the
 * stills (their ids are per project). Thumbnails are keyed by path, so they stay valid across projects.
 */
import { signal, batch } from "../core/signal.js";

const THUMB_CAP = 600, STILL_CAP = 200, PEAKS_CAP = 64;
const GRACE_MS = 2000;     // an <img> still showing a URL keeps it alive this long after release

/** A Map used as an LRU: get() refreshes, set() evicts the oldest past `cap` through onEvict. */
class Lru {
  constructor(cap, onEvict) { this.cap = cap; this.map = new Map(); this.onEvict = onEvict; }
  get(k) { const v = this.map.get(k); if (v !== undefined) { this.map.delete(k); this.map.set(k, v); } return v; }
  has(k) { return this.map.has(k); }
  set(k, v) {
    if (this.map.has(k)) this.map.delete(k);
    this.map.set(k, v);
    while (this.map.size > this.cap) { const [ok, ov] = this.map.entries().next().value; this.map.delete(ok); this.onEvict(ov, ok); }
  }
  delete(k) { const v = this.map.get(k); if (v !== undefined) { this.map.delete(k); this.onEvict(v, k); } }
  clear() { for (const [k, v] of [...this.map]) { this.map.delete(k); this.onEvict(v, k); } }
  keys() { return [...this.map.keys()]; }
}

const ms = (t) => Math.round((Number(t) || 0) * 1000);
// the adapter's clamps (NtwbAdapter.h kThumbMinEdge..kThumbMaxEdge, still 0..kMaxEdge): the blob's meta
// echoes the clamped edge, and the request must key the same way
const thumbEdge = (e) => Math.max(16, Math.min(1024, Math.round(Number(e) || 160)));
const stillEdge = (e) => Math.max(0, Math.min(3840, Math.round(Number(e) || 0)));
const thumbKey = (path, t, edge) => `${path}|${ms(t)}|${edge}`;

export class InterstellarSession {
  constructor(bridge) {
    this.bridge = bridge;
    this.status = signal(bridge.status);
    this.presence = signal({ session: bridge.session, clients: bridge.clients });
    this.model = signal(null);
    this.transport = signal(null);
    this.thumbEpoch = signal(0);
    this.thumbsVersion = signal(0);
    this.stillsVersion = signal(0);
    this.peaksVersion = signal(0);
    this.lastRejection = signal(null);
    this._subs = new Map();
    // blob router: "stream|key" -> {blob, meta, at} that arrived before its call resolved, and waiters
    this._blobGot = new Map();
    this._blobWait = new Map();
    // caches
    const revokeLater = (url) => setTimeout(() => URL.revokeObjectURL(url), GRACE_MS);
    this._thumbs = new Lru(THUMB_CAP, (v) => { if (v && v.url) revokeLater(v.url); });
    this._thumbMiss = new Map();      // key -> epoch it missed at
    this._thumbPending = new Map();   // key -> Promise
    this._thumbWaiters = new Map();   // key -> [resolve] waiting for an epoch to re-ask
    this._stills = new Lru(STILL_CAP, (v) => { if (v && v.url) revokeLater(v.url); });
    this._stillPending = new Map();
    this._peaks = new Lru(PEAKS_CAP, () => {});
    this._peakMiss = new Map();       // key -> peaksEpoch it missed at
    this._peakPending = new Map();
    this._peakWaiters = new Map();

    bridge.onStatus((s) => { this.status.value = s; });
    bridge.onPresence((p) => { this.presence.value = { ...p }; });
    bridge.onState("model", (m) => { if (m) this._model(m); });
    bridge.onState("transport", (t) => { if (t) this.transport.value = t; });
    bridge.onState("thumbs", (t) => { if (t && typeof t.epoch === "number") this._thumbEpochMoved(t.epoch); });
    for (const stream of ["frame", "thumb", "still", "capture"]) bridge.onBlob(stream, (blob, header) => this._blob(stream, blob, header));
    bridge.onEvent("*", (name, data) => this._event(name, data));
  }

  // ------------------------------------------------------------------ commands
  command(line) { return this.bridge.call("command", { line }); }
  notify(line) { this.bridge.notify("command", { line }); }
  commands() { return this.bridge.call("commands", {}); }
  call(method, params) { return this.bridge.call(method, params || {}); }
  on(name, fn) {
    if (!this._subs.has(name)) this._subs.set(name, new Set());
    this._subs.get(name).add(fn);
    return () => this._subs.get(name).delete(fn);
  }

  // ------------------------------------------------------------------ the blob router
  _blob(stream, blob, header) {
    const meta = (header && header.meta) || {};
    const key = stream + "|" + this._blobKey(stream, meta);
    const w = this._blobWait.get(key);
    if (w) { this._blobWait.delete(key); w({ blob, meta }); return; }
    this._blobGot.set(key, { blob, meta, at: performance.now() });
    // a blob nobody claimed within a few seconds (its call failed after sending it) is dropped
    for (const [k, v] of this._blobGot) if (performance.now() - v.at > 10000) this._blobGot.delete(k);
  }
  _blobKey(stream, meta) {
    if (stream === "thumb") return thumbKey(meta.path, meta.t, meta.edge);
    if (stream === "still") return `${meta.id}|${meta.edge}`;
    return String(meta.seq);   // frame, capture: the adapter's seq, echoed in the call's result
  }
  /** The blob a call sent before its result: already here, or the next one with that key. */
  _claim(stream, key, timeoutMs = 15000) {
    const k = stream + "|" + key;
    const got = this._blobGot.get(k);
    if (got) { this._blobGot.delete(k); return Promise.resolve(got); }
    return new Promise((resolve) => {
      this._blobWait.set(k, resolve);
      setTimeout(() => { if (this._blobWait.get(k) === resolve) { this._blobWait.delete(k); resolve(null); } }, timeoutMs);
    });
  }
  release(url) { if (url) setTimeout(() => URL.revokeObjectURL(url), GRACE_MS); }

  // ------------------------------------------------------------------ the monitor's picture
  async requestFrame(params) {
    const r = await this.bridge.call("frame", params || {});
    if (!r || r.empty) return { empty: true, lastError: (r && r.lastError) || "" };
    const got = await this._claim("frame", String(r.seq));
    if (!got) return { empty: true, lastError: "" };
    return { url: URL.createObjectURL(got.blob), w: got.meta.w || r.w, h: got.meta.h || r.h, meta: got.meta };
  }

  // ------------------------------------------------------------------ thumbnails
  thumbNow(path, t = 0, edge = 160) {
    edge = thumbEdge(edge);
    const k = thumbKey(path, t, edge);
    const v = this._thumbs.get(k);
    if (v) return v;
    this.thumb(path, t, edge);
    return null;
  }
  thumb(path, t = 0, edge = 160) {
    if (!path) return Promise.resolve(null);
    edge = thumbEdge(edge);
    const k = thumbKey(path, t, edge);
    const hit = this._thumbs.get(k);
    if (hit) return Promise.resolve(hit);
    if (this._thumbPending.has(k)) return this._thumbPending.get(k);
    // missed at this epoch already: wait for the epoch to move (no request until then)
    if (this._thumbMiss.has(k) && this._thumbMiss.get(k) >= this.thumbEpoch.peek()) return this._waitThumb(k);
    const p = this._askThumb(path, t, edge, k);
    this._thumbPending.set(k, p);
    p.finally(() => this._thumbPending.delete(k));
    return p;
  }
  async _askThumb(path, t, edge, k) {
    let r;
    try { r = await this.bridge.call("thumb", { path, t: ms(t) / 1000, edge }); }
    catch (e) { return null; }                    // no thumbnailer in this build: placeholders, as the native
    if (!r || !r.ready) {
      this._thumbMiss.set(k, Math.max(r ? r.epoch || 0 : 0, this.thumbEpoch.peek()));
      return this._waitThumb(k);
    }
    const got = await this._claim("thumb", k);
    if (!got) return null;
    this._thumbMiss.delete(k);
    const v = { url: URL.createObjectURL(got.blob), w: got.meta.w, h: got.meta.h };
    this._thumbs.set(k, v);
    this.thumbsVersion.value = this.thumbsVersion.peek() + 1;
    return v;
  }
  _waitThumb(k) {
    return new Promise((resolve) => {
      if (!this._thumbWaiters.has(k)) this._thumbWaiters.set(k, []);
      this._thumbWaiters.get(k).push(resolve);
    });
  }
  _thumbEpochMoved(epoch) {
    if (epoch === this.thumbEpoch.peek()) return;
    this.thumbEpoch.value = epoch;
    // re-ask, once each, for what someone is waiting on (the views re-ask for the rest through thumbNow)
    const waiting = [...this._thumbWaiters];
    this._thumbWaiters.clear();
    for (const [k, resolvers] of waiting) {
      const [path, tms, edge] = splitKey(k);
      this._thumbMiss.delete(k);
      // ask the adapter directly: thumb() would hand back the pending promise that waits on THESE resolvers
      this._askThumb(path, tms / 1000, edge, k).then((v) => { for (const r of resolvers) r(v); });
    }
  }

  // ------------------------------------------------------------------ stills
  stillNow(id, edge = 320) {
    edge = stillEdge(edge);
    const v = this._stills.get(`${id}|${edge}`);
    if (v) return v;
    this.still(id, edge);
    return null;
  }
  still(id, edge = 320) {
    if (!id) return Promise.resolve(null);
    edge = stillEdge(edge);
    const k = `${id}|${edge}`;
    const hit = this._stills.get(k);
    if (hit) return Promise.resolve(hit);
    if (this._stillPending.has(k)) return this._stillPending.get(k);
    const p = (async () => {
      let r;
      try { r = await this.bridge.call("still", { id, edge }); } catch (e) { return null; }
      if (!r || r.empty) return null;
      const got = await this._claim("still", k);
      if (!got) return null;
      const v = { url: URL.createObjectURL(got.blob), w: got.meta.w, h: got.meta.h };
      this._stills.set(k, v);
      this.stillsVersion.value = this.stillsVersion.peek() + 1;
      return v;
    })();
    this._stillPending.set(k, p);
    p.finally(() => this._stillPending.delete(k));
    return p;
  }

  // ------------------------------------------------------------------ waveform envelopes
  peaksNow(media, perSecond = 0) {
    const v = this._peaks.get(`${media}|${perSecond}`);
    if (v) return v;
    this.peaks(media, perSecond);
    return null;
  }
  peaks(media, perSecond = 0) {
    if (!media) return Promise.resolve(null);
    const k = `${media}|${perSecond}`;
    const hit = this._peaks.get(k);
    if (hit) return Promise.resolve(hit);
    if (this._peakPending.has(k)) return this._peakPending.get(k);
    const m = this.model.peek();
    const epoch = (m && m.peaksEpoch) || 0;
    if (this._peakMiss.has(k) && this._peakMiss.get(k) >= epoch) {
      return new Promise((resolve) => {
        if (!this._peakWaiters.has(k)) this._peakWaiters.set(k, []);
        this._peakWaiters.get(k).push(resolve);
      });
    }
    const p = this._askPeaks(media, perSecond, k, epoch);
    this._peakPending.set(k, p);
    p.finally(() => this._peakPending.delete(k));
    return p;
  }

  async _askPeaks(media, perSecond, k, epoch) {
    let r;
    const params = { media };
    if (perSecond > 0) params.perSecond = perSecond;
    try { r = await this.bridge.call("peaks", params); } catch (e) { return null; }
    if (!r || !r.ready) {
      this._peakMiss.set(k, Math.max(epoch, (r && r.peaksEpoch) || 0));
      return new Promise((resolve) => {
        if (!this._peakWaiters.has(k)) this._peakWaiters.set(k, []);
        this._peakWaiters.get(k).push(resolve);
      });
    }
    const v = { perSecond: r.perSecond, peaks: Float32Array.from(r.peaks || []) };
    this._peakMiss.delete(k);
    this._peaks.set(k, v);
    this.peaksVersion.value = this.peaksVersion.peek() + 1;
    return v;
  }

  // ------------------------------------------------------------------ Copy Frame, the picker
  async capture(bind) {
    const r = await this.bridge.call("capture", bind ? { bind } : {});
    const got = await this._claim("capture", String(r.seq), 60000);
    if (!got) throw new Error("the frame did not arrive");
    return got.blob;
  }
  browse(path, kinds) {
    const p = {};
    if (path) p.path = path;
    if (kinds && kinds.length) p.kinds = kinds;
    return this.bridge.call("browse", p);
  }

  // ------------------------------------------------------------------ the model
  _model(m) {
    const prev = this.model.peek();
    batch(() => { this.model.value = m; });
    // a waveform landed: re-ask for the envelopes someone waits on
    if (prev && m.peaksEpoch !== prev.peaksEpoch && this._peakWaiters.size) {
      const waiting = [...this._peakWaiters];
      this._peakWaiters.clear();
      for (const [k, resolvers] of waiting) {
        const i = k.lastIndexOf("|");
        this._peakMiss.delete(k);
        this._askPeaks(k.slice(0, i), Number(k.slice(i + 1)), k, m.peaksEpoch || 0).then((v) => { for (const r of resolvers) r(v); });
      }
    }
    // stills that left the gallery (deleted, another project) leave the cache
    const ids = new Set((m.stills || []).map((s) => s.id));
    for (const k of this._stills.keys()) if (!ids.has(k.slice(0, k.lastIndexOf("|")))) this._stills.delete(k);
  }

  _event(name, data) {
    if (name === "command.rejected" || name === "error") {
      const why = data && data.fields && data.fields.why;
      this.lastRejection.value = { text: why || (data && data.line) || name, at: performance.now() };
    }
    for (const key of [name, "*"]) {
      const fns = this._subs.get(key);
      if (fns) for (const fn of [...fns]) { try { key === "*" ? fn(name, data) : fn(data); } catch (e) { console.error(e); } }
    }
  }
}

function splitKey(k) {
  const b = k.lastIndexOf("|"), a = k.lastIndexOf("|", b - 1);
  return [k.slice(0, a), Number(k.slice(a + 1, b)), Number(k.slice(b + 1))];
}
