/*
 * Cosmo by arstro — mixer.js: the Mixer/Curve page - a StackPanel (widgets/StackPanel.h) of the
 * MixerPanel (0..250) over the CurvePanel (250..485), scrolled as one (content height 485).
 *
 * MixerPanel (widgets/MixerPanel.{h,cpp}): Hue/Sat/Lum picker (x 9.75, y 6.5, 284.75 x 22.75,
 * Roboto 10) choosing which of three stacked HueCurveEditors shows; reset (refreshCw 13.25^2 at
 * 301, 11.25) writes `0,0;120,0;240,0` into the current channel. No spread control (mixerSpread
 * keeps its value).
 *
 * HueCurveEditor (widgets/HueCurveEditor.{h,cpp}), 304.5 x 200 at y 35.75: x = input hue 0..360
 * (cyclic), y -1..1; px = 12 + hue/360 (w - 24), py = 95 - 85 y. Paint: plot bg + border r2,
 * midline (white 0.12), 60-degree grid (0.05), 48-rect hue strip at y 184 (h 8), the dashed
 * reference + caption at y 5, the curve (curve::sample cyclic, 2 px, per segment, the wrap
 * segment skipped; Hue coloured hue(x + 180 y), Sat/Lum primary), handles, nodes (r4 primary,
 * white 1.5). Fewer than 2 model points (the empty default included) show {0,0},{120,0},{240,0}
 * without writing them. Interaction: double-click a node (> 2 nodes) removes it, anywhere else
 * APPENDS a corner (unsorted); press grabs a handle / node with its grab offset (Alt = smooth +
 * symmetric pull); a node drags across the seam freely, y clamped; handles mirror (Alt live =
 * independent, x not wrapped). `set mixer<c>=<points>` per change.
 *
 * Deviations (R-G-1): points arriving from the model ease (220 ms, same count); the three
 * editors cross-fade (120 ms) on a channel switch.
 */
import { h, bindEffect } from "../core/dom.js";
import { untracked } from "../core/signal.js";
import { formatCurve } from "../model/params.js";
import { tok, css, S, setA, svgBox, T, rect, place, FONT, gestures, iconButton } from "../ui/controls.js";
import { segmented } from "../ui/segmented.js";
import { scrollPage, panelState, PAD, ROW_W, quiet } from "./page.js";
import { curveSample, dashedPath, clonePts, point, clamp, fr } from "./geometry.js";
import { curvePanel, referenceFade, easePoints, PICK } from "./curve.js";

const MIXER_H = 250, CURVE_H = 235;
const REF = [0.298, 0.710, 0.451];                                     // HueCurveEditor.cpp refc
const wrap360 = (hh) => { hh %= 360; return hh < 0 ? hh + 360 : hh; };
const DEFAULT = () => [point(0, 0), point(120, 0), point(240, 0)];
function hueColor(hh) {
  hh = wrap360(hh);
  const x = 1 - Math.abs(((hh / 60) % 2) - 1);
  let r = 0, g = 0, b = 0;
  if (hh < 60) { r = 1; g = x; } else if (hh < 120) { r = x; g = 1; } else if (hh < 180) { g = 1; b = x; }
  else if (hh < 240) { g = x; b = 1; } else if (hh < 300) { r = x; b = 1; } else { r = 1; b = x; }
  return [r, g, b, 1];
}

function hueEditor(vm, channel, onEmit) {
  const C = tok();
  const W = ROW_W, H = 200, P = 12, top = 10, bot = H - 20, mid = (top + bot) / 2, half = (bot - top) / 2;
  const pxh = (hue) => P + (hue / 360) * (W - 2 * P), pyv = (y) => mid - y * half;
  const nxh = (lx, wrap) => { const v = ((lx - P) / (W - 2 * P)) * 360; return wrap ? wrap360(v) : v; };
  const nyv = (ly) => clamp((mid - ly) / half, -1, 1);
  const el = h("div.cp-abs.cp-hue");
  place(el, 0, 0, W, H);
  const s = svgBox(W, H);
  s.append(rect(0, 0, W, H, 2, { fill: css(C.curvePlotBg), stroke: css(C.border), "stroke-width": 1 }));
  s.append(S("path", { d: `M${P} ${mid}L${W - P} ${mid}`, stroke: "rgba(255, 255, 255, 0.12)", "stroke-width": 1, fill: "none" }));   // HueCurveEditor.cpp:224 (centre line)
  let grid = "";
  for (let d = 60; d < 360; d += 60) grid += `M${pxh(d)} ${top}L${pxh(d)} ${bot}`;
  s.append(S("path", { d: grid, stroke: "rgba(255, 255, 255, 0.05)", "stroke-width": 1, fill: "none" }));   // HueCurveEditor.cpp:229 (grid)
  for (let i = 0; i < 48; i++) {
    const h0 = (i / 48) * 360;
    s.append(rect(pxh(h0), bot + 4, pxh(((i + 1) / 48) * 360) - pxh(h0) + 1, 8, 0, { fill: css(hueColor(h0)) }));
  }
  const refPath = S("path", { fill: "none", "stroke-width": 1 });
  const caption = S("g", { opacity: 0 });
  caption.append(S("path", { d: dashedPath([[P, 5], [P + 14, 5]], 3, 2.5), fill: "none", stroke: css([...REF, 0.34]), "stroke-width": 1 }));
  const capT = T(P + 20, 5 + 8.5 * 0.35, "final, with group", 8.5, FONT.sans);
  capT.setAttribute("fill", css(C.muted));
  caption.append(capT);
  const layer = S("g");
  s.append(refPath, caption, layer);
  el.append(s);

  let pts = DEFAULT(), ref = [];
  const mapped = channel === 0;
  function draw() {
    layer.replaceChildren();
    const dense = curveSample(pts, true, 360);
    for (let i = 0; i + 1 < dense.length; i++) {
      if (dense[i + 1][0] < dense[i][0]) continue;          // the wrap fold
      const col = mapped ? hueColor(dense[i][0] + dense[i][1] * 180) : C.primary;
      layer.append(S("path", { d: `M${pxh(dense[i][0])} ${pyv(dense[i][1])}L${pxh(dense[i + 1][0])} ${pyv(dense[i + 1][1])}`,
        stroke: css(col), "stroke-width": 2, fill: "none" }));
    }
    for (const p of pts) {
      if (p.smooth) for (const [hx, hy] of [[p.x + p.ix, p.y + p.iy], [p.x + p.ox, p.y + p.oy]]) {
        layer.append(S("path", { d: `M${pxh(p.x)} ${pyv(p.y)}L${pxh(hx)} ${pyv(hy)}`, stroke: css(C.primary, 0.5), "stroke-width": 1, fill: "none" }));
        layer.append(S("circle", { cx: pxh(hx), cy: pyv(hy), r: 3, fill: css(C.primary, 0.7) }));
      }
      layer.append(S("circle", { cx: pxh(p.x), cy: pyv(p.y), r: 4, fill: css(C.primary), stroke: css(C.white), "stroke-width": 1.5 }));
    }
  }
  draw();

  // ---- reference
  let pressed = false, revealAt = 0, refShown = [], revealTimer = 0;
  const fade = referenceFade(paintRef);
  function paintRef() {
    const a = fade.value;
    setA(caption, { opacity: a });
    if (a <= 0.001 || refShown.length < 2) { setA(refPath, { d: "" }); return; }
    const rd = curveSample(refShown, true, 360);
    const poly = rd.map(([x, y]) => [pxh(x), pyv(y)]);
    const brk = rd.map((p, i) => i > 0 && p[0] < rd[i - 1][0]);
    setA(refPath, { d: dashedPath(poly, 4, 3.5, brk), stroke: css([...REF, 0.34 * a]) });
  }
  const wanted = () => ref.length >= 2 && formatCurve(ref) !== formatCurve(pts);
  function updateRef() {
    const now = performance.now();
    const vis = wanted() && !pressed && now >= revealAt;
    if (vis) refShown = ref;
    fade.update(vis);
    paintRef();
    clearTimeout(revealTimer);
    if (wanted() && !pressed && now < revealAt) revealTimer = setTimeout(updateRef, revealAt - now + 5);
  }

  // ---- interaction
  let drag = null, lastSent = null, sentAt = 0;
  function emit(live) { lastSent = formatCurve(pts); sentAt = performance.now(); onEmit(pts, live); updateRef(); }
  const pointAt = (p) => {
    let best = -1, bd = PICK * PICK;
    pts.forEach((q, i) => { const d = (p.x - pxh(q.x)) ** 2 + (p.y - pyv(q.y)) ** 2; if (d <= bd) { bd = d; best = i; } });
    return best;
  };
  const handleAt = (p) => {
    for (let i = 0; i < pts.length; i++) {
      const q = pts[i];
      if (!q.smooth) continue;
      if ((p.x - pxh(q.x + q.ox)) ** 2 + (p.y - pyv(q.y + q.oy)) ** 2 <= PICK * PICK) return { idx: i, kind: 2 };
      if ((p.x - pxh(q.x + q.ix)) ** 2 + (p.y - pyv(q.y + q.iy)) ** 2 <= PICK * PICK) return { idx: i, kind: 1 };
    }
    return null;
  };
  const grabbedX = (p, wrap) => { let x = nxh(p.x, false) + drag.gdx; if (wrap) { while (x < 0) x += 360; while (x >= 360) x -= 360; } return x; };
  const grabbedY = (p) => clamp(nyv(p.y) + drag.gdy, -1, 1);
  gestures(el, {
    down: (p) => {
      pressed = true; updateRef();
      const hd = handleAt(p);
      if (hd) { const q = pts[hd.idx]; drag = { ...hd, gdx: q.x + (hd.kind === 1 ? q.ix : q.ox) - nxh(p.x, false), gdy: q.y + (hd.kind === 1 ? q.iy : q.oy) - nyv(p.y) }; return; }
      const i = pointAt(p);
      if (i >= 0) { let kind = 0; if (p.alt) { pts[i].smooth = true; kind = 3; } drag = { idx: i, kind, gdx: pts[i].x - nxh(p.x, false), gdy: pts[i].y - nyv(p.y) }; return; }
      drag = null;
    },
    drag: (p, e) => {
      if (!drag || !pts[drag.idx]) return;
      const q = pts[drag.idx];
      if (drag.kind === 0) { q.x = fr(grabbedX(p, true)); q.y = fr(grabbedY(p)); }
      else {
        const hx = fr(fr(grabbedX(p, false)) - q.x), hy = fr(fr(grabbedY(p)) - q.y);
        if (drag.kind === 3) { q.ox = hx; q.oy = hy; q.ix = -hx; q.iy = -hy; }
        else if (e.altKey) { if (drag.kind === 1) { q.ix = hx; q.iy = hy; } else { q.ox = hx; q.oy = hy; } }
        else if (drag.kind === 1) { q.ix = hx; q.iy = hy; q.ox = -hx; q.oy = -hy; }
        else { q.ox = hx; q.oy = hy; q.ix = -hx; q.iy = -hy; }
      }
      draw();
      emit(true);
    },
    up: () => { const was = drag; drag = null; pressed = false; revealAt = performance.now() + 220; if (was) emit(false); updateRef(); },
    doubleClick: (p) => {
      const hit = pointAt(p);
      if (hit >= 0 && pts.length > 2) pts.splice(hit, 1);
      else pts.push(point(nxh(p.x, true), nyv(p.y)));
      draw();
      emit(false);
    },
  });

  let easing = null;
  return {
    el,
    reset() { pts = DEFAULT(); draw(); emit(false); },
    /** The model's own points (wrapped + clamped; < 2 = the default) and the effective ones. */
    seed(model, effective) {
      ref = effective || [];
      if (drag) { updateRef(); return; }
      let next = (model || []).map((q) => ({ ...q, x: fr(wrap360(q.x)), y: fr(clamp(q.y, -1, 1)) }));
      if (next.length < 2) next = DEFAULT();
      const txt = formatCurve(next);
      if (lastSent && performance.now() - sentAt < 900 && txt !== lastSent) { updateRef(); return; }
      if (txt !== formatCurve(pts)) {
        const from = clonePts(pts);
        if (easing) { easing.onUpdate = null; easing.set(1); }
        if (from.length === next.length) easing = easePoints(from, next, 220, (q, done) => { pts = done ? next : q; draw(); if (done) { easing = null; updateRef(); } });
        else { pts = next; draw(); }
      }
      updateRef();
    },
  };
}

function mixerPanel(vm, ctx) {
  const C = tok();
  const st = panelState(vm);
  const el = h("div.cp-abs.cp-mixer");
  place(el, 0, 0, 324, MIXER_H);
  let channel = 0;
  const editors = [0, 1, 2].map((c) => hueEditor(vm, c, (pts, live) => quiet(vm.setFields({ ["mixer" + c]: formatCurve(pts) }, { live }))));
  const picker = segmented({ labels: ["Hue", "Sat", "Lum"], x: PAD, y: 6.5, w: ROW_W - 13.25 - 6.5, h: 22.75, size: 10,
    onChange: (i) => { channel = i; editors.forEach((e, k) => e.el.classList.toggle("off", k !== i)); } });
  const reset = iconButton({ icon: "refreshCw", size: 13.25, x: PAD + ROW_W - 13.25, y: 6.5 + (22.75 - 13.25) / 2,
    idleColor: C.muted, activeColor: C.foreground, onClick: () => editors[channel].reset() });
  el.append(picker.el, reset.el);
  const edY = 6.5 + 22.75 + 6.5;
  editors.forEach((e, k) => { place(e.el, PAD, edY); e.el.classList.add("cp-fade"); if (k) e.el.classList.add("off"); el.append(e.el); });
  bindEffect(el, () => {
    const own = st.own.value, eff = st.eff.value;
    untracked(() => editors.forEach((e, c) => e.seed(own.mixer[c], eff.mixer[c])));
  });
  return { el };
}

export function mixerCurvePage(vm, ctx) {
  const page = scrollPage();
  const mixer = mixerPanel(vm, ctx);
  const curve = curvePanel(vm, ctx);
  place(mixer.el, 0, 0);
  place(curve.el, 0, MIXER_H);
  page.content.append(mixer.el, curve.el);
  page.setContentHeight(MIXER_H + CURVE_H);
  return page;
}
