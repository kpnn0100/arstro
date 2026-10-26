/*
 * Interstellar — session.js: the MODEL side of the page (R-SVC-4; Solaris's and cosmo's
 * js/model/session.js are the pattern).
 *
 * The model itself is `interstellar-cc ntwb serve` on the machine - THE InterstellarService of this
 * Arstro Remote session, shared by every page of it. This class is its proxy in the page and nothing
 * more: it turns what the adapter publishes into signals and the view-model's intents into command
 * lines. It holds no view state and computes nothing the service could tell it:
 *
 *   state `model`      -> `model`      the AppModel, exactly what `state print --json` prints
 *   state `transport`  -> `transport`  what moves with no edit: the playhead, the shuttle, the meters
 *   blob  `frame`      -> `frame`      the picture THIS page asked for (`frame` call) - never shared
 *   events             -> on(name)     the service's Event stream, each with its --watch line
 *   status / presence  -> `status`, `presence`
 */
import { signal, batch } from "../core/signal.js";

export class InterstellarSession {
  constructor(bridge) {
    this.bridge = bridge;
    this.status = signal(bridge.status);
    this.presence = signal({ session: bridge.session, clients: bridge.clients });
    this.model = signal(null);
    this.transport = signal(null);
    this.frame = signal(null);            // {url, meta}: the newest picture this page asked for
    this.events = signal([]);             // the newest event lines, newest last (the console's log)
    this.lastRejection = signal(null);    // {text, at} of the newest command.rejected / error
    this._subs = new Map();
    this._api = null;

    bridge.onStatus((s) => { this.status.value = s; });
    bridge.onPresence((p) => { this.presence.value = { ...p }; });
    bridge.onState("model", (m) => { if (m) batch(() => { this.model.value = m; }); });
    bridge.onState("transport", (t) => { if (t) this.transport.value = t; });
    bridge.onBlob("frame", (blob, header) => {
      const old = this.frame.peek();
      this.frame.value = { url: URL.createObjectURL(blob), meta: (header && header.meta) || {} };
      // the old picture goes once the new one has been drawn (the <img> holds it until then)
      if (old) setTimeout(() => URL.revokeObjectURL(old.url), 2000);
    });
    bridge.onEvent("*", (name, data) => this._event(name, data));
  }

  /** One line of the command grammar (docs/API.md), answered: resolves with {output, revision},
   *  rejects with the service's reason (also the command.rejected event and model.lastError). */
  command(line) { return this.bridge.call("command", { line }); }
  /** A line with no reply awaited - the per-frame `playhead` of a scrub, a slider's `set`. */
  notify(line) { this.bridge.notify("command", { line }); }
  /** Every command with its usage, for the console's hints. */
  commands() { return this.bridge.call("commands", {}); }
  /** Ask for the picture this page shows: {t?, source?, edge?}. Arrives on `frame`; resolves with
   *  {seq, w, h, bytes, ms} or {empty: true}. */
  requestFrame(params) { return this.bridge.call("frame", params || {}); }
  /** The API document (`api --json`): the address table with ranges and neutrals, so a slider's
   *  range is the core's, never a copy. Asked once per session. */
  api() {
    if (!this._api) {
      this._api = this.command("api --json").then((r) => JSON.parse(r.output)).catch((e) => { this._api = null; throw e; });
    }
    return this._api;
  }

  on(name, fn) {
    if (!this._subs.has(name)) this._subs.set(name, new Set());
    this._subs.get(name).add(fn);
    return () => this._subs.get(name).delete(fn);
  }

  _event(name, data) {
    const line = (data && data.line) || "[evt] " + name;
    const list = this.events.peek();
    this.events.value = (list.length >= 200 ? list.slice(-199) : list.slice()).concat([{ name, line, at: performance.now() }]);
    if (name === "command.rejected" || name === "error") {
      const why = data && data.fields && data.fields.why;
      this.lastRejection.value = { text: why || line, at: performance.now() };
    }
    for (const key of [name, "*"]) {
      const fns = this._subs.get(key);
      if (fns) for (const fn of [...fns]) { try { key === "*" ? fn(name, data) : fn(data); } catch (e) { console.error(e); } }
    }
  }
}

/** A value as one token of the command grammar: quoted when it has a space or a quote. */
export function token(s) {
  s = String(s);
  if (/[\n]/.test(s)) throw new Error("a line break cannot be part of a command");
  return s === "" || /[\s"]/.test(s) ? `"${s.replace(/"/g, "'")}"` : s;
}
