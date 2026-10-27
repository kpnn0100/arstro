/*
 * Interstellar — kit/glyphs.js: the line icons cosmo's icon set has no word for, as SVG.
 *
 * A port of app/widgets/Glyphs.h (namespace glyph), shared because many widgets draw them (the
 * transport's play/pause/skip/camera, the timeline's film/lock/speaker/plus/minus, the bin's film,
 * the version switcher's chevrons and caret, the refusal chip's warn/check, every spinner). Same
 * geometry as the native's: strokes fitted to the box `b` ({x, y, w, h}, logical px), colour and
 * stroke width from the caller, butt caps and miter joins as Cairo strokes by default. Cosmo's
 * lucide set (folder, image, save, close, chevrons, ban, check, refresh) is ../ui/icons.js - use it
 * where the native uses cosmo's icon::.
 *
 *   glyph.warn(b, color, sw = 1.1) -> <g>        each returns an SVG group to append to an <svg>
 *   film, lock, play, pause, skip(b, c, dir), chevronLeft, chevronRightSmall, caretDown, camera,
 *   check, plus, minus, speaker, warn, spinner(cx, cy, r, phaseMs, alpha) (+ spinnerColors)
 *
 * `color` is any CSS colour string (tok.css("muted-foreground"), "var(--destructive)", rgba(var(--white-rgb), a));
 * it is set as the element's style, so tokens work.
 */
const NS = "http://www.w3.org/2000/svg";
/** Colours go in as STYLE, not presentation attributes: an attribute cannot hold var(--token). */
const STYLED = new Set(["fill", "stroke"]);
function el(tag, attrs) {
  const e = document.createElementNS(NS, tag);
  for (const k in attrs) {
    const v = attrs[k];
    if (v === undefined || v === null) continue;
    if (STYLED.has(k)) e.style[k] = v; else e.setAttribute(k, v);
  }
  return e;
}
function g(...kids) { const e = el("g", {}); for (const k of kids) e.append(k); return e; }
const f = (n) => Math.round(n * 1000) / 1000;
const stroke = (d, c, sw) => el("path", { d, fill: "none", stroke: c, "stroke-width": sw, "stroke-linecap": "butt", "stroke-linejoin": "miter" });
const fill = (d, c) => el("path", { d, fill: c, stroke: "none" });
/** drawRoundedRect with Shapes.cpp's radius clamp (at most half the shorter side). */
function rrect(x, y, w, h, r, attrs) {
  const k = Math.max(0, Math.min(r, w / 2, h / 2));
  return el("rect", { x: f(x), y: f(y), width: f(Math.max(0, w)), height: f(Math.max(0, h)), rx: k || null, ry: k || null, ...attrs });
}
const P = (b) => (fx, fy) => `${f(b.x + b.w * fx)} ${f(b.y + b.h * fy)}`;

export const glyph = {
  line(x0, y0, x1, y1, c, sw) { return stroke(`M${f(x0)} ${f(y0)} L${f(x1)} ${f(y1)}`, c, sw); },

  /** A film frame: a rounded body with a sprocket row top and bottom (Glyphs.h:31-42). */
  film(b, c, sw = 1.1) {
    const out = g(rrect(b.x + 0.5, b.y + b.h * 0.12, b.w - 1, b.h * 0.76, 1, { fill: "none", stroke: c, "stroke-width": sw }));
    for (let i = 0; i < 3; i++) {
      const x = b.x + b.w * (0.22 + 0.28 * i);
      out.append(glyph.line(x, b.y + b.h * 0.12, x, b.y + b.h * 0.26, c, sw), glyph.line(x, b.y + b.h * 0.74, x, b.y + b.h * 0.88, c, sw));
    }
    return out;
  },

  /** A padlock: shackle arc over a body (Glyphs.h:44-57). */
  lock(b, c, sw = 1.1) {
    const bw = b.w * 0.76, bh = b.h * 0.48, bx = b.x + (b.w - bw) * 0.5, by = b.y + b.h - bh - 0.5;
    const r = bw * 0.32, cx = b.x + b.w * 0.5, top = by - r * 1.15;
    return g(rrect(bx, by, bw, bh, 1, { fill: c }),
      stroke(`M${f(cx - r)} ${f(by)} L${f(cx - r)} ${f(top + r)} C${f(cx - r)} ${f(top - r * 0.35)} ${f(cx + r)} ${f(top - r * 0.35)} ${f(cx + r)} ${f(top + r)} L${f(cx + r)} ${f(by)}`, c, sw));
  },

  play(b, c) { const p = P(b); return fill(`M${p(0.24, 0.14)} L${p(0.86, 0.5)} L${p(0.24, 0.86)} Z`, c); },

  pause(b, c) {
    const w = b.w * 0.22;
    return g(rrect(b.x + b.w * 0.22, b.y + b.h * 0.16, w, b.h * 0.68, 1, { fill: c }), rrect(b.x + b.w * 0.56, b.y + b.h * 0.16, w, b.h * 0.68, 1, { fill: c }));
  },

  /** Skip to a cut: a bar and a triangle, pointing left (dir -1) or right (+1) (Glyphs.h:78-99). */
  skip(b, c, dir) {
    const cx = b.x + b.w * 0.5, barX = dir < 0 ? b.x + b.w * 0.2 : b.x + b.w * 0.8;
    const tri = dir < 0
      ? `M${f(b.x + b.w * 0.82)} ${f(b.y + b.h * 0.2)} L${f(cx - b.w * 0.2)} ${f(b.y + b.h * 0.5)} L${f(b.x + b.w * 0.82)} ${f(b.y + b.h * 0.8)} Z`
      : `M${f(b.x + b.w * 0.18)} ${f(b.y + b.h * 0.2)} L${f(cx + b.w * 0.2)} ${f(b.y + b.h * 0.5)} L${f(b.x + b.w * 0.18)} ${f(b.y + b.h * 0.8)} Z`;
    return g(glyph.line(barX, b.y + b.h * 0.2, barX, b.y + b.h * 0.8, c, 1.4), fill(tri, c));
  },

  chevronLeft(b, c, sw = 1.3) { const p = P(b); return stroke(`M${p(0.62, 0.22)} L${p(0.36, 0.5)} L${p(0.62, 0.78)}`, c, sw); },
  chevronRightSmall(b, c, sw = 1.3) { const p = P(b); return stroke(`M${p(0.38, 0.22)} L${p(0.64, 0.5)} L${p(0.38, 0.78)}`, c, sw); },
  caretDown(b, c, sw = 1.2) { const p = P(b); return stroke(`M${p(0.2, 0.38)} L${p(0.5, 0.66)} L${p(0.8, 0.38)}`, c, sw); },

  /** A camera body with a lens - "capture this frame" (Glyphs.h:130-156). */
  camera(b, c, sw = 1.2) {
    const x = b.x, y = b.y + b.h * 0.22, w = b.w, h = b.h * 0.66;
    const body = `M${f(x + w * 0.08)} ${f(y + h * 0.18)} L${f(x + w * 0.3)} ${f(y + h * 0.18)} L${f(x + w * 0.38)} ${f(y)} L${f(x + w * 0.62)} ${f(y)}` +
                 ` L${f(x + w * 0.7)} ${f(y + h * 0.18)} L${f(x + w * 0.92)} ${f(y + h * 0.18)} L${f(x + w * 0.92)} ${f(y + h)} L${f(x + w * 0.08)} ${f(y + h)} Z`;
    const cx = x + w * 0.5, cy = y + h * 0.58, r = Math.min(w, h) * 0.24, k = 0.5523 * r;
    const lens = `M${f(cx + r)} ${f(cy)} C${f(cx + r)} ${f(cy + k)} ${f(cx + k)} ${f(cy + r)} ${f(cx)} ${f(cy + r)}` +
                 ` C${f(cx - k)} ${f(cy + r)} ${f(cx - r)} ${f(cy + k)} ${f(cx - r)} ${f(cy)}` +
                 ` C${f(cx - r)} ${f(cy - k)} ${f(cx - k)} ${f(cy - r)} ${f(cx)} ${f(cy - r)}` +
                 ` C${f(cx + k)} ${f(cy - r)} ${f(cx + r)} ${f(cy - k)} ${f(cx + r)} ${f(cy)} Z`;
    return g(stroke(body, c, sw), stroke(lens, c, sw));
  },

  /** ✓ - a finished, successful action (the notice chip) (Glyphs.h:158-166). */
  check(b, c, sw = 1.3) { const p = P(b); return stroke(`M${p(0.12, 0.55)} L${p(0.4, 0.82)} L${p(0.9, 0.22)}`, c, sw); },

  plus(b, c, sw = 1.3) {
    const cx = b.x + b.w * 0.5, cy = b.y + b.h * 0.5, r = b.w * 0.36;
    return g(glyph.line(cx - r, cy, cx + r, cy, c, sw), glyph.line(cx, cy - r, cx, cy + r, c, sw));
  },
  minus(b, c, sw = 1.3) {
    const cx = b.x + b.w * 0.5, cy = b.y + b.h * 0.5, r = b.w * 0.36;
    return glyph.line(cx - r, cy, cx + r, cy, c, sw);
  },

  /** A speaker - an audio track (Glyphs.h:181-199). */
  speaker(b, c, sw = 1.1) {
    const p = P(b);
    return g(fill(`M${p(0.12, 0.38)} L${p(0.32, 0.38)} L${p(0.56, 0.16)} L${p(0.56, 0.84)} L${p(0.32, 0.62)} L${p(0.12, 0.62)} Z`, c),
      stroke(`M${p(0.7, 0.32)} Q${p(0.82, 0.5)} ${p(0.7, 0.68)}`, c, sw));
  },

  /** A warning triangle with a bang (Glyphs.h:201-213). */
  warn(b, c, sw = 1.1) {
    const p = P(b);
    return g(stroke(`M${p(0.5, 0.1)} L${p(0.94, 0.88)} L${p(0.06, 0.88)} Z`, c, sw),
      glyph.line(b.x + b.w * 0.5, b.y + b.h * 0.38, b.x + b.w * 0.5, b.y + b.h * 0.6, c, sw),
      el("circle", { cx: f(b.x + b.w * 0.5), cy: f(b.y + b.h * 0.74), r: f(sw * 0.6), fill: c }));
  },

  /** An indeterminate spinner arc - the "still coming" cell; the phase is a PERIOD (900 ms a turn),
   *  a timing rather than a tween (Glyphs.h:216-231). Re-create (or setAttribute "d") per frame. */
  spinner(cx, cy, r, phaseMs, alpha = 1) {
    const a0 = ((phaseMs % 900) / 900) * Math.PI * 2;
    let d = "";
    for (let s = 0; s <= 8; s++) {
      const a = a0 + (s / 8) * (Math.PI / 2);
      d += (s ? " L" : "M") + f(cx + Math.cos(a) * r) + " " + f(cy + Math.sin(a) * r);
    }
    return g(el("circle", { cx: f(cx), cy: f(cy), r: f(r), fill: "none", stroke: `rgba(var(--white-rgb), ${0.1 * alpha})`, "stroke-width": 1.6 }),
      stroke(d, `rgba(var(--primary-rgb), ${0.85 * alpha})`, 1.6));
  },
};
