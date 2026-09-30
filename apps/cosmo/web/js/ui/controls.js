/*
 * Cosmo by arstro — controls.js: the right column's small controls and the machinery they share.
 *
 * Ports (widgets/): PillButton, IconButton, SectionHeader, TextMetrics (estimateTextWidth),
 * HoverFade; (core/Artboard): the GestureRecognizer's press/drag/click/double-click rules,
 * Spring (the critically-damped follower sliders use), Interaction (hoverBox / brighten),
 * ToggleSwitch, ComboBox, TextBox.
 *
 * Every self-drawn control is an absolutely positioned <div> (its box = the native widget's hit
 * box) holding an <svg> drawn with the native paint calls: text at its alphabetic baseline,
 * centred / right-aligned by the native's byte estimate (never measureText), strokes centred on
 * the edge, rounded rects with the radius clamped to half the short side. SVG, not canvas, so
 * the shell's per-client UI scale needs no backing-store bookkeeping.
 *
 * Colours come from tokens.css (read once at first use); the only literals are the ones the
 * native hard-codes, each cited. Hover amounts are Tweens (120 ms EaseOutCubic, segment hover)
 * or HoverFades (linear + smoothstep, multi-region), exactly as the native computes them.
 */
import { Tween, Ease, reducedMotion } from "../core/motion.js";
import { h, own } from "../core/dom.js";
import { icon } from "./icons.js";

// ------------------------------------------------------------------ colours (tokens.css)
let TOK = null;
export function parseColor(s) {
  s = (s || "").trim();
  if (s[0] === "#") {
    const n = parseInt(s.slice(1, 7), 16);
    return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255, 1];
  }
  const m = s.match(/-?[\d.]+/g);
  if (!m || m.length < 3) return [0, 0, 0, 1];
  return [m[0] / 255, m[1] / 255, m[2] / 255, m[3] === undefined ? 1 : +m[3]];
}
/** Theme.h's palette, parsed from the generated tokens. */
export function tok() {
  if (TOK) return TOK;
  const cs = getComputedStyle(document.documentElement);
  const g = (n) => parseColor(cs.getPropertyValue(n));
  TOK = {
    background: g("--background"), foreground: g("--foreground"), card: g("--card"), popover: g("--popover"),
    primary: g("--primary"), secondary: g("--secondary"), muted: g("--muted-foreground"), destructive: g("--destructive"),
    border: g("--border"), input: g("--input"), ring: g("--ring"), canvasBg: g("--canvas-bg"), histogramBg: g("--histogram-bg"),
    segmentedBg: g("--segmented-bg"), curvePlotBg: g("--curve-plot-bg"), white: g("--white"),
    // Artboard's default TextStyle colour, the "active" label an outline pill lifts toward on hover
    // (core/Artboard/src/ui/base/Theme.h:14 `Color::rgba(244, 244, 244)`).
    abText: [244 / 255, 244 / 255, 244 / 255, 1],
    // The stacked-reach / "final" green (Artboard Slider.h:82, widgets/CurvePanel.cpp:25).
    green: [0.298, 0.710, 0.451, 1],
  };
  return TOK;
}
const f2 = (v) => Math.round(v * 100) / 100;
/** A colour as a CSS string; `a` multiplies its alpha. */
export function css(c, a = 1) {
  return `rgba(${f2(c[0] * 255)}, ${f2(c[1] * 255)}, ${f2(c[2] * 255)}, ${Math.round(Math.max(0, Math.min(1, c[3] * a)) * 1e4) / 1e4})`;
}
export const lerpColor = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
/** interaction::brighten: toward white per channel, alpha kept. */
export const brighten = (c, k) => [c[0] + (1 - c[0]) * k, c[1] + (1 - c[1]) * k, c[2] + (1 - c[2]) * k, c[3]];
export const alpha = (c, a) => [c[0], c[1], c[2], a];

// ------------------------------------------------------------------ text (TextMetrics.h)
const enc = new TextEncoder();
/** estimateTextWidth: UTF-8 bytes x px x 0.6 - what the native centres and right-aligns by. */
export const estW = (s, px) => enc.encode(s).length * px * 0.6;
export const FONT = { sans: "Roboto", medium: "Roboto Medium", semibold: "Roboto SemiBold", mono: "JetBrains Mono" };
const family = (f) => `"${f}", sans-serif`;

// ------------------------------------------------------------------ svg
const NS = "http://www.w3.org/2000/svg";
export function S(tag, attrs, ...kids) {
  const el = document.createElementNS(NS, tag);
  if (attrs) setA(el, attrs);
  for (const k of kids) if (k) el.append(k);
  return el;
}
export function setA(el, attrs) {
  for (const k in attrs) {
    const v = attrs[k];
    if (v === null || v === undefined) el.removeAttribute(k); else el.setAttribute(k, v);
  }
  return el;
}
export function svgBox(w, hgt) {
  return S("svg", { width: w, height: hgt, viewBox: `0 0 ${w} ${hgt}`, overflow: "visible", class: "cp-svg" });
}
/** A text run at its baseline, as IRenderTarget::drawText draws it. */
export function T(x, y, s, size, fam = FONT.sans, ls = 0) {
  const t = S("text", { x, y, "font-size": size, "font-family": family(fam), "letter-spacing": ls || null });
  t.textContent = s;
  return t;
}
/** drawRoundedRect's radius clamp (Shapes.cpp:28-31): at most half the shorter side. */
export const rr = (r, w, hgt) => Math.max(0, Math.min(r, w / 2, hgt / 2));
export function rect(x, y, w, hgt, r = 0, attrs = {}) {
  const k = rr(r, w, hgt);
  return S("rect", { x, y, width: Math.max(0, w), height: Math.max(0, hgt), rx: k || null, ry: k || null, ...attrs });
}
export function setRect(el, x, y, w, hgt, r = 0) {
  const k = rr(r, w, hgt);
  setA(el, { x, y, width: Math.max(0, w), height: Math.max(0, hgt), rx: k || null, ry: k || null });
}

/** Position a control's box in its parent (logical px). */
export function place(el, x, y, w, hgt) {
  // By transform, not left/top: Chrome pixel-snaps the offset of a box (and an <svg> root) to
  // whole pixels, which would move every drawn line of the column by up to half a pixel; a
  // translate keeps the native's fractional positions (9.75, 27.95, 94.325 ...).
  el.style.transform = `translate(${x}px, ${y}px)`;
  if (w !== undefined) el.style.width = w + "px";
  if (hgt !== undefined) el.style.height = hgt + "px";
  return el;
}

// ------------------------------------------------------------------ one frame clock
const tickers = new Set();
let raf = 0, last = 0;
function frame(now) {
  raf = 0;
  const dt = last ? Math.min(0.05, (now - last) / 1000) : 0;
  last = now;
  for (const fn of [...tickers]) { let keep = false; try { keep = fn(dt, now); } catch (e) { console.error(e); } if (!keep) tickers.delete(fn); }
  if (tickers.size) raf = requestAnimationFrame(frame); else last = 0;
}
/** Run fn(dtSeconds, nowMs) every frame for as long as it returns true. */
export function tick(fn) {
  tickers.add(fn);
  if (!raf) raf = requestAnimationFrame(frame);
}

/** Artboard's Spring (anim/Spring.cpp): critically damped, omega 18, closed form. */
export class Spring {
  constructor(v = 0, omega = 18) { this.value = v; this.target = v; this.vel = 0; this.omega = omega; }
  reset(v) { this.value = this.target = v; this.vel = 0; }
  step(dt) {
    if (reducedMotion()) { this.reset(this.target); return false; }
    const w = this.omega, y0 = this.value - this.target, c2 = this.vel + w * y0, e = Math.exp(-w * dt);
    this.value = this.target + (y0 + c2 * dt) * e;
    this.vel = (c2 - w * (y0 + c2 * dt)) * e;
    if (Math.abs(this.value - this.target) < 1e-4 && Math.abs(this.vel) < 1e-3) { this.value = this.target; this.vel = 0; return false; }
    return true;
  }
}

/** HoverFade (widgets/HoverFade.h): per-item raw amounts moving linearly by dt/120 ms toward 1
 *  (the hovered id) or 0; drawn through smoothstep, so moving between items cross-fades them. */
export class HoverFade {
  constructor(onChange, ms = 120) { this.raw = []; this.target = -1; this.ms = ms; this.onChange = onChange; this._run = false; }
  set(id) {
    if (id === this.target) return;
    this.target = id;
    if (id >= 0 && id >= this.raw.length) for (let i = this.raw.length; i <= id; i++) this.raw.push(0);
    if (!this._run) { this._run = true; tick((dt) => this._step(dt)); }
  }
  _step(dt) {
    const step = reducedMotion() ? 1 : Math.min(1, (dt * 1000) / this.ms);
    let moving = false;
    for (let i = 0; i < this.raw.length; i++) {
      const tg = i === this.target ? 1 : 0;
      if (this.raw[i] < tg) this.raw[i] = Math.min(tg, this.raw[i] + step);
      else if (this.raw[i] > tg) this.raw[i] = Math.max(tg, this.raw[i] - step);
      if (this.raw[i] !== tg) moving = true;
    }
    if (this.onChange) this.onChange();
    this._run = moving;
    return moving;
  }
  amount(id) { const t = this.raw[id] || 0; return t * t * (3 - 2 * t); }
}

/** Segment hover (Artboard Segment.cpp:54-61): 0<->1 over 120 ms EaseOutCubic whenever the
 *  pointer enters / leaves `el`. */
export function segHover(el, onUpdate) {
  const tw = new Tween(0, onUpdate);
  el.addEventListener("pointerenter", () => tw.to(1, 120, Ease.EaseOutCubic));
  el.addEventListener("pointerleave", () => tw.to(0, 120, Ease.EaseOutCubic));
  return tw;
}

// ------------------------------------------------------------------ pointer -> gestures
/** A pointer event in `el`'s own logical px (the shell may scale the UI with a transform). */
export function toLocal(el, e) {
  const r = el.getBoundingClientRect();
  const sx = el.offsetWidth ? r.width / el.offsetWidth : 1, sy = el.offsetHeight ? r.height / el.offsetHeight : sx;
  return { x: (e.clientX - r.left) / (sx || 1), y: (e.clientY - r.top) / (sy || 1), alt: !!e.altKey };
}

/**
 * GestureRecognizer semantics (Artboard input/GestureRecognizer): Down on press (the pressed
 * element captures the pointer); DragStart on the first move farther than 5 px from the press,
 * followed by Drag on that same move and every later one; Up on release, then Drop if it
 * dragged; Click on a release with no drag and no long-press (>= 500 ms); a second Click within
 * 300 ms and 5 px becomes a DoubleClick INSTEAD. `down` returning false = not consumed (the
 * press is not captured and nothing else follows). `hover(p)` / `leave()` report moves with no
 * press.
 */
export function gestures(el, hd) {
  let press = null, lastClick = null;
  const onDown = (e) => {
    if (press || (e.pointerType === "mouse" && e.button !== 0)) return;
    const p = toLocal(el, e);
    if (hd.down && hd.down(p, e) === false) return;
    press = { id: e.pointerId, x0: e.clientX, y0: e.clientY, drag: false, long: false, timer: 0 };
    try { el.setPointerCapture(e.pointerId); } catch { /* synthetic */ }
    press.timer = setTimeout(() => { if (press && !press.drag) { press.long = true; if (hd.longPress) hd.longPress(p); } }, 500);
  };
  const onMove = (e) => {
    if (!press) { if (hd.hover) hd.hover(toLocal(el, e), e); return; }
    if (e.pointerId !== press.id) return;
    const p = toLocal(el, e);
    if (!press.drag) {
      if (Math.hypot(e.clientX - press.x0, e.clientY - press.y0) <= 5) return;
      press.drag = true;
      clearTimeout(press.timer);
      if (hd.dragStart) hd.dragStart(p, e);
    }
    if (hd.drag) hd.drag(p, e);
  };
  const onUp = (e) => {
    if (!press || e.pointerId !== press.id) return;
    const pr = press;
    press = null;
    clearTimeout(pr.timer);
    const p = toLocal(el, e);
    if (hd.up) hd.up(p, e);
    if (pr.drag) { if (hd.drop) hd.drop(p, e); return; }
    if (e.type === "pointercancel" || pr.long) return;
    const now = performance.now();
    if (lastClick && now - lastClick.t <= 300 && Math.hypot(e.clientX - lastClick.x, e.clientY - lastClick.y) <= 5) {
      lastClick = null;
      if (hd.doubleClick) hd.doubleClick(p, e);
      return;
    }
    lastClick = { t: now, x: e.clientX, y: e.clientY };
    if (hd.click) hd.click(p, e);
  };
  el.addEventListener("pointerdown", onDown);
  el.addEventListener("pointermove", onMove);
  el.addEventListener("pointerup", onUp);
  el.addEventListener("pointercancel", onUp);
  if (hd.leave) el.addEventListener("pointerleave", () => { if (!press) hd.leave(); });
  return { get pressed() { return !!press; }, get dragging() { return !!(press && press.drag); } };
}

// ------------------------------------------------------------------ SectionHeader
export const SECTION_H = 27.95;              // SectionHeader.h kSectionHeaderHeight
export const SECTION_ACTION = SECTION_H - 2; // kSectionActionSize (the eyedropper's box)
/** drawSectionHeader at (x, y) of `w`: label + the hairline rule that takes what is left. The
 *  returned element is the whole row (27.95 high); the caller places it. */
export function sectionHeader(label, { w = 304.5, trailingW = 0 } = {}) {
  const C = tok();
  const el = h("div.cp-abs.cp-section");
  place(el, 0, 0, w, SECTION_H);
  const s = svgBox(w, SECTION_H);
  const baseline = 11.375 + 9 * 0.85;
  const lineY = baseline - 9 * 0.35;
  const labelW = estW(label, 9) + 9 * enc.encode(label).length * 0.13;
  const t = T(0, baseline, label, 9, FONT.semibold, 0.13 * 9);
  t.setAttribute("fill", css(C.muted));
  s.append(t);
  const x0 = labelW + 6.5, x1 = w - (trailingW > 0 ? trailingW + 6.5 : 0);
  const line = S("path", { d: `M${x0} ${lineY}L${x1} ${lineY}`, stroke: css(C.border), "stroke-width": 1, fill: "none" });
  if (x1 > x0) s.append(line);
  el.append(s);
  el.setLabel = (text) => { t.textContent = text; const lw = estW(text, 9) + 9 * enc.encode(text).length * 0.13;
    setA(line, { d: `M${lw + 6.5} ${lineY}L${x1} ${lineY}` }); };
  return el;
}

// ------------------------------------------------------------------ PillButton
/**
 * PillButton with the idle / active BoxStyle + TextStyle pair its callers configure:
 *   { label, w, h, size=10, fam=sans, radius=2, outline=true (transparent fill + border),
 *     idleText=muted, activeText=#F4F4F4, activeBox: {fill, stroke}|null, emphasis=white, onClick }
 * Hover: hoverBox(box, emphasis, hv) and label lerp(idle, active, 0.55 hv). Fires on Click. The
 * active flag (set(active)) eases over the hover duration - the native flips it in one frame.
 */
export function pillButton(o) {
  const C = tok();
  const w = o.w, hh = o.h, size = o.size || 10, fam = o.fam || FONT.sans, rad = o.radius ?? 2;
  const idleText = o.idleText || C.muted, activeText = o.activeText || C.abText, emphasis = o.emphasis || C.white;
  const el = h("div.cp-abs.cp-pill");
  place(el, o.x || 0, o.y || 0, w, hh);
  const s = svgBox(w, hh);
  const box = rect(0, 0, w, hh, rad);
  const label = T(0, hh * 0.5 + size * 0.35, "", size, fam, o.ls || 0);
  s.append(box, label);
  el.append(s);
  let text = "", active = false;
  const act = new Tween(0, paint);
  const hv = segHover(el, paint);
  function paint() {
    const k = hv.value, a = act.value;
    // idle box: transparent + border; active box (chips): tinted fill + primary stroke.
    let fill = [0, 0, 0, 0], stroke = o.outline === false ? null : C.border;
    if (o.activeBox && a > 0) {
      fill = lerpColor(fill, o.activeBox.fill, a);
      stroke = lerpColor(stroke || [0, 0, 0, 0], o.activeBox.stroke, a);
    }
    if (o.fill) fill = o.fill;
    fill = brighten(fill, 0.14 * k);
    if (stroke) stroke = lerpColor(stroke, emphasis, 0.5 * k);
    setA(box, { fill: css(fill), stroke: stroke ? css(stroke) : "none", "stroke-width": stroke ? 1 : null });
    const idleCol = lerpColor(idleText, activeText, 0.55 * k);
    setA(label, { fill: css(lerpColor(idleCol, activeText, a)) });
  }
  const api = {
    el,
    setLabel(t) { text = t; label.textContent = t; setA(label, { x: (w - estW(t, size)) * 0.5 }); },
    set(on, snap) { active = !!on; if (snap) act.set(active ? 1 : 0); else act.to(active ? 1 : 0, 120, Ease.EaseOutCubic); },
    get active() { return active; },
    get label() { return text; },
  };
  api.setLabel(o.label || "");
  if (o.active) api.set(true, true);
  paint();
  gestures(el, { click: () => { if (o.onClick) o.onClick(); } });
  return api;
}

// ------------------------------------------------------------------ IconButton
/**
 * IconButton: rounded (2) wash rgba(255,255,255, 0.10 x max(hv, pressed)); the glyph
 * brighten(active ? activeColor : idleColor, 0.4 hv), painted into the WHOLE button rect.
 *   { icon, size (square) | w,h, stroke?, idleColor=muted, activeColor=foreground, onClick }
 */
export function iconButton(o) {
  const C = tok();
  const w = o.w || o.size, hh = o.h || o.size;
  const el = h("div.cp-abs.cp-iconbtn");
  place(el, o.x || 0, o.y || 0, w, hh);
  const s = svgBox(w, hh);
  const bg = rect(0, 0, w, hh, 2, { fill: "none" });
  s.append(bg);
  el.append(s);
  const g = icon(o.icon, w, o.stroke);
  g.classList.add("cp-glyph");
  place(g, 0, 0, w, hh);
  el.append(g);
  let active = false, pressed = false;
  const idle = o.idleColor || C.muted, act = o.activeColor || C.foreground;
  const on = new Tween(0, paint);
  const hv = segHover(el, paint);
  function paint() {
    const k = hv.value;
    const vis = Math.max(k, pressed ? 1 : 0);
    setA(bg, { fill: vis > 0.001 ? css([1, 1, 1, 0.10 * vis]) : "none" });
    g.style.color = css(brighten(lerpColor(idle, act, on.value), 0.4 * k));
  }
  gestures(el, {
    down: () => { pressed = true; paint(); },
    up: () => { pressed = false; paint(); },
    click: () => { if (o.onClick) o.onClick(); },
  });
  paint();
  return {
    el,
    set(a, snap) { active = !!a; if (snap) on.set(active ? 1 : 0); else on.to(active ? 1 : 0, 120, Ease.EaseOutCubic); },
    get active() { return active; },
  };
}

// ------------------------------------------------------------------ ToggleSwitch
/** ToggleSwitch 22.75 x 14 (GradePanel's "Enable"): capsule lerp(secondary, primary, t) then
 *  brighten 0.14 hv; white thumb r = h/2 - 3 + hv at x = h/2 + t (w - h). Click toggles with a
 *  160 ms EaseOutCubic; set() from the model eases the same way (the native snaps it). */
export function toggleSwitch(o) {
  const C = tok();
  const w = 22.75, hh = 14;
  const el = h("div.cp-abs.cp-toggle");
  place(el, o.x || 0, o.y || 0, w, hh);
  const s = svgBox(w, hh);
  const track = rect(0, 0, w, hh, 9999);
  const thumb = S("circle", { cy: hh / 2, fill: css(C.white) });
  s.append(track, thumb);
  el.append(s);
  let on = false;
  const t = new Tween(0, paint);
  const hv = segHover(el, paint);
  function paint() {
    const k = hv.value, v = t.value;
    setA(track, { fill: css(brighten(lerpColor(C.secondary, C.primary, v), 0.14 * k)) });
    setA(thumb, { cx: hh / 2 + v * (w - hh), r: hh / 2 - 3 + k });
  }
  paint();
  gestures(el, { click: () => { on = !on; t.to(on ? 1 : 0, 160, Ease.EaseOutCubic); if (o.onChange) o.onChange(on); } });
  return {
    el,
    set(v, snap) { v = !!v; if (v === on && !snap) return; on = v; if (snap) t.set(on ? 1 : 0); else t.to(on ? 1 : 0, 160, Ease.EaseOutCubic); },
    get on() { return on; },
  };
}

// ------------------------------------------------------------------ ComboBox
/**
 * ComboBox (MaskPanel's picker). Field: secondary + border r2, hoverBox toward the caret colour
 * (muted); text Roboto 10 foreground at x 10, ellipsized to w - 32 (real metrics here, as the
 * native's fitText); a filled caret. Popup (in `o.host`, the column's top layer so the page clip
 * does not cut it): 22 px rows, <= 240 high, below unless there is more room above (4 px edge
 * margin), opacity + 6 px slide over 160 ms EaseOutCubic; selected row primary, hovered row
 * rgba(99,99,99,0.28) on a spring. A click on the field toggles it, on a row picks and closes.
 */
export function comboBox(o) {
  const C = tok();
  const w = o.w, hh = o.h;
  const el = h("div.cp-abs.cp-combo");
  place(el, o.x || 0, o.y || 0, w, hh);
  const s = svgBox(w, hh);
  const field = rect(0, 0, w, hh, 2, { "stroke-width": 1 });
  const text = T(10, hh * 0.5 + 3.5, "", 10, FONT.sans);
  text.setAttribute("fill", css(C.foreground));
  const caret = S("path", { d: `M${w - 18} ${hh / 2 - 3}L${w - 10} ${hh / 2 - 3}L${w - 14} ${hh / 2 + 3}Z`, fill: css(C.muted) });
  s.append(field, text, caret);
  el.append(s);
  const hv = segHover(el, paint);
  function paint() {
    const k = hv.value;
    setA(field, { fill: css(brighten(C.secondary, 0.14 * k)), stroke: css(lerpColor(C.border, C.muted, 0.5 * k)) });
  }
  paint();
  let options = [], selected = 0, open = false;
  const measure = document.createElement("canvas").getContext("2d");
  function fit(str, max) {
    measure.font = `10px ${family(FONT.sans)}`;
    if (max <= 0) return "";
    if (measure.measureText(str).width <= max) return str;
    let cut = [...str];
    while (cut.length) { cut.pop(); if (measure.measureText(cut.join("") + "…").width <= max) return cut.join("") + "…"; }
    return "…";
  }
  function show() { text.textContent = fit(options[selected] || "", w - 10 - 22); }

  // popup
  const pop = h("div.cp-abs.cp-combo-pop");
  const popSvg = svgBox(w + 2, 10);
  pop.append(popSvg);
  const openT = new Tween(0, layoutPop);
  const hiY = new Spring(0), hiA = new Spring(0);
  let hoverRow = -1, scroll = 0, geo = null;
  function popupGeo() {
    const rows = options.length, want = rows * 22;
    const host = o.host, hr = host.getBoundingClientRect(), er = el.getBoundingClientRect();
    const sc = host.offsetHeight ? hr.height / host.offsetHeight : 1;
    const top = (er.top - hr.top) / sc, left = (er.left - hr.left) / sc;
    const rootH = window.innerHeight / sc, worldY = er.top / sc;
    const below = rootH - (worldY + hh) - 4, above = worldY - 4;
    const cap = Math.max(1, Math.floor(240 / 22));
    let n = Math.min(rows, cap);
    const up = n * 22 > below && above > below;
    const room = up ? above : below;
    if (n * 22 > room) n = Math.max(1, Math.floor(room / 22));
    const ph = n * 22;
    return { left, top, up, ph, ptop: up ? -ph : hh, max: Math.max(0, want - ph) };
  }
  function layoutPop() {
    const p = openT.value;
    if (p <= 1e-3) { pop.style.display = "none"; return; }
    if (!geo) return;
    pop.style.display = "";
    const yoff = (1 - p) * (geo.up ? 6 : -6);
    place(pop, geo.left - 1, geo.top + geo.ptop - 1 + yoff, w + 2, geo.ph + 2);
    pop.style.opacity = p;
    popSvg.replaceChildren();
    setA(popSvg, { width: w + 2, height: geo.ph + 2, viewBox: `-1 -1 ${w + 2} ${geo.ph + 2}` });
    popSvg.append(rect(-1, -1, w + 2, geo.ph + 2, 2, { fill: css(C.secondary) }));
    popSvg.append(rect(0, 0, w, geo.ph, 2, { fill: css(C.popover), stroke: css(C.border), "stroke-width": 1 }));
    const clipId = "cpc" + Math.random().toString(36).slice(2);
    popSvg.append(S("defs", null, S("clipPath", { id: clipId }, rect(0, 0, w, geo.ph))));
    const g = S("g", { "clip-path": `url(#${clipId})` });
    if (hiA.value > 0.003) g.append(rect(0, hiY.value, w, 22, 0, { fill: css(C.muted, 0.28 * hiA.value) }));
    options.forEach((lab, i) => {
      const ry = i * 22 - scroll;
      if (ry + 22 < 0 || ry > geo.ph) return;
      if (i === selected) g.append(rect(0, ry, w, 22, 0, { fill: css(C.primary) }));
      const t = T(10, ry + 11 + 3.5, fit(lab, w - 20), 10, FONT.sans);
      t.setAttribute("fill", css(C.foreground));
      g.append(t);
    });
    popSvg.append(g);
  }
  function setOpen(v) {
    open = v;
    if (v) {
      geo = popupGeo();
      const centred = selected * 22 - (geo.ph - 22) * 0.5;
      scroll = Math.min(geo.max, Math.max(0, centred));
      o.host.append(pop);
    }
    openT.to(v ? 1 : 0, 160, Ease.EaseOutCubic);
  }
  pop.style.display = "none";
  const rowAt = (p) => { if (!geo) return -1; const r = Math.floor((p.y - 1 + scroll) / 22); return r >= 0 && r < options.length && p.y >= 1 && p.y < geo.ph + 1 ? r : -1; };
  let dragY0 = 0, scroll0 = 0;
  gestures(pop, {
    hover: (p) => { hoverRow = rowAt(p); animHi(); },
    leave: () => { hoverRow = -1; animHi(); },
    dragStart: (p) => { dragY0 = p.y; scroll0 = scroll; },
    drag: (p) => { if (geo && geo.max > 0) { scroll = Math.min(geo.max, Math.max(0, scroll0 - (p.y - dragY0) * 0.35)); layoutPop(); } },
    click: (p) => { const r = rowAt(p); if (r >= 0) { selected = r; show(); if (o.onChange) o.onChange(r); } setOpen(false); },
  });
  function animHi() {
    const showHi = open && hoverRow >= 0;
    if (showHi) { const ty = hoverRow * 22 - scroll; if (hiA.value < 0.01) hiY.reset(ty); hiY.target = ty; }
    hiA.target = showHi ? 1 : 0;
    tick((dt) => { const a = hiY.step(dt), b = hiA.step(dt); layoutPop(); return a || b; });
  }
  gestures(el, { click: () => setOpen(!open) });
  // A press anywhere else closes it (the native popup is modal to its own clicks only).
  const outside = (e) => { if (open && !el.contains(e.target) && !pop.contains(e.target)) setOpen(false); };
  document.addEventListener("pointerdown", outside, true);
  own(el, () => { document.removeEventListener("pointerdown", outside, true); pop.remove(); });
  return {
    el,
    setOptions(list, sel) {
      options = list.slice();
      selected = sel >= 0 && sel < options.length ? sel : 0;
      show();
      if (open) { geo = popupGeo(); layoutPop(); }
    },
    close() { if (open) setOpen(false); },
  };
}

// ------------------------------------------------------------------ TextBox
/** TextBox (XformPanel's custom ratio): h 20, r2, input fill, JetBrains Mono 10 foreground at
 *  padding 10, caret primary; border `border` idle / `ring` focused (eased), hover pulls it
 *  toward primary. A real <input> so editing is the platform's. */
export function textBox(o) {
  const el = h("input.cp-abs.cp-textbox", { type: "text", spellcheck: "false", autocomplete: "off" });
  place(el, o.x || 0, o.y || 0, o.w, 20);
  el.value = o.value || "";
  el.addEventListener("input", () => { if (o.onInput) o.onInput(el.value); });
  el.addEventListener("keydown", (e) => e.stopPropagation());   // typing is not an editor shortcut
  return el;
}

export { icon };
