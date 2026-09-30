/*
 * Cosmo by arstro — dom.js: building views and binding them to the view-model (R-NTWB-7).
 *
 * `h("div.row.active", {onclick}, child...)` builds an element; the `bind*` helpers tie one
 * property of it to a signal expression through an effect, so a view is written once and then
 * follows the view-model on its own. `keyed()` reconciles a list by key (filmstrip cells, tree
 * rows, cards) and never rebuilds a row that is still there - rebuilding would restart its
 * hover and focus and, worse, snap it (R-G-1): rows that arrive fade/grow in and rows that
 * leave fade/shrink out before they are removed.
 *
 * Every binding registers its disposer on the element's scope (`own(el, dispose)`), and
 * `destroy(el)` runs them for a subtree, so a view that goes away stops listening.
 */
import { effect } from "./signal.js";

const SCOPES = new WeakMap();

export function own(el, dispose) {
  let list = SCOPES.get(el);
  if (!list) SCOPES.set(el, (list = []));
  list.push(dispose);
  return dispose;
}

export function destroy(el) {
  const walker = [el];
  while (walker.length) {
    const n = walker.pop();
    const list = SCOPES.get(n);
    if (list) { SCOPES.delete(n); for (const d of list) { try { d(); } catch (e) { console.error(e); } } }
    for (const c of n.children || []) walker.push(c);
  }
}

const SVGNS = "http://www.w3.org/2000/svg";
const SVG_TAGS = new Set(["svg", "path", "circle", "rect", "line", "polyline", "polygon", "g", "defs",
                          "linearGradient", "radialGradient", "stop", "clipPath", "ellipse", "text", "mask"]);

/** h("tag.class1.class2#id", props, ...children). props: class, style (object), attrs as
 *  plain keys, on<event> handlers, `ref(el)`. Children: nodes, strings, arrays, null. */
export function h(spec, props, ...children) {
  const m = /^([a-zA-Z][\w-]*)?((?:[.#][\w-]+)*)$/.exec(spec);
  const tag = (m && m[1]) || "div";
  const el = SVG_TAGS.has(tag) ? document.createElementNS(SVGNS, tag) : document.createElement(tag);
  if (m && m[2]) {
    for (const part of m[2].match(/[.#][\w-]+/g)) {
      if (part[0] === ".") el.classList.add(part.slice(1)); else el.id = part.slice(1);
    }
  }
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v === undefined || v === null || v === false) continue;
      if (k === "class") { for (const c of String(v).split(/\s+/)) if (c) el.classList.add(c); }
      else if (k === "style" && typeof v === "object") Object.assign(el.style, v);
      else if (k === "ref") v(el);
      else if (k.startsWith("on") && typeof v === "function") el.addEventListener(k.slice(2).toLowerCase(), v);
      else if (k === "text") el.textContent = v;
      else if (k === "html") el.innerHTML = v;
      else el.setAttribute(k, v === true ? "" : v);
    }
  }
  append(el, children);
  return el;
}

function append(el, children) {
  for (const c of children) {
    if (c === null || c === undefined || c === false) continue;
    if (Array.isArray(c)) append(el, c);
    else el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
}

/** Set a CSS custom property / style from a signal expression. */
export function bindStyle(el, prop, fn) {
  return own(el, effect(() => {
    const v = fn();
    if (prop.startsWith("--")) el.style.setProperty(prop, v); else el.style[prop] = v;
  }));
}
export function bindText(el, fn) { return own(el, effect(() => { const t = String(fn() ?? ""); if (el.textContent !== t) el.textContent = t; })); }
export function bindClass(el, cls, fn) { return own(el, effect(() => { el.classList.toggle(cls, !!fn()); })); }
export function bindAttr(el, attr, fn) {
  return own(el, effect(() => {
    const v = fn();
    if (v === false || v === null || v === undefined) el.removeAttribute(attr); else el.setAttribute(attr, v === true ? "" : v);
  }));
}
/** Run any effect for as long as `el` lives. */
export function bindEffect(el, fn) { return own(el, effect(fn)); }

/**
 * Keyed list: keep `parent`'s children in step with `items()` (an array), one child per key.
 *   create(item, index) -> element   (called once per key)
 *   update(el, item, index)          (called on every change, may be omitted)
 * Entering rows get class `enter` for one frame (CSS transitions them in); leaving rows get
 * `leave` and are removed when their transition ends (or after `leaveMs`), so nothing a user
 * can see appears or vanishes in one frame (R-G-1).
 */
export function keyed(parent, items, keyOf, create, update, { leaveMs = 220, animate = true } = {}) {
  const rows = new Map();                       // key -> element
  return own(parent, effect(() => {
    const list = items() || [];
    const seen = new Set();
    let prev = null;
    list.forEach((item, i) => {
      const key = keyOf(item, i);
      seen.add(key);
      let el = rows.get(key);
      if (!el) {
        el = create(item, i);
        el.__key = key;
        rows.set(key, el);
        if (animate && parent.isConnected) {
          el.classList.add("enter");
          requestAnimationFrame(() => requestAnimationFrame(() => el.classList.remove("enter")));
        }
      } else if (el.classList.contains("leave")) {
        el.classList.remove("leave");            // came back before it finished leaving
      }
      if (update) update(el, item, i);
      const want = prev ? prev.nextSibling : parent.firstChild;
      if (want !== el) parent.insertBefore(el, want);
      prev = el;
    });
    for (const [key, el] of rows) {
      if (seen.has(key)) continue;
      rows.delete(key);
      if (!animate || !parent.isConnected) { destroy(el); el.remove(); continue; }
      el.classList.add("leave");
      const done = () => { if (el.classList.contains("leave")) { destroy(el); el.remove(); } };
      el.addEventListener("transitionend", done, { once: true });
      setTimeout(done, leaveMs + 60);
    }
  }));
}

/** Pointer drag with capture: onStart(e) -> false cancels; onMove(e, dx, dy); onEnd(e, moved). */
export function drag(el, { onStart, onMove, onEnd, threshold = 0 }) {
  el.addEventListener("pointerdown", (e) => {
    if (e.button !== 0 && e.pointerType === "mouse") return;
    if (onStart && onStart(e) === false) return;
    const x0 = e.clientX, y0 = e.clientY;
    let moved = false;
    el.setPointerCapture(e.pointerId);
    const move = (ev) => {
      const dx = ev.clientX - x0, dy = ev.clientY - y0;
      if (!moved && Math.hypot(dx, dy) < threshold) return;
      moved = true;
      if (onMove) onMove(ev, dx, dy);
    };
    const up = (ev) => {
      el.removeEventListener("pointermove", move);
      el.removeEventListener("pointerup", up);
      el.removeEventListener("pointercancel", up);
      if (onEnd) onEnd(ev, moved);
    };
    el.addEventListener("pointermove", move);
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
  });
}

/** Human file size, as the home cards print it ("50 MB", "1.2 GB"). */
export function formatBytes(n) {
  if (!n) return "0 B";
  const u = ["B", "KB", "MB", "GB", "TB"];
  let i = 0;
  while (n >= 1024 && i < u.length - 1) { n /= 1024; i++; }
  return (i >= 3 || n < 10 ? n.toFixed(1).replace(/\.0$/, "") : Math.round(n)) + " " + u[i];
}
