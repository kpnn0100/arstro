/*
 * Solaris — mixer/law.js: the mixer's numbers, ported exactly (core/service/FaderLaw.h, MixerDock.cpp:33-48).
 *   faderPos(dB) / faderDb(pos)  the fader law: −∞ (−120 dB) at 0, 0 dB at 0.708, +6 dB at the top (40 dB a decade)
 *   meterLevel(peak)             a linear peak on the meter's −60 … +6 dBFS scale, 0 … 1
 *   num(v, d), dbText(db), rnd(v, step)   the command lines' and readouts' number formatting
 */
export const faderPos = (dB) => (dB <= -119.95 ? 0 : Math.min(1, Math.max(0, Math.pow(10, (dB - 6) / 40))));
export const faderDb = (pos) => (pos <= 0.001 ? -120 : Math.min(6, Math.max(-120, 6 + 40 * Math.log10(pos))));
export function meterLevel(peak) {
  if (!(peak > 1e-6)) return 0;
  return Math.min(1, Math.max(0, (20 * Math.log10(peak) + 60) / 66));
}
export function num(v, d) {
  let s = Number(v).toFixed(d);
  if (/^-0(\.0+)?$/.test(s)) s = s.slice(1);
  return s;
}
export const dbText = (db) => (db <= -119.95 ? "-inf" : (db > 0.049 ? "+" : "") + num(db, 1));
export const rnd = (v, step) => Math.round(v / step) * step;
