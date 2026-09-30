/*
 * Cosmo by arstro — icons.js: the window's glyphs (widgets/Icons.cpp) as SVG.
 *
 * Every icon::* glyph, with its own path data: the native authors each in a 0..1 box (two in
 * the 24 / 32 viewBoxes of their source), draws it with butt caps and miter joins, and gives
 * the stroke in SCREEN pixels (it does not scale with the box) - hence non-scaling-stroke.
 * The quadratic "circles" are the native's own and are copied as they are, not as true arcs,
 * so a glyph here and a glyph in the window are the same shape.
 *
 *   icon("save", 13)             13x13 box, the glyph's default stroke
 *   icon("chevronRight", 9, 1.0) with an explicit stroke width
 */
const G = {
  chevronRight: { vb: 100, sw: 1.3, d: ["M32 18 L68 50 L32 82"] },
  chevronDown: { vb: 100, sw: 1.3, d: ["M18 36 L50 68 L82 36"] },
  panelLeft: { vb: 100, sw: 1.4, d: ["M20 8 L80 8 Q92 8 92 20 L92 80 Q92 92 80 92 L20 92 Q8 92 8 80 L8 20 Q8 8 20 8 Z", "M38 8 L38 92"] },
  save: { vb: 100, sw: 1.2, d: ["M15 10 L68 10 L85 27 L85 80 Q85 90 75 90 L25 90 Q15 90 15 80 Z", "M30 35 L70 35", "M32 55 L68 55 L68 78 L32 78 Z"] },
  upload: { vb: 100, sw: 1.2, d: ["M50 78 L50 20 M28 42 L50 20 L72 42", "M18 88 L82 88"] },
  download: { vb: 100, sw: 1.2, d: ["M50 20 L50 68 M28 46 L50 68 L72 46", "M18 88 L82 88"] },
  refreshCw: { vb: 100, sw: 1.2, d: ["M18 50 Q18 18 50 18 Q62.8 18 82 37.2", "M67.6 22.8 L82 37.2 L64.4 45.2",
                                     "M82 50 Q82 82 50 82 Q37.2 82 18 62.8", "M32.4 77.2 L18 62.8 L35.6 54.8"] },
  deleteBin: { vb: 24, sw: 1.1, d: ["M4 7 H20", "M9 5 C9 3.895 9.895 3 11 3 H13 C14.105 3 15 3.895 15 5 V7",
                                    "M6 10 V18 C6 19.657 7.343 21 9 21 H15 C16.657 21 18 19.657 18 18 V10", "M10 12 V17 M14 12 V17"] },
  rotateCcw: { vb: 100, sw: 1.2, d: ["M86 45.2 Q86 86 52 86 Q18 86 18 52 Q18 18 52 18", "M46.9 6.1 L52 18 L65.6 11.2"] },
  folder: { vb: 100, sw: 1.2, d: ["M8 82 L8 20 L42 20 L52 34 L92 34 L92 82 Z"] },
  image: { vb: 100, sw: 1.2, d: ["M12 16 L88 16 L88 84 L12 84 Z", "M42 36 Q42 28 34 28 Q26 28 26 36 Q26 44 34 44 Q42 44 42 36",
                                 "M14 78 L42 50 L62 70 L74 58 L87 72"] },
  close: { vb: 100, sw: 1.3, d: ["M22 22 L78 78 M78 22 L22 78"] },
  ban: { vb: 100, sw: 1.2, d: ["M88 50 Q88 88 50 88 Q12 88 12 50 Q12 12 50 12 Q88 12 88 50 Z", "M23.134 23.134 L76.866 76.866"] },
  check: { vb: 100, sw: 1.6, d: ["M18 52 L42 76 L84 26"] },
  checkCircle: { vb: 100, sw: 1.4, d: ["M92 50 Q92 92 50 92 Q8 92 8 50 Q8 8 50 8 Q92 8 92 50 Z", "M30.68 51.68 L45.8 67.64 L71 34.04"] },
  pipette: { vb: 32, sw: 1.2, fill: ["M17 8.6 C19.8 5.8 23.8 1.8 26.2 1.8 C29.2 4.8 29.2 7.2 27.7 8.7 L22.4 14 Z"],
             d: ["M14.51 7.67 L20.69 13.53", "M17.6 10.6 L5.6 25.2"] },
};

const NS = "http://www.w3.org/2000/svg";

export function icon(name, size = 12, stroke) {
  const g = G[name];
  if (!g) throw new Error("no icon " + name);
  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("viewBox", `0 0 ${g.vb} ${g.vb}`);
  svg.setAttribute("width", size);
  svg.setAttribute("height", size);
  svg.setAttribute("preserveAspectRatio", "none");
  svg.setAttribute("aria-hidden", "true");
  svg.classList.add("icon", "icon-" + name);
  for (const d of g.fill || []) {
    const p = document.createElementNS(NS, "path");
    p.setAttribute("d", d);
    p.setAttribute("fill", "currentColor");
    svg.append(p);
  }
  for (const d of g.d) {
    const p = document.createElementNS(NS, "path");
    p.setAttribute("d", d);
    p.setAttribute("fill", "none");
    p.setAttribute("stroke", "currentColor");
    p.setAttribute("stroke-width", stroke ?? g.sw);
    p.setAttribute("stroke-linecap", "butt");
    p.setAttribute("stroke-linejoin", "miter");
    p.setAttribute("vector-effect", "non-scaling-stroke");
    svg.append(p);
  }
  return svg;
}

export const ICONS = Object.keys(G);
