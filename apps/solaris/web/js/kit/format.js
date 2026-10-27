/*
 * Solaris — kit/format.js: how the native spells what it sends and shows, once for every widget.
 *
 *   q(s)              App::quote / ProjectScreen's q (App.cpp:29-32): a command argument as ONE token — as it is
 *                     when it has no space, tab or quote and is not empty, else wrapped in double quotes. Nothing
 *                     is escaped (the native does not; a `"` inside stays a `"` — the grammar's problem, not ours).
 *   beatText(b)       Timeline::beatText (Timeline.cpp:44-55): a beat rounded to the tick (960 PPQ), in the fewest
 *                     decimals that keep that tick. "0" for 0.
 *   fixed(v, d)       printf("%.*f") with "-0" made "0" (ParamFormat.h number()).
 *   g(v, prec)        printf("%.<prec>g") — the native's `%g` (storeText's %.6g, Quantize's %.10g).
 *   paramfmt          ParamFormat.h, the one parameter readout and taper: formatValue(p, v), storeText(v, integer),
 *                     logTaper(p), toPos(p, v), fromPos(p, s), toUnit(p, v), fromUnit(p, u) — `p` is a ParamModel
 *                     as the model JSON has it ({unit, min, max, integer, logScale, …}).
 */

export const PPQ = 960; // Timeline.cpp kPpq: a beat's ticks

/** App::quote — one token of the command grammar. */
export function q(s) {
  s = String(s ?? "");
  return s !== "" && !/[ \t"]/.test(s) ? s : `"${s}"`;
}

/** Timeline::beatText. */
export function beatText(beat) {
  const tick = Math.round(beat * PPQ);
  if (tick === 0) return "0";
  let out = "";
  for (let d = 0; d <= 6; ++d) { // six decimals always suffice: 1e-6 · 960 < ½
    out = cFixed(tick / PPQ, d);
    if (Math.round(parseFloat(out) * PPQ) === tick) break;
  }
  return out;
}

/** C's %.*f: glibc rounds the EXACT binary value, an exact tie to even; JS's toFixed rounds a tie up. */
function cFixed(v, d) {
  const s = v.toFixed(d);
  if (!Number.isFinite(v) || Math.abs(v) >= 1e21) return s;
  const long = Math.abs(v).toFixed(Math.min(100, d + 40));
  const tail = long.slice(long.length - 40);
  if (!/^50*$/.test(tail)) return s; // not an exact tie: toFixed already rounded to nearest
  const down = (Math.abs(v) - 5 * Math.pow(10, -(d + 1)));
  const lo = down.toFixed(d), last = parseInt(lo.replace(/\D/g, "").slice(-1), 10);
  const even = last % 2 === 0 ? lo : Math.abs(parseFloat(s)).toFixed(d);
  return (v < 0 ? "-" : "") + even;
}

/** ParamFormat.h number(): %.*f, and never "-0". */
export function fixed(v, d) {
  let s = cFixed(v, d);
  if (s === "-0" || s === "-0.0" || s === "-0.00") s = s.slice(1);
  return s;
}

/** printf %.<prec>g: `prec` significant digits, trailing zeros dropped, exponent form outside 1e-4 … 1e<prec>. */
export function g(v, prec = 6) {
  if (!Number.isFinite(v)) return Number.isNaN(v) ? "nan" : v < 0 ? "-inf" : "inf";
  if (v === 0) return Object.is(v, -0) ? "-0" : "0";
  const p = Math.max(1, prec);
  const exp = Math.floor(Math.log10(Math.abs(Number(v.toPrecision(p)))));
  if (exp < -4 || exp >= p) {
    let [m, e] = v.toExponential(p - 1).split("e");
    if (m.includes(".")) m = m.replace(/0+$/, "").replace(/\.$/, "");
    const n = parseInt(e, 10);
    return m + "e" + (n < 0 ? "-" : "+") + String(Math.abs(n)).padStart(2, "0");
  }
  let s = v.toFixed(Math.max(0, p - 1 - exp));
  if (s.includes(".")) s = s.replace(/0+$/, "").replace(/\.$/, "");
  return s;
}

/** ParamFormat.h, the parameter readout and taper. */
export const paramfmt = {
  number: fixed,
  formatValue(p, v) {
    const a = Math.abs(v), unit = p.unit || "";
    if (unit === "Hz") return a >= 1000 ? fixed(v / 1000, a >= 10000 ? 1 : 2) + " kHz" : fixed(v, a >= 100 ? 0 : 1) + " Hz";
    if (unit === "ms") return fixed(v, a >= 100 ? 0 : 1) + " ms";
    if (unit === "dB") return fixed(v, 1) + " dB";
    if (unit === "st" || unit === "ct" || unit === "oct") return (v > 0 ? "+" : "") + fixed(v, p.integer ? 0 : 1) + " " + unit;
    if (unit === "" && !p.integer && p.min >= 0 && p.max <= 1) return fixed(v * 100, 0) + " %";
    return fixed(v, p.integer ? 0 : 2) + (unit === "" ? "" : " " + unit);
  },
  /** The text a `set` stores: enough digits that a drag lands where it was let go. */
  storeText(v, integer) { return integer ? fixed(Math.round(v), 0) : g(v, 6); },
  logTaper(p) { return !!p.logScale && p.min > 0 && p.max > p.min; },
  toPos(p, v) { return paramfmt.logTaper(p) ? Math.log(Math.max(v, p.min) / p.min) / Math.log(p.max / p.min) : v; },
  fromPos(p, s) {
    let v = paramfmt.logTaper(p) ? p.min * Math.pow(p.max / p.min, Math.min(1, Math.max(0, s))) : s;
    if (p.integer) v = Math.round(v);
    return Math.min(p.max, Math.max(p.min, v));
  },
  toUnit(p, v) {
    if (paramfmt.logTaper(p)) return Math.min(1, Math.max(0, paramfmt.toPos(p, v)));
    return p.max > p.min ? Math.min(1, Math.max(0, (v - p.min) / (p.max - p.min))) : 0;
  },
  fromUnit(p, u) {
    u = Math.min(1, Math.max(0, u));
    return paramfmt.logTaper(p) ? paramfmt.fromPos(p, u) : paramfmt.fromPos(p, p.min + u * (p.max - p.min));
  },
};
