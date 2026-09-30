/*
 * Cosmo by arstro — histogram.js: HistogramWidget (widgets/HistogramWidget.{h,cpp}).
 *
 * Full column width x 94.325: histogramBg, a bottom hairline, "HISTOGRAM" (Roboto SemiBold 9,
 * tracking 1.08, muted, x 9.75, baseline 16.0625), then - once a frame has arrived - the plot
 * y 19.825..87.825 spanning x 0..w: point i at (w i/(n-1), 87.825 - 68 x 0.92 x min(1, v)),
 * v = log1p(count) / log1p(maxCount) with maxCount over all four rows. R, G, B as closed fills
 * (0.28 / 0.28 / 0.32 alpha, the native's literals, HistogramWidget.cpp:60-62), luminance as an
 * open 0.75 px polyline at white 0.32. The data persists until the next frame (also across photo
 * switches); with no frame yet only the background, label and hairline are drawn.
 *
 * It reads the preview meta's `hist` {r,g,b,lum}: 256 bins (+ `max`) when the core sends them,
 * else the 64 summed bins it sends today (CORE GAP G3: coarser, and log1p of 4x counts).
 * Deviation (R-G-1): a new histogram eases in over 160 ms instead of replacing the old in one
 * frame.
 */
import { h } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { tok, css, S, setA, svgBox, T, place, FONT } from "./controls.js";

export const HIST_H = 94.325;
const LABEL_H = 19.825, PLOT_H = 68;

/** histogram(w) -> { el, set(hist) } */
export function histogram(w = 324) {
  const C = tok();
  const el = h("div.cp-abs.cp-hist");
  place(el, 0, 0, w, HIST_H);
  const s = svgBox(w, HIST_H);
  s.append(S("rect", { x: 0, y: 0, width: w, height: HIST_H, fill: css(C.histogramBg) }));
  s.append(S("path", { d: `M0 ${HIST_H}L${w} ${HIST_H}`, stroke: css(C.border), "stroke-width": 1, fill: "none" }));
  const lab = T(9.75, LABEL_H * 0.5 + 9 * 0.35 + 3, "HISTOGRAM", 9, FONT.semibold, 0.12 * 9);
  lab.setAttribute("fill", css(C.muted));
  s.append(lab);
  // Literal series colours: HistogramWidget.cpp:54-56, 65 (the native hard-codes them).
  const r = S("path", { fill: "rgba(255, 68, 68, 0.28)" });
  const g = S("path", { fill: "rgba(52, 211, 80, 0.28)" });
  const b = S("path", { fill: "rgba(60, 120, 255, 0.32)" });
  const l = S("path", { fill: "none", stroke: "rgba(255, 255, 255, 0.32)", "stroke-width": 0.75 });
  s.append(r, g, b, l);
  el.append(s);

  const y0 = LABEL_H + PLOT_H;
  const tw = new Tween(null, draw);
  let n = 0;
  function draw(v) {
    if (!v) return;
    const pt = (row, i) => `${(w * i) / (n - 1)} ${y0 - PLOT_H * 0.92 * Math.min(1, v[row * n + i])}`;
    const closed = (row) => { let d = `M0 ${y0}`; for (let i = 0; i < n; i++) d += "L" + pt(row, i); return d + `L${w} ${y0}Z`; };
    setA(r, { d: closed(0) });
    setA(g, { d: closed(1) });
    setA(b, { d: closed(2) });
    let d = "";
    for (let i = 0; i < n; i++) d += (i ? "L" : "M") + pt(3, i);
    setA(l, { d });
  }
  return {
    el,
    set(hist) {
      if (!hist || !hist.r || !hist.g || !hist.b) return;
      const rows = [hist.r, hist.g, hist.b, hist.lum || hist.l || hist.r];
      const bins = rows[0].length;
      if (bins < 2) return;
      let maxCount = +hist.max || 0;
      if (!maxCount) for (const row of rows) for (const c of row) if (c > maxCount) maxCount = c;
      const den = Math.log1p(Math.max(1, maxCount));
      const v = new Array(bins * 4);
      rows.forEach((row, k) => { for (let i = 0; i < bins; i++) v[k * bins + i] = Math.log1p(Math.max(0, row[i] || 0)) / den; });
      if (bins !== n || tw.value === null) { n = bins; tw.set(v); return; }
      tw.to(v, 160, Ease.EaseOutCubic);
    },
  };
}
