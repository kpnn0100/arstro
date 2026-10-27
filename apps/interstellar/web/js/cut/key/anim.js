/*
 * Interstellar — cut/key/anim.js: keyframe curves, the SAME function the service renders with.
 *
 * A port of apps/interstellar/model/Anim.h (namespace interstellar::anim) - the parts the key graph
 * draws and edits with: After Effects' model, each key an incoming and an outgoing side (LINEAR,
 * BEZIER or HOLD); a bezier side a SPEED (value units / s) and an INFLUENCE (0.1-100 % of the
 * segment). Between key A and key B the curve is the cubic Bezier in (time, value) - Anim.h:9-20:
 *
 *     P0 = (tA, vA)                         P1 = (tA + infOutA·Δt, vA + speedOutA·infOutA·Δt)
 *     P2 = (tB - infInB·Δt, vB - speedInB·infInB·Δt)        P3 = (tB, vB)
 *
 * A linear side is a bezier side with the segment's own slope and influence 1/3; two linear sides
 * lerp; a hold on A's out keeps vA until B. Before the first key the first value, after the last the
 * last. The value at t is found by bisection on the curve parameter (48 steps, Anim.h:75-81).
 *
 *   fromModel(k)                 KeyGraph.cpp:26-38 - a model key (AnimModel.keys[i]) as an anim Key
 *   eval(keys, t)                Anim.h:85-92 - keys sorted by t, not empty (0 when empty)
 *   segment(a, b, t)             Anim.h:59-82
 *   clampInfl(pct)               Anim.h:56 - percent -> 0.001 … 1
 *   sideName(side)               Anim.h:38
 *   fmtG(v, p) / fmtF(v, p)      printf("%.<p>g") / printf("%.<p>f") - the graph's words (KeyGraph.cpp:586-600)
 * A side is the model's word: "linear" | "bezier" | "hold" (parseSide keeps an unknown word linear,
 * as fromModel's parseSide leaves the default untouched).
 */

const SIDES = new Set(["linear", "bezier", "hold"]);

/** KeyGraph.cpp fromModel (:26-38): the model's key, its sides parsed (an unknown word keeps linear). */
export function fromModel(k) {
  return {
    t: +k.t || 0, v: +k.v || 0,
    in: SIDES.has(k.in) ? k.in : "linear", out: SIDES.has(k.out) ? k.out : "linear",
    speedIn: +k.speedIn || 0, speedOut: +k.speedOut || 0,
    inflIn: k.inflIn === undefined ? 33.333 : +k.inflIn, inflOut: k.inflOut === undefined ? 33.333 : +k.inflOut,
  };
}
export const cloneKey = (k) => ({ ...k });

export const sideName = (s) => (s === "bezier" ? "bezier" : s === "hold" ? "hold" : "linear");
export const clampInfl = (pct) => Math.min(100, Math.max(0.1, pct)) / 100;

const bez = (p0, p1, p2, p3, u) => { const m = 1 - u; return m * m * m * p0 + 3 * m * m * u * p1 + 3 * m * u * u * p2 + u * u * u * p3; };

/** Anim.h:59-82 - the value of the segment a -> b at t (a.t <= t <= b.t). */
export function segment(a, b, t) {
  const dt = b.t - a.t;
  if (dt <= 0) return b.v;
  if (a.out === "hold") return a.v;
  const u0 = (t - a.t) / dt;
  if (a.out === "linear" && b.in === "linear") return a.v + (b.v - a.v) * u0;
  const slope = (b.v - a.v) / dt;
  const so = a.out === "bezier" ? a.speedOut : slope, io = a.out === "bezier" ? clampInfl(a.inflOut) : 1 / 3;
  const si = b.in === "bezier" ? b.speedIn : slope, ii = b.in === "bezier" ? clampInfl(b.inflIn) : 1 / 3;
  const x1 = io, x2 = 1 - ii;
  const y0 = a.v, y1 = a.v + so * io * dt, y2 = b.v - si * ii * dt, y3 = b.v;
  let lo = 0, hi = 1;
  for (let i = 0; i < 48; i++) {
    const mid = 0.5 * (lo + hi);
    if (bez(0, x1, x2, 1, mid) < u0) lo = mid; else hi = mid;
  }
  return bez(y0, y1, y2, y3, 0.5 * (lo + hi));
}

/** Anim.h:85-92 - the curve's value at t (std::upper_bound: the first key with t < k.t). */
export function evalCurve(keys, t) {
  if (!keys.length) return 0;
  if (t <= keys[0].t) return keys[0].v;
  if (t >= keys[keys.length - 1].t) return keys[keys.length - 1].v;
  let hi = 0;
  while (hi < keys.length && !(t < keys[hi].t)) hi++;
  return segment(keys[hi - 1], keys[hi], t);
}

/** printf("%.<p>g", v) (glibc: trailing zeros dropped, exponent at least two digits). */
export function fmtG(v, p) {
  v = Number(v);
  if (!Number.isFinite(v)) return Number.isNaN(v) ? "nan" : v > 0 ? "inf" : "-inf";
  if (p <= 0) p = 1;
  if (v === 0) return Object.is(v, -0) ? "-0" : "0";
  const exp = Number(v.toExponential(p - 1).split("e")[1]);
  if (exp < -4 || exp >= p) {
    let [mant, e] = v.toExponential(p - 1).split("e");
    if (mant.includes(".")) mant = mant.replace(/0+$/, "").replace(/\.$/, "");
    const n = Number(e);
    return mant + "e" + (n < 0 ? "-" : "+") + String(Math.abs(n)).padStart(2, "0");
  }
  let s = v.toFixed(Math.max(0, p - 1 - exp));
  if (s.includes(".")) s = s.replace(/0+$/, "").replace(/\.$/, "");
  return s;
}
/** printf("%.<p>f", v) - toFixed rounds and signs as glibc does ("-0.000" for a tiny negative). */
export const fmtF = (v, p) => Number(v).toFixed(p);
