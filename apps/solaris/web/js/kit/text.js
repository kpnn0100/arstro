/*
 * Solaris — kit/text.js: measured text, as the native measures it (interstellar's TextFit.h, which Solaris includes).
 *
 *   FONT                    the families Theme.h's font:: names (tokens.css --font-*): sans, medium, semibold, mono,
 *                           monoMedium — the faces `ntwb install` ships and base.css registers under those names.
 *   fontsReady()            a Promise: every face loaded (the shell mounts nothing before it, so a measurement is never
 *                           a fallback font's).
 *   measure(s, px, fam, tr) IRenderTarget::measureText: the advance of `s` at `px` in `fam`, `tr` px of tracking a glyph.
 *   ellipsize(s, maxW, px, fam, tr)
 *                           TextFit::ellipsize: `s`, or the longest prefix that fits with "…" (code points, never a
 *                           cut glyph; no trailing space before the "…"); "" when even "…" does not fit.
 *   fitSize(s, maxW, maxPx, minPx, fam, trPerPx)
 *                           TextFit::fitSize: the largest size ≤ maxPx (≥ minPx) at which `s` fits.
 *   ellipsizeFront(s, maxW, px, fam, tr)   the same from the front — a path keeps its tail ("…/Songs/a song.slp").
 *   baseline(cy, px)        TextFit::baseline: centred on cy — the universal 0.35 rule.
 *   cssFont(px, fam)        the CSS `font` for a canvas or an element.
 *   svgText(s, x, base, px, fam, tracking)   drawText as an SVG <text> at its ALPHABETIC baseline — PREFER IT (or a
 *                           canvas) where a pixel matters: Chrome snaps an HTML box's top to a whole pixel, an SVG
 *                           text's baseline rounds as Cairo's does (PORTING.md §4, the kit's baseline lesson).
 *                           `svgBox(w, h)` an <svg> to hold it (and SVG rects: their 1 px stroke straddles like Cairo's).
 *   textAt(el, x, base, px, fam, tracking)   a DOM text run placed so its ALPHABETIC BASELINE lands at `base`
 *                           (drawText's): absolute, unwrapped, line-height = px; the faces' hhea metrics put the
 *                           baseline at top + px·(0.5 + k), k = 0.3418 Roboto (1900/-500 @2048), 0.36 JetBrains Mono —
 *                           for text that may land 1 px off (a picker row, a note); else svgText.
 *
 * NOT cosmo's estimateTextWidth (len × px × 0.6): Solaris measures (TextFit.h's header says why). The borrowed cosmo
 * controls keep cosmo's estimate — they are cosmo's widgets.
 */

let FONT_CACHE = null;
/** Theme.h font:: — read from the generated tokens. */
export const FONT = new Proxy({}, {
  get(_, k) {
    if (!FONT_CACHE) {
      const cs = getComputedStyle(document.documentElement);
      const f = (n, fb) => (cs.getPropertyValue("--font-" + n).trim().replace(/^"|"$/g, "") || fb);
      FONT_CACHE = { sans: f("sans", "Roboto"), medium: f("sans-medium", "Roboto Medium"), semibold: f("sans-semibold", "Roboto SemiBold"),
                     mono: f("mono", "JetBrains Mono"), monoMedium: f("mono-medium", "JetBrains Mono Medium") };
    }
    return FONT_CACHE[k];
  },
});

export function cssFont(px, fam = FONT.sans) { return `${px}px "${fam}"`; }

let ready = null;
export function fontsReady() {
  if (ready) return ready;
  const fams = [FONT.sans, FONT.medium, FONT.semibold, FONT.mono, FONT.monoMedium];
  ready = Promise.all(fams.map((f) => document.fonts.load(cssFont(12, f)).catch(() => null))).then(() => { cache.clear(); });
  return ready;
}

const ctx = document.createElement("canvas").getContext("2d");
const cache = new Map(); // emptied once the faces load: nothing measured with a fallback face survives
/** measureText: the advance, with `tracking` px after every glyph but the last (Cairo adapter's letter spacing). */
export function measure(s, px, fam = FONT.sans, tracking = 0) {
  s = String(s ?? "");
  if (!s) return 0;
  const key = px + "|" + fam + "|" + s;
  let w = cache.get(key);
  if (w === undefined) {
    ctx.font = cssFont(px, fam);
    w = ctx.measureText(s).width;
    if (cache.size > 4000) cache.clear();
    cache.set(key, w);
  }
  if (tracking) w += tracking * Math.max(0, [...s].length - 1);
  return w;
}

const ELL = "…";
export function ellipsize(s, maxW, px, fam = FONT.sans, tracking = 0) {
  s = String(s ?? "");
  if (maxW <= 0) return "";
  if (measure(s, px, fam, tracking) <= maxW) return s;
  if (measure(ELL, px, fam, tracking) > maxW) return "";
  const cps = [...s];
  let lo = 0, hi = cps.length - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (measure(cps.slice(0, mid).join("") + ELL, px, fam, tracking) <= maxW) lo = mid; else hi = mid - 1;
  }
  return cps.slice(0, lo).join("").replace(/ +$/, "") + ELL;
}

export function fitSize(s, maxW, maxPx, minPx, fam = FONT.sans, trackingPerPx = 0) {
  const w = measure(s, maxPx, fam, trackingPerPx * maxPx);
  if (w <= maxW || w <= 0) return maxPx;
  const px = maxPx * maxW / w;
  return px < minPx ? minPx : px;
}

export const baseline = (cy, px) => cy + px * 0.35;

export function ellipsizeFront(s, maxW, px, fam = FONT.sans, tracking = 0) {
  s = String(s ?? "");
  if (maxW <= 0) return "";
  if (measure(s, px, fam, tracking) <= maxW) return s;
  if (measure(ELL, px, fam, tracking) > maxW) return "";
  const cps = [...s];
  let lo = 0, hi = cps.length - 1; // how many code points of the tail
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (measure(ELL + cps.slice(cps.length - mid).join(""), px, fam, tracking) <= maxW) lo = mid; else hi = mid - 1;
  }
  return ELL + cps.slice(cps.length - lo).join("");
}

const K = { mono: 0.36, sans: 0.3418 };
export function textAt(el, x, base, px, fam = FONT.sans, tracking = 0) {
  const st = el.style;
  const mono = fam === FONT.mono || fam === FONT.monoMedium;
  st.position = "absolute";
  st.left = x + "px";
  st.top = base - px * (0.5 + (mono ? K.mono : K.sans)) + "px";
  st.fontSize = px + "px";
  st.lineHeight = px + "px";
  st.fontFamily = `"${fam}"`;
  st.whiteSpace = "pre";
  st.letterSpacing = tracking ? tracking + "px" : "";
  return el;
}

const SVGNS = "http://www.w3.org/2000/svg";
export function svgBox(w, h) {
  const s = document.createElementNS(SVGNS, "svg");
  s.setAttribute("width", Math.max(0, w)); s.setAttribute("height", Math.max(0, h));
  s.setAttribute("overflow", "visible");
  s.style.display = "block";
  return s;
}
export function svgText(str, x, base, px, fam = FONT.sans, tracking = 0) {
  const t = document.createElementNS(SVGNS, "text");
  t.setAttribute("x", x); t.setAttribute("y", base);
  t.setAttribute("font-size", px);
  t.setAttribute("font-family", `"${fam}"`);
  if (tracking) t.setAttribute("letter-spacing", tracking);
  t.style.whiteSpace = "pre";
  t.textContent = String(str ?? "");
  return t;
}
