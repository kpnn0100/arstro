/*
 * Interstellar — kit/tokens.js: Theme.h's numbers and colours, read from the GENERATED tokens.css.
 *
 * A widget never types a value Theme.h has: CSS reads var(--…) directly; JS that lays out or draws
 * (a canvas, an SVG, an arithmetic port of a native layout()) reads it here. Every token
 * interstellar_web_tokens prints is available (cli/webTokens.cpp has the naming rule):
 *
 *   tok.px("shell-left-w")        -> 234          (a px / ms / plain number token, as a number)
 *   tok.ms("motion-cross-fade")   -> 200
 *   tok.color("primary")          -> [r, g, b, a] in 0..1 (cosmo's ui/controls.js parseColor shape)
 *   tok.css("primary")            -> "var(--primary)" text, for an SVG fill / a style
 *   tok.rgba("white", 0.14)       -> "rgba(255, 255, 255, 0.14)" - the parametric tokens
 *                                    (palette::whiteAlpha / primaryAlpha / successAlpha, surface::scrim)
 *   SHELL                         the shell:: metrics as numbers, and deckH(H) - shell::deckH ported
 *
 * Read lazily (first use), once: tokens.css is a <link> in index.html, applied before any module
 * runs. Reduced motion zeroes no token here (motion.js / base.css handle it).
 */
let CS = null;
const cs = () => CS || (CS = getComputedStyle(document.documentElement));
const raw = (name) => cs().getPropertyValue("--" + name).trim();

function parseColor(s) {
  s = (s || "").trim();
  if (s[0] === "#") {
    const n = parseInt(s.slice(1, 7), 16);
    return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255, 1];
  }
  const m = s.match(/-?[\d.]+/g);
  if (!m || m.length < 3) return [0, 0, 0, 1];
  return [m[0] / 255, m[1] / 255, m[2] / 255, m[3] === undefined ? 1 : +m[3]];
}

export const tok = {
  raw,
  px: (name) => parseFloat(raw(name)) || 0,
  ms: (name) => parseFloat(raw(name)) || 0,
  num: (name) => parseFloat(raw(name)) || 0,
  color: (name) => parseColor(raw(name)),
  css: (name) => `var(--${name})`,
  rgba: (name, a) => `rgba(${raw(name + "-rgb")}, ${a})`,
};

/** shell:: (Theme.h:104-133), as numbers. */
export const SHELL = {
  get topBarH() { return tok.px("shell-top-bar-h"); },
  get rightW() { return tok.px("shell-right-w"); },
  get leftW() { return tok.px("shell-left-w"); },
  get transportH() { return tok.px("shell-transport-h"); },
  get headerWidth() { return tok.px("shell-header-w"); },
  get rulerH() { return tok.px("shell-ruler-h"); },
  get trackH() { return tok.px("shell-track-h"); },
  get wheelNotchPx() { return tok.px("shell-wheel-notch"); },
  get minMonitorW() { return tok.px("shell-min-monitor-w"); },
  get minMonitorH() { return tok.px("shell-min-monitor-h"); },
  /** shell::deckH(windowH): ONE deck height for every tab, so the monitor never moves (R-UI-3). */
  deckH(windowH) {
    const lo = tok.px("shell-deck-min"), hi = tok.px("shell-deck-max");
    return Math.min(hi, Math.max(lo, windowH * tok.num("shell-deck-ratio")));
  },
};
