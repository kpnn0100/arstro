/*
 * Cosmo by arstro — session.js: the MODEL side of the page (R-NTWB-7).
 *
 * The model itself is cosmo-cc on the board - THE CosmoService, one per Arstro Remote session.
 * This class is its proxy in the page, nothing more: it turns what the adapter publishes into
 * signals and turns the view-model's intents into command lines. It holds no view state and
 * computes nothing the service could tell it:
 *
 *   state `model`   -> `model`     the AppModel (formatModel JSON), params parsed (params.js)
 *   `controls`      -> `controls`  the slider catalogue with sampled conversions (R-NTWB-4)
 *   blob `preview`  -> `frame`     the newest rendered preview + its histogram (R-NTWB-2)
 *   blob `thumb`    -> `thumbs`    filmstrip thumbnails by node, fetched once per decoded slot
 *   events          -> on(name)    the service's Event stream, each with its formatEvent() line
 *   status/presence -> `status`, `presence`  (NTWB 1.1: which session, how many share it)
 *
 * Every page has its own view-model on top of this, so two browsers on one session show the
 * same model - each in its own layout - and see each other's edits as the model changes.
 */
import { signal, batch } from "../core/signal.js";
import { parseParams } from "./params.js";

export class CosmoSession {
  constructor(bridge) {
    this.bridge = bridge;
    this.status = signal(bridge.status);
    this.presence = signal({ session: bridge.session, clients: bridge.clients });
    this.model = signal(null);
    this.controls = signal(null);
    this.frame = signal(null);            // {url, meta, blob}
    this.thumbs = signal(new Map());      // node -> {url, w, h, slot}
    this.lastRejection = signal(null);    // {text, at} of the newest command.rejected / error event
    this.opening = signal(null);          // {name, total} from project.opening, until the load ends
    this._events = new Map();
    this._thumbSlots = new Map();         // node -> slot we asked a thumbnail for
    this._thumbTimer = 0;

    bridge.onStatus((s, detail) => { this.status.value = s; if (s !== "running") this._thumbSlots.clear(); this.statusDetail = detail; });
    bridge.onPresence((p) => { this.presence.value = { ...p }; });
    bridge.onReady(() => this._ready());
    bridge.onState("model", (m) => this._model(m));
    bridge.onBlob("preview", (blob, h) => this._preview(blob, h));
    bridge.onBlob("thumb", (blob, h) => this._thumb(blob, h));
    bridge.onEvent("*", (name, data) => this._event(name, data));
  }

  // ------------------------------------------------------------------ intents -> the model
  /** One line of the command grammar (R-SVC-5), answered: resolves with the adapter's reply,
   *  rejects with the service's reason (it also lands in model.lastError). */
  command(line) { return this.bridge.call("command", { line }); }
  /** A line without a reply - the per-frame `set` of a drag (between `gesture on|off`). */
  notify(line) { this.bridge.notify("command", { line }); }
  call(method, params) { return this.bridge.call(method, params || {}); }
  /** A folder of the board (the open / import / export pickers). */
  browse(path) { return this.bridge.call("browse", path ? { path } : {}); }

  on(name, fn) {
    if (!this._events.has(name)) this._events.set(name, new Set());
    this._events.get(name).add(fn);
    return () => this._events.get(name).delete(fn);
  }

  // ------------------------------------------------------------------ the model -> signals
  _ready() {
    this.call("controls").then((c) => { this.controls.value = c; }).catch((e) => console.warn("controls", e));
    this.call("frame").catch(() => {});                      // the newest preview, for a late client
    this._thumbSlots.clear();
    this._wantThumbs();
  }

  _model(raw) {
    if (!raw) return;
    // Parse the params only when their TEXT changed: a model push per frame of a drag would
    // otherwise hand every view a new object and re-run everything that reads them.
    const prev = this.model.peek();
    const p = prev && prev.params === raw.params ? prev.p : parseParams(raw.params || "");
    const m = { ...raw, p };
    batch(() => { this.model.value = m; });
    this._wantThumbs();
  }

  _preview(blob, header) {
    const meta = header.meta || {};
    const url = URL.createObjectURL(blob);
    const prev = this.frame.peek();
    this.frame.value = { url, meta, blob };
    // The view keeps the previous image on screen until the new one has decoded; give it a
    // moment before the old URL goes away.
    if (prev) setTimeout(() => URL.revokeObjectURL(prev.url), 4000);
  }

  _thumb(blob, header) {
    const meta = header.meta || {};
    const next = new Map(this.thumbs.peek());
    const old = next.get(meta.node);
    next.set(meta.node, { url: URL.createObjectURL(blob), w: meta.w, h: meta.h, slot: meta.slot });
    this.thumbs.value = next;
    if (old) setTimeout(() => URL.revokeObjectURL(old.url), 4000);
  }

  /** Ask for the thumbnails of decoded photos we have none for (once per node and slot). */
  _wantThumbs() {
    const m = this.model.peek();
    if (!m || this.status.peek() !== "running") return;
    const missing = (m.nodes || []).filter((n) => n.slot >= 0 && this._thumbSlots.get(n.node) !== n.slot);
    if (!missing.length) return;
    clearTimeout(this._thumbTimer);
    // A load publishes a model per decoded photo; one request for the batch is enough.
    this._thumbTimer = setTimeout(() => {
      const now = this.model.peek();
      const want = (now.nodes || []).filter((n) => n.slot >= 0 && this._thumbSlots.get(n.node) !== n.slot);
      for (const n of want) this._thumbSlots.set(n.node, n.slot);
      if (want.length === 1) this.call("thumbs", { node: want[0].node }).catch(() => {});
      else if (want.length) this.call("thumbs").catch(() => {});
    }, 120);
  }

  _event(name, data) {
    // A new project reuses node and slot numbers: the old thumbnails would show on its cells.
    if (name === "project.opening") {
      this.opening.value = { name: (data && data.text) || "", total: (data && data.b) || 0 };
      for (const t of this.thumbs.peek().values()) setTimeout(() => URL.revokeObjectURL(t.url), 4000);
      this.thumbs.value = new Map();
      this._thumbSlots.clear();
    }
    if (name === "load.finished" || name === "project.closed") this.opening.value = null;
    if (name === "command.rejected" || name === "error") {
      this.lastRejection.value = { text: (data && data.text) || (data && data.line) || name, at: performance.now() };
    }
    for (const key of [name, "*"]) {
      const fns = this._events.get(key);
      if (fns) for (const fn of [...fns]) { try { key === "*" ? fn(name, data) : fn(data); } catch (e) { console.error(e); } }
    }
  }
}

/** A path as one token of the command grammar: quoted when it has spaces. */
export function quotePath(s) {
  if (/["\n]/.test(s)) throw new Error("paths with quotes or line breaks are not supported");
  return /\s/.test(s) ? `"${s}"` : s;
}
