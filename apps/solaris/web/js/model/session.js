/*
 * Solaris — model/session.js: the MODEL side of the page (R-SVC-7; cosmo's js/model/session.js).
 *
 * The model itself is `solaris-cc ntwb serve` on the machine — THE SolarisService of this Arstro Remote session (the
 * manifest is `single: true`: one session, shared by every page). This class is its proxy in the page and nothing
 * more: what the adapter publishes becomes signals, the view-model's intents become calls. It holds no view state and
 * computes nothing the service could tell it (cli/NtwbAdapter.h is the other half):
 *
 *   state `model`      -> model       the AppModel, exactly what `state print --json` prints (AppModelCodec.cpp
 *                                     modelToJson: read the codec for field names — never guess them)
 *   state `transport`  -> transport   what moves with no edit, ≤ 20/s: {playing, position, loopFrom, loopTo, latencyMs,
 *                                     masterPeak: [l, r], peaks: {<strip>: [l, r]}, audition, auditionPlaying,
 *                                     auditionProgress}
 *   state `live`       -> live        EVERY value that moves with no edit, at the model's own paths, ≤ 25/s
 *                                     (NtwbAdapter::liveJson: transport, strips' meters, readings, bindings' live,
 *                                     loudness, video time, audition, midiMessages) — the vm merges it over `model`
 *   events             -> on(name, fn) / events   the service's Event stream: {line (its --watch line), fields}
 *   status, presence   -> status ("starting" | "running" | "stopped" | "failed" …), presence {session, clients}
 *   lastRejection      -> {text, at} of the newest command.rejected / error event (any page's)
 *
 *   command(line)  -> Promise<{output, revision}>   one line of the grammar; rejects with the service's sentence
 *   notify(line)                                     the same line, no reply awaited (a drag's per-frame `set`)
 *   browse(path, kinds) -> Promise<{path, parent, entries: [{name, path, kind, size?}]}>   a folder of the machine
 *   commands()     -> Promise<[{name, usage, summary}]>
 *   call(method, params)                             any other adapter method (docs/ntwb-api.json)
 */
import { signal, batch } from "../core/signal.js";

export class SolarisSession {
  constructor(bridge) {
    this.bridge = bridge;
    this.status = signal(bridge.status);
    this.presence = signal({ session: bridge.session, clients: bridge.clients });
    this.model = signal(null);
    this.transport = signal(null);
    this.live = signal(null);
    this.events = signal([]);             // the newest event lines, newest last (at most 200)
    this.lastRejection = signal(null);
    this._subs = new Map();

    bridge.onStatus((s) => { this.status.value = s; });
    bridge.onPresence((p) => { this.presence.value = { ...p }; });
    bridge.onState("model", (m) => { if (m) batch(() => { this.model.value = m; }); });
    bridge.onState("transport", (t) => { if (t) this.transport.value = t; });
    bridge.onState("live", (l) => { if (l) this.live.value = l; });
    bridge.onEvent("*", (name, data) => this._event(name, data));
  }

  command(line) { return this.bridge.call("command", { line }); }
  notify(line) { this.bridge.notify("command", { line }); }
  commands() { return this.bridge.call("commands", {}); }
  browse(path, kinds) {
    const p = {};
    if (path) p.path = path;
    if (kinds && kinds.length) p.kinds = kinds;
    return this.bridge.call("browse", p);
  }
  call(method, params) { return this.bridge.call(method, params || {}); }

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
