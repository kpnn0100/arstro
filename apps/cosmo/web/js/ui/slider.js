/*
 * Cosmo by arstro — slider.js: SliderRow, the most repeated control of the column.
 *
 * Ports widgets/SliderRow.{h,cpp} over core/Artboard's Slider (ui/concrete/Slider.cpp) with
 * cosmo's theme (Theme.cpp: secondary track, primary range fill, white thumb r 4.5):
 *
 *   label   Roboto 10 muted at x 0, baseline 13.5
 *   slider  hit box x 94.125 (86 + 8.125), y 5.5, w rowW - 125, h 9; track 3.15 capsule at 2.925;
 *           fill from the zero point (0 inside the range) else from the left edge; Temperature /
 *           Tint draw a gradient capsule (quadratic ends, Slider::onPaint) instead;
 *           green stacked reach (DR-EDIT-4) from the thumb to value + offset, with a 1.6 x 4.95
 *           end tick, on its own spring; thumb circle (4.5 + 2 hv) white, stroke lerp(white,
 *           primary, 0.5 hv)
 *   readout JetBrains Mono 10 muted, right-aligned by the byte estimate, lround'ed TARGET value
 *
 * Thumb / fill / reach follow critically damped springs (omega 18), so a value that arrives from
 * the model - another client dragging - glides here. Interaction: a press focuses (arrows then
 * step (max-min)/20, one `final` each); a click does NOT jump (setClickJumps(false)); a drag past
 * 5 px sets the value absolutely under the pointer, `live` per move and one `final` on release;
 * a double-click resets to clamp(0, min, max).
 *
 * Deliberate deviations: re-seeds are ignored while this row is dragged (the web model arrives
 * asynchronously - panels.md 0.5) and for a short hold after the release until the model has
 * caught up with the value sent; the release sends the final value as a call (the view-model's
 * rule), the native sends nothing more on Drop.
 */
import { h } from "../core/dom.js";
import { tok, css, lerpColor, S, setA, svgBox, T, rect, setRect, place, estW, FONT, tick, Spring, segHover, gestures } from "./controls.js";

let gradSeq = 0;
const HOLD_MS = 900;

function readout(v) {
  const n = Math.sign(v) * Math.floor(Math.abs(v) + 0.5) || 0;
  return v > 0 ? "+" + n : String(n);
}

/**
 * sliderRow({ label, min, max, width = 304.5, gradient: [left, right] (colour arrays) | null,
 *             onChange(value, {live}) }) -> { el, set(v), setOffset(o), value, dragging }
 */
export function sliderRow(o) {
  const C = tok();
  const W = o.width || 304.5, trackW = Math.max(0, W - 86 - 8.125 - 22.75 - 8.125);
  const min = o.min, max = o.max, span = max - min;
  const clampV = (v) => Math.min(max, Math.max(min, v));
  const el = h("div.cp-abs.cp-slider-row");
  place(el, 0, 0, W, 20);
  const s = svgBox(W, 20);
  const label = T(0, 20 * 0.5 + 10 * 0.35, o.label, 10, FONT.sans);
  label.setAttribute("fill", css(C.muted));
  const value = T(0, 13.5, "", 10, FONT.mono);
  value.setAttribute("fill", css(C.muted));
  const g = S("g", { transform: "translate(94.125 5.5)" });
  const th = 9 * 0.35, ty = (9 - th) * 0.5, tr = th * 0.5;
  let track = null;
  if (o.gradient) {
    const id = "cpg" + ++gradSeq;
    const w = trackW;
    g.append(S("defs", null, S("linearGradient", { id, gradientUnits: "userSpaceOnUse", x1: 0, y1: 0, x2: w, y2: 0 },
      S("stop", { offset: 0, "stop-color": css(o.gradient[0]) }), S("stop", { offset: 1, "stop-color": css(o.gradient[1]) }))));
    // Slider::onPaint's capsule: straight runs and QUADRATIC ends (not arcs).
    g.append(S("path", { fill: `url(#${id})`, d: `M${tr} ${ty}L${w - tr} ${ty}Q${w} ${ty} ${w} ${ty + tr}L${w} ${ty + th - tr}` +
      `Q${w} ${ty + th} ${w - tr} ${ty + th}L${tr} ${ty + th}Q0 ${ty + th} 0 ${ty + th - tr}L0 ${ty + tr}Q0 ${ty} ${tr} ${ty}Z` }));
  } else {
    track = rect(0, ty, trackW, th, 9999, { fill: css(C.secondary) });
    g.append(track);
  }
  const fill = rect(0, ty, 0, th, 9999, { fill: css(C.primary) });
  const sub = rect(0, ty, 0, th, th * 0.5, { fill: css(C.green) });
  const subTick = rect(0, (9 - 9 * 0.55) * 0.5, 1.6, 9 * 0.55, 0.8, { fill: css(C.green) });
  const thumb = S("circle", { cy: 4.5, fill: css(C.white), "stroke-width": 1 });
  if (o.gradient) fill.style.display = "none";
  g.append(fill, sub, subTick, thumb);
  s.append(label, g, value);
  el.append(s);
  const hit = h("div.cp-abs.cp-slider-hit", { tabindex: "-1" });
  place(hit, 94.125, 5.5, trackW, 9);
  el.append(hit);

  let target = clampV(0), offset = 0, dragging = false, holdUntil = 0, held = 0;
  const disp = new Spring(target), subDisp = new Spring(target);
  const hv = segHover(hit, paint);
  let running = false;
  function run() {
    if (running) return;
    running = true;
    tick((dt) => { const a = disp.step(dt), b = subDisp.step(dt); paint(); running = a || b; return running; });
  }
  const norm = (v) => (span > 0 ? Math.min(1, Math.max(0, (v - min) / span)) : 0);
  function paint() {
    const n = norm(disp.value), k = hv.value;
    let from = 0;
    if (min < 0 && max > 0) from = (0 - min) / span;
    const lo = Math.min(from, n), hi = Math.max(from, n);
    setRect(fill, trackW * lo, ty, trackW * (hi - lo), th, 9999);
    const show = offset !== 0;
    sub.style.display = subTick.style.display = show ? "" : "none";
    if (show) {
      const sn = norm(subDisp.value);
      setRect(sub, trackW * Math.min(sn, n), ty, trackW * Math.abs(sn - n), th, th * 0.5);
      setA(subTick, { x: trackW * sn - 0.8 });
    }
    setA(thumb, { cx: n * trackW, r: 4.5 + 2 * k, stroke: css(lerpColor(C.white, C.primary, 0.5 * k)) });
  }
  function showReadout() {
    const t = readout(target);
    value.textContent = t;
    setA(value, { x: W - estW(t, 10) });
  }
  function setTarget(v) {
    target = clampV(v);
    disp.target = target;
    subDisp.target = target + offset;
    showReadout();
    run();
  }
  const valueAt = (x) => min + span * Math.min(1, Math.max(0, trackW > 0 ? x / trackW : 0));
  function emit(live) { if (o.onChange) o.onChange(target, { live }); }
  gestures(hit, {
    down: () => { try { hit.focus({ preventScroll: true }); } catch { /* old engines */ } },
    drag: (p) => { dragging = true; setTarget(valueAt(p.x)); emit(true); },
    drop: () => { dragging = false; holdUntil = performance.now() + HOLD_MS; held = target; emit(false); },
    up: () => { if (!dragging) return; },
    doubleClick: () => { setTarget(clampV(0)); holdUntil = performance.now() + HOLD_MS; held = target; emit(false); },
  });
  // Focus follows the press (Artboard): a press anywhere else takes it away again, so the arrows
  // go back to the filmstrip.
  const blurOutside = (e) => { if (e.target !== hit) hit.blur(); };
  hit.addEventListener("focus", () => document.addEventListener("pointerdown", blurOutside, true));
  hit.addEventListener("blur", () => document.removeEventListener("pointerdown", blurOutside, true));
  hit.addEventListener("keydown", (e) => {
    if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
    e.preventDefault();
    e.stopPropagation();
    setTarget(target + (e.key === "ArrowLeft" ? -1 : 1) * (span / 20));
    holdUntil = performance.now() + HOLD_MS;
    held = target;
    emit(false);
  });

  let seeded = false;
  const api = {
    el,
    /** A value from the model (track units). The first one places the thumb; later ones glide. */
    set(v) {
      v = clampV(v);
      if (!seeded) { seeded = true; target = v; disp.reset(v); subDisp.reset(v + offset); showReadout(); paint(); return; }
      if (dragging) return;
      if (holdUntil) {
        if (performance.now() < holdUntil && Math.abs(v - held) > 1e-3 * span) return;
        holdUntil = 0;
      }
      if (v === target) return;
      setTarget(v);
    },
    /** The green reach: how much ancestor groups add, in track units (0 hides it). */
    setOffset(off) {
      if (off === offset) return;
      // A reach that appears grows out of the thumb (both ends glide, Slider::advance).
      if (offset === 0) subDisp.reset(disp.value);
      offset = off;
      subDisp.target = target + offset;
      paint();
      run();
    },
    get value() { return target; },
    get dragging() { return dragging; },
  };
  showReadout();
  paint();
  return api;
}
