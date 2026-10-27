/*
 * Interstellar — kit/paint.js: the native widgets' immediate-mode painting, in the page.
 *
 * Every widget of app/ is an Artboard Segment: advance(nowMs) moves its AnimatedProperty / HoverFade
 * values, onPaint(IRenderTarget&) re-issues every rect, line and run of text from the model and those
 * values. A port keeps that shape so it reads line for line against its native file:
 *
 *   const p = new Painter(svg)            one <svg> per widget, rebuilt on a frame something changed
 *   p.begin() … p.end()                   onPaint's body between them
 *   p.rect(x, y, w, h, r, {fill, stroke, sw})      drawRoundedRect (radius clamped to half the short
 *                                                  side, Shapes.cpp), Paint::filled / stroked / filledStroked
 *   p.corners(x, y, w, h, tl, tr, br, bl, fill)    drawRoundedRectCorners
 *   p.line(x0, y0, x1, y1, color, sw)              a stroked segment (glyph::line, beginPath/lineTo/strokePath)
 *   p.path(d, {fill, stroke, sw}) · p.circle(cx, cy, r, {fill, stroke, sw})
 *   p.text(s, x, baseline, px, family, color, tracking)   IRenderTarget::drawText at its ALPHABETIC baseline
 *                                                  (SVG text rounds like Cairo; an HTML box would snap)
 *   p.image(url, x, y, w, h, opacity) · p.cover(url, imgW, imgH, x, y, w, h, opacity)   ImageSlot::drawCover
 *   p.icon(name, x, y, size, color, sw)            cosmo's icon::* (ui/icons.js)
 *   p.add(node)                                    a kit/glyphs.js glyph as it is
 *   p.save() / p.clip(x, y, w, h) / p.group(alpha) / p.restore()   t.save / clipRect / pushLayer / pop
 *
 * Colours are [r, g, b, a] in 0..1 from the GENERATED tokens (col("primary")), or a CSS string:
 *   col(name) · css(c) · fade(c, a) (c.a *= a) · lerp(a, b, t) (lerpColor) · brighten(c, k)
 *   whiteA(a) / primaryA(a) (palette::whiteAlpha / primaryAlpha) · hoverWash(t) (whiteAlpha(0.07 t))
 *
 * Motion (Artboard, core/Artboard/src/anim):
 *   new Anim(v)        AnimatedProperty: set(v), animateTo(target, ms, ease, nowMs) from wherever it is
 *                      NOW, update(nowMs), value, target, animating; reduced motion lands in one step
 *   new HoverFade()    cosmo's widgets/HoverFade.h: setHovered(id), clear(), advance(nowMs, ms = 120),
 *                      amount(id) through smoothstep, moving
 *   frameLoop(step)    the widget's frame clock: kick() runs step(nowMs) every animation frame while it
 *                      returns true (something still moves), then sleeps until the next kick()
 * Pointer: localPoint(el, e) - the event in el's own logical px under the shell's scale; inRect(p, r).
 */
import { tok } from "./tokens.js";
import { FAMILY } from "./textfit.js";
import { icon as cosmoIcon } from "../ui/icons.js";
import { Ease, reducedMotion } from "../core/motion.js";

export { Ease, FAMILY as F };

// ------------------------------------------------------------------ colours
const COLS = new Map();
/** A token as [r, g, b, a] (read once; the palette does not move at run time in Interstellar). */
export function col(name) {
  let c = COLS.get(name);
  if (!c) { c = tok.color(name); COLS.set(name, c); }
  return c;
}
export const fade = (c, a) => [c[0], c[1], c[2], c[3] * a];
export const lerp = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
export const brighten = (c, k) => [c[0] + (1 - c[0]) * k, c[1] + (1 - c[1]) * k, c[2] + (1 - c[2]) * k, c[3]];
export const whiteA = (a) => [1, 1, 1, a];
export const primaryA = (a) => { const p = col("primary"); return [p[0], p[1], p[2], a]; };
export const hoverWash = (t) => [1, 1, 1, 0.07 * t];
const q3 = (v) => Math.round(v * 1000) / 1000;
export function css(c) {
  if (typeof c === "string") return c;
  if (!c) return "none";
  return `rgba(${q3(c[0] * 255)}, ${q3(c[1] * 255)}, ${q3(c[2] * 255)}, ${q3(Math.max(0, Math.min(1, c[3])))})`;
}

// ------------------------------------------------------------------ the painter
const NS = "http://www.w3.org/2000/svg";
export function svgEl(tag, attrs) {
  const e = document.createElementNS(NS, tag);
  if (attrs) for (const k in attrs) if (attrs[k] !== undefined && attrs[k] !== null) e.setAttribute(k, attrs[k]);
  return e;
}
const f = (n) => Math.round(n * 1000) / 1000;
let clipSeq = 0;

export class Painter {
  constructor(svg) {
    this.svg = svg;
    this.images = new Map();     // url -> <image>, kept across frames so a picture never reloads
    this.used = new Set();
  }
  begin() {
    this.frag = document.createDocumentFragment();
    this.defs = svgEl("defs");
    this.frag.append(this.defs);
    this.stack = [this.frag];
    this.used = new Set();
    this.clipBase = ++clipSeq;
    this.clipN = 0;
  }
  get cur() { return this.stack[this.stack.length - 1]; }
  end() {
    this.svg.replaceChildren(this.frag);
    for (const k of [...this.images.keys()]) if (!this.used.has(k)) this.images.delete(k);
  }
  _paint(e, o) {
    e.style.fill = o && o.fill ? css(o.fill) : "none";
    if (o && o.stroke) {
      e.style.stroke = css(o.stroke);
      e.setAttribute("stroke-width", o.sw ?? 1);
    }
    return e;
  }
  rect(x, y, w, h, r = 0, o = {}) {
    if (!(w > 0) || !(h > 0)) return null;
    const k = Math.max(0, Math.min(r, w / 2, h / 2));
    const e = svgEl("rect", { x: f(x), y: f(y), width: f(w), height: f(h), rx: k ? f(k) : null, ry: k ? f(k) : null });
    this.cur.append(this._paint(e, o));
    return e;
  }
  corners(x, y, w, h, tl, tr, br, bl, fill) {
    const kk = 0.5522847498307936, c = (r) => Math.max(0, Math.min(r, w / 2, h / 2));
    tl = c(tl); tr = c(tr); br = c(br); bl = c(bl);
    const d = `M${f(x + tl)} ${f(y)}L${f(x + w - tr)} ${f(y)}C${f(x + w - tr + tr * kk)} ${f(y)} ${f(x + w)} ${f(y + tr - tr * kk)} ${f(x + w)} ${f(y + tr)}` +
      `L${f(x + w)} ${f(y + h - br)}C${f(x + w)} ${f(y + h - br + br * kk)} ${f(x + w - br + br * kk)} ${f(y + h)} ${f(x + w - br)} ${f(y + h)}` +
      `L${f(x + bl)} ${f(y + h)}C${f(x + bl - bl * kk)} ${f(y + h)} ${f(x)} ${f(y + h - bl + bl * kk)} ${f(x)} ${f(y + h - bl)}` +
      `L${f(x)} ${f(y + tl)}C${f(x)} ${f(y + tl - tl * kk)} ${f(x + tl - tl * kk)} ${f(y)} ${f(x + tl)} ${f(y)}Z`;
    return this.path(d, { fill });
  }
  line(x0, y0, x1, y1, color, sw = 1) {
    const e = svgEl("path", { d: `M${f(x0)} ${f(y0)}L${f(x1)} ${f(y1)}`, "stroke-width": sw, fill: "none" });
    e.style.stroke = css(color);
    this.cur.append(e);
    return e;
  }
  path(d, o = {}) {
    const e = svgEl("path", { d });
    this.cur.append(this._paint(e, o));
    return e;
  }
  circle(cx, cy, r, o = {}) {
    const e = svgEl("circle", { cx: f(cx), cy: f(cy), r: f(Math.max(0, r)) });
    this.cur.append(this._paint(e, o));
    return e;
  }
  text(s, x, base, px, family = FAMILY.sans, color = "var(--foreground)", tracking = 0) {
    s = String(s ?? "");
    if (!s) return null;
    const e = svgEl("text", { x: f(x), y: f(base), "font-size": px, "font-family": `"${family}"`, "letter-spacing": tracking ? f(tracking) : null });
    e.style.fill = css(color);
    e.style.whiteSpace = "pre";
    e.textContent = s;
    this.cur.append(e);
    return e;
  }
  image(url, x, y, w, h, opacity = 1) {
    let e = this.images.get(url);
    if (!e) {
      e = svgEl("image", { preserveAspectRatio: "none" });
      e.setAttribute("href", url);
      this.images.set(url, e);
    }
    this.used.add(url);
    for (const [k, v] of [["x", x], ["y", y], ["width", w], ["height", h]]) e.setAttribute(k, f(v));
    e.setAttribute("opacity", f(opacity));
    this.cur.append(e);
    return e;
  }
  /** ImageSlot::drawCover: the picture scaled to COVER the rect, centred, clipped to it. */
  cover(url, iw, ih, x, y, w, h, opacity = 1) {
    if (!(iw > 0) || !(ih > 0) || !(w > 0) || !(h > 0)) return null;
    const s = Math.max(w / iw, h / ih), dw = iw * s, dh = ih * s;
    this.clip(x, y, w, h);
    const e = this.image(url, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh, opacity);
    this.restore();
    return e;
  }
  /** cosmo's icon::<name>(t, box, color, sw) in a size × size box at (x, y). */
  icon(name, x, y, size, color, sw) {
    const s = cosmoIcon(name, size, sw);              // a nested <svg> with its own viewBox
    s.setAttribute("x", f(x));
    s.setAttribute("y", f(y));
    s.setAttribute("overflow", "visible");
    s.style.color = css(color);
    this.cur.append(s);
    return s;
  }
  add(node) { if (node) this.cur.append(node); return node; }
  save() { const g = svgEl("g"); this.cur.append(g); this.stack.push(g); return g; }
  clip(x, y, w, h) {
    const id = `pt-clip-${this.clipBase}-${this.clipN++}`;
    const cp = svgEl("clipPath", { id });
    cp.append(svgEl("rect", { x: f(x), y: f(y), width: f(Math.max(0, w)), height: f(Math.max(0, h)) }));
    this.defs.append(cp);
    const g = svgEl("g", { "clip-path": `url(#${id})` });
    this.cur.append(g);
    this.stack.push(g);
    return g;
  }
  group(opacity = 1) { const g = svgEl("g", { opacity: f(Math.max(0, Math.min(1, opacity))) }); this.cur.append(g); this.stack.push(g); return g; }
  restore() { if (this.stack.length > 1) this.stack.pop(); }
}

/** A full-size <svg> in logical px (overflow visible, no pointer events of its own). */
export function canvasSvg(cls = "") {
  const s = svgEl("svg", { class: "pt-svg" + (cls ? " " + cls : "") });
  Object.assign(s.style, { position: "absolute", left: "0", top: "0", overflow: "visible", pointerEvents: "none" });
  return s;
}

// ------------------------------------------------------------------ motion
/** Artboard's AnimatedProperty: a value eased toward a target over a duration, on the caller's clock. */
export class Anim {
  constructor(v = 0) { this.v = v; this.from = v; this.to = v; this.t0 = 0; this.ms = 0; this.ease = Ease.EaseOutCubic; this.running = false; }
  get value() { return this.v; }
  get target() { return this.to; }
  get animating() { return this.running; }
  set(v) { this.v = this.from = this.to = v; this.running = false; }
  /** From wherever the value is NOW, so a new target mid-flight never jumps. */
  animateTo(target, ms, ease, nowMs) {
    if (this.running && target === this.to) return;
    if (!this.running && target === this.v) { this.to = target; return; }
    if (reducedMotion() || !(ms > 0)) { this.set(target); return; }
    this.from = this.v; this.to = target; this.t0 = nowMs; this.ms = ms; this.ease = ease || Ease.EaseOutCubic; this.running = true;
  }
  update(nowMs) {
    if (!this.running) return false;
    const k = Math.max(0, Math.min(1, (nowMs - this.t0) / this.ms));
    this.v = this.from + (this.to - this.from) * this.ease(k);
    if (k >= 1) { this.v = this.to; this.running = false; }
    return true;
  }
}

/** cosmo_v2::HoverFade (widgets/HoverFade.h): each region's own eased 0..1, cross-fading. */
export class HoverFade {
  constructor() { this.target = -1; this.last = -1; this.amt = new Map(); }
  setHovered(id) { this.target = id === undefined || id === null ? -1 : id; }
  clear() { this.target = -1; }
  hovered() { return this.target; }
  /** Ease every amount toward its target; true while something still moves. */
  advance(nowMs, durMs = 120) {
    if (this.target !== -1 && !this.amt.has(this.target)) this.amt.set(this.target, 0);
    const dt = this.last < 0 ? 0 : nowMs - this.last;
    this.last = nowMs;
    const step = reducedMotion() || durMs <= 0 ? 1 : Math.min(1, Math.max(0, dt / durMs));
    let moving = false;
    for (const [id, v] of this.amt) {
      const tg = id === this.target ? 1 : 0;
      const nv = v < tg ? Math.min(tg, v + step) : Math.max(tg, v - step);
      if (nv <= 0 && tg === 0) this.amt.delete(id); else this.amt.set(id, nv);
      if (nv !== tg) moving = true;
    }
    if (!moving) this.last = -1;
    return moving;
  }
  amount(id) { const t = this.amt.get(id) || 0; return t * t * (3 - 2 * t); }
}

/** The widget's frame clock: kick() runs step(nowMs) each animation frame while it returns true. */
export function frameLoop(step) {
  let raf = 0, dead = false;
  const run = (now) => {
    raf = 0;
    if (dead) return;
    let more = false;
    try { more = step(now); } catch (e) { console.error(e); }
    if (more) raf = requestAnimationFrame(run);
  };
  return {
    kick() { if (!raf && !dead) raf = requestAnimationFrame(run); },
    now() { if (raf) { cancelAnimationFrame(raf); raf = 0; } if (!dead) run(performance.now()); },
    kill() { dead = true; if (raf) cancelAnimationFrame(raf); raf = 0; },
  };
}

// ------------------------------------------------------------------ pointer
/** A pointer event in `el`'s own logical px (the shell scales the root with a transform). */
export function localPoint(el, e) {
  const r = el.getBoundingClientRect();
  const sx = el.offsetWidth ? r.width / el.offsetWidth : 1;
  const sy = el.offsetHeight ? r.height / el.offsetHeight : sx;
  return { x: (e.clientX - r.left) / (sx || 1), y: (e.clientY - r.top) / (sy || 1) };
}
export const inRect = (p, r) => !!r && p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
