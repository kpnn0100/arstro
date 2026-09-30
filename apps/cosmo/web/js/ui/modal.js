/*
 * Cosmo by arstro — modal.js: the shared modal skeleton of the five desktop dialogs
 * (SettingsDialog / ConfirmDialog / InfoDialog / PresetDialog / ExportDialog .cpp) and the
 * web's file pickers, which borrow it.
 *
 * Drawn over the WHOLE window (the scrim covers everything); one centred card of a fixed width
 * whose top-left is `((W - w)/2, (H - h)/2)`, each clamped to >= 4 (a short window cuts the card
 * off at the bottom - native has no scroll). Everything inside is multiplied by the appear value
 * `a`: 0 -> 1 over 150 ms EaseOutCubic (Export 160), back to 0 over 120 ms on close; a closing
 * modal no longer takes input. Card: fill popover (Export: card), 1 px `border` (straddling, as
 * Cairo strokes), radius 2; scrim rgba(0,0,0,.55·a) (Export .60) - the literal the native uses
 * (SettingsDialog.cpp:238 `Color{0, 0, 0, 0.55}`). Click outside the card = the dialog's
 * `onOutside` (close, or cancel). Keys go to the TOP modal only (`onKey(e)` returns true when it
 * consumed the key); the others are swallowed while any modal is up, except what a text field
 * inside the modal types.
 *
 * Modals stack (a picker over the Export dialog, a "Replace?" over a picker): each is its own
 * layer in the host, the newest on top.
 */
import { h } from "../core/dom.js";

/**
 * openModal(layer, opts) -> api
 *   opts: {width, height (number), fill: "popover"|"card", scrim: .55, openMs: 150, closeMs: 120,
 *          rise: 0 (px the card rises by while appearing), cls, onOutside(), onKey(e), onClosed(),
 *          place(api) -> {x, y} (custom placement, e.g. the Export drag),
 *          passKeys: true (the keys the dialog does not use go on to the page - Settings / Preset)}
 *   api:  {el (the modal layer), card, scrimEl, close(), setHeight(h), relayout(), closing, W, H}
 */
export function openModal(layer, opts) {
  const o = { fill: "popover", scrim: 0.55, openMs: 150, closeMs: 120, rise: 0, ...opts };
  const el = h("div.md" + (o.cls ? "." + o.cls : ""));
  const scrimEl = h("div.md-scrim", { style: { background: `rgba(0, 0, 0, ${o.scrim})` } });   // SettingsDialog.cpp:238, ExportDialog.cpp:901
  const card = h("div.md-card.md-fill-" + o.fill);
  const frame = h("div.md-frame");        // the card's stroked outline (straddles its edge)
  card.append(frame);
  el.append(scrimEl, card);
  el.style.setProperty("--md-open", o.openMs + "ms");
  el.style.setProperty("--md-close", o.closeMs + "ms");
  el.style.setProperty("--md-rise", o.rise + "px");
  layer.append(el);

  const api = {
    el, card, scrimEl, closing: false, opts: o, height: o.height || 100, W: 0, H: 0, x: 0, y: 0,
    onKey: o.onKey || null,
    setHeight(hh) { api.height = hh; api.relayout(); },
    relayout() {
      api.W = layer.clientWidth; api.H = layer.clientHeight;
      let x, y;
      if (o.place) ({ x, y } = o.place(api));
      else { x = Math.max(4, (api.W - o.width) / 2); y = Math.max(4, (api.H - api.height) / 2); }
      api.x = x; api.y = y;
      // placed by a transform: Chrome snaps a box's left/top to whole pixels, which would move
      // the 1 px frame of a card at y = 238.5 off the native's pixel row
      card.style.setProperty("--md-x", x + "px");
      card.style.setProperty("--md-y", y + "px");
      Object.assign(card.style, { width: o.width + "px", height: api.height + "px" });
    },
    close(result) {
      if (api.closing) return;
      api.closing = true;
      el.classList.remove("on");
      el.classList.add("closing");
      unstack(api);
      const done = () => { el.remove(); if (o.onClosed) o.onClosed(result); };
      setTimeout(done, o.closeMs + 40);
    },
  };
  // Pointer: a press that starts AND ends outside the card is the outside click (a drag that
  // began inside a field and ended on the scrim is not).
  let downOutside = false;
  el.addEventListener("pointerdown", (e) => { downOutside = !card.contains(e.target); });
  el.addEventListener("click", (e) => {
    if (api.closing) return;
    if (downOutside && !card.contains(e.target)) { if (o.onOutside) o.onOutside(); else api.close(null); }
  });
  el.addEventListener("wheel", (e) => { if (!card.contains(e.target)) e.preventDefault(); }, { passive: false });

  api.relayout();
  const ro = new ResizeObserver(() => { if (!api.closing) api.relayout(); });
  ro.observe(layer);
  const origClose = api.close;
  api.close = (r) => { ro.disconnect(); origClose(r); };
  stack(api);
  requestAnimationFrame(() => requestAnimationFrame(() => { if (!api.closing) el.classList.add("on"); }));
  return api;
}

// ------------------------------------------------------------------ the stack + keys
const STACK = [];
function stack(m) { STACK.push(m); ensureKeys(); }
function unstack(m) { const i = STACK.indexOf(m); if (i >= 0) STACK.splice(i, 1); }
export const modalOpen = () => STACK.length > 0;
export const topModal = () => STACK[STACK.length - 1] || null;

let keysOn = false;
function ensureKeys() {
  if (keysOn) return;
  keysOn = true;
  const route = (e) => {
    const top = topModal();
    if (!top) return;
    const inField = e.target && e.target.closest && e.target.closest(".md") &&
                    (e.target.tagName === "INPUT" || e.target.tagName === "TEXTAREA");
    const used = top.onKey ? top.onKey(e) : false;
    if (used) { e.preventDefault(); e.stopPropagation(); return; }
    if (inField) { e.stopPropagation(); return; }           // the field types it; nobody else sees it
    // Settings / Preset take no keys natively ("not routed", spec §8): the keys go on to
    // whatever is behind them (the editor's shortcuts), exactly as if no modal were up
    if (top.opts.passKeys) return;
    // swallowed: a modal is up (browser shortcuts other than plain keys still work)
    if (!(e.ctrlKey || e.metaKey) || e.key.length === 1) e.stopPropagation();
    if (e.key === " " || e.key.startsWith("Arrow") || e.key === "Tab") e.preventDefault();
  };
  window.addEventListener("keydown", route, true);
}
