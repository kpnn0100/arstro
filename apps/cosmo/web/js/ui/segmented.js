/*
 * Cosmo by arstro — segmented.js: SegmentedControl (widgets/SegmentedControl.{h,cpp}).
 *
 * The Hue/Sat/Lum, RGB/R/G/B and Shadows/Midtones/Highlights pickers: a #111111 tray with a
 * 1 px border (r2), padding 2, gap 2, segW = (w - 4 - 2(n-1)) / n; ONE primary highlight at
 * x = 2 + p (segW + 2) whose slot index p slides over 220 ms EaseOutCubic, its corner radii
 * blended between the neighbouring slots (outer corners 2, inner 1). Labels are PillButtons:
 * centred by the byte estimate at baseline h/2 + 0.35 size, the active one white, idle ones
 * lerp(muted, white, 0.55 hv) with the 120 ms segment hover (idle segments have no fill).
 * Click selects (and fires onChange even for the selected one); setImmediate() snaps.
 *
 * Deviation: the active label's colour eases over the hover duration (the native flips it).
 */
import { h } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { tok, css, lerpColor, S, setA, svgBox, T, place, estW, FONT, segHover, gestures } from "./controls.js";

function cornerPath(x, y, w, hh, tl, tr, br, bl) {
  const k = 0.5522847498307936;
  const c = (r) => Math.max(0, Math.min(r, w / 2, hh / 2));
  tl = c(tl); tr = c(tr); br = c(br); bl = c(bl);
  return `M${x + tl} ${y}L${x + w - tr} ${y}C${x + w - tr + tr * k} ${y} ${x + w} ${y + tr - tr * k} ${x + w} ${y + tr}` +
    `L${x + w} ${y + hh - br}C${x + w} ${y + hh - br + br * k} ${x + w - br + br * k} ${y + hh} ${x + w - br} ${y + hh}` +
    `L${x + bl} ${y + hh}C${x + bl - bl * k} ${y + hh} ${x} ${y + hh - bl + bl * k} ${x} ${y + hh - bl}` +
    `L${x} ${y + tl}C${x} ${y + tl - tl * k} ${x + tl - tl * k} ${y} ${x + tl} ${y}Z`;
}

/** segmented({ labels, w, h, size = 10, onChange(i) }) -> { el, select(i), setImmediate(i), index } */
export function segmented(o) {
  const C = tok();
  const w = o.w, hh = o.h, size = o.size || 10, n = o.labels.length;
  const pad = 2, gap = 2, segW = (w - 2 * pad - gap * (n - 1)) / n, innerH = hh - 2 * pad;
  const el = h("div.cp-abs.cp-seg");
  place(el, o.x || 0, o.y || 0, w, hh);
  const s = svgBox(w, hh);
  const tray = S("rect", { x: 0, y: 0, width: w, height: hh, rx: 2, ry: 2, fill: css(C.segmentedBg), stroke: css(C.border), "stroke-width": 1 });
  const hi = S("path", { fill: css(C.primary) });
  s.append(tray, hi);
  el.append(s);
  let selected = 0;
  const pos = new Tween(0, paintHi);
  const radii = (i) => { const first = i <= 0, last = i >= n - 1; return [first ? 2 : 1, last ? 2 : 1, last ? 2 : 1, first ? 2 : 1]; };
  function paintHi() {
    const p = Math.min(n - 1, Math.max(0, pos.value));
    const a = Math.floor(p), b = Math.min(n - 1, a + 1), f = p - a;
    const ra = radii(a), rb = radii(b), m = (i) => ra[i] + (rb[i] - ra[i]) * f;
    setA(hi, { d: cornerPath(pad + p * (segW + gap), pad, segW, innerH, m(0), m(1), m(2), m(3)) });
  }
  const segs = o.labels.map((label, i) => {
    const seg = h("div.cp-abs.cp-seg-item");
    place(seg, pad + i * (segW + gap), pad, segW, innerH);
    const ts = svgBox(segW, innerH);
    const t = T((segW - estW(label, size)) * 0.5, innerH * 0.5 + size * 0.35, label, size, FONT.sans);
    ts.append(t);
    seg.append(ts);
    el.append(seg);
    const act = new Tween(i === 0 ? 1 : 0, paint);
    const hv = segHover(seg, paint);
    function paint() {
      const idle = lerpColor(C.muted, C.white, 0.55 * hv.value);
      setA(t, { fill: css(lerpColor(idle, C.white, act.value)) });
    }
    paint();
    gestures(seg, { click: () => api.select(i) });
    return { act };
  });
  paintHi();
  const api = {
    el,
    select(i) {
      if (i < 0 || i >= n) return;
      const changed = i !== selected;
      selected = i;
      segs.forEach((sg, k) => sg.act.to(k === i ? 1 : 0, 120, Ease.EaseOutCubic));
      if (changed) pos.to(i, 220, Ease.EaseOutCubic);
      if (o.onChange) o.onChange(i);
    },
    setImmediate(i) {
      if (i < 0 || i >= n) return;
      selected = i;
      segs.forEach((sg, k) => sg.act.set(k === i ? 1 : 0));
      pos.set(i);
    },
    get index() { return selected; },
  };
  return api;
}
