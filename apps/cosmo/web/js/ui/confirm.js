/*
 * Cosmo by arstro — ui/confirm.js: cosmo_v2::ConfirmDialog (widgets/ConfirmDialog.{h,cpp}) for any
 * web face - a small modal with a title, one message line and a right-aligned row of buttons (each
 * optionally primary = the accent, or destructive = red). Generic: Cosmo's own dialogs/confirm.js
 * is the editor's port (with its name prompt); this one is the widget alone. Built on ./modal.js
 * (the same modal chrome: scrim, card, fade, the key stack); imports only ../core/*.js, ./modal.js
 * and ./controls.js; loads css/dialogs.css (the modal skeleton) and css/ui-kit.css itself.
 *
 * API
 *   confirmDialog(layer, {title, message, buttons: [{label, primary, destructive, onClick}],
 *                         accent, onClose(index | null)}) -> dialog
 *       layer    a full-window, absolutely positioned element of the page's scaled root
 *       accent   the primary button's fill, a CSS colour (default var(--primary)) - the native
 *                takes it at construction (ConfirmDialog.h:30; both apps pass palette::primary())
 *       onClose  once, when the dialog is answered or dismissed: the index of the button that
 *                answered it (after its onClick ran), or null (outside click, Escape with no
 *                cancel button, close())
 *   dialog = {el, card, close(), activate(i), cancel(), confirmDefault(), confirmDestructive(),
 *             relabel(title, message, labels) -> bool, isOpen(), buttonCount(), buttonLabel(i),
 *             buttonRect(i) -> {x, y, w, h} in the card's px}
 *     close()               snap shut at once, no button runs (ConfirmDialog.h:42-43)
 *     activate(i)           press button i as a click would: the dialog starts closing FIRST, then
 *                           the button's onClick runs (:34-44) - it may open another modal
 *     cancel()              the button that is neither primary nor destructive; with none, just
 *                           close (:46-53) - what Escape means
 *     confirmDefault()      the primary button; false if there is none (:55-60) - what Enter means
 *     confirmDestructive()  the destructive one - deliberately bound to no key (:62-67, :72-74)
 *
 * Geometry (ConfirmDialog.cpp): card 400 wide, 20 + 26 + 22 + 22 + 28 + 20 = 138 high (:13-19,
 * :113), centred in the layer, each coordinate clamped to >= 4 (:114-116); scrim black @ 0.55
 * (:161); popover fill + 1 px border, radius::control (:164-165). Title Roboto SemiBold 14
 * foreground at (20, 36) (:168); message Roboto 12 muted at (20, 58), one line, not cut (:170).
 * Buttons 28 high, est(label, 12) + 2 * 14 wide, 8 apart, right-aligned at 20 from the card's
 * right and laid out right to left, so the last declared is the rightmost (:119-134); fill
 * destructive / accent / secondary + 1 px border (:177-181); an eased hover wash
 * palette::hoverWash (:183-185); label Roboto Medium 12, centred by the estimate, baseline
 * middle + 4, primaryForeground on a filled button, foreground on a plain one (:186-189).
 * It fades in over 150 ms (:30) and out over 120 ms (:90), EaseOutCubic, every alpha times the
 * appear amount; a closing dialog takes no input (ConfirmDialog.h:41, :75).
 *
 * Keys (:69-85): Escape = cancel(); Enter / Return = confirmDefault(), else cancel(); every other
 * key is kept from the page while it is open (modal.js lets the browser's own Ctrl / Cmd
 * shortcuts through). Pointer (:136-154): a click on a button = activate; outside the card the
 * dialog just closes - no button runs (:142); inside, elsewhere, nothing.
 */
import { h } from "../core/dom.js";
import { openModal } from "./modal.js";
import { gestures } from "./controls.js";

// ------------------------------------------------------------------ the native's numbers
const CARD_W = 400, PAD = 20, TITLE_H = 26, MSG_H = 22;          // ConfirmDialog.cpp:13-16
const BTN_H = 28, BTN_GAP = 8, BTN_PAD_X = 14;                    // ConfirmDialog.cpp:17
const FOOTER_GAP = 22, FONT_PX = 12;                              // ConfirmDialog.cpp:18-19
const CARD_H = PAD + TITLE_H + MSG_H + FOOTER_GAP + BTN_H + PAD;  // ConfirmDialog.cpp:113 (138)
const OPEN_MS = 150, CLOSE_MS = 120;                              // ConfirmDialog.cpp:30, :90
const SCRIM = 0.55;                                               // ConfirmDialog.cpp:161

// ------------------------------------------------------------------ stylesheets
function useCss(name) {
  const href = new URL(`../../css/${name}`, import.meta.url).href;
  for (const l of document.querySelectorAll('link[rel="stylesheet"]')) if (l.href === href) return;
  document.head.append(h("link", { rel: "stylesheet", href }));
}
useCss("dialogs.css");
useCss("ui-kit.css");

// ------------------------------------------------------------------ text (TextMetrics.h, CairoTarget)
const enc = new TextEncoder();
/** estimateTextWidth (widgets/TextMetrics.h:19-22): UTF-8 bytes x px x 0.6. */
const est = (s, px) => enc.encode(String(s)).length * px * 0.6;
const SVGNS = "http://www.w3.org/2000/svg";
const FAMILY = { sans: "--font-sans", medium: "--font-sans-medium", semibold: "--font-sans-semibold" };
let famNames = null, mctx = null;
const pending = new Set();   // labels drawn before their face loaded: re-spaced once it has
function famName(f) {
  if (!famNames) {
    const cs = getComputedStyle(document.documentElement);
    famNames = {};
    for (const k in FAMILY) famNames[k] = cs.getPropertyValue(FAMILY[k]).trim() || '"Roboto"';
  }
  return famNames[f] || famNames.sans;
}
/** Cairo lays text out with hinted advances - each glyph's rounded to a whole device pixel; the
 *  browser keeps them fractional. The difference, spread as letter spacing, puts every glyph
 *  where the window puts it (as Cosmo's desktop/editor.js does). */
function hint(str, px, f, S) {
  const chars = [...str];
  if (chars.length < 2 || !(S > 0)) return 0;
  const spec = `${px}px ${famName(f)}`;
  if (document.fonts && !document.fonts.check(spec)) {
    document.fonts.load(spec).then(() => { const els = [...pending]; pending.clear(); for (const el of els) setLabel(el, el.__t.str, el.__t.S, true); }, () => {});
    return NaN;
  }
  if (!mctx) mctx = document.createElement("canvas").getContext("2d");
  mctx.font = spec;
  let d = 0;
  for (let i = 0; i < chars.length - 1; i++) { const a = mctx.measureText(chars[i]).width; d += Math.round(a * S) / S - a; }
  return d / (chars.length - 1);
}
/** A label drawn like drawText(str, x, baseline, px, family): an SVG <text> on its baseline. Chrome
 *  rounds an SVG baseline to the device pixel, as Cairo does; an HTML box would snap its TOP instead
 *  and land the baseline a pixel off for most fractional positions. */
function label(str, x, base, px, f = "sans", cls = "", S = 1) {
  const el = document.createElementNS(SVGNS, "svg");
  el.setAttribute("class", "uk-t" + (cls ? " " + cls : ""));
  el.setAttribute("width", 1);
  el.setAttribute("height", 1);
  const t = document.createElementNS(SVGNS, "text");
  t.style.fontFamily = `var(${FAMILY[f]})`;
  t.style.fontSize = px + "px";
  el.append(t);
  el.__t = { px, f, S, t, str: null };
  moveLabel(el, x, base);
  setLabel(el, str, S, true);
  return el;
}
function moveLabel(el, x, base) {
  el.__t.t.setAttribute("x", x);
  el.__t.t.setAttribute("y", base);
}
function setLabel(el, str, S, force) {
  str = String(str ?? "");
  const m = el.__t;
  if (!force && m.str === str && m.S === S) return;
  m.str = str;
  m.S = S;
  m.t.textContent = str;
  const ls = hint(str, m.px, m.f, S);
  if (Number.isNaN(ls)) pending.add(el); else pending.delete(el);
  m.t.style.letterSpacing = (ls || 0) + "px";
}
/** drawRoundedRect as SVG (radius::control): Chrome pixel-snaps a CSS box's fill and outline, Cairo
 *  does not; a rect's 1 px stroke straddles its edge like Cairo's (Shapes.cpp:170-175). */
function body(cls, w, hh) {
  const s = document.createElementNS(SVGNS, "svg");
  s.setAttribute("class", "uk-body " + cls);
  const r = document.createElementNS(SVGNS, "rect");
  s.append(r);
  s.size = (ww, hhh) => {
    for (const el of [s, r]) { el.setAttribute("width", Math.max(0, ww)); el.setAttribute("height", Math.max(0, hhh)); }
    const k = Math.max(0, Math.min(2, ww / 2, hhh / 2));             // the radius clamp (Shapes.cpp:28-31)
    r.setAttribute("rx", k); r.setAttribute("ry", k);
  };
  s.size(w, hh);
  return s;
}
function deviceScale(el) {
  const w = el.offsetWidth;
  return (w > 0 ? el.getBoundingClientRect().width / w : 1) * (window.devicePixelRatio || 1);
}

// ------------------------------------------------------------------ ConfirmDialog
export function confirmDialog(layer, { title = "", message = "", buttons = [], accent, onClose } = {}) {
  const B = (buttons || []).map((b) => ({ label: String(b.label ?? ""), primary: !!b.primary, destructive: !!b.destructive, onClick: b.onClick }));
  const S = deviceScale(layer);
  let closing = false;

  const m = openModal(layer, {
    width: CARD_W, height: CARD_H, cls: "uk-cf", fill: "popover", scrim: SCRIM, openMs: OPEN_MS, closeMs: CLOSE_MS,
    onOutside: () => finish(null, null),                                 // :142 - closes, runs nothing
    onKey: (e) => {                                                      // :69-85
      if (closing) return false;
      if (e.key === "Escape") { api.cancel(); return true; }
      if (e.key === "Enter") { if (!api.confirmDefault()) api.cancel(); return true; }
      return false;                                                      // modal.js swallows the rest
    },
  });
  const card = m.card;
  m.el.setAttribute("role", "dialog");
  m.el.setAttribute("aria-modal", "true");
  if (accent) card.style.setProperty("--uk-accent", accent);

  // The card's 1 px border, straddling its edge like Cairo's stroke (:164-165).
  const frame = body("uk-cf-frame", CARD_W, CARD_H);

  const titleEl = label(title, PAD, PAD + 16, 14, "semibold", "uk-cf-title", S);            // :168
  const msgEl = label(message, PAD, PAD + TITLE_H + 12, FONT_PX, "sans", "uk-cf-msg", S);  // :170
  card.append(titleEl, msgEl, frame);

  const btnEls = B.map((b, i) => {
    const el = h("div.uk-cf-btn." + (b.destructive ? "destructive" : b.primary ? "primary" : "plain"), { role: "button", "aria-label": b.label });
    el.fill = body("uk-cf-fill", 0, BTN_H);                                                  // :177-181
    el.wash = body("uk-cf-wash", 0, BTN_H);                                                  // :183-185
    el.lab = label(b.label, 0, BTN_H * 0.5 + 4, FONT_PX, "medium", "uk-cf-label", S);       // :186-189
    el.append(el.fill, el.wash, el.lab);
    gestures(el, { click: () => api.activate(i) });
    card.append(el);
    return el;
  });

  function rects() {                                                     // buttonRects :119-134
    const out = [];
    let right = CARD_W - PAD;
    const y = CARD_H - PAD - BTN_H;
    for (let i = B.length - 1; i >= 0; i--) {
      const w = est(B[i].label, FONT_PX) + 2 * BTN_PAD_X;
      out[i] = { x: right - w, y, w, h: BTN_H };
      right -= w + BTN_GAP;
    }
    return out;
  }
  function layoutButtons() {
    rects().forEach((r, i) => {
      const el = btnEls[i];
      Object.assign(el.style, { transform: `translate(${r.x}px, ${r.y}px)`, width: r.w + "px", height: r.h + "px" });
      el.fill.size(r.w, r.h);
      el.wash.size(r.w, r.h);
      moveLabel(el.lab, (r.w - est(B[i].label, FONT_PX)) * 0.5, BTN_H * 0.5 + 4);   // :188-189
    });
  }
  layoutButtons();

  /** Start closing (beginClose :87-91); `run` then runs, then onClose(result). */
  function finish(result, run) {
    if (closing) return false;
    closing = true;
    m.close(result);
    if (run) { try { run(); } catch (err) { console.error(err); } }
    if (onClose) { try { onClose(result); } catch (err) { console.error(err); } }
    return true;
  }

  const api = {
    el: m.el,
    card,
    isOpen() { return !closing; },
    close() {                                                            // snap shut (ConfirmDialog.h:43)
      const was = !closing;
      closing = true;
      m.close(null);
      m.el.remove();
      if (was && onClose) { try { onClose(null); } catch (err) { console.error(err); } }
    },
    activate(i) {                                                        // :34-44
      if (closing || !(i >= 0 && i < B.length)) return false;
      return finish(i, B[i].onClick);
    },
    cancel() {                                                           // :46-53
      const i = B.findIndex((b) => !b.primary && !b.destructive);
      if (i >= 0) return api.activate(i);
      return finish(null, null);
    },
    confirmDefault() { const i = B.findIndex((b) => b.primary); return i >= 0 ? api.activate(i) : false; },          // :55-60
    confirmDestructive() { const i = B.findIndex((b) => b.destructive); return i >= 0 ? api.activate(i) : false; },  // :62-67
    relabel(t, msg, list) {                                              // ConfirmDialog.h:34-40
      if (!Array.isArray(list) || list.length !== B.length) return false;
      setLabel(titleEl, t, S);
      setLabel(msgEl, msg, S);
      list.forEach((l, i) => { B[i].label = String(l ?? ""); setLabel(btnEls[i].lab, B[i].label, S); btnEls[i].setAttribute("aria-label", B[i].label); });
      layoutButtons();
      return true;
    },
    buttonCount() { return B.length; },
    buttonLabel(i) { return B[i] ? B[i].label : ""; },
    buttonRect(i) { return rects()[i] || { x: 0, y: 0, w: 0, h: 0 }; },
  };
  return api;
}
