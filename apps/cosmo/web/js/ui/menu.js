/*
 * Cosmo by arstro — ui/menu.js: cosmo_v2::ContextMenu (widgets/ContextMenu.{h,cpp}) for any web
 * face - a small popup of labelled actions shown at a point, modal while open, and its inline
 * rename mode (DR-TREE-5). Generic: it knows nothing of an app (Cosmo's editor keeps its own port
 * in desktop/contextmenu.js; this is the same drawing and motion with no editor in it). Imports
 * only ../core/*.js and ./controls.js; loads its stylesheet (css/ui-kit.css) itself.
 *
 * API
 *   openMenu(layer, items, x, y, opts?) -> menu                         ContextMenu::open (:28-37)
 *       items  [{label, action(menu)}] - the action runs AFTER the menu closed, with the menu, so
 *              an item can morph it into rename mode: action: (m) => m.enterRenameMode(name, fn)
 *       x, y   the popup's top-left (the cursor) in `layer`'s logical px; clamped into the layer
 *       opts   {onClose()} - once, when this opening is gone (after the item's action ran)
 *       An empty list opens nothing (:35).
 *   openRename(layer, name, x, y, onRename, {title?, onClose?}) -> menu straight to rename (:53-67)
 *   relabel(layer, labels) -> bool         the open items in other words (ContextMenu.h:28-32)
 *   closeMenu(layer)                       close whatever the layer's menu shows (ContextMenu.h:33)
 *   menu = {el, close(), relabel(labels), enterRenameMode(name, onRename, {title?}), isOpen(),
 *           isRenaming(), itemCount(), itemRect(i) -> {x, y, w, h} in layer px}
 *       A handle whose opening was replaced by a later openMenu / openRename no longer acts.
 *
 * `layer` is a full-window, absolutely positioned element inside the page's scaled root (the
 * shell's menus layer). ONE menu lives per layer, as the native App has one ContextMenu: a second
 * openMenu replaces the first (the replaced opening's onClose runs). Opening raises it to the top
 * of the layer (:36).
 *
 * Drawing (ContextMenu.cpp): width max(150, est(label, 11) + 2*11) (:80-84), 208 in rename mode
 * (:82); height rows * 24 + 2*4 (:85-86); x, y clamped so the whole menu is inside the layer, then
 * to >= 0 (:87-90). Popover fill, 1 px border straddling the edge, radius::control (:202-204);
 * labels Roboto 11 foreground at x 11, baseline row middle + 11 * 0.35 (:278); per-item hover
 * wash white @ 0.08, inset 3 px, radius::control (:270-274), cross-fading over 120 ms like
 * HoverFade (HoverFade.h:36-66). It appears over 130 ms and goes over 100 ms, EaseOutCubic (:122),
 * rising 6 px into place as it fades in (:200); every alpha is times the appear amount.
 *
 * Behaviour (:159-193): modal while open - every press is taken; a click or right click on an
 * item closes the menu and then runs the item (:176-185); anywhere else it just closes (:187);
 * moves track the hovered item (:174); drags, double clicks and the wheel are swallowed. Hit
 * testing is the native's arithmetic (itemAt :107-113, truncating, so the 4 px top pad is item 0),
 * on the settled rect, not the rising one. No keys outside rename mode (:132).
 *
 * Rename mode (:39-76, :130-157, :222-264): the morph runs 260 ms EaseInOutCubic (:48, :64) -
 * in the first half the items fade and the menu collapses to a 22 px header that fades in
 * (`title`, default "Rename", muted, ContextMenu.h:47), in the second the light field grows below
 * it (6 px gap, 30 high, inset 8; :93-99). The field is seeded with the name - "Group" when it is
 * empty (:46, :61) - wholly selected (primary @ 0.26, radius::hairline, :248-252). Typing replaces
 * the selection or appends; Backspace clears the selection or drops the last character; Enter
 * closes and calls onRename(name) if the name is not empty (:147-152); Escape cancels (:154);
 * every other key is swallowed. A click in the field drops the selection; a click outside the
 * menu cancels (:163-169). The field shows the TAIL of a long name (:241-243) with an 8 px inner
 * pad; the 1.6 px primary caret sits at est(text) + 1 and blinks 0.55 + 0.45 cos(t * 0.006)
 * (:258-262), hidden while the field is selected. Text widths are the native's estimate.
 *
 * Deviations: (1) closing a rename keeps its rename look while it fades out (the native's close()
 * drops mRenaming first and paints the old item list for those 100 ms; Cosmo's own port does as
 * here). (2) The keys come through a hidden text input, so a phone's on-screen keyboard can type
 * in the field; a key the browser owns (Ctrl / Cmd shortcuts) is kept from the page but not from
 * the browser. (3) Characters are removed per code point, not per UTF-8 byte (:144).
 */
import { Tween, Ease } from "../core/motion.js";
import { h } from "../core/dom.js";
import { toLocal } from "./controls.js";

// ------------------------------------------------------------------ the native's numbers
const ITEM_H = 24, FONT_PX = 11, PAD_X = 11, PAD_Y = 4, MIN_W = 150;          // ContextMenu.cpp:15-18
const HEADER_H = 22, FIELD_H = 30, FIELD_GAP = 6, FIELD_MX = 8, RENAME_W = 208; // ContextMenu.cpp:20-24
const IN_MS = 130, OUT_MS = 100;                                                 // ContextMenu.cpp:122
const RISE = 6;                                                                  // ContextMenu.cpp:200
const MORPH_MS = 260;                                                            // ContextMenu.cpp:48, :64
const INNER_PAD = 8;                                                             // ContextMenu.cpp:239
const SEL_A = 0.26;                                                              // ContextMenu.cpp:250
const DRAG = 5, DOUBLE_MS = 300, LONG_MS = 500;   // Artboard GestureRecognizer.h:38-40
const clamp01 = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);
const inside = (r, p) => p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;   // Geometry.h:30

// ------------------------------------------------------------------ stylesheet
function useCss(name) {
  const href = new URL(`../../css/${name}`, import.meta.url).href;
  for (const l of document.querySelectorAll('link[rel="stylesheet"]')) if (l.href === href) return;
  document.head.append(h("link", { rel: "stylesheet", href }));
}
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
// ------------------------------------------------------------------ shapes
// drawRoundedRect as SVG: Chrome pixel-snaps a CSS box's background and outline, Cairo does not -
// an SVG rect keeps the fractional edges and strokes its 1 px border centred on them (Shapes.cpp:170-175).
function body(cls) {
  const s = document.createElementNS(SVGNS, "svg");
  s.setAttribute("class", "uk-body " + cls);
  const r = document.createElementNS(SVGNS, "rect");
  r.setAttribute("rx", 2); r.setAttribute("ry", 2);                 // radius::control (Theme.h:154)
  s.append(r);
  s.size = (w, hh) => {
    w = Math.max(0, w); hh = Math.max(0, hh);
    s.setAttribute("width", w); s.setAttribute("height", hh);
    r.setAttribute("width", w); r.setAttribute("height", hh);
    const k = Math.min(2, w / 2, hh / 2);                            // the radius clamp (Shapes.cpp:28-31)
    r.setAttribute("rx", k); r.setAttribute("ry", k);
  };
  return s;
}

/** Device pixels per logical px where `el` is drawn (the page's UI scale x devicePixelRatio). */
function deviceScale(el) {
  const w = el.offsetWidth;
  return (w > 0 ? el.getBoundingClientRect().width / w : 1) * (window.devicePixelRatio || 1);
}

// ------------------------------------------------------------------ the API
const MENUS = new WeakMap();   // layer -> its one ContextMenu
function menuFor(layer) {
  let m = MENUS.get(layer);
  if (!m) { m = new ContextMenu(layer); MENUS.set(layer, m); }
  return m;
}
export function openMenu(layer, items, x, y, opts = {}) { return menuFor(layer).open(items, x, y, opts); }
export function openRename(layer, name, x, y, onRename, opts = {}) { return menuFor(layer).openRename(name, x, y, onRename, opts); }
export function relabel(layer, labelsNow) { const m = MENUS.get(layer); return m ? m.relabel(labelsNow) : false; }
export function closeMenu(layer) { const m = MENUS.get(layer); if (m) m.close(); }

// ------------------------------------------------------------------ ContextMenu
class ContextMenu {
  constructor(layer) {
    this.layer = layer;
    this.items = [];
    this.x = 0; this.y = 0;
    this.opened = false; this.wasOpened = false; this.queued = false;
    this.renaming = false; this.text = ""; this.selectAll = false; this.preH = 0;
    this.title = "Rename"; this.onRename = null; this.onClose = null;
    this.gen = 0; this.hover = -1; this.press = null; this.lastClick = null; this.S = 1;
    this.blinkRaf = 0; this.ro = null; this.keysOn = false;

    this.root = h("div.uk-cm-layer");
    this.pop = h("div.uk-cm", { role: "menu" });
    this.list = h("div.uk-cm-items");
    this.header = label("", PAD_X, PAD_Y + HEADER_H * 0.5 + FONT_PX * 0.35, FONT_PX, "sans", "uk-cm-header");   // :229
    this.sel = h("div.uk-cm-sel");
    this.ftext = label("", INNER_PAD, 0, FONT_PX, "sans", "uk-cm-ftext");
    this.caret = h("div.uk-cm-caret");
    this.fieldBody = body("uk-cm-fieldbody");                       // inputLight, radius::control (:236-237)
    this.field = h("div.uk-cm-field", {}, this.fieldBody, this.sel, this.ftext, this.caret);
    this.sink = h("input.uk-cm-sink", { type: "text", autocomplete: "off", autocapitalize: "off", spellcheck: "false", tabindex: "-1" });
    this.frame = body("uk-cm-body");                                 // popover + border (:202-204)
    this.pop.append(this.frame, this.list, this.header, this.field);
    this.root.append(this.pop, this.sink);
    this.root.style.visibility = "hidden";

    this.appear = new Tween(0, () => this.paint());
    this.morph = new Tween(0, () => this.paint());
    this.onKey = (e) => this.key(e);
    this.bindPointer();
    this.bindSink();
  }

  // ---- opening
  open(items, x, y, opts) {
    this.replaced();
    this.items = (items || []).map((it) => ({ label: String(it.label ?? ""), action: it.action }));
    this.x = +x || 0; this.y = +y || 0;
    this.renaming = false;
    this.stopKeys();
    this.morph.set(0);
    this.onClose = opts.onClose || null;
    this.S = deviceScale(this.layer);
    this.renderItems();
    this.raise();
    this.setOpen(this.items.length > 0);
    this.paint();
    return this.handle();
  }
  openRename(name, x, y, onRename, opts) {
    this.replaced();
    this.items = [];                     // straight to rename: no list to collapse (:55)
    this.renderItems();
    this.x = +x || 0; this.y = +y || 0;
    this.preH = HEADER_H;                // :58
    this.onClose = opts.onClose || null;
    this.S = deviceScale(this.layer);
    this.morph.set(0);                   // :63
    this.onRename = onRename || null;
    this.beginRename(name, undefined, opts.title);
    return this.handle();
  }
  enterRenameMode(name, onRename, title) {
    // The item's action already closed us; the list collapses into the header (:39-51).
    this.preH = this.items.length * ITEM_H;
    this.beginRename(name, onRename, title);
  }
  beginRename(name, onRename, title) {
    this.renaming = true;
    this.text = name ? String(name) : "Group";   // :46, :61
    this.selectAll = true;
    if (onRename !== undefined) this.onRename = onRename || null;
    this.title = title || "Rename";              // ContextMenu.h:47
    setLabel(this.header, this.title, this.S);
    this.setHover(-1);
    this.morph.to(1, MORPH_MS, Ease.EaseInOutCubic);
    this.raise();
    this.setOpen(true);
    this.startKeys();
    this.paint();
  }
  /** A new opening over a live one: the old one's handle goes stale and its onClose runs. */
  replaced() {
    this.gen++;
    if (this.opened || this.queued) this.fireClose();
  }
  raise() { this.layer.append(this.root); }
  close() {
    this.stopKeys();
    this.setOpen(false);
  }
  relabel(list) {
    if (!Array.isArray(list) || list.length !== this.items.length) return false;
    list.forEach((l, i) => { this.items[i].label = String(l ?? ""); });
    [...this.list.children].forEach((row, i) => setLabel(row.firstChild, this.items[i].label, this.S));
    this.paint();
    return true;
  }
  handle() {
    const m = this, gen = this.gen, live = () => gen === m.gen;
    return {
      el: m.pop,
      close() { if (live()) m.close(); },
      relabel(list) { return live() && m.relabel(list); },
      enterRenameMode(name, onRename, o = {}) { if (live()) m.enterRenameMode(name, onRename, o.title); },
      isOpen() { return live() && m.opened; },
      isRenaming() { return live() && m.opened && m.renaming; },
      itemCount() { return live() ? m.items.length : 0; },
      itemRect(i) { const r = m.menuRect(); return { x: r.x, y: r.y + PAD_Y + i * ITEM_H, w: r.w, h: ITEM_H }; },   // :101-105
    };
  }

  // ---- open state: modal now, the fade decided once the click's action has run (advance, :117-123)
  setOpen(on) {
    this.opened = on;
    this.root.classList.toggle("modal", on);
    if (!on) { this.setHover(-1); this.press = null; }
    if (!this.queued) { this.queued = true; queueMicrotask(() => this.settle()); }
  }
  settle() {
    this.queued = false;
    if (this.opened !== this.wasOpened) {
      this.wasOpened = this.opened;
      this.appear.to(this.opened ? 1 : 0, this.opened ? IN_MS : OUT_MS, Ease.EaseOutCubic);
      if (this.opened) this.watch(); else { this.unwatch(); this.fireClose(); }
    }
    this.paint();
  }
  fireClose() {
    const cb = this.onClose;
    this.onClose = null;
    if (cb) { try { cb(); } catch (e) { console.error(e); } }
  }
  watch() {
    if (this.ro || typeof ResizeObserver !== "function") return;
    this.ro = new ResizeObserver(() => this.paint());
    this.ro.observe(this.layer);
  }
  unwatch() { if (this.ro) { this.ro.disconnect(); this.ro = null; } }

  // ---- geometry (ContextMenu.cpp:69-113)
  amounts() {
    const amt = this.morph.value;
    return { amt, collapse: clamp01(amt * 2), grow: clamp01((amt - 0.5) * 2) };
  }
  menuRect() {
    let w = MIN_W;
    if (this.renaming) w = RENAME_W;
    else for (const it of this.items) w = Math.max(w, est(it.label, FONT_PX) + 2 * PAD_X);
    let contentH;
    if (this.renaming) {
      const { collapse, grow } = this.amounts();
      contentH = this.preH + (HEADER_H - this.preH) * collapse + (FIELD_GAP + FIELD_H) * grow;   // :69-76
    } else contentH = this.items.length * ITEM_H;
    const hh = contentH + 2 * PAD_Y;
    const W = this.layer.clientWidth, H = this.layer.clientHeight;
    return { x: Math.max(0, Math.min(this.x, W - w)), y: Math.max(0, Math.min(this.y, H - hh)), w, h: hh };
  }
  fieldRect() {
    const r = this.menuRect(), { grow } = this.amounts();
    return { x: r.x + FIELD_MX, y: r.y + PAD_Y + HEADER_H + FIELD_GAP * grow, w: r.w - 2 * FIELD_MX, h: FIELD_H * grow };
  }
  itemAt(p) {
    const r = this.menuRect();
    if (!inside(r, p)) return -1;
    const i = Math.trunc((p.y - (r.y + PAD_Y)) / ITEM_H);   // C++ (int): the top pad is item 0
    return i >= 0 && i < this.items.length ? i : -1;
  }

  // ---- drawing (ContextMenu.cpp:195-280)
  renderItems() {
    this.hover = -1;
    this.list.replaceChildren(...this.items.map((it, i) => {
      const row = h("div.uk-cm-item", { role: "menuitem", style: { top: PAD_Y + i * ITEM_H + "px" } },
        label(it.label, PAD_X, ITEM_H * 0.5 + FONT_PX * 0.35, FONT_PX, "sans", "uk-cm-label"));
      setLabel(row.firstChild, it.label, this.S);
      return row;
    }));
  }
  setHover(i) {
    if (i === this.hover) return;
    const rows = this.list.children;
    if (rows[this.hover]) rows[this.hover].classList.remove("hover");
    this.hover = i;
    if (rows[i]) rows[i].classList.add("hover");
  }
  paint() {
    const a = this.appear.value;
    const shown = this.opened || a > 0.001;
    this.root.style.visibility = shown ? "visible" : "hidden";
    if (!shown) return;
    const r = this.menuRect();
    const ps = this.pop.style;
    ps.opacity = a;
    ps.transform = `translate(${r.x}px, ${r.y - (1 - a) * RISE}px)`;   // :200
    ps.width = r.w + "px";
    ps.height = r.h + "px";
    this.frame.size(r.w, r.h);
    if (!this.renaming) {
      this.list.style.opacity = 1;
      this.header.style.opacity = 0;
      this.field.style.display = "none";
      return;
    }
    const { amt, collapse, grow } = this.amounts();
    this.list.style.opacity = 1 - collapse;                              // :213
    this.header.style.opacity = clamp01((amt - 0.1) / 0.4);             // :224
    if (grow <= 0.001) { this.field.style.display = "none"; return; }   // :233
    const fw = r.w - 2 * FIELD_MX, fh = FIELD_H * grow;
    const fs = this.field.style;
    fs.display = "block";
    fs.transform = `translate(${FIELD_MX}px, ${PAD_Y + HEADER_H + FIELD_GAP * grow}px)`;
    fs.width = fw + "px";
    fs.height = fh + "px";
    this.fieldBody.size(fw, fh);
    // Scroll to keep the tail (caret end) visible if the name overflows (:241-243).
    let shownText = this.text;
    const innerW = fw - 2 * INNER_PAD;
    while (est(shownText, FONT_PX) > innerW && [...shownText].length > 1) shownText = [...shownText].slice(1).join("");
    const tw = est(shownText, FONT_PX);
    setLabel(this.ftext, shownText, this.S);
    moveLabel(this.ftext, INNER_PAD, fh * 0.5 + FONT_PX * 0.35);       // :246
    const bar = Math.max(0, fh - 10);
    Object.assign(this.sel.style, {
      opacity: this.selectAll && shownText ? SEL_A * grow : 0,         // :248-252
      transform: `translate(${INNER_PAD - 2}px, 5px)`, width: tw + 4 + "px", height: bar + "px",
    });
    const blink = 0.55 + 0.45 * Math.cos(performance.now() * 0.006);   // :258
    Object.assign(this.caret.style, {
      opacity: this.selectAll ? 0 : grow * blink,                      // :259-262
      transform: `translate(${INNER_PAD + tw + 1}px, 5px)`, height: bar + "px",
    });
  }
  blink() {
    if (!(this.opened && this.renaming)) { this.blinkRaf = 0; return; }
    this.paint();
    this.blinkRaf = requestAnimationFrame(() => this.blink());
  }

  // ---- pointer: modal (ContextMenu.cpp:159-193), Artboard's gesture rules
  bindPointer() {
    const r = this.root;
    r.addEventListener("pointerdown", (e) => {
      if (!this.opened) return;
      e.preventDefault();
      e.stopPropagation();
      if (this.press) return;
      this.press = { id: e.pointerId, x0: e.clientX, y0: e.clientY, t: performance.now(), right: e.button === 2, drag: false,
                     s: Math.max(1e-6, deviceScale(this.layer) / (window.devicePixelRatio || 1)) };
      try { r.setPointerCapture(e.pointerId); } catch { /* synthetic */ }
    });
    r.addEventListener("pointermove", (e) => {
      if (!this.opened) return;
      const pr = this.press;
      if (pr && e.pointerId === pr.id && !pr.drag && Math.hypot(e.clientX - pr.x0, e.clientY - pr.y0) / pr.s > DRAG) pr.drag = true;
      if (pr && pr.drag) return;                                       // a drag: swallowed (:192)
      if (!this.renaming) this.setHover(this.itemAt(toLocal(this.layer, e)));   // :174
    });
    const end = (e, cancelled) => {
      const pr = this.press;
      if (!pr || e.pointerId !== pr.id) return;
      this.press = null;
      try { if (r.hasPointerCapture(e.pointerId)) r.releasePointerCapture(e.pointerId); } catch { /* gone */ }
      if (cancelled || pr.drag || !this.opened) return;
      e.stopPropagation();
      const p = toLocal(this.layer, e);
      if (pr.right) { this.click(p); return; }                         // RightClick (:176)
      const now = performance.now();
      if (now - pr.t >= LONG_MS) return;                               // a long press is no click
      const lc = this.lastClick;
      if (lc && now - lc.t <= DOUBLE_MS && Math.hypot(e.clientX - lc.x, e.clientY - lc.y) / pr.s <= DRAG) {
        this.lastClick = null;                                         // a DoubleClick: swallowed
        return;
      }
      this.lastClick = { t: now, x: e.clientX, y: e.clientY };
      this.click(p);
    };
    r.addEventListener("pointerup", (e) => end(e, false));
    r.addEventListener("pointercancel", (e) => end(e, true));
    r.addEventListener("pointerleave", () => { if (!this.press) this.setHover(-1); });
    r.addEventListener("click", (e) => e.stopPropagation());   // the click after a press the menu took is the menu's
    r.addEventListener("contextmenu", (e) => { e.preventDefault(); e.stopPropagation(); });
    r.addEventListener("wheel", (e) => { if (this.opened) { e.preventDefault(); e.stopPropagation(); } }, { passive: false });
  }
  click(p) {
    if (this.renaming) {                                               // :163-169
      if (inside(this.menuRect(), p)) {
        if (inside(this.fieldRect(), p)) { this.selectAll = false; this.paint(); }
        this.focusSink();
        return;
      }
      this.close();                                                    // click-away cancels
      return;
    }
    const i = this.itemAt(p);
    if (i < 0) { this.close(); return; }                               // :187
    const action = this.items[i].action, handle = this.handle();      // copy before closing (:181)
    this.close();
    if (action) { try { action(handle); } catch (err) { console.error(err); } }
  }

  // ---- keys: rename mode only (ContextMenu.cpp:130-157)
  startKeys() {
    if (!this.keysOn) { this.keysOn = true; window.addEventListener("keydown", this.onKey, true); }
    this.focusSink();
    if (!this.blinkRaf) this.blinkRaf = requestAnimationFrame(() => this.blink());
  }
  stopKeys() {
    if (this.keysOn) { this.keysOn = false; window.removeEventListener("keydown", this.onKey, true); }
    if (document.activeElement === this.sink) this.sink.blur();
  }
  focusSink() {
    // Under the field, so a phone keyboard opens there; never scrolls the page.
    const f = this.fieldRect();
    this.sink.style.transform = `translate(${f.x}px, ${f.y}px)`;
    try { this.sink.focus({ preventScroll: true }); } catch { /* detached */ }
  }
  key(e) {
    if (!(this.opened && this.renaming)) return;
    const k = e.key;
    e.stopPropagation();                                               // swallow every key (:156)
    if (e.isComposing || k === "Process" || k === "Unidentified" || k === "Dead") return;   // the sink's input types it
    if (k === "Enter") this.commit();
    else if (k === "Escape") this.close();                             // :154
    else if (k === "Backspace") this.backspace();
    else if ([...k].length === 1 && !e.ctrlKey && !e.metaKey) this.insert(k);
    else if (e.ctrlKey || e.metaKey) return;                           // the browser's own shortcuts
    e.preventDefault();
    this.paint();
  }
  bindSink() {
    const s = this.sink;
    s.addEventListener("input", (e) => {
      if (!(this.opened && this.renaming)) { s.value = ""; return; }
      if (e.inputType === "insertText" && e.data) this.insert(e.data);
      else if (e.inputType === "deleteContentBackward") this.backspace();
      else if (e.inputType === "insertLineBreak") this.commit();
      if (!e.isComposing) s.value = "";
      this.paint();
    });
    s.addEventListener("compositionend", (e) => {
      if (this.opened && this.renaming && e.data) this.insert(e.data);
      s.value = "";
      this.paint();
    });
  }
  insert(t) {
    if (this.selectAll) { this.text = ""; this.selectAll = false; }   // typing replaces the selection (:135)
    this.text += t;
  }
  backspace() {
    if (this.selectAll) { this.text = ""; this.selectAll = false; }   // :143
    else this.text = [...this.text].slice(0, -1).join("");
  }
  commit() {
    const name = this.text, cb = this.onRename;                        // :147-152
    this.close();
    if (cb && name) { try { cb(name); } catch (err) { console.error(err); } }
  }
}
