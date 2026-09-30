/*
 * Cosmo by arstro — kit.js: the pieces every dialog card is built from, as the native paints
 * them inside its modals (ConfirmDialog / SettingsDialog / PresetDialog / InfoDialog /
 * ExportDialog .cpp):
 *
 *   T(parent, text, x, baseline, px, fam)   a text run on a native baseline (paint.text)
 *   box(parent, cls, x, y, w, h, r)         a drawRoundedRect: an SVG rect, fill/stroke from CSS
 *                                           classes, the stroke straddling the edge like Cairo
 *   button(parent, {x, y, w, h, label, kind, px, onClick})
 *                                           a dialog button: fill (primary / destructive / plain =
 *                                           secondary + border), a hover wash over it
 *                                           (palette::hoverWash, 120 ms), the label centred by est()
 *   checkbox(parent, x, y, size, state)     the 3-state box of ExportDialog / PresetDialog
 *   frame(card)                             the card's own 1 px outline
 */
import { h } from "../core/dom.js";
import { text, svg, est, at } from "./paint.js";

export function T(parent, str, x, base, px, fam = "sans", cls = "") {
  const el = text(h("div.dg-t" + (cls ? "." + cls.split(" ").join(".") : ""), { text: str }), x, base, px, fam);
  if (parent) parent.append(el);
  return el;
}

export function box(parent, cls, x, y, w, hh, r = 2) {
  const s = svg("svg", { class: "dg-box " + cls, width: Math.max(0, w), height: Math.max(0, hh) });
  s.append(svg("rect", { x: 0, y: 0, width: "100%", height: "100%", rx: r, ry: r }));
  at(s, x, y);
  if (parent) parent.append(s);
  return s;
}
export function sizeBox(s, x, y, w, hh) {
  s.setAttribute("width", Math.max(0, w)); s.setAttribute("height", Math.max(0, hh));
  at(s, x, y);
}

export function frame(card) {
  const f = svg("svg", { class: "dg-frame" });
  f.append(svg("rect", { x: 0, y: 0, width: "100%", height: "100%", rx: 2, ry: 2 }));
  card.append(f);
  return f;
}

/** kind: "primary" | "destructive" | "plain"; fam defaults to Roboto Medium (sansMedium). */
export function button(parent, { x, y, w, h: hh, label, kind = "plain", px = 12, fam = "medium", base, onClick, cls = "" }) {
  const b = h("button.dg-btn.dg-" + kind + (cls ? "." + cls : ""), { "aria-label": label,
    style: { left: x + "px", top: y + "px", width: w + "px", height: hh + "px" } });
  box(b, "dg-btn-fill", 0, 0, w, hh);
  box(b, "dg-wash", 0, 0, w, hh);
  const lbl = T(b, label, (w - est(label, px)) / 2, base ?? hh / 2 + (px === 12 ? 4 : px * 0.35), px, fam, "dg-btn-label");
  b.label = lbl;
  if (onClick) b.addEventListener("click", (e) => { e.stopPropagation(); if (!b.classList.contains("disabled")) onClick(e); });
  if (parent) parent.append(b);
  return b;
}
export function setLabel(b, label, px = 12) {
  b.label.textContent = label;
  b.label.style.left = parseFloat(b.style.width) / 2 - est(label, px) / 2 + "px";
}

/** state 0 = empty, 1 = ticked, 2 = indeterminate (ExportDialog.cpp checkbox / PresetDialog). */
export function checkbox(parent, x, y, size, r = 3) {
  const el = svg("svg", { class: "dg-check", width: size, height: size });
  const rect = svg("rect", { x: 0, y: 0, width: size, height: size, rx: r, ry: r, class: "dg-check-box" });
  const tick = svg("polyline", { class: "dg-check-tick", fill: "none", "stroke-width": 1.8,
    points: `3.5,${0.55 * size} ${0.42 * size},${size - 4} ${size - 3},4` });
  const dash = svg("path", { class: "dg-check-dash", fill: "none", "stroke-width": 1.8, d: `M3.5 ${size / 2} L${size - 3.5} ${size / 2}` });
  el.append(rect, tick, dash);
  at(el, x, y);
  el.set = (state) => { el.dataset.state = String(state); };
  el.set(0);
  if (parent) parent.append(el);
  return el;
}
