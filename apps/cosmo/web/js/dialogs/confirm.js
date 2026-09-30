/*
 * Cosmo by arstro — confirm.js: ConfirmDialog (widgets/ConfirmDialog.cpp) and the one-line name
 * prompt that replaces the host's GTK "Save preset" dialog (linux_main.cpp, spec §5.6).
 *
 * ConfirmDialog: card 400 x 138 (20 + 26 + 22 + 22 + 28 + 20); title Roboto SemiBold 14 at
 * (20, 36); message Roboto 12 muted at (20, 58), one line; buttons 28 high, est(label, 12) + 28
 * wide, 8 apart, right-aligned and laid out right to left so the last declared is rightmost;
 * destructive fill #E5534B / primary fill accent (white text), plain = secondary + border; hover
 * wash per button. Esc = the button that is neither primary nor destructive (else just close);
 * Enter = primary (else cancel); destructive is never key-bound; outside click = cancel. A button
 * click closes first, then runs its action.
 *
 *   open(host, {title, message, buttons: [{label, primary, destructive, run}]})  (vm.view.dialog)
 *   confirm(vm, {title, message, buttons}) -> Promise<index of the button | null>  (stacked)
 *   prompt(vm, {title, placeholder, value, ok}) -> Promise<string | null>          (stacked)
 *
 * The prompt is a web addition in the ConfirmDialog card: a light text field (inputLight /
 * inputLightText) in the message slot; Enter = the confirm button, Esc = Cancel.
 */
import { h } from "../core/dom.js";
import { openModal } from "../ui/modal.js";
import { est, fitEnd } from "./paint.js";
import { T, button, frame } from "./kit.js";
import { hostOf, ensureHost } from "./host.js";

const W = 400, PAD = 20, BTN_H = 28, GAP = 8, PADX = 14;

function card({ layer, title, message, buttons, onPick, field }) {
  const hgt = PAD + 26 + (field ? 28 : 22) + 22 + BTN_H + PAD;
  let m;
  const pick = (i) => { if (m.closing) return; m.close(i); onPick(i); };
  const cancelIndex = () => buttons.findIndex((b) => !b.primary && !b.destructive);
  const primaryIndex = () => buttons.findIndex((b) => b.primary);
  const cancel = () => { const i = cancelIndex(); pick(i >= 0 ? i : null); };
  m = openModal(layer, {
    width: W, height: hgt, cls: "dg-confirm",
    onOutside: cancel,
    onKey: (e) => {
      if (e.key === "Escape") { cancel(); return true; }
      if (e.key === "Enter") {
        const p = primaryIndex();
        if (p >= 0) { if (!(field && !field.value.trim())) pick(p); } else cancel();
        return true;
      }
      return false;
    },
  });
  const c = m.card;
  frame(c);
  T(c, title || "", PAD, PAD + 16, 14, "semibold", "dg-fg");
  if (field) {
    const f = h("input.dg-light", { type: "text", placeholder: field.placeholder || "", spellcheck: "false", autocomplete: "off",
      style: { left: PAD + "px", top: PAD + 26 + "px", width: W - 2 * PAD + "px", height: "28px" } });
    f.value = field.value || "";
    c.append(f);
    field.el = f;
    Object.defineProperty(field, "value", { get: () => f.value });
    requestAnimationFrame(() => { f.focus(); f.select(); });
  } else {
    T(c, fitEnd(message || "", W - 2 * PAD, 12), PAD, PAD + 26 + 12, 12, "sans", "dg-muted");
  }
  const y = hgt - PAD - BTN_H;
  let right = W - PAD;
  const els = [];
  for (let i = buttons.length - 1; i >= 0; i--) {
    const b = buttons[i];
    const w = est(b.label, 12) + 2 * PADX;
    els[i] = button(c, { x: right - w, y, w, h: BTN_H, label: b.label, kind: b.destructive ? "destructive" : b.primary ? "primary" : "plain",
                         onClick: () => pick(i) });
    right -= w + GAP;
  }
  if (field) {
    const p = primaryIndex();
    const sync = () => { if (p >= 0) els[p].classList.toggle("disabled", !field.el.value.trim()); };
    field.el.addEventListener("input", sync);
    sync();
  }
  return m;
}

/** vm.view.dialog = {name: "confirm", title, message, buttons: [{label, primary, destructive, run}]} */
export function open(host, spec) {
  const buttons = spec.buttons && spec.buttons.length ? spec.buttons : [{ label: "OK", primary: true }];
  return card({ layer: host.layer, title: spec.title, message: spec.message, buttons,
    onPick: (i) => {
      host.closed(spec);
      const b = i === null || i === undefined ? null : buttons[i];
      if (b && b.run) b.run();
      if (spec.done) spec.done(i);
    } });
}

/** A stacked confirmation; resolves with the index of the button pressed (null = dismissed). */
export function confirm(vm, { title, message, buttons }) {
  const host = hostOf(vm) || ensureHost(vm);
  return new Promise((resolve) => card({ layer: host.layer, title, message, buttons, onPick: (i) => resolve(i ?? null) }));
}

/** vm.view.dialog = {name: "prompt", title, placeholder, value, ok, run(text)} */
export function openPrompt(host, spec) {
  const field = { placeholder: spec.placeholder, value: spec.value };
  const buttons = [{ label: "Cancel" }, { label: spec.ok || "Save", primary: true }];
  return card({ layer: host.layer, title: spec.title, buttons, field,
    onPick: (i) => {
      host.closed(spec);
      if (i === 1 && spec.run) spec.run(field.value.trim());
    } });
}

export function prompt(vm, { title, placeholder, value, ok }) {
  const host = hostOf(vm) || ensureHost(vm);
  const field = { placeholder, value };
  const buttons = [{ label: "Cancel" }, { label: ok || "Save", primary: true }];
  return new Promise((resolve) => card({ layer: host.layer, title, buttons, field,
    onPick: (i) => resolve(i === 1 ? field.value.trim() : null) }));
}
