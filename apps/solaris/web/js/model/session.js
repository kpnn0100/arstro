/*
 * Solaris — session.js: the MODEL side of the page (R-SVC-7; cosmo's js/model/session.js).
 *
 * The model itself is `solaris-cc ntwb serve` on the machine - THE SolarisService of this Arstro
 * Remote session, shared by every page of it. This class is its proxy in the page and nothing more:
 * it turns what the adapter publishes into signals and the view-model's intents into command lines.
 * It holds no view state and computes nothing the service could tell it:
 *
 *   state `model`      -> `model`      the AppModel, exactly what `state print --json` prints
 *   state `transport`  -> `transport`  what moves with no edit: position, playing, the meters
 *   events             -> on(name)     the service's Event stream, each with its --watch line
 *   status / presence  -> `status`, `presence`
 */
import { signal, batch } from "../core/signal.js";

export class SolarisSession {
  constructor(bridge) {
    this.bridge = bridge;
    this.status = signal(bridge.status);
    this.presence = signal({ session: bridge.session, clients: bridge.clients });
    this.model = signal(null);
    this.transport = signal(null);
    this.events = signal([]);             // the newest event lines, newest last (the console's log)
    this.lastRejection = signal(null);    // {text, at} of the newest command.rejected / error
    this._subs = new Map();

    bridge.onStatus((s) => { this.status.value = s; });
    bridge.onPresence((p) => { this.presence.value = { ...p }; });
    bridge.onState("model", (m) => { if (m) batch(() => { this.model.value = m; }); });
    bridge.onState("transport", (t) => { if (t) this.transport.value = t; });
    bridge.onEvent("*", (name, data) => this._event(name, data));
  }

  /** One line of the command grammar (docs/API.md), answered: resolves with {output, revision},
   *  rejects with the service's reason (also the command.rejected event and model.lastError). */
  command(line) { return this.bridge.call("command", { line }); }
  /** A line with no reply awaited - the per-frame `set` of a fader drag. */
  notify(line) { this.bridge.notify("command", { line }); }
  /** Every command with its usage, for the console's hints. */
  commands() { return this.bridge.call("commands", {}); }

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
