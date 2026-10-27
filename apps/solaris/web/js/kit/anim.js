/*
 * Solaris — kit/anim.js: the native's frame-driven motion, for widgets that draw per frame.
 *
 *   Anim          artboard::AnimatedProperty — set(v) places, animateTo(target, ms, ease, nowMs) starts a tween
 *                 FROM WHERE IT IS NOW, update(nowMs) advances, value / animating / target. Reduced motion
 *                 lands every tween at once (Animation.cpp:61, FR-4e).
 *   Ease          artboard's easings by their names (Easing.cpp): Linear, EaseInCubic, EaseOutCubic, EaseInOutCubic.
 *   frames(fn)    run fn(nowMs) every animation frame for as long as it returns true (a widget's advance()).
 *   EasedScroll   interstellar_v1::EasedScroll (EasedScroll.h): target clamped at both ends, an eased live
 *                 value (motion::kScrollMs EaseOutCubic), scrollable() for wheel bubbling, reveal(), drawBar().
 *   AnimatedRows  interstellar_v1::AnimatedRows (AnimatedRows.h): keyed rows that TRAVEL — a moved row eases to its
 *                 y (kSelectMs), a new one fades in (kCrossFadeMs; not on the first sync), a removed one is kept
 *                 as a ghost and fades out.
 *   reduced()     the OS's "reduce motion" OR the app's setting (settings.reducedMotion — App.cpp:589-593); the
 *                 vm calls setAppReducedMotion. Cosmo's core/motion.js knows only the OS's (see its header).
 *
 * The durations are Theme.h's `motion::` (tokens.css --motion-*), read once here as MOTION.
 */

export const Ease = {
  Linear: (t) => t,
  EaseInCubic: (t) => t * t * t,
  EaseOutCubic: (t) => 1 - Math.pow(1 - t, 3),
  EaseInOutCubic: (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2),
};

// ------------------------------------------------------------------ reduced motion: the OS's, or the app's setting
const mq = typeof matchMedia === "function" ? matchMedia("(prefers-reduced-motion: reduce)") : null;
let appReduced = false;
export function setAppReducedMotion(on) { appReduced = !!on; }
export function reduced() { return appReduced || !!(mq && mq.matches); }
export function osReducedMotion() { return !!(mq && mq.matches); }

// ------------------------------------------------------------------ motion:: (Theme.h), from the generated tokens
let MOTION_CACHE = null;
/** Theme.h motion:: in ms: hover, modalOpen, modalClose, scroll, crossFade, dissolve, select, slide, catchUp, reflow,
 *  screenFade, toastIn, toastOut, toastHold, meterFall. */
export function motion() {
  if (MOTION_CACHE) return MOTION_CACHE;
  const cs = getComputedStyle(document.documentElement);
  const ms = (n, fb) => { const v = parseFloat(cs.getPropertyValue("--motion-" + n)); return Number.isFinite(v) ? v : fb; };
  MOTION_CACHE = {
    hover: ms("hover", 120), modalOpen: ms("modal-open", 150), modalClose: ms("modal-close", 120), scroll: ms("scroll", 180),
    crossFade: ms("cross-fade", 200), dissolve: ms("dissolve", 160), select: ms("select", 200), slide: ms("slide", 220),
    catchUp: ms("catch-up", 220), reflow: ms("reflow", 260), screenFade: ms("screen-fade", 260), toastIn: ms("toast-in", 150),
    toastOut: ms("toast-out", 300), toastHold: ms("toast-hold", 3200), meterFall: ms("meter-fall", 300),
  };
  return MOTION_CACHE;
}

// ------------------------------------------------------------------ AnimatedProperty
export class Anim {
  constructor(v = 0) { this.value = v; this.target = v; this._from = v; this._t0 = 0; this._ms = 0; this._ease = Ease.EaseOutCubic; this._on = false; }
  get animating() { return this._on; }
  /** Placed: no travel (the first placement of anything — there is nowhere to travel from). */
  set(v) { this.value = this.target = v; this._on = false; return this; }
  animateTo(target, ms, ease = Ease.EaseOutCubic, nowMs = performance.now()) {
    this.target = target;
    if (reduced() || !(ms > 0)) { this.value = target; this._on = false; return this; }
    this._from = this.value; this._t0 = nowMs; this._ms = ms; this._ease = ease; this._on = true;
    return this;
  }
  /** Advance to `nowMs`; true while it moves. */
  update(nowMs = performance.now()) {
    if (!this._on) return false;
    const k = Math.max(0, Math.min(1, (nowMs - this._t0) / this._ms));
    this.value = this._from + (this.target - this._from) * this._ease(k);
    if (k >= 1) { this.value = this.target; this._on = false; }
    return this._on;
  }
}

// ------------------------------------------------------------------ a widget's per-frame advance
const loops = new Set();
let raf = 0;
function frame(now) {
  raf = 0;
  for (const fn of [...loops]) {
    let keep = false;
    try { keep = fn(now); } catch (e) { console.error(e); }
    if (!keep) loops.delete(fn);
  }
  if (loops.size) raf = requestAnimationFrame(frame);
}
/** Run fn(nowMs) every animation frame while it returns true. Calling it again with the same fn while it runs is a
 *  no-op, so "something moved: keep advancing" is just `frames(advance)`. */
export function frames(fn) {
  loops.add(fn);
  if (!raf) raf = requestAnimationFrame(frame);
  return () => loops.delete(fn);
}

// ------------------------------------------------------------------ EasedScroll.h
export class EasedScroll {
  constructor() { this._target = 0; this._last = 0; this._live = new Anim(0); this.viewTop = 0; this.viewH = 0; this.contentH = 0; }
  /** Re-measure every layout; clamps the target when the content shrank under it. */
  setExtent(viewTop, viewH, contentH) {
    this.viewTop = viewTop; this.viewH = Math.max(0, viewH); this.contentH = Math.max(0, contentH);
    this._target = Math.min(Math.max(this._target, 0), this.maxScroll());
  }
  maxScroll() { return Math.max(0, this.contentH - this.viewH); }
  scrollable() { return this.maxScroll() > 0.5; }
  /** Move the target by px (+ toward the end); false when there is nothing to scroll (let the wheel bubble). */
  scrollBy(px) {
    if (!this.scrollable()) return false;
    this._target = Math.min(Math.max(this._target + px, 0), this.maxScroll());
    return true;
  }
  reveal(top, h) {
    if (top < this._target) this._target = top;
    else if (top + h > this._target + this.viewH) this._target = top + h - this.viewH;
    this._target = Math.min(Math.max(this._target, 0), this.maxScroll());
  }
  reset() { this._target = this._last = 0; this._live.set(0); }
  /** Starts the tween when the target moved; true while moving. */
  advance(nowMs) {
    if (this._target !== this._last) { this._live.animateTo(this._target, motion().scroll, Ease.EaseOutCubic, nowMs); this._last = this._target; }
    const moving = this._live.animating;
    this._live.update(nowMs);
    return moving;
  }
  value() { return this._live.value; }
  target() { return this._target; }
  bandVisible(top, h) { const y = top - this.value(); return y + h > 0 && y < this.viewH; }
  /** The thin pill thumb at the viewport's right edge, only when there is more (whiteAlpha .14). */
  drawBar(g, right, alpha = 1) {
    if (!this.scrollable() || this.viewH <= 0) return;
    const thumbH = Math.max(18, this.viewH * (this.viewH / this.contentH));
    const y = this.viewTop + (this.viewH - thumbH) * (this.value() / this.maxScroll());
    g.fillStyle = `rgba(255, 255, 255, ${0.14 * alpha})`;
    const r = 1.5;
    g.beginPath();
    g.roundRect(right - 4, y, 3, thumbH, r);
    g.fill();
  }
}

// ------------------------------------------------------------------ AnimatedRows.h
export class AnimatedRows {
  constructor() { this._rows = []; this._count = 0; this._synced = false; this._ever = false; }
  /** Re-key to `items` = [[key, data], ...] in display order; uniform row height. */
  sync(items, rowH) {
    for (const r of this._rows) r.index = -1;
    items.forEach(([key, data], i) => {
      let row = this._find(key);
      if (!row) {
        row = { key, data, index: -1, yTarget: 0, aTarget: 1, yLast: 0, aLast: 1, y: new Anim(0), alpha: new Anim(0), placed: false, gone: false,
                liveY() { return this.y.value; }, liveAlpha() { return this.alpha.value; } };
        this._rows.push(row);
      }
      row.data = data; row.index = i; row.yTarget = i * rowH; row.aTarget = 1; row.gone = false;
    });
    for (const r of this._rows) if (r.index < 0) { r.gone = true; r.aTarget = 0; }
    this._count = items.length;
    this._synced = true;
  }
  advance(nowMs) {
    const m = motion();
    for (const r of this._rows) {
      if (!r.placed) {
        r.y.set(r.yTarget); r.yLast = r.yTarget;
        if (this._ever && !reduced()) { r.alpha.set(0); r.alpha.animateTo(1, m.crossFade, Ease.EaseOutCubic, nowMs); } else r.alpha.set(1);
        r.aLast = 1; r.placed = true;
      }
      if (r.yTarget !== r.yLast) { r.y.animateTo(r.yTarget, m.select, Ease.EaseOutCubic, nowMs); r.yLast = r.yTarget; }
      if (r.aTarget !== r.aLast) { r.alpha.animateTo(r.aTarget, m.crossFade, Ease.EaseOutCubic, nowMs); r.aLast = r.aTarget; }
      r.y.update(nowMs); r.alpha.update(nowMs);
    }
    this._rows = this._rows.filter((r) => !(r.gone && !r.alpha.animating && r.alpha.value <= 0.001));
    if (this._synced) this._ever = true;
  }
  moving() { return this._rows.some((r) => r.y.animating || r.alpha.animating); }
  rows() { return this._rows; }
  count() { return this._count; }
  byIndex(i) { return this._rows.find((r) => r.index === i) || null; }
  _find(key) { return this._rows.find((r) => r.key === key && !r.gone) || this._rows.find((r) => r.key === key) || null; }
}
