/*
 * Interstellar — kit/confirm-fallback.js: a plain ConfirmDialog with cosmo's ui/confirm.js API, used
 * ONLY while that borrowed module is not installed. The shell picks the real one first.
 *
 *   confirmDialog(layer, {title, message, buttons: [{label, primary, destructive, onClick}]}) -> {close()}
 *
 * The shared modal skeleton (ui/modal.js) with a title, one line of message and the buttons right-
 * aligned; a button closes the dialog, then runs its onClick (design rule §5); Escape / outside = cancel.
 */
import { h } from "../core/dom.js";
import { openModal } from "../ui/modal.js";
import { useCss } from "./css.js";
import { placeText, width as textWidth, FAMILY } from "./textfit.js";

export function confirmDialog(layer, { title = "", message = "", buttons = [], onCancel } = {}) {
  useCss("css/dialogs.css");
  useCss("css/dialogs/picker.css");
  const PAD = 20, BTN_H = 28;
  const W = Math.max(360, textWidth(message, 11) + 2 * PAD), H = 132;
  let done = false;
  const md = openModal(layer, { width: W, height: H, cls: "ip-confirm", onOutside: () => cancel(), onKey: (e) => {
    if (e.key === "Escape") { cancel(); return true; }
    if (e.key === "Enter") { const i = buttons.findIndex((b) => b.primary); if (i >= 0) press(i); return true; }
    return false;
  } });
  const c = md.card;
  c.append(placeText(h("div.ip-t.ip-fg", { text: title }), PAD, PAD + 16, 14, FAMILY.semibold));
  c.append(placeText(h("div.ip-t.ip-muted", { text: message }), PAD, PAD + 42, 11, FAMILY.sans));
  let x = W - PAD;
  for (let i = buttons.length - 1; i >= 0; i--) {
    const b = buttons[i];
    const w = textWidth(b.label, 12, FAMILY.medium) + 28;
    x -= w;
    const el = h("button.ip-btn.ip-" + (b.primary ? "primary" : b.destructive ? "destructive" : "plain"),
      { style: { transform: `translate(${x}px, ${H - PAD - BTN_H}px)`, width: w + "px", height: BTN_H + "px" } });
    el.append(placeText(h("div.ip-t.ip-btn-label", { text: b.label }), 14, BTN_H / 2 + 4, 12, FAMILY.medium));
    el.addEventListener("click", (e) => { e.stopPropagation(); press(i); });
    c.append(el);
    x -= 8;
  }
  function press(i) { if (done) return; done = true; md.close(); if (buttons[i].onClick) buttons[i].onClick(); }
  function cancel() { if (done) return; done = true; md.close(); if (onCancel) onCancel(); }
  return { close: () => cancel() };
}
