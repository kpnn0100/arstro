/*
 * Interstellar — dialogs/prompt.js: ctx.prompt's STAND-IN until edit/topbar.js provides the NamePrompt
 * port (vm.provide("prompt", fn) - the shell then uses that and never loads this file).
 *
 *   prompt(layer, {title, message, value, ok, fields?: [{label, value}]}) -> Promise<string | string[] | null>
 *
 * NamePrompt's skeleton (app/widgets/NamePrompt.{h,cpp}): a 360-wide card (168 high; with fields
 * 64 + 34 per field + 16 + 28 + 16), centred and 24 px above the middle, a field (or one labelled field
 * a row, Tab moves), Cancel and the confirm button; Escape cancels, Enter confirms, a click outside
 * cancels, and the dialog closes BEFORE its answer is delivered (design rule §5). Drawn with the shared
 * modal skeleton (ui/modal.js). The look is close, not pixel-exact: the topbar owner's port is.
 */
import { h } from "../core/dom.js";
import { openModal } from "../ui/modal.js";
import { useCss } from "../kit/css.js";
import { placeText, width as textWidth, FAMILY } from "../kit/textfit.js";

const CARD_W = 360, CARD_H = 168, PAD = 16, FIELD_H = 28, BTN_H = 28, BTN_W = 88, ROW_H = 34, LABEL_W = 150;

export function prompt(layer, { title = "", message = "", value = "", ok = "OK", fields = null } = {}) {
  useCss("css/dialogs.css");
  useCss("css/dialogs/picker.css");
  const multi = Array.isArray(fields) && fields.length > 0;
  const cardH = multi ? 64 + fields.length * ROW_H + PAD + BTN_H + PAD : CARD_H;
  return new Promise((resolve) => {
    let done = false;
    const finish = (v) => { if (done) return; done = true; md.close(); setTimeout(() => resolve(v), 0); };
    const md = openModal(layer, { width: CARD_W, height: cardH, cls: "ip-prompt", onOutside: () => finish(null), onKey,
      place: (api) => ({ x: Math.max(4, (api.W - CARD_W) / 2), y: Math.max(4, (api.H - cardH) / 2 - 24) }) });
    const c = md.card;
    c.append(placeText(h("div.ip-t.ip-fg", { text: title }), PAD, PAD + 14, 14, FAMILY.semibold));
    c.append(placeText(h("div.ip-t.ip-muted", { text: message }), PAD, PAD + 36, 11, FAMILY.sans));
    const inputs = [];
    const mkField = (x, y, w, v) => {
      const f = h("input.ip-field", { type: "text", spellcheck: "false", autocomplete: "off",
        style: { transform: `translate(${x}px, ${y}px)`, width: w + "px", height: FIELD_H + "px" } });
      f.value = v ?? "";
      c.append(f);
      inputs.push(f);
    };
    if (multi) {
      fields.forEach((fd, i) => {
        const y = 64 + i * ROW_H;
        c.append(placeText(h("div.ip-t.ip-muted", { text: fd.label }), PAD, y + 18, 11, FAMILY.sans));
        mkField(PAD + LABEL_W, y, CARD_W - 2 * PAD - LABEL_W, fd.value);
      });
    } else mkField(PAD, 72, CARD_W - 2 * PAD, value);
    const by = cardH - PAD - BTN_H;
    const btn = (x, label, kind, fn) => {
      const b = h("button.ip-btn.ip-" + kind, { style: { transform: `translate(${x}px, ${by}px)`, width: BTN_W + "px", height: BTN_H + "px" } });
      b.append(placeText(h("div.ip-t.ip-btn-label", { text: label }), (BTN_W - textWidth(label, 12, FAMILY.medium)) / 2, BTN_H / 2 + 4, 12, FAMILY.medium));
      b.addEventListener("click", (e) => { e.stopPropagation(); fn(); });
      c.append(b);
    };
    btn(CARD_W - PAD - BTN_W, ok, "primary", confirm);
    btn(CARD_W - PAD - 2 * BTN_W - 8, "Cancel", "plain", () => finish(null));
    requestAnimationFrame(() => { const f = inputs[0]; if (f) { f.focus(); f.select(); } });
    function confirm() { finish(multi ? inputs.map((f) => f.value) : inputs[0].value); }
    function onKey(e) {
      if (e.key === "Escape") { finish(null); return true; }
      if (e.key === "Enter") { confirm(); return true; }
      return false;
    }
  });
}
