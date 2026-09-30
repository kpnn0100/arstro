/*
 * Cosmo by arstro — controls.js: the slider catalogue's conversions, never re-derived (R-NTWB-4).
 *
 * `controls` (the adapter method) publishes EditControls.h with each track -> engine
 * conversion SAMPLED by the C++ functions the window uses (EV/80, mired temperature, tint,
 * radius). The page interpolates between those samples - both ways - and evaluates no
 * formula of its own, so a slider here and a slider in the window turn the same track
 * position into the same engine value.
 */

/** Track position -> engine value. */
export function toEngine(c, t) {
  const s = c.stops;
  if (!s || s.length < 2) return t;
  if (t <= s[0][0]) return s[0][1];
  for (let i = 1; i < s.length; i++) {
    if (t <= s[i][0]) {
      const [t0, e0] = s[i - 1], [t1, e1] = s[i];
      return e0 + ((e1 - e0) * (t - t0)) / (t1 - t0 || 1);
    }
  }
  return s[s.length - 1][1];
}

/** Engine value -> track position (the samples are monotonic in either direction). */
export function toTrack(c, e) {
  const s = c.stops;
  if (!s || s.length < 2) return e;
  const up = s[s.length - 1][1] >= s[0][1];
  const lo = up ? s[0] : s[s.length - 1], hi = up ? s[s.length - 1] : s[0];
  if (e <= lo[1]) return lo[0];
  if (e >= hi[1]) return hi[0];
  for (let i = 1; i < s.length; i++) {
    const [t0, e0] = s[i - 1], [t1, e1] = s[i];
    if ((e >= Math.min(e0, e1)) && (e <= Math.max(e0, e1))) return t0 + ((t1 - t0) * (e - e0)) / (e1 - e0 || 1);
  }
  return c.min;
}

/** The track position a control rests at when the engine value is its neutral one. */
export function neutralTrack(c) { return c.min < 0 ? 0 : c.min; }
