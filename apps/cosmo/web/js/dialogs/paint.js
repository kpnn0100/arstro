/*
 * Cosmo by arstro — paint.js: the drawing vocabulary the Home, the loading screen and every
 * dialog share, ported from the native primitives they are drawn with.
 *
 *   est(s, px)            widgets/TextMetrics.h:21 estimateTextWidth - UTF-8 BYTES x px x 0.6.
 *                         The native centres / right-aligns most labels with it, so the web
 *                         must place them with it too (not with real glyph widths).
 *   measure(s, px, fam)   real glyph advance (canvas measureText, same letter-spacing) - the
 *                         few places the native measures (wordmark dot, splash).
 *   text(el, x, base, px, fam)   positions a text box so its ALPHABETIC BASELINE lands at `base`
 *                         (Cairo move_to + show_text): with line-height = px the baseline sits at
 *                         top + px/2 + k*px, k from the shipped TTFs' hhea metrics (Roboto
 *                         1900/-500 @2048 -> 0.3418, JetBrains Mono 1020/-300 @1000 -> 0.36).
 *   fitEnd / fitFront     the native's ellipsis rule (InfoDialog / ExportDialog): keep what fits
 *                         by est(), else floor((maxW - est("...")) / (px*0.6)) bytes + U+2026.
 *   tokens()              the token colours (tokens.css, generated from Theme.h) as RGBA, and
 *   lerpColor / brighten / css   Artboard's colour maths (componentwise RGBA lerp,
 *                         `brighten(c,a) = c + (1-c)*a`), so a hover end-state the native derives
 *                         from two tokens is derived from the same two tokens here - never typed.
 *   svg(tag, attrs)       SVG strokes: Cairo centres a stroke on its path (a 1 px hairline at an
 *                         integer x covers two half pixels); SVG does the same, CSS borders do not.
 */

const enc = new TextEncoder();
export const bytes = (s) => enc.encode(String(s)).length;
export const est = (s, px) => bytes(s) * px * 0.6;

const K = { sans: 0.3418, mono: 0.36 };
const FAMILY = {
  sans: "var(--font-sans)", medium: "var(--font-sans-medium)", semibold: "var(--font-sans-semibold)",
  mono: "var(--font-mono)", monoMedium: "var(--font-mono-medium)",
};
const CANVAS_FAMILY = {
  sans: '"Roboto"', medium: '"Roboto Medium"', semibold: '"Roboto SemiBold"',
  mono: '"JetBrains Mono"', monoMedium: '"JetBrains Mono Medium"',
};
export const isMono = (fam) => fam === "mono" || fam === "monoMedium";
export const fontFamily = (fam) => FAMILY[fam] || FAMILY.sans;
export const canvasFont = (px, fam) => `${px}px ${CANVAS_FAMILY[fam] || CANVAS_FAMILY.sans}`;

/** Top of a line box (line-height = px) whose baseline must sit at `base`. */
export const topFor = (base, px, fam = "sans") => base - px * (0.5 + (isMono(fam) ? K.mono : K.sans));

/** Build (or restyle) an absolutely placed, unwrapped text run. */
export function text(el, x, base, px, fam = "sans", spacing = 0) {
  const s = el.style;
  s.position = "absolute";
  s.left = x + "px";
  s.top = topFor(base, px, fam) + "px";
  s.fontSize = px + "px";
  s.lineHeight = px + "px";
  s.fontFamily = fontFamily(fam);
  s.whiteSpace = "pre";
  if (spacing) s.letterSpacing = spacing + "px";
  return el;
}

let mctx = null;
export function measure(s, px, fam = "sans", spacing = 0) {
  if (!mctx) mctx = document.createElement("canvas").getContext("2d");
  mctx.font = canvasFont(px, fam);
  mctx.letterSpacing = spacing + "px";
  return mctx.measureText(s).width;
}

/** End-ellipsis to maxW by est() (InfoDialog.cpp fit rule). */
export function fitEnd(s, maxW, px) {
  s = String(s);
  if (est(s, px) <= maxW) return s;
  const keep = Math.max(0, Math.floor((maxW - est("...", px)) / (px * 0.6)));
  return cutBytes(s, keep, false) + "…";
}
/** Front-ellipsis (paths): keep the tail. */
export function fitFront(s, maxW, px) {
  s = String(s);
  if (est(s, px) <= maxW) return s;
  const keep = Math.max(0, Math.floor((maxW - est("...", px)) / (px * 0.6)));
  return "…" + cutBytes(s, keep, true);
}
function cutBytes(s, n, fromEnd) {
  const chars = Array.from(s);
  let out = "", used = 0;
  if (fromEnd) {
    for (let i = chars.length - 1; i >= 0; i--) { const b = bytes(chars[i]); if (used + b > n) break; used += b; out = chars[i] + out; }
  } else {
    for (const c of chars) { const b = bytes(c); if (used + b > n) break; used += b; out += c; }
  }
  return out;
}

// ------------------------------------------------------------------ colour (Artboard Color)
let TOK = null;
function parseColor(v) {
  v = (v || "").trim();
  let m = /^#([0-9a-f]{6})$/i.exec(v);
  if (m) { const n = parseInt(m[1], 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255, 1]; }
  m = /^rgba?\(([^)]+)\)$/i.exec(v);
  if (m) { const p = m[1].split(/[\s,/]+/).filter(Boolean).map(Number); return [p[0], p[1], p[2], p.length > 3 ? p[3] : 1]; }
  return [0, 0, 0, 1];
}
/** Token colours as [r,g,b,a] (0..255, a 0..1), read once from tokens.css. */
export function tokens() {
  if (TOK) return TOK;
  const cs = getComputedStyle(document.documentElement);
  const get = (n) => parseColor(cs.getPropertyValue("--" + n));
  TOK = {};
  for (const n of ["background", "foreground", "card", "popover", "primary", "primary-foreground", "secondary", "muted",
                   "muted-foreground", "destructive", "success", "border", "input", "input-light", "input-light-text",
                   "switch-background", "ring", "canvas-bg", "left-rail-bg", "folder-chip-bg", "segmented-bg", "white"]) TOK[n] = get(n);
  return TOK;
}
export const withAlpha = (c, a) => [c[0], c[1], c[2], a];
export const fade = (c, k) => [c[0], c[1], c[2], c[3] * k];
export const lerpColor = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
export const brighten = (c, a) => [c[0] + (255 - c[0]) * a, c[1] + (255 - c[1]) * a, c[2] + (255 - c[2]) * a, c[3]];
export const css = (c) => `rgba(${c[0].toFixed(2)}, ${c[1].toFixed(2)}, ${c[2].toFixed(2)}, ${+c[3].toFixed(4)})`;
/** interaction::kHoverFillLift / palette::hoverWash (Theme.h:92) */
export const HOVER_LIFT = 0.14;
export const hoverWash = (t) => [255, 255, 255, 0.07 * t];

/** Set derived colours as custom properties on `el`: {name: [r,g,b,a]}. */
export function setVars(el, vars) {
  for (const [k, v] of Object.entries(vars)) el.style.setProperty("--" + k, Array.isArray(v) ? css(v) : v);
}

// ------------------------------------------------------------------ SVG
const NS = "http://www.w3.org/2000/svg";
export function svg(tag, attrs, ...kids) {
  const el = document.createElementNS(NS, tag);
  if (attrs) for (const [k, v] of Object.entries(attrs)) if (v !== undefined && v !== null) el.setAttribute(k, v);
  for (const k of kids) if (k) el.append(k);
  return el;
}
/** A stroke-only glyph in a `size` box: `paths` in a 0..vb viewBox, butt caps, miter joins,
 *  stroke width in screen px (non-scaling), as widgets/Icons.cpp draws. */
export function glyph(paths, size, sw, vb = size, extra) {
  const el = svg("svg", { width: size, height: size, viewBox: `0 0 ${vb} ${vb}`, "aria-hidden": "true", class: "glyph" });
  el.style.overflow = "visible";
  for (const p of paths) {
    const tag = p.circle ? "circle" : "path";
    const a = p.circle ? { cx: p.circle[0], cy: p.circle[1], r: p.circle[2] } : { d: p };
    el.append(svg(tag, { ...a, fill: "none", stroke: "currentColor", "stroke-width": sw, "stroke-linecap": "butt",
                        "stroke-linejoin": "miter", "vector-effect": "non-scaling-stroke" }));
  }
  if (extra) el.append(extra);
  return el;
}

/** Place an SVG at a fractional (x, y) without the browser snapping it: the element sits on
 *  the integer part and its content is translated by the fraction (Chrome snaps a replaced
 *  element's box to whole pixels, which moves a 1.6 px stroke by half a pixel). */
export function at(el, x, y, cls) {
  const ix = Math.floor(x), iy = Math.floor(y), fx = x - ix, fy = y - iy;
  el.style.position = "absolute";
  el.style.left = ix + "px"; el.style.top = iy + "px";
  el.style.overflow = "visible";
  if (cls) el.classList.add(...cls.split(" "));
  let g = el.querySelector(":scope > g.at");
  if (fx || fy || g) {
    const vb = el.viewBox && el.viewBox.baseVal, w = parseFloat(el.getAttribute("width")) || 1;
    const k = vb && vb.width ? vb.width / w : 1;
    if (!g) {
      g = document.createElementNS(NS, "g");
      g.setAttribute("class", "at");
      while (el.firstChild) g.append(el.firstChild);
      el.append(g);
    }
    g.setAttribute("transform", `translate(${fx * k} ${fy * k})`);
  }
  return el;
}

/** Relative date and size exactly as App.cpp:1296-1322 print them. */
export function cardBytes(b) {
  if (!(b > 0)) return "";
  const u = ["B", "KB", "MB", "GB", "TB"];
  let v = b, i = 0;
  while (v >= 1024 && i < 4) { v /= 1024; i++; }
  return (v < 10 && i > 0 ? v.toFixed(1) : v.toFixed(0)) + " " + u[i];
}
export function relativeTime(t) {
  if (!(t > 0)) return "";
  const d = Math.max(0, Math.floor(Date.now() / 1000) - t);
  if (d < 60) return "just now";
  if (d < 3600) return Math.floor(d / 60) + "m ago";
  if (d < 86400) return Math.floor(d / 3600) + "h ago";
  if (d < 172800) return "Yesterday";
  if (d < 604800) return Math.floor(d / 86400) + " days ago";
  if (d < 2592000) return Math.floor(d / 604800) + " weeks ago";
  if (d < 31536000) return Math.floor(d / 2592000) + " months ago";
  return Math.floor(d / 31536000) + " years ago";
}
export const photosLabel = (n) => n + (n === 1 ? " photo" : " photos");

/** The effective CSS-px-per-logical-px of an element inside the UI-scale root. */
export function scaleOf(el) {
  const w = el.offsetWidth;
  if (!w) return 1;
  return el.getBoundingClientRect().width / w;
}

/** A canvas covering `host` (the logical root), its backing store at the drawn scale. */
export function surface(host, cls) {
  const cv = document.createElement("canvas");
  cv.className = cls;
  host.append(cv);
  const g = cv.getContext("2d");
  let W = 0, H = 0, K = 0;
  function fit() {
    const w = host.clientWidth, h = host.clientHeight;
    const r = host.getBoundingClientRect();
    const k = (w ? r.width / w : 1) * (window.devicePixelRatio || 1);
    if (w !== W || h !== H || Math.abs(k - K) > 1e-3) {
      W = w; H = h; K = k;
      cv.width = Math.max(1, Math.round(w * k)); cv.height = Math.max(1, Math.round(h * k));
      cv.style.width = w + "px"; cv.style.height = h + "px";
    }
    g.setTransform(K, 0, 0, K, 0, 0);
    g.fontKerning = "none";
    g.textBaseline = "alphabetic";
    return { W, H };
  }
  return { cv, g, fit };
}
