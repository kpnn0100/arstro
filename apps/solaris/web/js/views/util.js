/*
 * Solaris — util.js: the small pieces every view shares.
 *
 * The fader law and the meter law are the WINDOW's (app/widgets/MixerDock.cpp:89-90, :37), cited
 * rather than re-invented, so a fader at the same height is the same gain on both faces. `ease()` is
 * the page's eased copy of a value it does not control (a fader moved by an undo, the playhead
 * catching up): nothing on screen changes in one frame (arstro.design.rule §1).
 */
import { h as domH } from "../core/dom.js";

/** cosmo's h() with its props made optional: h("div.x", child, …) or h("div.x", {props}, child, …). */
export function h(spec, ...rest) {
  const first = rest[0];
  const isProps = first !== null && typeof first === "object" && !(first instanceof Node) && !Array.isArray(first);
  return isProps ? domH(spec, first, ...rest.slice(1)) : domH(spec, null, ...rest);
}

/** dB -> fader position 0…1 (MixerDock::faderPos: 10^((dB−6)/40), −120 dB = the bottom). */
export function faderPos(db) { return db <= -119.95 ? 0 : Math.min(1, Math.max(0, Math.pow(10, (db - 6) / 40))); }
/** fader position -> dB (MixerDock::faderDb). */
export function faderDb(pos) { return pos <= 0.001 ? -120 : Math.min(6, Math.max(-120, 6 + 40 * Math.log10(pos))); }
/** A linear peak -> meter height 0…1 over −60 … +6 dBFS (MixerDock meterLevel). */
export function meterLevel(peak) { return peak <= 1e-6 ? 0 : Math.min(1, Math.max(0, (20 * Math.log10(peak) + 60) / 66)); }
/** The window's number spelling (MixerDock `num`): fixed decimals, never "-0.0". */
export function num(v, d) { const s = Number(v).toFixed(d); return /^-0(\.0+)?$/.test(s) ? s.slice(1) : s; }
export function dbText(db) { return db <= -119.95 ? "-inf" : (db > 0.049 ? "+" : "") + num(db, 1); }

/** bar.beat.sixteenth of a position in beats (1-based, as the window's ruler counts). */
export function barBeat(pos, beatsPerBar) {
  const p = Math.max(0, pos || 0);
  const bar = Math.floor(p / beatsPerBar) + 1;
  const beat = Math.floor(p % beatsPerBar) + 1;
  const six = Math.floor((p % 1) * 4) + 1;
  return `${bar}.${beat}.${six}`;
}

const reduced = () => matchMedia("(prefers-reduced-motion: reduce)").matches;

/** An eased copy: `set(target)` glides `apply(value)` there over `ms` (EaseOutCubic); `jump(v)`
 *  is direct manipulation (the pointer IS the animation). The FIRST `set` lands where it is told:
 *  a control appearing starts at the model's value (it fades in), it does not slide from zero. */
export function ease(apply, ms = 220, start = 0) {
  let value = start, from = start, to = start, t0 = 0, raf = 0, fresh = true;
  const step = (now) => {
    const k = Math.min(1, (now - t0) / ms);
    const e = 1 - Math.pow(1 - k, 3);
    value = from + (to - from) * e;
    apply(value);
    raf = k < 1 ? requestAnimationFrame(step) : 0;
  };
  return {
    get value() { return value; },
    set(target) {
      if (fresh) { fresh = false; this.jump(target); return; }
      if (target === to && raf) return;
      if (target === value && !raf) return;
      if (reduced()) { this.jump(target); return; }
      from = value; to = target; t0 = performance.now();
      if (!raf) raf = requestAnimationFrame(step);
    },
    jump(v) { if (raf) cancelAnimationFrame(raf); raf = 0; value = from = to = v; apply(v); },
  };
}

/** A 16-px stroke icon from a path list (cosmo's line icons: 1.5 px, round). */
export function icon(paths, cls = "") {
  const NS = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("viewBox", "0 0 16 16");
  svg.setAttribute("class", "icon " + cls);
  for (const d of paths) {
    const p = document.createElementNS(NS, "path");
    p.setAttribute("d", d);
    svg.append(p);
  }
  return svg;
}
export const ICONS = {
  play: ["M5 3.5v9l7-4.5z"],
  stop: ["M4.5 4.5h7v7h-7z"],
  rewind: ["M4 4v8", "M12 4.5v7L6.5 8z"],
  undo: ["M5.5 4 3 6.5 5.5 9", "M3 6.5h6.5a3.5 3.5 0 0 1 0 7H7"],
  redo: ["M10.5 4 13 6.5 10.5 9", "M13 6.5H6.5a3.5 3.5 0 0 0 0 7H9"],
  save: ["M3.5 3.5h7l2 2v7h-9z", "M5.5 3.5v3h4v-3", "M5.5 12.5v-3h5v3"],
  close: ["M4.5 4.5l7 7", "M11.5 4.5l-7 7"],
};
