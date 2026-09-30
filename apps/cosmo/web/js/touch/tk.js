/*
 * Cosmo by arstro — touch/tk.js: the touch shell's toolkit.
 *
 * Ports the helpers every PhoneApp widget is drawn with (touch/PhoneApp.cpp:50-100: the frame
 * constants, txt / txtC / txtR text placed by its alphabetic baseline, rrect / hline) and the
 * whole of touch/TouchIcons.cpp as SVG, plus the input and motion primitives the native gets
 * from Artboard: the gesture recogniser (core/Artboard/src/input/GestureRecognizer: tap, double
 * tap, 500 ms long-press, 5 px drag threshold), the critically damped ω = 18 spring that slider
 * thumbs follow (Spring.h:35) and the tray's white press wash (PhoneApp.cpp:356-357).
 *
 * Deliberate deviations: the long-press / double-tap slop is 16 px for a finger (the Android host's
 * own long-press slop, android_main.cpp:297-325) and 5 px for a mouse, and the Click that ends a
 * long-press is swallowed (native defect, screens_touch.md §14.2).
 */
import { h } from "../core/dom.js";
import { reducedMotion } from "../core/motion.js";

// PhoneApp.cpp:50-52 - the frame (1 dp = 1 CSS px).
export const K = {
  topBar: 48, crumb: 24, film: 72, toolBar: 56, action: 52, handle: 20, header: 36, hist: 60,
  row: 48, labelW: 120, valueW: 40, rail: 64, // rail = collapsed tray: tool bar + 8
};

export const FACE = {
  sans: "var(--font-sans)", medium: "var(--font-sans-medium)", semibold: "var(--font-sans-semibold)",
  mono: "var(--font-mono)", monoMedium: "var(--font-mono-medium)",
};
// Baseline -> top of a line box of height = size. The faces' hhea: Roboto 1900/-500 @2048,
// JetBrains Mono 1020/-300 @1000 (screens_touch.md §0.2). Chrome lays a line out with the ascent
// and descent each rounded to whole px and the half-leading floored, then snaps the glyphs to the
// pixel grid - so the offset is computed the same way, and a baseline lands on the native's row.
const ASC = { sans: 1900 / 2048, medium: 1900 / 2048, semibold: 1900 / 2048, mono: 1.02, monoMedium: 1.02 };
const DSC = { sans: 500 / 2048, medium: 500 / 2048, semibold: 500 / 2048, mono: 0.3, monoMedium: 0.3 };
/** Distance from the top of a line box (line-height = size) to its baseline, as Chrome places it. */
export function baseOffset(size, face = "sans") {
  const a = Math.round(ASC[face] * size), d = Math.round(DSC[face] * size);
  return Math.floor((size - a - d) / 2) + a - 0.01;      // a baseline on .5 goes to the lower row, like Cairo's
}

const u = (v) => (typeof v === "number" ? v + "px" : v);

/** Place `el` so the baseline of its `size` px text in `face` sits at (x, y); align = where x is
 *  (left / center / right) - the native txt / txtC / txtR (PhoneApp.cpp:64-69, measureText-based). */
export function place(el, x, y, size, face = "sans", align = "left") {
  const off = baseOffset(size, face);
  el.style.left = u(x);
  el.style.top = (typeof y === "number" ? y - off + "px" : `calc(${y} - ${off}px)`);
  el.style.fontSize = size + "px";
  el.style.lineHeight = size + "px";
  el.style.fontFamily = FACE[face];
  el.style.transform = align === "center" ? "translateX(-50%)" : align === "right" ? "translateX(-100%)" : "";
  return el;
}

/** A text run at a baseline (see place). `cls` adds classes (colour comes from CSS). */
export function tx(text, x, y, size, face = "sans", align = "left", cls = "") {
  const el = h("span.tt" + (cls ? "." + cls.trim().split(/\s+/).join(".") : ""), { text });
  return place(el, x, y, size, face, align);
}

/** A box of height H whose flowing text has its baseline at B (padding-top + a size-high line). */
export function baselineBox(el, H, B, size, face = "sans") {
  el.style.height = H + "px";
  el.style.paddingTop = Math.max(0, B - baseOffset(size, face)) + "px";
  el.style.lineHeight = size + "px";
  el.style.fontSize = size + "px";
  el.style.fontFamily = FACE[face];
  return el;
}

/** Absolute rect (numbers are px, strings are raw CSS lengths). */
export function rect(el, x, y, w, hh) {
  if (x !== undefined && x !== null) el.style.left = u(x);
  if (y !== undefined && y !== null) el.style.top = u(y);
  if (w !== undefined && w !== null) el.style.width = u(w);
  if (hh !== undefined && hh !== null) el.style.height = u(hh);
  return el;
}

// ------------------------------------------------------------------ TouchIcons.cpp as SVG
// viewBox 0 0 24 24 (the native maps a 24x24 SVG box into the target rect); strokes are device
// px (non-scaling), butt caps, miter joins. Circles: the native 4-cubic k=0.5523 ring is
// visually a circle. screens_touch.md §15.1.
const NS = "http://www.w3.org/2000/svg";
const S = 'vector-effect="non-scaling-stroke"';
const TI = {
  back: `<polyline ${S} points="15,18 9,12 15,6"/>`,
  undo: `<polyline ${S} points="9,14 4,9 9,4"/><path ${S} d="M20 20 L20 13 Q20 9 16 9 L4 9"/>`,
  redo: `<polyline ${S} points="15,14 20,9 15,4"/><path ${S} d="M4 20 L4 13 Q4 9 8 9 L20 9"/>`,
  more: `<circle cx="5" cy="12" r="1.4" fill="currentColor" stroke="none"/><circle cx="12" cy="12" r="1.4" fill="currentColor" stroke="none"/><circle cx="19" cy="12" r="1.4" fill="currentColor" stroke="none"/>`,
  chevronUp: `<polyline ${S} points="18,15 12,9 6,15"/>`,
  plusCircle: `<circle ${S} cx="12" cy="12" r="10"/><path ${S} d="M12 8 L12 16 M8 12 L16 12"/>`,
  folderOpen: `<polygon ${S} points="2,19 2,5 9,5 11,8 22,8 22,19"/>`,
  importDown: `<polyline ${S} points="21,15 21,19 3,19 3,15"/><polyline ${S} points="7,8 12,3 17,8"/><path ${S} d="M12 3 L12 15"/>`,
  search: `<circle ${S} cx="11" cy="11" r="7"/><path ${S} d="M16 16 L21 21"/>`,
  tabBasic: `<circle ${S} cx="12" cy="12" r="3"/><path ${S} d="M3 12 L4 12 M20 12 L21 12 M12 3 L12 4 M12 20 L12 21 M5.6 5.6 L6.3 6.3 M17.7 17.7 L18.4 18.4 M5.6 18.4 L6.3 17.7 M17.7 6.3 L18.4 5.6"/>`,
  tabMask: `<path ${S} d="M12 22 Q20 18 20 12 L20 5 L12 2 L4 5 L4 12 Q4 18 12 22 Z"/>`,
  tabCurve: `<path ${S} d="M3 20 C6 20 6 4 12 4 C18 4 18 20 21 20"/>`,
  tabGrade: `<circle ${S} cx="12" cy="12" r="4"/><path ${S} d="M12 2 L12 4 M12 20 L12 22 M2 12 L4 12 M20 12 L22 12 M4.9 4.9 L6.3 6.3 M17.7 17.7 L19.1 19.1 M4.9 19.1 L6.3 17.7 M17.7 6.3 L19.1 4.9"/>`,
  tabXform: `<polygon ${S} points="5,3 19,12 5,21"/>`,
};

/** A TouchIcons glyph, `size` px square, stroke `sw` device px (TouchIcons.h default 1.75). */
export function ticon(name, size = 20, sw = 1.75) {
  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.setAttribute("width", size);
  svg.setAttribute("height", size);
  svg.setAttribute("fill", "none");
  svg.setAttribute("stroke", "currentColor");
  svg.setAttribute("stroke-width", sw);
  svg.setAttribute("stroke-linecap", "butt");
  svg.setAttribute("stroke-linejoin", "miter");
  svg.setAttribute("aria-hidden", "true");
  svg.classList.add("ticon");
  svg.innerHTML = TI[name];
  return svg;
}

// ------------------------------------------------------------------ gestures
const local = (el, e) => { const r = el.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };

/**
 * The recogniser, per element (GestureRecognizer.cpp): one pointer at a time.
 *   down(e, p) -> false = not mine     tap(e, p)       doubleTap(e, p)   longPress(e, p)
 *   dragStart(e, p0, p)   drag(e, p, {dx, dy})   dragEnd(e, p, cancelled)   up(e)
 * p is local to `el` at event time. A drag starts after > 5 px; a long-press fires at 500 ms if the
 * pointer stayed within the slop and swallows the tap that would end it.
 */
export function gestures(el, g) {
  let st = null;
  let last = null;                                   // the previous tap: {t, x, y}
  const end = (e, cancelled) => {
    if (!st) return;
    const s = st; st = null;
    clearTimeout(s.timer);
    const p = local(el, e);
    if (s.dragging) { if (g.dragEnd) g.dragEnd(e, p, cancelled); }
    else if (!cancelled && !s.long && !s.moved) {
      const now = performance.now();
      const slop = s.touch ? 16 : 5;
      if (g.doubleTap && last && now - last.t <= 300 && Math.hypot(e.clientX - last.x, e.clientY - last.y) <= slop) {
        last = null;
        g.doubleTap(e, p);
      } else {
        last = { t: now, x: e.clientX, y: e.clientY };
        if (g.tap) g.tap(e, p);
      }
    }
    if (g.up) g.up(e, cancelled);
  };
  el.addEventListener("pointerdown", (e) => {
    if (st || (e.pointerType === "mouse" && e.button !== 0)) return;
    const p = local(el, e);
    if (g.down && g.down(e, p) === false) return;
    e.stopPropagation();                             // the innermost target owns the press (Artboard hit test)
    st = { id: e.pointerId, x0: e.clientX, y0: e.clientY, p0: p, dragging: false, long: false, timer: 0,
           touch: e.pointerType !== "mouse" };
    if (g.capture !== false) { try { el.setPointerCapture(e.pointerId); } catch { /* gone */ } }
    if (g.longPress) {
      st.timer = setTimeout(() => {
        if (st && !st.dragging) { st.long = true; g.longPress(e, st.p0); }
      }, 500);
    }
  });
  el.addEventListener("pointermove", (e) => {
    if (!st || e.pointerId !== st.id) return;
    const dx = e.clientX - st.x0, dy = e.clientY - st.y0, d = Math.hypot(dx, dy);
    if (!st.dragging) {
      if (d > (st.touch ? 16 : 5)) clearTimeout(st.timer);
      if (d > 5) st.moved = true;                    // travelled: no longer a tap
      if (d > 5 && !st.long && (g.drag || g.dragStart)) {
        st.dragging = true;
        if (g.dragStart && g.dragStart(e, st.p0, local(el, e)) === false) { st.dragging = false; st.long = true; return; }
      } else return;
    }
    if (g.drag) g.drag(e, local(el, e), { dx, dy });
  });
  el.addEventListener("pointerup", (e) => { if (st && e.pointerId === st.id) end(e, false); });
  el.addEventListener("pointercancel", (e) => { if (st && e.pointerId === st.id) end(e, true); });
  el.addEventListener("lostpointercapture", (e) => { if (st && e.pointerId === st.id && g.capture !== false) end(e, true); });
}

/** A plain tap target (with the press wash). */
export function tappable(el, onTap, { wash = true, doubleTap, longPress } = {}) {
  gestures(el, {
    down: () => { if (wash) pressWash(el); },
    tap: onTap ? (e, p) => onTap(e, p) : null,
    doubleTap, longPress,
  });
  return el;
}

// ------------------------------------------------------------------ motion
const EASE_OUT = "cubic-bezier(0.3333, 1, 0.6667, 1)";       // EaseOutCubic, exact (Easing.cpp:104)
export const EASE = { out: EASE_OUT, in: "cubic-bezier(0.3333, 0, 0.6667, 0)" };

/** On press: a white wash α 0.30 -> 0 over 300 ms EaseOutCubic over the pressed rect, radius 2
 *  (PhoneApp.cpp:357, Tray::pressAt). */
export function pressWash(el) {
  const w = h("i.press");
  el.append(w);
  const a = w.animate([{ opacity: 1 }, { opacity: 0 }], { duration: reducedMotion() ? 1 : 300, easing: EASE_OUT, fill: "forwards" });
  a.onfinish = () => w.remove();
}

/** Slide new content in from +dx to 0 (PhoneApp.cpp:291, Tray::slideIn - 200 ms EaseOutCubic),
 *  fading it in as it goes (the brief's tray-body cross-fade). */
export function slideIn(el, dx) {
  if (reducedMotion()) return;
  el.animate([{ transform: `translateX(${dx}px)`, opacity: 0 }, { transform: "translateX(0)", opacity: 1 }],
             { duration: 200, easing: EASE_OUT });
}

/** Critically damped follower, ω = 18 rad/s, dt capped at 50 ms (Spring.h:35, closed form). */
export class Spring {
  constructor(value, onUpdate, eps = 1e-3) {
    this.x = value; this.v = 0; this.target = value; this.onUpdate = onUpdate; this.eps = eps;
    this.raf = 0; this.t = 0;
    this._step = this._step.bind(this);
  }
  set(v) {
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = 0; this.x = this.target = v; this.v = 0;
    this.onUpdate(v);
  }
  to(target) {
    if (reducedMotion()) return this.set(target);
    this.target = target;
    if (!this.raf && Math.abs(this.x - target) > this.eps) { this.t = performance.now(); this.raf = requestAnimationFrame(this._step); }
  }
  _step(now) {
    const dt = Math.min(0.05, Math.max(0, (now - this.t) / 1000));
    this.t = now;
    const w = 18, y0 = this.x - this.target, c2 = this.v + w * y0, e = Math.exp(-w * dt);
    this.x = this.target + (y0 + c2 * dt) * e;
    this.v = (c2 - w * (y0 + c2 * dt)) * e;
    if (Math.abs(this.x - this.target) < this.eps && Math.abs(this.v) < this.eps * 10) {
      this.x = this.target; this.v = 0; this.raf = 0; this.onUpdate(this.x); return;
    }
    this.onUpdate(this.x);
    this.raf = requestAnimationFrame(this._step);
  }
}

// ------------------------------------------------------------------ misc
export const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
export const lerp = (a, b, t) => a + (b - a) * t;

/** A CSS custom property of the touch root, for canvas drawing (tokens stay in CSS). */
export function tok(el, name) { return getComputedStyle(el).getPropertyValue(name).trim(); }

/** HSV -> css (PhoneApp.cpp CurveEditor::hsv), h in degrees. */
export function hsv(hh, s, v) {
  const c = v * s, x = c * (1 - Math.abs(((hh / 60) % 2) - 1)), m = v - c;
  let r = 0, g = 0, b = 0;
  if (hh < 60) { r = c; g = x; } else if (hh < 120) { r = x; g = c; } else if (hh < 180) { g = c; b = x; }
  else if (hh < 240) { g = x; b = c; } else if (hh < 300) { r = x; b = c; } else { r = c; b = x; }
  return `rgb(${Math.round((r + m) * 255)},${Math.round((g + m) * 255)},${Math.round((b + m) * 255)})`;
}

/** A <canvas> sized to its CSS box at the device pixel ratio; draw in CSS px from the box's
 *  origin. `m` px of bleed on every side lets marks overhang the box (curve nodes at its corners
 *  - Artboard children are not clipped to their parent). */
export function canvasFor(el, m = 0) {
  const c = h("canvas");
  if (m) { c.style.position = "absolute"; c.style.left = -m + "px"; c.style.top = -m + "px"; }
  el.append(c);
  const ctx = c.getContext("2d");
  return {
    el: c, ctx,
    size() {
      const w = el.clientWidth, hh = el.clientHeight, d = window.devicePixelRatio || 1;
      const cw = Math.max(1, Math.round((w + 2 * m) * d)), ch = Math.max(1, Math.round((hh + 2 * m) * d));
      if (c.width !== cw || c.height !== ch) {
        c.width = cw; c.height = ch;
        c.style.width = w + 2 * m + "px"; c.style.height = hh + 2 * m + "px";
      }
      ctx.setTransform(d, 0, 0, d, m * d, m * d);
      return [w, hh];
    },
    clear() { ctx.save(); ctx.setTransform(1, 0, 0, 1, 0, 0); ctx.clearRect(0, 0, c.width, c.height); ctx.restore(); },
  };
}

/** Round a slider value like the native readout: lround, "+" only when the rounded value > 0
 *  (PhoneApp.cpp:136-138). */
export function readout(v) {
  const n = Math.sign(v) * Math.floor(Math.abs(v) + 0.5) || 0;
  return (n > 0 ? "+" : "") + n;
}
