/*
 * Interstellar — kit/textfit.js: measured text, ellipsized against the space actually left (R5).
 *
 * A port of app/widgets/TextFit.h (namespace textfit). Interstellar measures with REAL metrics -
 * IRenderTarget::measureText, which Cairo answers from the face - never cosmo's byte estimate
 * (len × px × 0.6, "what once detached cosmo's wordmark dot"). Here the face is the same file
 * (base.css registers the TTFs `ntwb install` ships under the native's family names) measured by a
 * 2D canvas with kerning OFF, as Cairo's show_text draws it (CairoTarget.cpp:242-275):
 *
 *   width(s, px, family, tracking=0)        IRenderTarget::measureText; tracked text = the sum of each
 *                                           code point's advance + tracking (CairoTarget's pen)
 *   ellipsize(s, maxW, px, family, tr=0)    `s`, or the longest prefix + "…" that fits; "" when even "…"
 *                                           does not fit; trailing spaces before "…" trimmed (TextFit.h:43)
 *   fitSize(s, maxW, maxPx, minPx, family, trackingPerPx=0)   the largest size ≤ maxPx (≥ minPx) that fits
 *   baseline(centerY, px)                   centerY + px × 0.35 - text centred on a line (TextFit.h:76)
 *   FAMILY.sans / medium / semibold / mono / monoMedium      the families (font:: tokens)
 *   fontEpoch                               a signal that rises when a face finished loading: measure
 *                                           again (a width taken before the face loaded is the fallback's)
 *   svgText(x, baseline, s, px, family)     an SVG <text> at its ALPHABETIC BASELINE, as drawText places it - USE
 *                                           THIS (or ui/controls.js T()) for a native-matched label: Chrome snaps an
 *                                           HTML box's top, so an HTML run at a fractional baseline lands 1 px off,
 *                                           while SVG text rounds like Cairo (PORTING §4's baseline lesson)
 *   placeText(el, x, baseline, px, family)  a DOM text run placed so its baseline is at `baseline` (line-height 1,
 *                                           the baseline BASELINE_EM[family] × px below the box top - from the TTFs:
 *                                           Roboto hhea 0.9277/0.2441 -> 0.8418, JetBrains Mono typo 1.02/0.30 ->
 *                                           0.86); by transform, so not snapped - for web-only surfaces (the picker)
 *
 * Measure on demand (layout code), not per frame for unchanged text: results are cached per
 * (font, text) - widths are pure functions of them.
 */
import { signal } from "../core/signal.js";

export const FAMILY = { sans: "Roboto", medium: "Roboto Medium", semibold: "Roboto SemiBold", mono: "JetBrains Mono", monoMedium: "JetBrains Mono Medium" };
const ELL = "…";

export const fontEpoch = signal(0);
let ctx2d = null;
const cache = new Map();
function g() {
  if (!ctx2d) {
    ctx2d = document.createElement("canvas").getContext("2d");
    if ("fontKerning" in ctx2d) ctx2d.fontKerning = "none";
  }
  return ctx2d;
}
if (typeof document !== "undefined" && document.fonts) {
  document.fonts.addEventListener("loadingdone", () => { cache.clear(); fontEpoch.value = fontEpoch.peek() + 1; });
}

function advance(s, px, family) {
  const font = `${px}px "${family}"`;
  const key = font + "\u0000" + s;
  let w = cache.get(key);
  if (w === undefined) {
    const c = g();
    c.font = font;
    w = c.measureText(s).width;
    if (cache.size > 4000) cache.clear();
    cache.set(key, w);
  }
  return w;
}

export function width(s, px, family = FAMILY.sans, tracking = 0) {
  s = String(s ?? "");
  if (!tracking) return advance(s, px, family);
  let w = 0;
  for (const cp of s) w += advance(cp, px, family) + tracking;
  return w;
}

export function ellipsize(s, maxW, px, family = FAMILY.sans, tracking = 0) {
  s = String(s ?? "");
  if (maxW <= 0) return "";
  if (width(s, px, family, tracking) <= maxW) return s;
  if (width(ELL, px, family, tracking) > maxW) return "";
  const cps = Array.from(s);
  let lo = 0, hi = cps.length - 1;   // the number of code points kept; 0 always fits
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (width(cps.slice(0, mid).join("") + ELL, px, family, tracking) <= maxW) lo = mid;
    else hi = mid - 1;
  }
  return cps.slice(0, lo).join("").replace(/ +$/, "") + ELL;
}

export function fitSize(s, maxW, maxPx, minPx, family = FAMILY.sans, trackingPerPx = 0) {
  const w = width(s, maxPx, family, trackingPerPx * maxPx);
  if (w <= maxW || w <= 0) return maxPx;
  const px = (maxPx * maxW) / w;
  return px < minPx ? minPx : px;
}

export const baseline = (centerY, px) => centerY + px * 0.35;

export function svgText(x, base, s, px, family = FAMILY.sans, fill = "var(--foreground)") {
  const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
  t.setAttribute("x", x);
  t.setAttribute("y", base);
  t.setAttribute("font-size", px);
  t.setAttribute("font-family", `"${family}"`);
  t.style.fill = fill;
  t.style.whiteSpace = "pre";
  t.textContent = String(s ?? "");
  return t;
}

export const BASELINE_EM = { [FAMILY.sans]: 0.8418, [FAMILY.medium]: 0.8418, [FAMILY.semibold]: 0.8418, [FAMILY.mono]: 0.86, [FAMILY.monoMedium]: 0.86 };
export function placeText(el, x, base, px, family = FAMILY.sans) {
  Object.assign(el.style, { position: "absolute", left: "0", top: "0", whiteSpace: "pre", lineHeight: px + "px", fontSize: px + "px",
                            fontFamily: `"${family}"`, transform: `translate(${x}px, ${base - (BASELINE_EM[family] ?? 0.8418) * px}px)` });
  return el;
}
