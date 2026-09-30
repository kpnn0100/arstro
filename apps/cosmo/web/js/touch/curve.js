/*
 * Cosmo by arstro — touch/curve.js: the tone-curve / hue-mixer editors of the touch shell.
 *
 * Ports PhoneApp.cpp's CurveEditor (:146-243): a #0D0D0D plot with a quarter grid, the identity
 * diagonal (tone) or the centre line (mixer), the 48-swatch hue strip over the Hue channel, the
 * curve 2 px in the channel colour, touch-sized nodes (r 8, 10 while dragged, #0A0A0A fill, 2.5 px
 * ring), a 32 px grab radius, interior nodes clamped between their neighbours, endpoints locked in x.
 * Adds the fullscreen editor + loupe that R-TOUCH-4 requires and the native lacks
 * (screens_touch.md §14.11): tap the in-tray plot to open it; there a press selects the NEAREST
 * node (no hit needed) and a drag moves it relative to the finger while a 2.5x loupe 96 px above
 * the finger shows it; double-tap empty space adds a node, double-tap a node removes it.
 *
 * Deliberate deviations: the curve is sampled with the engine's sampler (CurvePoint.h, panels.md
 * §9.5) so smooth points made on the desktop draw as the engine renders them (the native touch
 * joins points with straight lines); Mixer channels use the engine's domain (x = hue 0..360 cyclic,
 * y = -1..1, 0,0;120,0;240,0 shown when empty - the desktop HueCurveEditor), not the native
 * touch's 0..1 square which wrote wrong values; the group-stacked reference (green α .5, 1.5 px)
 * is fed from params vs ownParams (DR-EDIT-5; native supports it but never feeds it); in the
 * tray a single tap opens the fullscreen editor (after the 300 ms double-tap window), so nodes are
 * added there rather than by a tap in the small plot.
 */
import { h, bindEffect, own } from "../core/dom.js";
import { signal, computed, untracked } from "../core/signal.js";
import { parseCurve, formatCurve } from "../model/params.js";
import { gestures, canvasFor, tok, hsv, clamp, tx, ticon, rect, pressWash } from "./tk.js";
import { segmented } from "./controls.js";

const TONE_KEYS = ["curve", "curveR", "curveG", "curveB"];
const LINE_TOK = ["--primary", "--t-chr", "--t-chg", "--t-chb"];
const MIXER_DEFAULT = [{ x: 0, y: 0 }, { x: 120, y: 0 }, { x: 240, y: 0 }];
const f32 = Math.fround;
const quiet = (p) => { if (p && p.catch) p.catch(() => {}); return p; };

// CurvePoint.h:40-84 - the engine's sampler.
const cubic = (p0, p1, p2, p3, t) => { const u = 1 - t; return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3; };
function sampleSeg(a, b, bx, per, period, out) {
  const x0 = a.x, x3 = bx;
  let x1 = a.smooth ? a.x + a.ox : x0; const y1 = a.smooth ? a.y + a.oy : a.y;
  let x2 = b.smooth ? bx + b.ix : x3; const y2 = b.smooth ? b.y + b.iy : b.y;
  x1 = Math.min(Math.max(x1, x0), x3); x2 = Math.min(Math.max(x2, x0), x3);
  for (let s = 1; s <= per; s++) {
    const t = s / per; let x = cubic(x0, x1, x2, x3, t); const y = cubic(a.y, y1, y2, b.y, t);
    if (period > 0) x = x - period * Math.floor(x / period);
    out.push([x, y]);
  }
}
function curveSample(pts, cyclic, period, per = 14) {
  if (!pts.length) return [];
  const p = [...pts].sort((a, b) => a.x - b.x);
  const out = [[p[0].x, p[0].y]];
  for (let i = 0; i + 1 < p.length; i++) sampleSeg(p[i], p[i + 1], p[i + 1].x, per, 0, out);
  if (cyclic && p.length >= 2 && period > 0) sampleSeg(p[p.length - 1], p[0], p[0].x + period, per, period, out);
  return out;
}

/** The points an editor shows and the key it writes, draft-aware (a drag in flight outranks the
 *  model). mode 0 = tone (ch 0..3), 1 = mixer (ch 0..2). */
function source(vm, mode, ch) {
  const key = mode === 0 ? TONE_KEYS[ch] : "mixer" + ch;
  const d = vm.drafts.value.get(key);
  let pts;
  if (d !== undefined) pts = parseCurve(d);
  else pts = mode === 0 ? vm.ownParams.value[key] : vm.ownParams.value.mixer[ch];
  pts = (pts || []).map((p) => ({ ...p }));
  if (mode === 0 && pts.length < 2) pts = [{ x: 0, y: 0 }, { x: 1, y: 1 }];
  if (mode === 1 && pts.length < 2) pts = MIXER_DEFAULT.map((p) => ({ ...p }));
  const effRaw = vm.params.value.raw[key], ownRaw = vm.ownParams.value.raw[key];
  const ref = effRaw !== undefined && effRaw !== ownRaw ? parseCurve(effRaw) : null;
  return { key, pts, ref: ref && ref.length >= 2 ? ref : null };
}

/** Plot geometry for a box of w x h (PhoneApp.cpp:158-160: the hue strip takes the top 18). */
function geom(w, hh, mode, ch) {
  const strip = mode === 1 && ch === 0;
  const y0 = strip ? 18 : 0, ph = strip ? hh - 18 : hh;
  if (mode === 0) return { strip, y0, ph, w, sx: (x) => x * w, sy: (y) => y0 + (1 - y) * ph, ux: (px) => px / w, uy: (py) => 1 - (py - y0) / ph };
  return { strip, y0, ph, w, sx: (x) => (x / 360) * w, sy: (y) => y0 + (1 - (y + 1) / 2) * ph,
           ux: (px) => (px / w) * 360, uy: (py) => (1 - (py - y0) / ph) * 2 - 1 };
}

function paintPlot(c, host, w, hh, mode, ch, pts, ref, grab) {
  const g = geom(w, hh, mode, ch);
  const border = tok(host, "--border");
  if (g.strip) for (let i = 0; i < 48; i++) { c.fillStyle = hsv(i * 7.5, 0.8, 0.9); c.fillRect((i * w) / 48, 0, w / 48 + 1, 16); }
  rr(c, 0.5, g.y0 + 0.5, w - 1, g.ph - 1, 2);
  c.fillStyle = tok(host, "--curve-plot-bg"); c.fill();
  c.strokeStyle = border; c.lineWidth = 1; c.stroke();
  c.beginPath();
  for (const q of [0.25, 0.5, 0.75]) {
    c.moveTo(0, g.y0 + q * g.ph); c.lineTo(w, g.y0 + q * g.ph);
    c.moveTo(q * w, g.y0); c.lineTo(q * w, g.y0 + g.ph);
  }
  if (mode === 0) { c.moveTo(0, g.y0 + g.ph); c.lineTo(w, g.y0); }
  c.stroke();
  const line = tok(host, mode === 0 ? LINE_TOK[ch] : "--primary");
  const draw = (list, color, sw) => {
    const s = curveSample(list, mode === 1, 360);
    c.strokeStyle = color; c.lineWidth = sw; c.beginPath();
    for (let i = 0; i < s.length; i++) {
      const x = g.sx(s[i][0]), y = g.sy(mode === 1 ? clamp(s[i][1], -1, 1) : clamp(s[i][1], 0, 1));
      if (i === 0 || (mode === 1 && s[i][0] < s[i - 1][0])) c.moveTo(x, y); else c.lineTo(x, y);
    }
    c.stroke();
  };
  if (ref) { c.globalAlpha = 0.5; draw(ref, tok(host, "--t-green"), 1.5); c.globalAlpha = 1; }
  draw(pts, line, 2);
  const stage = tok(host, "--canvas-bg");
  pts.forEach((p, i) => {
    const r = i === grab ? 10 : 8;
    c.beginPath(); c.arc(g.sx(p.x), g.sy(p.y), r, 0, Math.PI * 2);
    c.fillStyle = stage; c.fill();
    c.strokeStyle = line; c.lineWidth = 2.5; c.stroke();
  });
  return g;
}
function rr(c, x, y, w, hh, r) {
  c.beginPath();
  c.moveTo(x + r, y); c.lineTo(x + w - r, y); c.quadraticCurveTo(x + w, y, x + w, y + r);
  c.lineTo(x + w, y + hh - r); c.quadraticCurveTo(x + w, y + hh, x + w - r, y + hh);
  c.lineTo(x + r, y + hh); c.quadraticCurveTo(x, y + hh, x, y + hh - r);
  c.lineTo(x, y + r); c.quadraticCurveTo(x, y, x + r, y); c.closePath();
}

/**
 * An editable plot in `box` (sized by the caller).
 *   o = { mode: () => 0|1, ch: () => channel, hasTarget, inTray, openFull, loupe: element | null }
 */
export function curvePlot(box, vm, o) {
  box.classList.add("t-plot");
  const cv = canvasFor(box, 12);
  const grab = signal(-1);
  let drag = null;              // {i, pts, key, mode, start: {x, y}, p0}
  let pendingTap = 0;
  const data = computed(() => source(vm, o.mode(), o.ch()));

  function paint() {
    const [w, hh] = cv.size();
    cv.clear();
    const mode = untracked(o.mode), ch = untracked(o.ch);
    const d = drag ? { pts: drag.pts, ref: null } : untracked(() => data.value);
    const g = paintPlot(cv.ctx, box, w, hh, mode, ch, d.pts, d.ref, untracked(() => grab.value));
    if (o.loupe && drag) {
      const n = drag.pts[drag.i];
      o.loupe.paint(g.sx(n.x), g.sy(n.y), (c) => paintPlot(c, box, w, hh, mode, ch, d.pts, null, drag.i));
    }
  }
  bindEffect(box, () => { data.value; grab.value; paint(); });
  const ro = new ResizeObserver(() => paint());
  ro.observe(box);
  own(box, () => { ro.disconnect(); clearTimeout(pendingTap); });

  const G = () => { const w = box.clientWidth, hh = box.clientHeight; return geom(w, hh, o.mode(), o.ch()); };
  const nearest = (p, pts, limit) => {
    const g = G(); let best = -1, bd = limit;
    pts.forEach((q, i) => { const d = Math.hypot(g.sx(q.x) - p.x, g.sy(q.y) - p.y); if (d < bd) { bd = d; best = i; } });
    return best;
  };
  const write = (pts, key, live) => {
    if (!o.hasTarget()) return;
    const out = pts.map((q) => ({ ...q, x: f32(q.x), y: f32(q.y) }));
    quiet(vm.setFields({ [key]: formatCurve(out) }, { live }));
  };
  const moveTo = (d, nx, ny) => {
    const p = d.pts[d.i];
    if (d.mode === 0) {
      const end = d.i === 0 || d.i === d.pts.length - 1;
      if (!end) p.x = clamp(nx, d.pts[d.i - 1].x + 0.01, d.pts[d.i + 1].x - 0.01);
      p.y = clamp(ny, 0, 1);
    } else {
      p.x = nx - 360 * Math.floor(nx / 360);
      p.y = clamp(ny, -1, 1);
    }
  };

  gestures(box, {
    down: (e, p) => {
      const d = untracked(() => data.value);
      const i = o.inTray ? nearest(p, d.pts, 32) : nearest(p, d.pts, Infinity);
      grab.value = i;
      if (i >= 0) drag = { i, pts: d.pts.map((q) => ({ ...q })), key: d.key, mode: untracked(o.mode), start: { ...d.pts[i] }, p0: p, live: false };
      return true;
    },
    dragStart: () => { if (!drag) return false; drag.live = true; if (o.loupe) o.loupe.show(true); },
    drag: (e, p) => {
      if (!drag) return;
      const g = G();
      if (o.inTray) moveTo(drag, g.ux(p.x), g.uy(p.y));                       // absolute (native)
      else moveTo(drag, g.ux(g.sx(drag.start.x) + p.x - drag.p0.x), g.uy(g.sy(drag.start.y) + p.y - drag.p0.y));   // relative: the finger never covers the node
      if (o.loupe) o.loupe.at(e.clientX, e.clientY);
      write(drag.pts, drag.key, true);
      paint();
    },
    dragEnd: () => {
      if (!drag) return;
      const d = drag; drag = null;
      if (o.loupe) o.loupe.show(false);
      write(d.pts, d.key, false);
      if (o.inTray) grab.value = -1;
      paint();
    },
    up: () => { if (drag && !drag.live) drag = null; if (o.inTray) grab.value = -1; },
    tap: () => {
      if (!o.inTray) return;                                                   // fullscreen: the press already selected
      clearTimeout(pendingTap);
      pendingTap = setTimeout(() => { if (o.openFull) o.openFull(); }, 300);
    },
    doubleTap: (e, p) => {
      clearTimeout(pendingTap);
      const d = untracked(() => data.value), mode = untracked(o.mode);
      const hit = nearest(p, d.pts, 32);
      const pts = d.pts.map((q) => ({ ...q }));
      if (hit >= 0) {
        if (mode === 0 && (hit === 0 || hit === pts.length - 1)) return;
        if (mode === 1 && pts.length <= 2) return;
        pts.splice(hit, 1);
        grab.value = -1;
      } else {
        if (o.inTray) return;                                                  // add nodes in the big plot
        const g = G();
        if (mode === 0) {
          pts.push({ x: clamp(g.ux(p.x), 0.02, 0.98), y: clamp(g.uy(p.y), 0, 1), smooth: false, ix: 0, iy: 0, ox: 0, oy: 0 });
          pts.sort((a, b) => a.x - b.x);
        } else {
          const x = g.ux(p.x);
          pts.push({ x: x - 360 * Math.floor(x / 360), y: clamp(g.uy(p.y), -1, 1), smooth: false, ix: 0, iy: 0, ox: 0, oy: 0 });
        }
      }
      write(pts, d.key, false);
    },
  });
  return { paint };
}

/** A loupe: Ø 88, 96 px above the finger (clamped on screen), 2.5x the plot around the node,
 *  1.5 px accent ring, fading 120 ms. */
function loupe(host) {
  const el = h("div.t-loupe");
  host.append(el);
  const cv = canvasFor(el);
  return {
    show(on) { el.classList.toggle("on", on); },
    at(cx, cy) {
      const r = host.getBoundingClientRect();
      const x = clamp(cx - r.left - 44, 4, r.width - 92), y = clamp(cy - r.top - 96 - 44, 4, r.height - 92);
      el.style.transform = `translate(${x}px, ${y}px)`;
    },
    paint(nx, ny, draw) {
      const [w, hh] = cv.size();
      const c = cv.ctx;
      c.clearRect(0, 0, w, hh);
      c.save();
      c.beginPath(); c.arc(44, 44, 43, 0, Math.PI * 2); c.clip();
      c.fillStyle = tok(host, "--canvas-bg"); c.fillRect(0, 0, 88, 88);
      c.translate(44, 44); c.scale(2.5, 2.5); c.translate(-nx, -ny);
      draw(c);
      c.restore();
      c.beginPath(); c.arc(44, 44, 43.25, 0, Math.PI * 2);
      c.strokeStyle = tok(host, "--primary"); c.lineWidth = 1.5; c.stroke();
    },
  };
}

/**
 * The fullscreen editor (R-TOUCH-4), hosted by the sheet layer: a #0A0A0A panel with a 48 px top
 * bar (back = Done, centred title), the channel picker, the plot inset 16 and a 56 px bottom row
 * with Reset. o = { mode, curveCh, mixerCh (signals), hasTarget }, close() dismisses.
 */
export function curveFull(vm, o, close) {
  const el = h("div.t-full");
  const top = h("div.t-full-top");
  const back = h("div.t-ibtn");
  rect(back, 0, 0, 52, 48);
  const bi = ticon("back", 20, 1.75); bi.style.left = "12px"; bi.style.top = "14px";
  back.append(bi);
  gestures(back, { down: () => pressWash(back), tap: close });
  const title = tx("", "50%", 29, 14, "medium", "center", "t-fg");
  bindEffect(title, () => {
    title.textContent = o.mode.value === 0 ? "Tone curve" : "Hue vs " + ["Hue", "Sat", "Lum"][o.mixerCh.value];
  });
  top.append(back, title, h("i.t-hl.bottom"));
  el.append(top);
  const chWrap = h("div.t-abs");
  rect(chWrap, 16, 60, "calc(100% - 32px)", 30);
  chWrap.append(o.mode.peek() === 0
    ? segmented(["RGB", "R", "G", "B"], { sel: () => o.curveCh.value, pick: (i) => { o.curveCh.value = i; },
        colors: () => ["var(--primary)", "var(--t-chr)", "var(--t-chg)", "var(--t-chb)"] })
    : segmented(["Hue", "Sat", "Lum"], { sel: () => o.mixerCh.value, pick: (i) => { o.mixerCh.value = i; } }));
  el.append(chWrap);
  const plot = h("div.t-fullplot");
  el.append(plot);
  const lp = loupe(el);
  const mode = () => o.mode.value, ch = () => (o.mode.value === 0 ? o.curveCh.value : o.mixerCh.value);
  curvePlot(plot, vm, { mode, ch, hasTarget: o.hasTarget, inTray: false, loupe: lp });
  const bottom = h("div.t-full-bottom", {}, h("i.t-hl"));
  const reset = h("div.t-xbtn.dim");
  rect(reset, 16, 12, 76, 32);
  reset.append(tx("Reset", "50%", 21, 13, "sans", "center"));
  gestures(reset, {
    down: () => pressWash(reset),
    tap: () => {
      if (!o.hasTarget()) return;
      if (o.mode.peek() === 0) quiet(vm.setFields({ [TONE_KEYS[o.curveCh.peek()]]: "0,0;1,1" }));
      else quiet(vm.setFields({ ["mixer" + o.mixerCh.peek()]: "0,0;120,0;240,0" }));
    },
  });
  bottom.append(reset, tx("Double-tap to add or remove a point", "calc(100% - 16px)", 32.5, 11, "sans", "right", "t-muted"));
  el.append(bottom);
  return el;
}
