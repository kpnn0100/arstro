/*
 * Cosmo by arstro — geometry.js: the pure maths the edit panels and the on-photo overlays share.
 *
 * Verbatim ports, so a box dragged here and one dragged in the window agree to the float:
 *   UnitConversions.h            track <-> engine (EV/80, mired temperature, tint x1.5, radius /10)
 *   CurvePoint.h:40-84           curve::sample (the engine's own sampler: sorted, clamped handles)
 *   MaskStack.h:48-75            maskPathPolygon (stored order, closed, 12 steps per segment)
 *   DashedLine.h                 strokeDashedPolyline, as an SVG path of dash runs
 *   CropGeometry.h               clampToFrame, clampPositionOnly, fitKeepingRatio, applyRatio,
 *                                partAt, resizeBy, moveTo
 * Point fields are float32 in the engine, so points made here go through Math.fround.
 */

export const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
export const fr = Math.fround;

// ------------------------------------------------------------------ UnitConversions.h
const M_N = 1e6 / 6500, M_W = 1e6 / 2000, M_C = 1e6 / 19500;
export const toEv = (t) => t / 80, fromEv = (ev) => ev * 80;
export const toKelvin = (t) => { const v = clamp(t, -100, 100);
  const m = v <= 0 ? M_N + (-v / 100) * (M_W - M_N) : M_N - (v / 100) * (M_N - M_C); return 1e6 / m; };
export const fromKelvin = (k) => { const m = 1e6 / clamp(k, 2000, 19500);
  return m >= M_N ? (-100 * (m - M_N)) / (M_W - M_N) : (100 * (M_N - m)) / (M_N - M_C); };
export const toTint = (t) => t * 1.5, fromTint = (x) => x / 1.5;
export const toRadiusPx = (t) => t / 10, fromRadiusPx = (px) => px * 10;
/** The exact conversions of the catalogue's converted fields (panels.md 9.3 - the sampled stops
 *  of `controls` are off by up to 42 K at the cool end of Temperature). */
export const EXACT = {
  exposure: [toEv, fromEv], temp: [toKelvin, fromKelvin], tint: [toTint, fromTint], sharpenRadius: [toRadiusPx, fromRadiusPx],
};

// ------------------------------------------------------------------ curves
const cubic = (p0, p1, p2, p3, t) => { const u = 1 - t; return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3; };
function sampleSeg(a, b, bx, per, period, out) {
  const x0 = a.x, x3 = bx;
  let x1 = a.smooth ? a.x + a.ox : x0; const y1 = a.smooth ? a.y + a.oy : a.y;
  let x2 = b.smooth ? bx + b.ix : x3; const y2 = b.smooth ? b.y + b.iy : b.y;
  x1 = Math.min(Math.max(x1, x0), x3); x2 = Math.min(Math.max(x2, x0), x3);
  for (let s = 1; s <= per; s++) {
    const t = s / per;
    let x = cubic(x0, x1, x2, x3, t); const y = cubic(a.y, y1, y2, b.y, t);
    if (period > 0) x -= period * Math.floor(x / period);
    out.push([x, y]);
  }
}
/** curve::sample(pts, cyclic, period, 14): [[x, y], ...] */
export function curveSample(pts, cyclic, period = 0, per = 14) {
  if (!pts.length) return [];
  const p = [...pts].sort((a, b) => a.x - b.x);
  const out = [[p[0].x, p[0].y]];
  for (let i = 0; i + 1 < p.length; i++) sampleSeg(p[i], p[i + 1], p[i + 1].x, per, 0, out);
  if (cyclic && p.length >= 2 && period > 0) sampleSeg(p[p.length - 1], p[0], p[0].x + period, per, period, out);
  return out;
}
/** maskPathPolygon(path, 12): closed outline in stored order; [] under 3 points. */
export function maskPathPolygon(pts, per = 12) {
  if (pts.length < 3) return [];
  const out = [];
  for (let i = 0; i < pts.length; i++) {
    const a = pts[i], b = pts[(i + 1) % pts.length];
    const x1 = a.smooth ? a.x + a.ox : a.x, y1 = a.smooth ? a.y + a.oy : a.y;
    const x2 = b.smooth ? b.x + b.ix : b.x, y2 = b.smooth ? b.y + b.iy : b.y;
    out.push([a.x, a.y]);
    for (let s = 1; s < per; s++) { const t = s / per; out.push([cubic(a.x, x1, x2, b.x, t), cubic(a.y, y1, y2, b.y, t)]); }
  }
  return out;
}
export const point = (x, y) => ({ x: fr(x), y: fr(y), smooth: false, ix: 0, iy: 0, ox: 0, oy: 0 });
export const clonePts = (pts) => pts.map((p) => ({ ...p }));

/** strokeDashedPolyline as an SVG path `d`: the dash rhythm carries across segments;
 *  `breaks[i]` true starts a new run before point i (the hue editor's 360/0 seam). */
export function dashedPath(pts, on = 4, off = 3.5, breaks = null) {
  if (pts.length < 2 || on <= 0 || off < 0) return "";
  let d = "", rem = on, pen = true;
  for (let i = 0; i + 1 < pts.length; i++) {
    if (breaks && breaks[i + 1]) continue;
    const [ax, ay] = pts[i], [bx, by] = pts[i + 1];
    const dx = bx - ax, dy = by - ay, len = Math.hypot(dx, dy);
    if (len <= 1e-9) continue;
    let pos = 0;
    while (pos < len) {
      const step = Math.min(rem, len - pos);
      if (pen) {
        const t0 = pos / len, t1 = (pos + step) / len;
        d += `M${ax + dx * t0} ${ay + dy * t0}L${ax + dx * t1} ${ay + dy * t1}`;
      }
      pos += step; rem -= step;
      if (rem <= 1e-9) { pen = !pen; rem = pen ? on : off; }
    }
  }
  return d;
}

// ------------------------------------------------------------------ CropGeometry.h
export const MIN = 0.02;
export const clampToFrame = (r) => {
  r = { ...r };
  r.w = clamp(r.w, MIN, 1); r.h = clamp(r.h, MIN, 1);
  r.x = clamp(r.x, 0, 1 - r.w); r.y = clamp(r.y, 0, 1 - r.h);
  return r;
};
export const clampPositionOnly = (r) => ({ ...r, x: clamp(r.x, 0, Math.max(0, 1 - r.w)), y: clamp(r.y, 0, Math.max(0, 1 - r.h)) });
export function fitKeepingRatio(w, h, nr, maxW, maxH) {
  if (nr <= 0) return [w, h];
  if (w < MIN) { w = MIN; h = w / nr; }
  if (h < MIN) { h = MIN; w = h * nr; }
  let s = 1;
  if (maxW > 0 && w > maxW) s = Math.min(s, maxW / w);
  if (maxH > 0 && h > maxH) s = Math.min(s, maxH / h);
  if (s < 1) { w *= s; h *= s; }
  let s2 = 1;
  if (w > 1) s2 = Math.min(s2, 1 / w);
  if (h > 1) s2 = Math.min(s2, 1 / h);
  if (s2 < 1) { w *= s2; h *= s2; }
  return [w, h];
}
export const normalisedRatio = (r, sw, sh) => (r <= 0 || sw <= 0 || sh <= 0 ? 0 : (r * sh) / sw);
export function applyRatio(r, nr) {
  if (nr <= 0) return clampToFrame(r);
  const cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
  let w = r.w, h = r.w / nr;
  if (h > 1 || h > r.h * 4) { h = r.h; w = r.h * nr; }
  [w, h] = fitKeepingRatio(w, h, nr, 2 * Math.min(cx, 1 - cx), 2 * Math.min(cy, 1 - cy));
  return clampPositionOnly({ x: cx - w * 0.5, y: cy - h * 0.5, w, h });
}
export const Part = { None: 0, Move: 1, TopLeft: 2, Top: 3, TopRight: 4, Right: 5, BottomRight: 6, Bottom: 7, BottomLeft: 8, Left: 9 };
export const isCorner = (p) => p === Part.TopLeft || p === Part.TopRight || p === Part.BottomRight || p === Part.BottomLeft;
export function partAt(box, p, grab) {
  const nearL = Math.abs(p.x - box.x) <= grab, nearR = Math.abs(p.x - (box.x + box.w)) <= grab;
  const nearT = Math.abs(p.y - box.y) <= grab, nearB = Math.abs(p.y - (box.y + box.h)) <= grab;
  const inX = p.x >= box.x - grab && p.x <= box.x + box.w + grab, inY = p.y >= box.y - grab && p.y <= box.y + box.h + grab;
  if (!inX || !inY) return Part.None;
  if (nearL && nearT) return Part.TopLeft;
  if (nearR && nearT) return Part.TopRight;
  if (nearR && nearB) return Part.BottomRight;
  if (nearL && nearB) return Part.BottomLeft;
  if (nearL) return Part.Left;
  if (nearR) return Part.Right;
  if (nearT) return Part.Top;
  if (nearB) return Part.Bottom;
  const inside = p.x > box.x && p.x < box.x + box.w && p.y > box.y && p.y < box.y + box.h;
  return inside ? Part.Move : Part.None;
}
export function resizeBy(rect, part, nx, ny, nr) {
  const m = MIN, l0 = rect.x, t0 = rect.y, r0 = rect.x + rect.w, b0 = rect.y + rect.h;
  if (nr <= 0) {
    let l = l0, t = t0, r = r0, b = b0;
    switch (part) {
      case Part.Left: l = Math.min(nx, r - m); break;
      case Part.Right: r = Math.max(nx, l + m); break;
      case Part.Top: t = Math.min(ny, b - m); break;
      case Part.Bottom: b = Math.max(ny, t + m); break;
      case Part.TopLeft: l = Math.min(nx, r - m); t = Math.min(ny, b - m); break;
      case Part.TopRight: r = Math.max(nx, l + m); t = Math.min(ny, b - m); break;
      case Part.BottomRight: r = Math.max(nx, l + m); b = Math.max(ny, t + m); break;
      case Part.BottomLeft: l = Math.min(nx, r - m); b = Math.max(ny, t + m); break;
      default: return rect;
    }
    return clampToFrame({ x: l, y: t, w: r - l, h: b - t });
  }
  const cx0 = (l0 + r0) * 0.5, cy0 = (t0 + b0) * 0.5;
  const roomCx = 2 * Math.min(cx0, 1 - cx0), roomCy = 2 * Math.min(cy0, 1 - cy0);
  let w = 0, h = 0;
  switch (part) {
    case Part.BottomRight: w = nx - l0; h = w / nr; [w, h] = fitKeepingRatio(w, h, nr, 1 - l0, 1 - t0); return clampPositionOnly({ x: l0, y: t0, w, h });
    case Part.TopLeft: w = r0 - nx; h = w / nr; [w, h] = fitKeepingRatio(w, h, nr, r0, b0); return clampPositionOnly({ x: r0 - w, y: b0 - h, w, h });
    case Part.TopRight: w = nx - l0; h = w / nr; [w, h] = fitKeepingRatio(w, h, nr, 1 - l0, b0); return clampPositionOnly({ x: l0, y: b0 - h, w, h });
    case Part.BottomLeft: w = r0 - nx; h = w / nr; [w, h] = fitKeepingRatio(w, h, nr, r0, 1 - t0); return clampPositionOnly({ x: r0 - w, y: t0, w, h });
    case Part.Right: w = nx - l0; h = w / nr; [w, h] = fitKeepingRatio(w, h, nr, 1 - l0, roomCy); return clampPositionOnly({ x: l0, y: cy0 - h * 0.5, w, h });
    case Part.Left: w = r0 - nx; h = w / nr; [w, h] = fitKeepingRatio(w, h, nr, r0, roomCy); return clampPositionOnly({ x: r0 - w, y: cy0 - h * 0.5, w, h });
    case Part.Bottom: h = ny - t0; w = h * nr; [w, h] = fitKeepingRatio(w, h, nr, roomCx, 1 - t0); return clampPositionOnly({ x: cx0 - w * 0.5, y: t0, w, h });
    case Part.Top: h = b0 - ny; w = h * nr; [w, h] = fitKeepingRatio(w, h, nr, roomCx, b0); return clampPositionOnly({ x: cx0 - w * 0.5, y: b0 - h, w, h });
    default: return rect;
  }
}
export const moveTo = (r, nx, ny) => clampToFrame({ ...r, x: nx, y: ny });
