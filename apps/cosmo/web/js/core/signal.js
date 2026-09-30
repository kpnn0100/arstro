/*
 * Cosmo by arstro — signal.js: the view-model's reactive core (R-NTWB-7).
 *
 * The web front end is MVVM: the session's MODEL lives in cosmo-cc on the board and arrives
 * as NTWB state; each page holds its own VIEW-MODEL, made of these signals, and the views
 * subscribe to exactly the signals they read. That is what keeps two clients' views
 * independent (each page has its own signals) while the model they derive from is shared.
 *
 * Deliberately tiny and dependency-free (no build step, R-NTWB-5): a Signal holds a value,
 * `computed` derives one and re-derives only when something it read changed, `effect` runs a
 * function now and again whenever what it read changes. Reads are tracked automatically;
 * `batch` coalesces a burst of writes (one model push touches dozens of signals) into one
 * round of effects, so a view never paints a half-updated model.
 */

let tracking = null;                 // the computation currently reading signals
let batchDepth = 0;
const pending = new Set();           // effects to run when the outermost batch ends

class Node {
  constructor() { this.subs = new Set(); }
  track() {
    if (tracking) { this.subs.add(tracking); tracking.deps.add(this); }
  }
  notify() {
    for (const s of [...this.subs]) s.stale();
  }
}

export class Signal extends Node {
  constructor(value, equals = Object.is) { super(); this._v = value; this._eq = equals; }
  get value() { this.track(); return this._v; }
  set value(v) {
    if (this._eq(v, this._v)) return;
    this._v = v;
    batch(() => this.notify());
  }
  /** Read without subscribing - for event handlers and one-off decisions. */
  peek() { return this._v; }
  /** Write through a function of the current value. */
  update(fn) { this.value = fn(this._v); }
}

class Computed extends Node {
  constructor(fn, equals = Object.is) { super(); this.fn = fn; this.eq = equals; this.dirty = true; this.deps = new Set(); this._v = undefined; }
  /** A dependency changed. With nobody listening, just remember to recompute on the next read.
   *  With listeners, recompute NOW and tell them only if the value really changed - one model push
   *  touches every derived signal, and a view whose inputs came out the same must not re-run (it
   *  would reset a zoom, rebuild a control mid-drag, re-request a frame). */
  stale() {
    if (this.dirty) return;
    if (!this.subs.size) { this.dirty = true; return; }
    const old = this._v;
    this._compute();
    let same = false;
    try { same = this.eq(old, this._v); } catch { same = false; }
    if (!same) this.notify();
  }
  _compute() {
    for (const d of this.deps) d.subs.delete(this);
    this.deps.clear();
    const prev = tracking;
    tracking = this;
    try { this._v = this.fn(); } finally { tracking = prev; }
    this.dirty = false;
  }
  get value() {
    this.track();
    if (this.dirty) this._compute();
    return this._v;
  }
  peek() { const prev = tracking; tracking = null; try { return this.value; } finally { tracking = prev; } }
}

class Effect {
  constructor(fn) { this.fn = fn; this.deps = new Set(); this.cleanup = null; this.disposed = false; }
  stale() { if (!this.disposed) { pending.add(this); if (!batchDepth) flush(); } }
  run() {
    if (this.disposed) return;
    for (const d of this.deps) d.subs.delete(this);
    this.deps.clear();
    if (typeof this.cleanup === "function") { try { this.cleanup(); } catch (e) { console.error(e); } }
    const prev = tracking;
    tracking = this;
    try { this.cleanup = this.fn(); } catch (e) { console.error(e); } finally { tracking = prev; }
  }
  dispose() {
    this.disposed = true;
    for (const d of this.deps) d.subs.delete(this);
    this.deps.clear();
    if (typeof this.cleanup === "function") this.cleanup();
  }
}

function flush() {
  while (pending.size) {
    const list = [...pending];
    pending.clear();
    for (const e of list) e.run();
  }
}

export function signal(v, equals) { return new Signal(v, equals); }
export function computed(fn, equals) { return new Computed(fn, equals); }

/** Run `fn` now and whenever a signal it read changes; `fn` may return a cleanup. Returns the
 *  disposer. */
export function effect(fn) {
  const e = new Effect(fn);
  e.run();
  return () => e.dispose();
}

export function batch(fn) {
  batchDepth++;
  try { return fn(); } finally { if (--batchDepth === 0) flush(); }
}

/** Read signals inside `fn` without subscribing the current computation to them. */
export function untracked(fn) {
  const prev = tracking;
  tracking = null;
  try { return fn(); } finally { tracking = prev; }
}

/** Shallow structural equality for plain arrays / objects of primitives - for signals that
 *  are re-derived from every model push but usually come out the same. */
export function shallowEqual(a, b) {
  if (Object.is(a, b)) return true;
  if (!a || !b || typeof a !== "object" || typeof b !== "object") return false;
  const ka = Object.keys(a), kb = Object.keys(b);
  if (ka.length !== kb.length) return false;
  for (const k of ka) if (!Object.is(a[k], b[k])) return false;
  return true;
}

/** Deep equality for JSON-shaped values (the model's parts). */
export function jsonEqual(a, b) {
  if (Object.is(a, b)) return true;
  if (typeof a !== typeof b || !a || !b || typeof a !== "object") return false;
  if (Array.isArray(a) !== Array.isArray(b)) return false;
  if (Array.isArray(a)) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (!jsonEqual(a[i], b[i])) return false;
    return true;
  }
  const ka = Object.keys(a);
  if (ka.length !== Object.keys(b).length) return false;
  for (const k of ka) if (!jsonEqual(a[k], b[k])) return false;
  return true;
}
