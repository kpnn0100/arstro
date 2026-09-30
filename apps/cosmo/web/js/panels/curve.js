/*
 * Cosmo by arstro — curve.js: CurvePanel (widgets/CurvePanel.{h,cpp}) and the curve editors'
 * shared "final, with group" reference (DR-EDIT-5).
 *
 * Layout (page y + 250 in the Mixer/Curve stack): header "Tone Curve"; RGB/R/G/B picker
 * (x 9.75, y 27.95, 284.75 x 16.25, Roboto 9); reset (refreshCw, 13.25^2 at 301, 29.45); the
 * plot (x 9.75, y 50.7, 304.5 x 164): curvePlotBg + border r2, quarter grid at white 0.05, the
 * diagonal at 0.08, the dashed reference, the curve (curve::sample, 1.5 px, the channel colour),
 * smooth handles (colour 0.5 line, r3 dot at 0.7), nodes (r4, white 1.5 stroke); the caption
 * strip at plot bottom + 11. Four independent curves: curve (master), curveR, curveG, curveB.
 *
 * Interaction (pick radius 13, nearest node, smooth handles first): double-click an interior
 * node removes it, empty inserts a corner sorted by x (endpoints: nothing); press grabs a
 * handle / node with its grab offset (Alt at the press on a node = smooth + symmetric pull);
 * drag moves (endpoints x-locked, interior x within the neighbours +- 0.01, y 0..1), pulls or
 * mirrors handles (Alt live = independent); every change sends `set <key>=<points>` (live while
 * dragging, final at the release / for a double-click or a reset). The reference is wanted
 * while the effective curve differs from the own one, and shown when nothing is pressed and
 * 220 ms have passed since the last release; it fades in 180 ms / out 110 ms.
 *
 * Deviations (R-G-1): a curve that changes under the panel from the model (another client)
 * eases its points over 220 ms when the point count is unchanged; the four channel curves
 * cross-fade (120 ms) on a channel switch, where the native swaps in one frame.
 */
import { h, bindEffect } from "../core/dom.js";
import { untracked } from "../core/signal.js";
import { Tween, Ease } from "../core/motion.js";
import { formatCurve } from "../model/params.js";
import { tok, css, S, setA, svgBox, T, rect, place, FONT, gestures, iconButton, sectionHeader, SECTION_H } from "../ui/controls.js";
import { segmented } from "../ui/segmented.js";
import { panelState, PAD, ROW_W, quiet } from "./page.js";
import { curveSample, dashedPath, clonePts, point, clamp, fr } from "./geometry.js";

export const PICK = 13;                  // metrics::anchorHitRadius
const HOLD_MS = 220, SEED_HOLD_MS = 900;
const REF = [0.298, 0.710, 0.451];       // CurvePanel.cpp:25 kRefColor (alpha 0.34)

/** The reference fade: want(bool) each frame-ish; calls paint with the alpha. */
export function referenceFade(paint) {
  const a = new Tween(0, paint);
  let target = 0;
  return {
    update(want) { const w = want ? 1 : 0; if (w === target) return; target = w; a.to(w, w ? 180 : 110, Ease.EaseOutCubic); },
    get value() { return a.value; },
  };
}

/** Tween a point list toward `to` (same length) - model values arriving ease (R-G-1). */
export function easePoints(from, to, ms, onFrame) {
  const tw = new Tween(0, (k) => {
    onFrame(to.map((p, i) => {
      const q = from[i];
      const L = (a, b) => a + (b - a) * k;
      return { ...p, x: L(q.x, p.x), y: L(q.y, p.y), ix: L(q.ix, p.ix), iy: L(q.iy, p.iy), ox: L(q.ox, p.ox), oy: L(q.oy, p.oy) };
    }), k >= 1);
  });
  tw.to(1, ms, Ease.EaseOutCubic);
  return tw;
}

const KEYS = ["curve", "curveR", "curveG", "curveB"];
const IDENT = () => [point(0, 0), point(1, 1)];

export function curvePanel(vm, ctx) {
  const C = tok();
  const st = panelState(vm);
  const COLORS = [C.primary, [0.90, 0.32, 0.32, 1], [0.38, 0.80, 0.42, 1], [0.42, 0.58, 0.96, 1]];  // CurvePanel.cpp channelColor
  const el = h("div.cp-abs.cp-curve");
  place(el, 0, 0, 324, 235);
  const hd = sectionHeader("Tone Curve", { w: ROW_W });
  place(hd, PAD, 0);
  el.append(hd);
  let channel = 0;
  const picker = segmented({ labels: ["RGB", "R", "G", "B"], x: PAD, y: SECTION_H, w: ROW_W - 13.25 - 6.5, h: 16.25, size: 9,
    onChange: (i) => showChannel(i) });
  const reset = iconButton({ icon: "refreshCw", size: 13.25, x: PAD + ROW_W - 13.25, y: SECTION_H + (16.25 - 13.25) / 2,
    idleColor: C.muted, activeColor: C.foreground,
    onClick: () => { curves[channel] = IDENT(); drawChannel(channel); emit(false); } });
  el.append(picker.el, reset.el);

  const PW = ROW_W, PH = 164, PY = SECTION_H + 16.25 + 6.5;
  const plot = h("div.cp-abs.cp-plot");
  place(plot, PAD, PY, PW, PH + 30);
  const s = svgBox(PW, PH);
  s.append(rect(0, 0, PW, PH, 2, { fill: css(C.curvePlotBg), stroke: css(C.border), "stroke-width": 1 }));
  let grid = "";
  for (let i = 1; i <= 3; i++) grid += `M${(i * PW) / 4} 0L${(i * PW) / 4} ${PH}M0 ${(i * PH) / 4}L${PW} ${(i * PH) / 4}`;
  s.append(S("path", { d: grid, stroke: "rgba(255, 255, 255, 0.05)", "stroke-width": 1, fill: "none" }));   // CurvePanel.cpp grid
  s.append(S("path", { d: `M0 ${PH}L${PW} 0`, stroke: "rgba(255, 255, 255, 0.08)", "stroke-width": 1, fill: "none" }));
  const refPath = S("path", { fill: "none", "stroke-width": 1 });
  s.append(refPath);
  const layers = KEYS.map((_, i) => { const g = S("g", { class: "cp-chan", opacity: i === 0 ? 1 : 0 }); s.append(g); return g; });
  const caption = S("g", { opacity: 0 });
  const capY = PH + 11;
  caption.append(S("path", { d: dashedPath([[0, capY], [14, capY]], 3, 2.5), fill: "none", stroke: css([...REF, 0.34]), "stroke-width": 1 }));
  const capT = T(20, capY + 8.5 * 0.35, "final, with group", 8.5, FONT.sans);
  capT.setAttribute("fill", css(C.muted));
  caption.append(capT);
  s.append(caption);
  s.setAttribute("height", PH + 30);
  plot.append(s);
  el.append(plot);

  const curves = KEYS.map(() => IDENT());      // what the plot edits (own)
  const refs = KEYS.map(() => []);             // effective curves
  const px = (x) => x * PW, py = (y) => PH - y * PH;
  const nxOf = (lx) => clamp(lx / PW, 0, 1), nyOf = (ly) => clamp(1 - ly / PH, 0, 1);

  function drawChannel(i) {
    const g = layers[i], pts = curves[i], col = COLORS[i];
    g.replaceChildren();
    const dense = curveSample(pts, false, 0);
    if (dense.length >= 2) {
      let d = "";
      dense.forEach(([x, y], k) => { d += (k ? "L" : "M") + px(x) + " " + py(y); });
      g.append(S("path", { d, fill: "none", stroke: css(col), "stroke-width": 1.5 }));
    }
    for (const p of pts) {
      if (!p.smooth) continue;
      for (const [hx, hy] of [[p.x + p.ix, p.y + p.iy], [p.x + p.ox, p.y + p.oy]]) {
        g.append(S("path", { d: `M${px(p.x)} ${py(p.y)}L${px(hx)} ${py(hy)}`, stroke: css(col, 0.5), "stroke-width": 1, fill: "none" }));
        g.append(S("circle", { cx: px(hx), cy: py(hy), r: 3, fill: css(col, 0.7) }));
      }
    }
    for (const p of pts) g.append(S("circle", { cx: px(p.x), cy: py(p.y), r: 4, fill: css(col), stroke: css(C.white), "stroke-width": 1.5 }));
  }
  curves.forEach((_, i) => drawChannel(i));

  // ---- reference
  let pressed = false, revealAt = 0, refShown = [];
  const fade = referenceFade(paintRef);
  function paintRef() {
    const a = fade.value;
    setA(caption, { opacity: a });
    if (a <= 0.001 || refShown.length < 2) { setA(refPath, { d: "" }); return; }
    const pts = curveSample(refShown, false, 0).map(([x, y]) => [px(x), py(y)]);
    setA(refPath, { d: dashedPath(pts, 4, 3.5), stroke: css([...REF, 0.34 * a]) });
  }
  const wanted = () => refs[channel].length >= 2 && formatCurve(refs[channel]) !== formatCurve(curves[channel]);
  let revealTimer = 0;
  function updateRef() {
    const vis = wanted() && !pressed && performance.now() >= revealAt;
    if (vis) refShown = refs[channel];
    fade.update(vis);
    paintRef();
    clearTimeout(revealTimer);
    if (wanted() && !pressed && performance.now() < revealAt) revealTimer = setTimeout(updateRef, revealAt - performance.now() + 5);
  }

  function showChannel(i) {
    channel = clamp(i, 0, 3);
    drag = null;
    layers.forEach((g, k) => g.setAttribute("opacity", k === channel ? 1 : 0));
    updateRef();
  }

  // ---- emission
  let lastSent = null, sentAt = 0;
  function emit(live) {
    const key = KEYS[channel], text = formatCurve(curves[channel]);
    lastSent = { key, text }; sentAt = performance.now();
    quiet(vm.setFields({ [key]: text }, { live }));
    updateRef();
  }

  // ---- interaction
  let drag = null;    // {idx, kind: 0 node | 1 in | 2 out | 3 pull, gdx, gdy}
  const pointAt = (pl) => {
    let best = -1, bd = PICK * PICK;
    curves[channel].forEach((p, i) => { const dx = pl.x - px(p.x), dy = pl.y - py(p.y), d = dx * dx + dy * dy; if (d <= bd) { bd = d; best = i; } });
    return best;
  };
  const handleAt = (pl) => {
    const pts = curves[channel];
    for (let i = 0; i < pts.length; i++) {
      const p = pts[i];
      if (!p.smooth) continue;
      const ox = px(p.x + p.ox), oy = py(p.y + p.oy), ix = px(p.x + p.ix), iy = py(p.y + p.iy);
      if ((pl.x - ox) ** 2 + (pl.y - oy) ** 2 <= PICK * PICK) return { idx: i, kind: 2 };
      if ((pl.x - ix) ** 2 + (pl.y - iy) ** 2 <= PICK * PICK) return { idx: i, kind: 1 };
    }
    return null;
  };
  const inPlot = (p) => p.x >= 0 && p.x <= PW && p.y >= 0 && p.y <= PH;
  gestures(plot, {
    down: (p) => {
      if (!inPlot(p)) return false;
      pressed = true; updateRef();
      const pts = curves[channel];
      const hdl = handleAt(p);
      if (hdl) {
        const cp = pts[hdl.idx];
        const tx = cp.x + (hdl.kind === 1 ? cp.ix : cp.ox), ty = cp.y + (hdl.kind === 1 ? cp.iy : cp.oy);
        drag = { ...hdl, gdx: tx - nxOf(p.x), gdy: ty - nyOf(p.y) };
        return;
      }
      const i = pointAt(p);
      if (i >= 0) {
        let kind = 0;
        if (p.alt) { pts[i].smooth = true; kind = 3; }
        drag = { idx: i, kind, gdx: pts[i].x - nxOf(p.x), gdy: pts[i].y - nyOf(p.y) };
        return;
      }
      drag = null;
    },
    drag: (p, e) => {
      if (!drag) return;
      const pts = curves[channel], cp = pts[drag.idx];
      if (!cp) return;
      const gx = clamp(nxOf(p.x) + drag.gdx, 0, 1), gy = clamp(nyOf(p.y) + drag.gdy, 0, 1);
      const hx = fr(gx - cp.x), hy = fr(gy - cp.y);
      if (drag.kind === 0) {
        let x = gx;
        if (drag.idx === 0) x = 0;
        else if (drag.idx === pts.length - 1) x = 1;
        else x = clamp(x, pts[drag.idx - 1].x + 0.01, pts[drag.idx + 1].x - 0.01);
        cp.x = fr(x); cp.y = fr(gy);
      } else if (drag.kind === 3) { cp.ox = hx; cp.oy = hy; cp.ix = -hx; cp.iy = -hy; }
      else if (e.altKey) { if (drag.kind === 1) { cp.ix = hx; cp.iy = hy; } else { cp.ox = hx; cp.oy = hy; } }
      else if (drag.kind === 1) { cp.ix = hx; cp.iy = hy; cp.ox = -hx; cp.oy = -hy; }
      else { cp.ox = hx; cp.oy = hy; cp.ix = -hx; cp.iy = -hy; }
      drawChannel(channel);
      emit(true);
    },
    up: () => {
      const wasDrag = drag;
      drag = null;
      pressed = false;
      revealAt = performance.now() + HOLD_MS;
      if (wasDrag) emit(false);
      updateRef();
    },
    doubleClick: (p) => {
      if (!inPlot(p)) return;
      const pts = curves[channel], hit = pointAt(p);
      if (hit > 0 && hit < pts.length - 1) pts.splice(hit, 1);
      else if (hit < 0) {
        const c = point(nxOf(p.x), nyOf(p.y));
        let k = pts.findIndex((q) => !(q.x < c.x));
        if (k < 0) k = pts.length;
        pts.splice(k, 0, c);
      } else return;
      drawChannel(channel);
      emit(false);
    },
  });

  // ---- model -> panel (own curves; references = effective)
  const easing = KEYS.map(() => null);
  bindEffect(el, () => {
    const own = st.own.value, eff = st.eff.value;
    untracked(() => {
      KEYS.forEach((k, i) => {
        refs[i] = eff[k] || [];
        const next = (own[k] && own[k].length ? own[k] : IDENT()).map((p) => ({ ...p }));
        if (drag && i === channel) return;              // a gesture in flight outranks the model
        const txt = formatCurve(next);
        if (lastSent && lastSent.key === k && performance.now() - sentAt < SEED_HOLD_MS && txt !== lastSent.text) return;
        if (txt === formatCurve(curves[i])) return;
        const from = clonePts(curves[i]);
        if (easing[i]) { easing[i].onUpdate = null; easing[i].set(1); }
        if (from.length === next.length) {
          easing[i] = easePoints(from, next, 220, (pts, done) => { curves[i] = done ? next : pts; drawChannel(i); if (done) { easing[i] = null; updateRef(); } });
        } else { curves[i] = next; drawChannel(i); }
      });
      updateRef();
    });
  });
  return { el };
}
