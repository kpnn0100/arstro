/*
 * Cosmo by arstro — ui/menustrip.js: cosmo_v2::MenuStrip (widgets/MenuStrip.{h,cpp}) for any web
 * face - the top bar's row of menu titles and their dropdowns, at most one open at a time.
 * Generic: the menus are data. Cosmo's editor keeps its own port inside desktop/topbar.js; this is
 * that logic without the editor. Imports only ../core/*.js and ./controls.js; loads its stylesheet
 * (css/ui-kit.css) itself.
 *
 * API
 *   menuStrip({menus: [{title, items: [{label, action()}]}], height = 21, onOpenChanged(i),
 *              hoverSwitch = false}) -> strip
 *   strip = {
 *     el                    the strip's box (contentWidth x height); the caller places it (absolute)
 *     width, height         its size - width is contentWidth(), what a top bar lays itself out by
 *     contentWidth()        all titles end to end, 1 px apart (MenuStrip.cpp:53-58)
 *     setItems(i, items)    replace menu i's items; closes the strip if i is the open one (:46-51)
 *     relabel(i, title, labels) -> bool   the same menu in other words (MenuStrip.h:46-51)
 *     addMenu({title, items})             (:27)
 *     setOpen(i)            open menu i (-1 closes) - exactly as a click on its title would
 *     close(), openIndex()  (:88, MenuStrip.h:53)
 *     onOpenChanged         settable; called with the open index or -1 on every change (:85)
 *     menuCount(), menu(i), titleRect(i), itemRect(menu, item), pointInActiveArea({x, y})
 *                           read-only geometry in the strip's own px (MenuStrip.h:55-63)
 *     destroy()             drop the window listener (an open strip holds one)
 *   }
 *
 * The open dropdown is a child of `el` hanging below it (top = height + 3): the bar holding the
 * strip must not clip it and should raise itself above its siblings while one is open - as the
 * apps' bars do from onOpenChanged (Interstellar EditTopBar.cpp:43, Cosmo App.cpp:469).
 *
 * Geometry (MenuStrip.cpp): titles Roboto Medium 11, est(title, 11) + 2 * 8.125 wide (:29), 1 px
 * apart (:31-36), centred by the estimate, baseline height / 2 + 11 * 0.35 (:194-199); the height
 * is 21 unless the bar sets its own (:25 - Solaris's SongBar uses its full 29.25). A dropdown
 * sits at (titleX, height + 3), max(152, est(label, 11) + 2 * 11) wide, rows * 24 + 8 high (:60-67),
 * popover fill + 1 px border, radius::control (:215-216); rows from y 4, labels Roboto 11
 * foreground at x 11 (:221-229); the hovered row washes palette::hoverWash, inset 3 px (:223-226).
 *
 * Colours and motion: the open title is white on a primary highlight; another title is
 * mutedForeground, lifting toward white as it is hovered (HoverFade, 120 ms, :190-197). The
 * highlight EXPANDS from zero width centred on the title when a menu opens from closed, SLIDES
 * (centre + width) to the new title when switching, SHRINKS in place on close - 190 ms
 * EaseOutCubic (:92-105). A dropdown opened from closed grows top-down over 160 ms EaseOutCubic,
 * its rows clipped to the revealed height and the hover wash times the reveal (:107-111, :211-226);
 * a switch shows the new one whole; closing removes it at once (:82) while the highlight shrinks.
 *
 * Behaviour (:149-177): a click on an item of the open dropdown closes the strip and then runs the
 * item; on a title it toggles that menu (another title switches to it); anywhere else in the strip
 * it closes. A press anywhere outside the strip and its dropdown closes the open menu and then
 * goes on to whatever it landed on (Cosmo App.cpp:822-829, Solaris App.cpp:615-619). Moves track
 * the hovered title and row (:151-155); a double click is not a click (GestureRecognizer); no keys.
 * Hovering another title while one is open does NOT switch in the native - `hoverSwitch: true`
 * opts into that (a web addition, off by default).
 *
 * Deviation: the open title's colour eases to and from white (the native flips it in one frame).
 */
import { Tween, Ease } from "../core/motion.js";
import { h } from "../core/dom.js";
import { gestures, toLocal } from "./controls.js";

// ------------------------------------------------------------------ the native's numbers
const FONT_PX = 11, PAD_X = 8.125, GAP = 1, ITEM_H = 24, ITEM_FONT_PX = 11, DROP_PAD_X = 11;   // MenuStrip.cpp:15-20
const GROW_MS = 190, EXPAND_MS = 160;                                                          // MenuStrip.cpp:21-22
const HEIGHT = 21;                                                                             // MenuStrip.cpp:25
const DROP_MIN_W = 152, DROP_GAP = 3, DROP_PAD_Y = 4;                                          // MenuStrip.cpp:43, :63-66
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
// drawRoundedRect as SVG: Chrome pixel-snaps a CSS box's background and outline, Cairo does not -
// an SVG rect keeps the fractional edges and strokes its 1 px border centred on them (Shapes.cpp:170-175).
function body(cls) {
  const s = document.createElementNS(SVGNS, "svg");
  s.setAttribute("class", "uk-body " + cls);
  const r = document.createElementNS(SVGNS, "rect");
  s.append(r);
  s.size = (w, hh, x = 0) => {
    w = Math.max(0, w); hh = Math.max(0, hh);
    s.setAttribute("width", w); s.setAttribute("height", hh);
    s.style.transform = x ? `translateX(${x}px)` : "";
    r.setAttribute("width", w); r.setAttribute("height", hh);
    const k = Math.min(2, w / 2, hh / 2);                            // radius::control, clamped (Shapes.cpp:28-31)
    r.setAttribute("rx", k); r.setAttribute("ry", k);
  };
  s.size(0, 0);
  return s;
}
function deviceScale(el) {
  const w = el.offsetWidth;
  return (w > 0 ? el.getBoundingClientRect().width / w : 1) * (window.devicePixelRatio || 1);
}

// ------------------------------------------------------------------ MenuStrip
const norm = (m) => ({ title: String(m.title ?? ""), items: (m.items || []).map((it) => ({ label: String(it.label ?? ""), action: it.action })) });

export function menuStrip({ menus = [], height = HEIGHT, onOpenChanged = null, hoverSwitch = false } = {}) {
  const M = menus.map(norm);
  const H = +height || HEIGHT;
  let open = -1, shown = -1, titleHover = -1, itemHover = -1, S = 1;

  const el = h("div.uk-ms", { role: "menubar" });
  const hi = body("uk-ms-hi");
  const tabs = h("div.uk-ms-tabs");
  const drop = h("div.uk-ms-drop", { role: "menu" });
  const dropBody = body("uk-ms-body");                               // popover + border (:215-216)
  const clip = h("div.uk-ms-clip");                                  // the rows, clipped to the revealed height (:217-218)
  const list = h("div.uk-ms-list");
  clip.append(list);
  drop.append(dropBody, clip);
  el.append(hi, tabs, drop);
  el.style.height = H + "px";

  // ---- geometry (MenuStrip.cpp:29-67, :124-145)
  const titleW = (i) => est(M[i].title, FONT_PX) + 2 * PAD_X;
  const titleX = (i) => { let x = 0; for (let k = 0; k < i; k++) x += titleW(k) + GAP; return x; };
  const contentWidth = () => { let w = 0; for (let i = 0; i < M.length; i++) w += titleW(i) + (i ? GAP : 0); return w; };
  const titleRect = (i) => ({ x: titleX(i), y: 0, w: titleW(i), h: H });
  const dropRect = (i) => {
    let w = DROP_MIN_W;
    for (const it of M[i].items) w = Math.max(w, est(it.label, ITEM_FONT_PX) + 2 * DROP_PAD_X);
    return { x: titleX(i), y: H + DROP_GAP, w, h: M[i].items.length * ITEM_H + 8 };
  };
  const itemRect = (m, i) => { const d = dropRect(m); return { x: d.x, y: d.y + DROP_PAD_Y + i * ITEM_H, w: d.w, h: ITEM_H }; };
  const titleAt = (p) => {
    if (p.y < 0 || p.y > H) return -1;
    for (let i = 0; i < M.length; i++) if (p.x >= titleX(i) && p.x < titleX(i) + titleW(i)) return i;
    return -1;
  };
  const itemAt = (m, p) => {
    if (m < 0 || !M[m].items.length) return -1;
    const d = dropRect(m);
    if (!inside(d, p)) return -1;
    const i = Math.trunc((p.y - (d.y + DROP_PAD_Y)) / ITEM_H);   // C++ (int)
    return i >= 0 && i < M[m].items.length ? i : -1;
  };
  const pointInActiveArea = (p) =>
    (p.x >= 0 && p.x <= contentWidth() && p.y >= 0 && p.y <= H) || (open >= 0 && M[open].items.length > 0 && inside(dropRect(open), p));

  // ---- titles (onPaint :179-201)
  function layoutTitles() {
    S = deviceScale(el.isConnected ? el : document.documentElement);
    el.style.width = contentWidth() + "px";
    tabs.replaceChildren(...M.map((m, i) => {
      const w = titleW(i);
      const tab = h("div.uk-ms-tab", { role: "menuitem", "aria-haspopup": "true", style: { width: w + "px", height: H + "px", transform: `translateX(${titleX(i)}px)` } },
        label(m.title, (w - est(m.title, FONT_PX)) * 0.5, H * 0.5 + FONT_PX * 0.35, FONT_PX, "medium", "uk-ms-title", S));
      tab.classList.toggle("open", i === open);
      tab.classList.toggle("hover", i === titleHover);
      return tab;
    }));
  }
  function setTitleHover(i) {
    if (i === titleHover) return;
    const t = tabs.children;
    if (t[titleHover]) t[titleHover].classList.remove("hover");
    titleHover = i;
    if (t[i]) t[i].classList.add("hover");
  }

  // ---- the highlight: expand / slide / shrink (advance :92-105)
  const hiC = new Tween(0, paintHi), hiW = new Tween(0, paintHi);
  function paintHi() {
    const w = hiW.value;
    hi.style.visibility = w > 0.5 ? "visible" : "hidden";   // :184
    hi.size(w, H, hiC.value - w * 0.5);
  }

  // ---- the dropdown (onOverlay :203-232)
  const reveal = new Tween(0, paintDrop);
  function paintDrop() {
    if (shown < 0 || !M[shown].items.length) { drop.style.visibility = "hidden"; return; }
    const d = dropRect(shown), r = clamp01(reveal.value), revH = d.h * r;
    drop.style.visibility = revH < 1 ? "hidden" : "visible";   // :213
    dropBody.size(d.w, revH);
    clip.style.height = revH + "px";
    drop.style.setProperty("--reveal", r);
  }
  function fillDrop(i) {
    shown = i;
    itemHover = -1;
    list.replaceChildren();
    if (i >= 0) {
      S = deviceScale(el.isConnected ? el : document.documentElement);
      const d = dropRect(i);
      drop.style.transform = `translate(${d.x}px, ${d.y}px)`;
      drop.style.width = d.w + "px";
      M[i].items.forEach((it, k) => list.append(
        h("div.uk-ms-item", { role: "menuitem", style: { top: DROP_PAD_Y + k * ITEM_H + "px" } },
          label(it.label, DROP_PAD_X, ITEM_H * 0.5 + ITEM_FONT_PX * 0.35, ITEM_FONT_PX, "sans", "uk-ms-label", S))));
    }
    paintDrop();
  }
  function setItemHover(i) {
    if (i === itemHover) return;
    const rows = list.children;
    if (rows[itemHover]) rows[itemHover].classList.remove("hover");
    itemHover = i;
    if (rows[i]) rows[i].classList.add("hover");
  }

  // ---- open state (setOpen :69-86)
  function setOpen(idx) {
    idx = Number.isInteger(idx) && idx >= 0 && idx < M.length ? idx : -1;
    if (idx === open) return;
    const fromClosed = open < 0 && idx >= 0;
    open = idx;
    if (open >= 0) {
      const cx = titleX(open) + titleW(open) * 0.5;
      if (fromClosed) hiC.set(cx);                                 // expand from the tab: centre pinned
      else hiC.to(cx, GROW_MS, Ease.EaseOutCubic);                 // switch: slide the centre
      hiW.to(titleW(open), GROW_MS, Ease.EaseOutCubic);
      fillDrop(open);
      if (fromClosed) { reveal.set(0); reveal.to(1, EXPAND_MS, Ease.EaseOutCubic); }   // grow the dropdown open
      else reveal.set(1);                                          // switch: keep it open, swap contents
      armOutside();
    } else {
      hiW.to(0, GROW_MS, Ease.EaseOutCubic);                       // close: shrink in place
      fillDrop(-1);
      reveal.set(0);                                               // closed: the next open grows fresh
      disarmOutside();
    }
    [...tabs.children].forEach((t, i) => t.classList.toggle("open", i === open));
    el.classList.toggle("open", open >= 0);
    if (api.onOpenChanged) { try { api.onOpenChanged(open); } catch (e) { console.error(e); } }
  }

  // ---- a press outside closes, then carries on (Cosmo App.cpp:822-829)
  let armed = false;
  const onPressAnywhere = (e) => { if (open >= 0 && !el.contains(e.target)) setOpen(-1); };
  function armOutside() { if (!armed) { armed = true; window.addEventListener("pointerdown", onPressAnywhere, true); } }
  function disarmOutside() { if (armed) { armed = false; window.removeEventListener("pointerdown", onPressAnywhere, true); } }

  // ---- pointer (handleGesture :149-177)
  el.addEventListener("pointermove", (e) => {
    const p = toLocal(el, e);
    const t = titleAt(p);
    setTitleHover(t);
    setItemHover(open >= 0 ? itemAt(open, p) : -1);
    if (hoverSwitch && open >= 0 && t >= 0 && t !== open) setOpen(t);
  });
  el.addEventListener("pointerleave", () => { setTitleHover(-1); setItemHover(-1); });   // :114
  gestures(el, {
    click: (p) => {
      if (open >= 0 && M[open].items.length) {
        const it = itemAt(open, p);
        if (it >= 0) {
          const action = M[open].items[it].action;                 // copy before closing
          setOpen(-1);
          if (action) { try { action(); } catch (err) { console.error(err); } }
          return;
        }
      }
      const t = titleAt(p);
      if (t >= 0) { setOpen(open === t ? -1 : t); return; }
      setOpen(-1);
    },
  });

  const api = {
    el,
    get width() { return contentWidth(); },
    height: H,
    contentWidth,
    onOpenChanged,
    setOpen,
    close() { setOpen(-1); },
    openIndex() { return open; },
    setItems(i, items) {
      if (!(i >= 0 && i < M.length)) return;
      if (open === i) setOpen(-1);
      M[i].items = norm({ items }).items;
    },
    relabel(i, title, labelsNow) {
      if (!(i >= 0 && i < M.length) || !Array.isArray(labelsNow) || labelsNow.length !== M[i].items.length) return false;
      M[i].title = String(title ?? "");
      labelsNow.forEach((l, k) => { M[i].items[k].label = String(l ?? ""); });
      layoutTitles();
      if (open >= 0) {                                             // the titles moved: the highlight follows at once
        hiC.set(titleX(open) + titleW(open) * 0.5);
        hiW.set(titleW(open));
        if (open === i) fillDrop(open); else { const d = dropRect(open); drop.style.transform = `translate(${d.x}px, ${d.y}px)`; }
      }
      return true;
    },
    addMenu(m) { M.push(norm(m)); layoutTitles(); },
    menuCount() { return M.length; },
    menu(i) { return M[i]; },
    titleRect,
    itemRect,
    pointInActiveArea,
    destroy() { disarmOutside(); },
  };
  layoutTitles();
  paintHi();
  paintDrop();
  // The labels' hinting needs the scale the strip is drawn at: measure again once it is placed.
  requestAnimationFrame(() => { if (el.isConnected && Math.abs(deviceScale(el) - S) > 1e-3) layoutTitles(); });
  return api;
}
