/*
 * Cosmo by arstro — params.js: EditParamsIO in the browser - read the model's `params`, write `set`.
 *
 * The model carries the edit target's params as EditParamsIO text (serializeParams,
 * core/ImageProcessing/src/engine/EditParamsIO.cpp) and every change goes back as
 * `set key=value ...` in the same syntax. This is a CODEC, not a second model: it parses the
 * exact text the C++ writes and formats values the C++ parser reads (deserializeParams /
 * parseCurvePoints / parseMaskBlob), so a curve or a mask survives the round trip bit for bit.
 * Numbers are written like the C++ stream writes them (7 significant digits).
 *
 *   curve / curveR|G|B / mixer0..2 / a mask's path:  "x,y;x,y,ix,iy,ox,oy;..."  (6 fields = smooth)
 *   grade0..2: "hue,sat,lum"      crop: "x,y,w,h" (0..1)      curveLog / remapEnable: 0|1
 *   mask: "type,inv,feather,cx,cy,rx,ry,x0,y0,x1,y1|12 local adjusts|x:y:r:f;...|path"
 */

export const SCALARS = ["exposure", "contrast", "highlights", "shadows", "whites", "blacks", "temp", "tint",
  "vibrance", "saturation", "texture", "clarity", "dehaze", "grainAmount", "grainSize", "sharpenAmount",
  "sharpenRadius", "sharpenMasking", "nrLuminance", "nrColor", "lensDistortion", "lensCA", "lensVignette",
  "mixerSpread", "balance", "remapSrc", "remapRange", "remapDst", "remapStrength", "rotation", "quarterTurns"];

export const LOCAL_ADJUST = ["exposure", "contrast", "highlights", "shadows", "whites", "blacks", "temp", "tint",
  "saturation", "texture", "clarity", "dehaze"];

/** A number as the C++ `ostream` with precision(7) prints it. */
export function num(v) {
  if (!Number.isFinite(v)) return "0";
  const s = String(Number(v.toPrecision(7)));
  return s === "-0" ? "0" : s;
}

const f = (s) => { const v = parseFloat(s); return Number.isFinite(v) ? v : 0; };

export function parseCurve(s) {
  const out = [];
  for (const seg of (s || "").split(";")) {
    const n = seg.split(",").filter((t) => t !== "").map(f);
    if (n.length < 2) continue;
    const p = { x: n[0], y: n[1], smooth: false, ix: 0, iy: 0, ox: 0, oy: 0 };
    if (n.length >= 6) Object.assign(p, { smooth: true, ix: n[2], iy: n[3], ox: n[4], oy: n[5] });
    out.push(p);
  }
  return out;
}

export function formatCurve(points) {
  return points.map((p) => (p.smooth ? [p.x, p.y, p.ix, p.iy, p.ox, p.oy] : [p.x, p.y]).map(num).join(",")).join(";");
}

export function parseMask(s) {
  const parts = (s || "").split("|");
  const g = (parts[0] || "").split(",").map(f);
  const m = { type: 0, inverted: false, feather: 0, cx: 0, cy: 0, rx: 0, ry: 0, x0: 0, y0: 0, x1: 0, y1: 0,
              adjust: Object.fromEntries(LOCAL_ADJUST.map((k) => [k, 0])), dabs: [], path: [] };
  if (g.length >= 11) {
    Object.assign(m, { type: g[0] | 0, inverted: g[1] !== 0, feather: g[2], cx: g[3], cy: g[4], rx: g[5], ry: g[6],
                       x0: g[7], y0: g[8], x1: g[9], y1: g[10] });
  }
  const a = (parts[1] || "").split(",").map(f);
  if (a.length >= 12) LOCAL_ADJUST.forEach((k, i) => { m.adjust[k] = a[i]; });
  if (parts[2]) {
    for (const d of parts[2].split(";")) {
      const n = d.split(":").map(f);
      if (n.length >= 4) m.dabs.push({ x: n[0], y: n[1], radius: n[2], flow: n[3] });
    }
  }
  if (parts[3]) m.path = parseCurve(parts[3]);
  return m;
}

export function formatMask(m) {
  let s = [m.type, m.inverted ? 1 : 0, m.feather, m.cx, m.cy, m.rx, m.ry, m.x0, m.y0, m.x1, m.y1].map(num).join(",");
  s += "|" + LOCAL_ADJUST.map((k) => num(m.adjust[k] || 0)).join(",") + "|";
  s += m.dabs.map((d) => [d.x, d.y, d.radius, d.flow].map(num).join(":")).join(";");
  if (m.path && m.path.length) s += "|" + formatCurve(m.path);
  return s;
}

/** The params text -> typed values. Keeps the raw text of every key too (`raw`), so a view
 *  can tell "unchanged" by string compare without re-formatting. */
export function parseParams(text) {
  const p = { raw: {}, masks: [], curve: [], curveR: [], curveG: [], curveB: [], mixer: [[], [], []],
              grade: [{ hue: 0, sat: 0, lum: 0 }, { hue: 0, sat: 0, lum: 0 }, { hue: 0, sat: 0, lum: 0 }],
              crop: { x: 0, y: 0, w: 1, h: 1 }, curveLog: true, remapEnable: false };
  for (const s of SCALARS) p[s] = 0;
  p.temp = 6500;
  p.sharpenRadius = 1;
  for (const line of (text || "").split("\n")) {
    const eq = line.indexOf("=");
    if (eq <= 0) continue;
    const k = line.slice(0, eq), v = line.slice(eq + 1);
    if (k === "mask") { p.masks.push(parseMask(v)); continue; }
    p.raw[k] = v;
    if (k === "curve" || k === "curveR" || k === "curveG" || k === "curveB") p[k] = parseCurve(v);
    else if (/^mixer[0-2]$/.test(k)) p.mixer[+k[5]] = parseCurve(v);
    else if (/^grade[0-2]$/.test(k)) { const n = v.split(",").map(f); p.grade[+k[5]] = { hue: n[0] || 0, sat: n[1] || 0, lum: n[2] || 0 }; }
    else if (k === "crop") { const n = v.split(",").map(f); p.crop = { x: n[0] ?? 0, y: n[1] ?? 0, w: n[2] ?? 1, h: n[3] ?? 1 }; }
    else if (k === "curveLog" || k === "remapEnable") p[k] = v !== "0";
    else if (k === "quarterTurns") p[k] = f(v) | 0;
    else p[k] = f(v);
  }
  return p;
}

/** `set` fields for a grade wheel, a crop, a curve ... in the exact text the parser reads. */
export const fmt = {
  grade: (g) => [g.hue, g.sat, g.lum].map(num).join(","),
  crop: (c) => [c.x, c.y, c.w, c.h].map(num).join(","),
  curve: formatCurve,
  mask: formatMask,
  bool: (b) => (b ? "1" : "0"),
  num,
};

/** "set a=1 b=2" from {a: "1", b: "2"} (values already formatted). */
export function setLine(fields) {
  return "set " + Object.entries(fields).map(([k, v]) => `${k}=${v}`).join(" ");
}
