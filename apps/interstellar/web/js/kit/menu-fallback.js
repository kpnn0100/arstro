/*
 * Interstellar — kit/menu-fallback.js: a plain context menu with cosmo's ui/menu.js API, used ONLY
 * while that borrowed module is not installed (web/BORROW lists it once it exists). The shell picks
 * the real one first; nothing else imports this file.
 *
 *   openMenu(layer, items [{label, action}], x, y) -> {close()}
 *   openRename(layer, name, x, y, onRename, {title}) -> {close()}
 *
 * A popover list of 24 px rows at (x, y) in the layer's logical px, kept inside the layer; a row
 * click closes the menu, then runs its action; a click outside or Escape closes it. Eased in and out
 * (motion::kDropdownMs) - nothing appears in one frame.
 */
import { h } from "../core/dom.js";
import { useCss } from "./css.js";
import { width as textWidth, placeText, FAMILY } from "./textfit.js";

const ROW = 24, PADX = 12;

function shell(layer, x, y, w, hgt, onClose) {
  useCss("css/dialogs/picker.css");
  const el = h("div.ip-menu", { style: { width: w + "px", height: hgt + "px" } });
  const W = layer.clientWidth || 1e4, H = layer.clientHeight || 1e4;
  const px = Math.max(4, Math.min(x, W - w - 4)), py = Math.max(4, Math.min(y, H - hgt - 4));
  el.style.transform = `translate(${px}px, ${py}px)`;
  layer.append(el);
  requestAnimationFrame(() => requestAnimationFrame(() => el.classList.add("on")));
  let closed = false;
  const outside = (e) => { if (!el.contains(e.target)) api.close(); };
  const key = (e) => { if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); api.close(); } };
  setTimeout(() => { window.addEventListener("pointerdown", outside, true); window.addEventListener("keydown", key, true); }, 0);
  const api = {
    el,
    close() {
      if (closed) return;
      closed = true;
      window.removeEventListener("pointerdown", outside, true);
      window.removeEventListener("keydown", key, true);
      el.classList.remove("on");
      setTimeout(() => el.remove(), 180);
      if (onClose) onClose();
    },
  };
  return api;
}

export function openMenu(layer, items, x, y) {
  const w = Math.max(140, ...items.map((it) => textWidth(it.label, 11, FAMILY.sans) + 2 * PADX));
  const api = shell(layer, x, y, w, items.length * ROW + 8);
  items.forEach((it, i) => {
    const r = h("div.ip-menu-row" + (it.action ? "" : ".inert"), { style: { transform: `translateY(${4 + i * ROW}px)`, width: w + "px" } });
    r.append(placeText(h("div.ip-t", { text: it.label }), PADX, ROW / 2 + 4, 11, FAMILY.sans));
    if (it.action) r.addEventListener("click", (e) => { e.stopPropagation(); api.close(); it.action(); });
    api.el.append(r);
  });
  return api;
}

export function openRename(layer, name, x, y, onRename, { title = "Rename" } = {}) {
  const w = 220;
  const api = shell(layer, x, y, w, 58);
  api.el.append(placeText(h("div.ip-t.ip-muted", { text: title }), PADX, 18, 10, FAMILY.sans));
  const f = h("input.ip-field.light", { type: "text", spellcheck: "false", style: { transform: `translate(${PADX - 4}px, 26px)`, width: w - 2 * PADX + 8 + "px", height: "24px" } });
  f.value = name;
  f.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); const v = f.value; api.close(); onRename(v); }
  });
  api.el.append(f);
  requestAnimationFrame(() => { f.focus(); f.select(); });
  return api;
}
