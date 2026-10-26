/*
 * Interstellar — util.js: the small pieces every view shares (Solaris's views/util.js, for
 * Interstellar): cosmo's h() with optional props, an eased copy of a value the page does not control
 * (nothing on screen changes in one frame - arstro.design.rule §1), and cosmo's line icons.
 */
import { h as domH } from "../core/dom.js";

/** cosmo's h() with its props made optional: h("div.x", child, …) or h("div.x", {props}, child, …). */
export function h(spec, ...rest) {
  const first = rest[0];
  const isProps = first !== null && typeof first === "object" && !(first instanceof Node) && !Array.isArray(first);
  return isProps ? domH(spec, first, ...rest.slice(1)) : domH(spec, null, ...rest);
}

const reduced = () => matchMedia("(prefers-reduced-motion: reduce)").matches;

/** An eased copy: `set(target)` glides `apply(value)` there over `ms` (EaseOutCubic); `jump(v)` is
 *  direct manipulation (the pointer IS the animation). The first `set` lands where it is told. */
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
  pause: ["M5 3.5v9", "M11 3.5v9"],
  prevCut: ["M4 4v8", "M12 4.5v7L6.5 8z"],
  nextCut: ["M12 4v8", "M4 4.5v7L9.5 8z"],
  stepBack: ["M10.5 4.5 7 8l3.5 3.5"],
  stepFwd: ["M5.5 4.5 9 8l-3.5 3.5"],
  undo: ["M5.5 4 3 6.5 5.5 9", "M3 6.5h6.5a3.5 3.5 0 0 1 0 7H7"],
  redo: ["M10.5 4 13 6.5 10.5 9", "M13 6.5H6.5a3.5 3.5 0 0 0 0 7H9"],
  save: ["M3.5 3.5h7l2 2v7h-9z", "M5.5 3.5v3h4v-3", "M5.5 12.5v-3h5v3"],
  close: ["M4.5 4.5l7 7", "M11.5 4.5l-7 7"],
  split: ["M8 2.5v11", "M3 5.5h3v5H3", "M13 5.5h-3v5h3"],
  trash: ["M3.5 4.5h9", "M6 4.5V3h4v1.5", "M4.5 4.5l.7 8h5.6l.7-8"],
  plus: ["M8 3.5v9", "M3.5 8h9"],
  marker: ["M5 3h6v6l-3 3-3-3z"],
};
