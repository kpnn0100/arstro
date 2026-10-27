/*
 * Solaris — kit/draw.js: the paint calls several widgets share, on a <canvas> in LOGICAL px (PORTING.md §2).
 *
 * COLOURS — never a hex Theme.h has (tokens.css is generated from it):
 *   color(name)             a token as [r, g, b, a] (0…1): color("deck-bg"), color("primary"), color("track-3").
 *                           LIVE under high contrast: the shell eases the tokens (a CSS transition on :root) and
 *                           calls `invalidateColors()` each frame of it, which also bumps `colorsVersion` — a widget
 *                           that paints a canvas reads `colorsVersion.value` in its draw effect and repaints.
 *   rgba(c, a = 1)          [r,g,b,a] → a CSS colour string, alpha multiplied by `a`.
 *   whiteAlpha(a), primaryAlpha(a), successAlpha(a), hoverWash(t), scrim(a)   palette's parametric tokens.
 *   track(i)                surface::track(i) — a strip's colour index (wraps at 10); `i < 0` = by kind: the caller's.
 *   lerpColor(a, b, t), brighten(c, k)   artboard interaction helpers (Interaction.h).
 *
 * CANVAS:
 *   canvasIn(host, ctx)     a <canvas> filling `host`, its backing store host size × devicePixelRatio × the drawn UI
 *                           scale (ctx.scale()), re-sized when any of them changes. Returns
 *                           { el, g, w, h, begin() } — begin() clears and sets the transform so you draw in logical px;
 *                           call it at the top of every paint. `onResize(fn)` re-paints after a size change.
 *   rr(r, w, h)             drawRoundedRect's radius clamp (≤ half the short side, Shapes.cpp:28-31).
 *   fillRR(g, x, y, w, h, r, color)    drawRoundedRect filled.
 *   strokeRR(g, x, y, w, h, r, color, sw)  … stroked: like Cairo, the stroke straddles the edge.
 *   line(g, x0, y0, x1, y1, color, sw)     one stroked segment.
 *   text(g, s, x, base, px, fam, color, tracking)   drawText at a baseline.
 */
import { signal } from "../core/signal.js";
import { FONT, cssFont } from "./text.js";

// ------------------------------------------------------------------ colours
let cache = new Map();
export const colorsVersion = signal(0);
/** Forget the parsed tokens (the shell calls it each frame of a contrast change). */
export function invalidateColors() { cache = new Map(); colorsVersion.value = colorsVersion.peek() + 1; }

export function parseColor(s) {
  s = (s || "").trim();
  if (s[0] === "#") {
    const hex = s.slice(1);
    const n = parseInt(hex.length === 3 ? hex.split("").map((c) => c + c).join("") : hex.slice(0, 6), 16);
    const a = hex.length === 8 ? parseInt(hex.slice(6, 8), 16) / 255 : 1;
    return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255, a];
  }
  const m = s.match(/-?[\d.]+/g);
  if (!m || m.length < 3) return [0, 0, 0, 1];
  return [m[0] / 255, m[1] / 255, m[2] / 255, m[3] === undefined ? 1 : +m[3]];
}

/** A colour token (tokens.css --<name>) as [r, g, b, a]. */
export function color(name) {
  let c = cache.get(name);
  if (!c) {
    const v = getComputedStyle(document.documentElement).getPropertyValue("--" + name);
    if (!v) console.warn("solaris: no token --" + name);
    c = parseColor(v);
    cache.set(name, c);
  }
  return c;
}

const f2 = (v) => Math.round(v * 100) / 100;
export function rgba(c, a = 1) {
  return `rgba(${f2(c[0] * 255)}, ${f2(c[1] * 255)}, ${f2(c[2] * 255)}, ${Math.round(Math.max(0, Math.min(1, c[3] * a)) * 1e4) / 1e4})`;
}
export const withAlpha = (c, a) => [c[0], c[1], c[2], a];
export const whiteAlpha = (a) => withAlpha(color("white"), a);
export const primaryAlpha = (a) => withAlpha(color("primary"), a);
export const successAlpha = (a) => withAlpha(color("success"), a);
export const hoverWash = (t) => whiteAlpha(color("hover-wash")[3] * t);
export const scrim = (a) => withAlpha(color("scrim"), color("scrim")[3] * a);
export const track = (i) => color("track-" + (((i % 10) + 10) % 10));
export const lerpColor = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
export const brighten = (c, k) => [c[0] + (1 - c[0]) * k, c[1] + (1 - c[1]) * k, c[2] + (1 - c[2]) * k, c[3]];

// ------------------------------------------------------------------ canvas
export function canvasIn(host, ctx) {
  const el = document.createElement("canvas");
  el.className = "sl-canvas";
  host.append(el);
  const g = el.getContext("2d");
  const api = { el, g, w: 0, h: 0, k: 1, _cb: null,
    onResize(fn) { api._cb = fn; return api; },
    /** Size the backing store if needed; then clear and draw in logical px. */
    begin() {
      const w = host.clientWidth || (ctx && ctx.box ? ctx.box.peek().w : 0), h = host.clientHeight || (ctx && ctx.box ? ctx.box.peek().h : 0);
      const k = (window.devicePixelRatio || 1) * (ctx && ctx.scale ? ctx.scale() : 1);
      if (w !== api.w || h !== api.h || Math.abs(k - api.k) > 1e-3) {
        api.w = w; api.h = h; api.k = k;
        el.width = Math.max(1, Math.round(w * k));
        el.height = Math.max(1, Math.round(h * k));
        el.style.width = w + "px";
        el.style.height = h + "px";
      }
      g.setTransform(1, 0, 0, 1, 0, 0);
      g.clearRect(0, 0, el.width, el.height);
      g.setTransform(el.width / Math.max(1, w), 0, 0, el.height / Math.max(1, h), 0, 0);
      return g;
    },
  };
  const ro = new ResizeObserver(() => { if (api._cb) api._cb(); });
  ro.observe(host);
  return api;
}

export const rr = (r, w, h) => Math.max(0, Math.min(r, w / 2, h / 2));
function pathRR(g, x, y, w, h, r) {
  g.beginPath();
  const k = rr(r, w, h);
  if (k > 0) g.roundRect(x, y, Math.max(0, w), Math.max(0, h), k); else g.rect(x, y, Math.max(0, w), Math.max(0, h));
}
export function fillRR(g, x, y, w, h, r, c) { pathRR(g, x, y, w, h, r); g.fillStyle = typeof c === "string" ? c : rgba(c); g.fill(); }
export function strokeRR(g, x, y, w, h, r, c, sw = 1) {
  pathRR(g, x, y, w, h, r);
  g.strokeStyle = typeof c === "string" ? c : rgba(c);
  g.lineWidth = sw;
  g.stroke();
}
export function line(g, x0, y0, x1, y1, c, sw = 1) {
  g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1);
  g.strokeStyle = typeof c === "string" ? c : rgba(c);
  g.lineWidth = sw; g.lineCap = "butt";
  g.stroke();
}
export function text(g, s, x, base, px, fam = FONT.sans, c = color("foreground"), tracking = 0) {
  g.font = cssFont(px, fam);
  g.fillStyle = typeof c === "string" ? c : rgba(c);
  g.textBaseline = "alphabetic";
  g.textAlign = "left";
  if ("letterSpacing" in g) g.letterSpacing = tracking ? tracking + "px" : "0px";
  g.fillText(String(s), x, base);
}
