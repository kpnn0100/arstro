/*
 * Cosmo by arstro — motion.js: R-G-1 in the browser - nothing a user can see changes in one frame.
 *
 * Most motion is CSS (transitions on transform / opacity / width, with the durations and
 * easings cosmo uses, as custom properties in base.css). This file is for what CSS cannot
 * ease: values drawn into a <canvas> (the histogram, curves, wheels), numbers derived from an
 * eased value, and scroll offsets. `Tween` is Artboard's AnimatedProperty: `to(target, ms,
 * ease)` starts from wherever the value is NOW, so a new target mid-flight never jumps.
 *
 * Reduced motion is honoured here once, like Artboard honours it in its primitives: the OS
 * preference (prefers-reduced-motion) makes every tween land in one step and base.css zeroes
 * the CSS durations.
 */

// Artboard's easings (core/Artboard/src/anim/Easing): the same curves, by the same names.
export const Ease = {
  Linear: (t) => t,
  EaseOutCubic: (t) => 1 - Math.pow(1 - t, 3),
  EaseInCubic: (t) => t * t * t,
  EaseInOutCubic: (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2),
};

const mq = typeof matchMedia === "function" ? matchMedia("(prefers-reduced-motion: reduce)") : null;
export function reducedMotion() { return !!(mq && mq.matches); }

const active = new Set();
let raf = 0;
function frame(now) {
  raf = 0;
  for (const t of [...active]) t._step(now);
  if (active.size) raf = requestAnimationFrame(frame);
}
function schedule(t) {
  active.add(t);
  if (!raf) raf = requestAnimationFrame(frame);
}

/** One eased scalar (or array of scalars). onUpdate(value) runs every frame while moving. */
export class Tween {
  constructor(value, onUpdate) {
    this.value = value;
    this.target = value;
    this.onUpdate = onUpdate || null;
    this._from = value;
    this._t0 = 0;
    this._ms = 0;
    this._ease = Ease.EaseOutCubic;
  }
  get animating() { return active.has(this); }
  /** Snap (the first placement of anything - there is nowhere to travel from). */
  set(v) {
    active.delete(this);
    this.value = this.target = v;
    if (this.onUpdate) this.onUpdate(v);
  }
  to(target, ms = 180, ease = Ease.EaseOutCubic) {
    if (same(target, this.target) && this.animating) return;
    if (same(target, this.value) && !this.animating) { this.target = target; return; }
    if (reducedMotion() || ms <= 0) return this.set(target);
    this._from = clone(this.value);
    this.target = target;
    this._t0 = performance.now();
    this._ms = ms;
    this._ease = ease;
    schedule(this);
  }
  _step(now) {
    // A frame stamped before to() was called (rAF times are the frame's start) is progress 0,
    // not a step backwards.
    const k = Math.max(0, Math.min(1, (now - this._t0) / this._ms));
    const e = this._ease(k);
    this.value = lerp(this._from, this.target, e);
    if (k >= 1) { this.value = this.target; active.delete(this); }
    if (this.onUpdate) this.onUpdate(this.value);
  }
}

function lerp(a, b, t) {
  if (Array.isArray(b)) {
    const out = new Array(b.length);
    for (let i = 0; i < b.length; i++) out[i] = (a && a[i] !== undefined ? a[i] : b[i]) + ((b[i] - (a && a[i] !== undefined ? a[i] : b[i])) * t);
    return out;
  }
  return a + (b - a) * t;
}
function clone(v) { return Array.isArray(v) ? v.slice() : v; }
function same(a, b) {
  if (Array.isArray(a) && Array.isArray(b)) return a.length === b.length && a.every((x, i) => x === b[i]);
  return a === b;
}

/** Wait two frames: a class added now will transition from the state the browser has laid out. */
export function nextFrame() { return new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))); }
