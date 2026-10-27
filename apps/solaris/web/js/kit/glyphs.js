/*
 * Solaris — kit/glyphs.js: interstellar's Glyphs.h (the line icons cosmo's icon set has no word for), which Solaris's
 * widgets draw — play, a speaker, a plus, a caret, chevrons, a warning … — on a canvas, in logical px.
 *
 *   glyph.<name>(g, b, c, sw?)   b = {x, y, w, h} (the box the strokes fit), c = a colour ([r,g,b,a] or CSS string),
 *                                sw = stroke width (each glyph's default is the native's). The arithmetic is Glyphs.h's,
 *                                line for line.
 *   glyph.spinner(g, cx, cy, r, phaseMs, alpha)   the indeterminate arc (900 ms a turn).
 *
 * Cosmo's lucide glyphs (folder, save, close, chevronRight/Down, check …) are the borrowed ui/icons.js (SVG).
 */
import { rgba, color, withAlpha } from "./draw.js";

const css = (c) => (typeof c === "string" ? c : rgba(c));
function stroke(g, c, sw) { g.strokeStyle = css(c); g.lineWidth = sw; g.lineCap = "butt"; g.lineJoin = "miter"; g.stroke(); }
function fill(g, c) { g.fillStyle = css(c); g.fill(); }
function rrect(g, x, y, w, h, r) { g.beginPath(); g.roundRect(x, y, Math.max(0, w), Math.max(0, h), Math.max(0, Math.min(r, w / 2, h / 2))); }

export const glyph = {
  line(g, x0, y0, x1, y1, c, sw) { g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); stroke(g, c, sw); },

  /** A film frame: a rounded body with a sprocket row top and bottom. */
  film(g, b, c, sw = 1.1) {
    rrect(g, b.x + 0.5, b.y + b.h * 0.12, b.w - 1.0, b.h * 0.76, 1.0); stroke(g, c, sw);
    for (let i = 0; i < 3; ++i) {
      const x = b.x + b.w * (0.22 + 0.28 * i);
      glyph.line(g, x, b.y + b.h * 0.12, x, b.y + b.h * 0.26, c, sw);
      glyph.line(g, x, b.y + b.h * 0.74, x, b.y + b.h * 0.88, c, sw);
    }
  },

  /** A padlock: shackle arc over a body. */
  lock(g, b, c, sw = 1.1) {
    const bw = b.w * 0.76, bh = b.h * 0.48, bx = b.x + (b.w - bw) * 0.5, by = b.y + b.h - bh - 0.5;
    rrect(g, bx, by, bw, bh, 1.0); fill(g, c);
    const r = bw * 0.32, cx = b.x + b.w * 0.5, top = by - r * 1.15;
    g.beginPath();
    g.moveTo(cx - r, by); g.lineTo(cx - r, top + r);
    g.bezierCurveTo(cx - r, top - r * 0.35, cx + r, top - r * 0.35, cx + r, top + r);
    g.lineTo(cx + r, by);
    stroke(g, c, sw);
  },

  play(g, b, c) {
    g.beginPath();
    g.moveTo(b.x + b.w * 0.24, b.y + b.h * 0.14);
    g.lineTo(b.x + b.w * 0.86, b.y + b.h * 0.5);
    g.lineTo(b.x + b.w * 0.24, b.y + b.h * 0.86);
    g.closePath();
    fill(g, c);
  },

  pause(g, b, c) {
    const w = b.w * 0.22;
    rrect(g, b.x + b.w * 0.22, b.y + b.h * 0.16, w, b.h * 0.68, 1.0); fill(g, c);
    rrect(g, b.x + b.w * 0.56, b.y + b.h * 0.16, w, b.h * 0.68, 1.0); fill(g, c);
  },

  /** Skip to a cut: a bar and a triangle, pointing left (dir = -1) or right (dir = +1). */
  skip(g, b, c, dir) {
    const cx = b.x + b.w * 0.5, barX = dir < 0 ? b.x + b.w * 0.2 : b.x + b.w * 0.8;
    glyph.line(g, barX, b.y + b.h * 0.2, barX, b.y + b.h * 0.8, c, 1.4);
    g.beginPath();
    if (dir < 0) {
      g.moveTo(b.x + b.w * 0.82, b.y + b.h * 0.2); g.lineTo(cx - b.w * 0.2, b.y + b.h * 0.5); g.lineTo(b.x + b.w * 0.82, b.y + b.h * 0.8);
    } else {
      g.moveTo(b.x + b.w * 0.18, b.y + b.h * 0.2); g.lineTo(cx + b.w * 0.2, b.y + b.h * 0.5); g.lineTo(b.x + b.w * 0.18, b.y + b.h * 0.8);
    }
    g.closePath();
    fill(g, c);
  },

  chevronLeft(g, b, c, sw = 1.3) {
    g.beginPath();
    g.moveTo(b.x + b.w * 0.62, b.y + b.h * 0.22); g.lineTo(b.x + b.w * 0.36, b.y + b.h * 0.5); g.lineTo(b.x + b.w * 0.62, b.y + b.h * 0.78);
    stroke(g, c, sw);
  },
  chevronRightSmall(g, b, c, sw = 1.3) {
    g.beginPath();
    g.moveTo(b.x + b.w * 0.38, b.y + b.h * 0.22); g.lineTo(b.x + b.w * 0.64, b.y + b.h * 0.5); g.lineTo(b.x + b.w * 0.38, b.y + b.h * 0.78);
    stroke(g, c, sw);
  },
  caretDown(g, b, c, sw = 1.2) {
    g.beginPath();
    g.moveTo(b.x + b.w * 0.2, b.y + b.h * 0.38); g.lineTo(b.x + b.w * 0.5, b.y + b.h * 0.66); g.lineTo(b.x + b.w * 0.8, b.y + b.h * 0.38);
    stroke(g, c, sw);
  },

  /** A camera body with a lens — "capture this frame". */
  camera(g, b, c, sw = 1.2) {
    const x = b.x, y = b.y + b.h * 0.22, w = b.w, h = b.h * 0.66;
    g.beginPath();
    g.moveTo(x + w * 0.08, y + h * 0.18); g.lineTo(x + w * 0.30, y + h * 0.18); g.lineTo(x + w * 0.38, y); g.lineTo(x + w * 0.62, y);
    g.lineTo(x + w * 0.70, y + h * 0.18); g.lineTo(x + w * 0.92, y + h * 0.18); g.lineTo(x + w * 0.92, y + h); g.lineTo(x + w * 0.08, y + h);
    g.closePath();
    stroke(g, c, sw);
    const cx = x + w * 0.5, cy = y + h * 0.58, r = Math.min(w, h) * 0.24, k = 0.5523 * r;
    g.beginPath();
    g.moveTo(cx + r, cy);
    g.bezierCurveTo(cx + r, cy + k, cx + k, cy + r, cx, cy + r);
    g.bezierCurveTo(cx - k, cy + r, cx - r, cy + k, cx - r, cy);
    g.bezierCurveTo(cx - r, cy - k, cx - k, cy - r, cx, cy - r);
    g.bezierCurveTo(cx + k, cy - r, cx + r, cy - k, cx + r, cy);
    g.closePath();
    stroke(g, c, sw);
  },

  /** ✓ — a finished, successful action. */
  check(g, b, c, sw = 1.3) {
    g.beginPath();
    g.moveTo(b.x + b.w * 0.12, b.y + b.h * 0.55); g.lineTo(b.x + b.w * 0.40, b.y + b.h * 0.82); g.lineTo(b.x + b.w * 0.90, b.y + b.h * 0.22);
    stroke(g, c, sw);
  },

  plus(g, b, c, sw = 1.3) {
    const cx = b.x + b.w * 0.5, cy = b.y + b.h * 0.5, r = b.w * 0.36;
    glyph.line(g, cx - r, cy, cx + r, cy, c, sw);
    glyph.line(g, cx, cy - r, cx, cy + r, c, sw);
  },
  minus(g, b, c, sw = 1.3) {
    const cx = b.x + b.w * 0.5, cy = b.y + b.h * 0.5, r = b.w * 0.36;
    glyph.line(g, cx - r, cy, cx + r, cy, c, sw);
  },

  /** A speaker — an audio track. */
  speaker(g, b, c, sw = 1.1) {
    g.beginPath();
    g.moveTo(b.x + b.w * 0.12, b.y + b.h * 0.38); g.lineTo(b.x + b.w * 0.32, b.y + b.h * 0.38); g.lineTo(b.x + b.w * 0.56, b.y + b.h * 0.16);
    g.lineTo(b.x + b.w * 0.56, b.y + b.h * 0.84); g.lineTo(b.x + b.w * 0.32, b.y + b.h * 0.62); g.lineTo(b.x + b.w * 0.12, b.y + b.h * 0.62);
    g.closePath();
    fill(g, c);
    g.beginPath();
    g.moveTo(b.x + b.w * 0.70, b.y + b.h * 0.32);
    g.quadraticCurveTo(b.x + b.w * 0.82, b.y + b.h * 0.5, b.x + b.w * 0.70, b.y + b.h * 0.68);
    stroke(g, c, sw);
  },

  /** A warning triangle with a bang. */
  warn(g, b, c, sw = 1.1) {
    g.beginPath();
    g.moveTo(b.x + b.w * 0.5, b.y + b.h * 0.1); g.lineTo(b.x + b.w * 0.94, b.y + b.h * 0.88); g.lineTo(b.x + b.w * 0.06, b.y + b.h * 0.88);
    g.closePath();
    stroke(g, c, sw);
    glyph.line(g, b.x + b.w * 0.5, b.y + b.h * 0.38, b.x + b.w * 0.5, b.y + b.h * 0.6, c, sw);
    g.beginPath(); g.arc(b.x + b.w * 0.5, b.y + b.h * 0.74, sw * 0.6, 0, Math.PI * 2); fill(g, c);
  },

  /** The indeterminate spinner arc (a PERIOD of 900 ms a turn — a timing, not a tween). */
  spinner(g, cx, cy, r, phaseMs, alpha = 1) {
    g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2); stroke(g, withAlpha(color("white"), 0.10 * alpha), 1.6);
    const a0 = ((phaseMs % 900) / 900) * 6.28318530718;
    g.beginPath();
    for (let s = 0; s <= 8; ++s) {
      const a = a0 + (s / 8) * 1.5707963268, px = cx + Math.cos(a) * r, py = cy + Math.sin(a) * r;
      if (s === 0) g.moveTo(px, py); else g.lineTo(px, py);
    }
    stroke(g, withAlpha(color("primary"), 0.85 * alpha), 1.6);
  },
};
