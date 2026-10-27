/*
 * Solaris — windows/layer.js: the WindowLayer and its FloatWindow frames (widgets/FloatWindow.{h,cpp}, R-WIN-1..5).
 *
 * The layer covers the song view under the song bar (its box {0, top, W, H − top}, pointer-events none: a click
 * between windows falls through). It draws vm.view.windows, front LAST, one FloatWindow each:
 *   a frame — popover fill, 1 px border, radius::control; a 26 px title bar (secondary) with the title (11 px Roboto
 *   Medium, ellipsized before ×) and × (19.5 square at W − 6.5 − 19.5, 3.25; a hover wash eased motion::hover);
 *   the content at {1, 26, W − 2, H − 27}, mounted with ctx.slot by kind (screen.js's table);
 *   the corner grip (13 px, three strokes) resizes it, never below 260 × 156 nor past the layer (R-WIN-5).
 * Pressed anywhere it comes to the front (vm.raiseWindow); dragged by its title bar it follows the pointer exactly,
 * kept inside the layer (y ≤ H − 26). Opened it fades in motion::modalOpen, closed out motion::modalClose, both
 * EaseOutCubic; while closing it takes no input. A closed window is kept, hidden, and reopens where it was.
 * A new window cascades from the top right (WindowLayer::place): w, h clamped to the layer − 13 (≥ 160 × 120),
 * x = clamp(W − w − 19.5 − 26k), y = clamp(13 + 26k), k = opened count mod 6.
 * The front roll's window carries `data-front-roll`; vm.view.frontRoll is written here.
 */
import { signal, effect, untracked } from "../core/signal.js";
import { h, own } from "../core/dom.js";
import { Anim, Ease, frames, motion } from "../kit/anim.js";
import { Cursor, holdCursor } from "../kit/cursor.js";
import { useCss } from "../kit/css.js";
import { FONT, ellipsize } from "../kit/text.js";

const TITLE_H = 26, GRIP = 13, MIN_W = 260, MIN_H = 156;
const PAD_X = 9.75;
const KINDS = {
  device: ["device/panel.js", "DevicePanel"],
  roll: ["piano/roll.js", "PianoRoll"],
  automation: ["automation/panel.js", "AutomationPanel"],
  loudness: ["mixer/loudness.js", "LoudnessPanel"],
  video: ["video/panel.js", "VideoPanel"],
};
const same = (a, b) => a.x === b.x && a.y === b.y && a.w === b.w && a.h === b.h;
const SVG = "http://www.w3.org/2000/svg";

function sizeOf(vm, w) {
  if (w.kind === "device") {
    const m = vm.model.peek() || {};
    const all = [...(m.strips || []).flatMap((s) => s.devices || []), ...(m.masterDevices || [])];
    const d = all.find((x) => x.id === w.id);
    return d && d.instrument && (d.analogFace || d.face) ? [820, 580] : [390, 520];
  }
  if (w.kind === "roll") return [720, 520];
  if (w.kind === "automation") return [422.5, 390];
  if (w.kind === "loudness") return [390, 357.5];
  return [448, 299];
}

function titleOf(vm, w) {
  const m = vm.model.value || {};
  if (w.kind === "device") {
    for (const s of m.strips || []) for (const d of s.devices || []) if (d.id === w.id) return d.label + " — " + s.name;
    for (const d of m.masterDevices || []) if (d.id === w.id) return d.label + " — " + vm.tr("Master");
    return w.id;
  }
  if (w.kind === "roll") {
    const p = (m.patterns || []).find((x) => x.id === w.id);
    return p && p.name ? p.name : w.id;
  }
  if (w.kind === "automation") {
    const a = (m.automations || []).find((x) => x.id === w.id);
    return vm.fillIn(vm.tr("Automation — {1}"), a && a.name ? a.name : w.id);
  }
  if (w.kind === "loudness") return vm.tr("Loudness — Master");
  const v = m.video || {};
  if (!v.id) return vm.tr("Video");
  const src = v.src || "";
  return vm.fillIn(vm.tr("Video — {1}"), src.slice(src.lastIndexOf("/") + 1));
}

export function mount(host, vm, ctx) {
  useCss("windows/layer.css");
  const M = motion();
  host.classList.add("sl-wl");
  const wins = new Map(); // key -> state
  let cascade = 0;
  const layerSize = () => ({ w: ctx.box.peek().w, h: ctx.box.peek().h });

  function placeNew(st) {
    const { w: W, h: H } = layerSize();
    const ww = Math.min(st.w0, Math.max(160, W - 13)), hh = Math.min(st.h0, Math.max(120, H - 13));
    const off = 26 * (cascade++ % 6);
    st.w = ww; st.h = hh;
    st.x = Math.max(0, Math.min(W - ww - 19.5 - off, Math.max(0, W - ww)));
    st.y = Math.max(0, Math.min(13 + off, Math.max(0, H - hh)));
  }

  function layoutWin(st) {
    const el = st.el;
    el.style.transform = `translate(${st.x}px, ${st.y}px)`;
    el.style.width = st.w + "px";
    el.style.height = st.h + "px";
    const b = ctx.box.peek();
    st.box.value = { x: b.x + st.x + 1, y: b.y + st.y + TITLE_H, w: Math.max(0, st.w - 2), h: Math.max(0, st.h - TITLE_H - 1) };
    drawTitle(st);
  }

  function drawTitle(st) {
    const maxW = Math.max(0, st.w - 6.5 - 19.5 - 2 * PAD_X);
    const t = ellipsize(st.title || "", maxW, 11, FONT.medium);
    if (st.titleEl.textContent !== t) st.titleEl.textContent = t;
  }

  function advance(now) {
    let moving = false;
    for (const [key, st] of wins) {
      if (st.wanted !== st.applied) {
        st.appear.animateTo(st.wanted ? 1 : 0, st.wanted ? M.modalOpen : M.modalClose, Ease.EaseOutCubic, now);
        st.applied = st.wanted;
      }
      moving = st.appear.update(now) || moving;
      st.el.style.opacity = String(st.appear.value);
      st.el.style.visibility = st.appear.value <= 0.001 && !st.wanted ? "hidden" : "visible";
      if (!st.wanted && st.appear.value <= 0.001 && st.slot && st.kind === "device" && !st.keep) { /* kept, hidden */ }
      void key;
    }
    return moving;
  }
  const kick = () => frames(advance);

  function create(w) {
    const [w0, h0] = sizeOf(vm, w);
    const titleEl = h("div.sl-fw-title-text");
    const xSvg = document.createElementNS(SVG, "svg");
    xSvg.setAttribute("viewBox", "0 0 19.5 19.5");
    xSvg.setAttribute("width", "19.5");
    xSvg.setAttribute("height", "19.5");
    for (const [a, b, c, d] of [[6, 6, 13.5, 13.5], [13.5, 6, 6, 13.5]]) {
      const l = document.createElementNS(SVG, "line");
      l.setAttribute("x1", a); l.setAttribute("y1", b); l.setAttribute("x2", c); l.setAttribute("y2", d);
      xSvg.append(l);
    }
    const closeEl = h("div.sl-fw-close", { title: "" }, xSvg);
    const bar = h("div.sl-fw-bar", null, titleEl, closeEl);
    const body = h("div.sl-fw-body");
    const grip = h("div.sl-fw-grip");
    const gSvg = document.createElementNS(SVG, "svg");
    gSvg.setAttribute("viewBox", `0 0 ${GRIP} ${GRIP}`);
    gSvg.setAttribute("width", GRIP); gSvg.setAttribute("height", GRIP);
    for (let k = 1; k <= 3; k++) {
      const d = 3.25 * k, l = document.createElementNS(SVG, "line");
      l.setAttribute("x1", GRIP - d); l.setAttribute("y1", GRIP - 1); l.setAttribute("x2", GRIP - 1); l.setAttribute("y2", GRIP - d);
      gSvg.append(l);
    }
    grip.append(gSvg);
    grip.style.cursor = Cursor.ResizeDiag;
    const el = h("div.sl-fw", null, bar, body, grip);
    el.dataset.key = w.key;
    const st = { key: w.key, kind: w.kind, id: w.id, el, titleEl, body, w0, h0, x: 0, y: 0, w: w0, h: h0,
                 appear: new Anim(0), wanted: true, applied: false, box: signal({ x: 0, y: 0, w: 0, h: 0 }, same), title: "", slot: null };
    placeNew(st);
    host.append(el);

    // touched anywhere: to the front; the title bar drags it (direct manipulation)
    el.addEventListener("pointerdown", (e) => {
      if (!st.wanted) return;
      vm.raiseWindow(st.key);
      if (e.button !== 0) return;
      const p = ctx.screenPoint(e), b = ctx.box.peek();
      const lx = p.x - b.x - st.x, ly = p.y - b.y - st.y;
      if (ly >= TITLE_H || closeEl.contains(e.target)) return;
      e.preventDefault();
      const grabX = lx, grabY = ly;
      el.setPointerCapture(e.pointerId);
      const move = (ev) => {
        const q = ctx.screenPoint(ev), bb = ctx.box.peek();
        const W = bb.w, H = bb.h;
        st.x = Math.max(0, Math.min(q.x - bb.x - grabX, Math.max(0, W - st.w)));
        st.y = Math.max(0, Math.min(q.y - bb.y - grabY, Math.max(0, H - TITLE_H)));
        layoutWin(st);
      };
      const up = () => { el.removeEventListener("pointermove", move); el.removeEventListener("pointerup", up); el.removeEventListener("pointercancel", up); };
      el.addEventListener("pointermove", move);
      el.addEventListener("pointerup", up);
      el.addEventListener("pointercancel", up);
    }, true);
    closeEl.addEventListener("click", (e) => { e.stopPropagation(); if (st.wanted) vm.closeWindow(st.key); });

    // R-WIN-5: the corner resizes it
    grip.addEventListener("pointerdown", (e) => {
      if (!st.wanted || e.button !== 0) return;
      e.preventDefault(); e.stopPropagation();
      vm.raiseWindow(st.key);
      grip.setPointerCapture(e.pointerId);
      const g0 = ctx.screenPoint(e), w0s = st.w, h0s = st.h;
      const release = holdCursor(Cursor.ResizeDiag);
      grip.classList.add("held");
      const move = (ev) => {
        const q = ctx.screenPoint(ev), bb = ctx.box.peek();
        const maxW = Math.max(MIN_W, bb.w - st.x), maxH = Math.max(MIN_H, bb.h - st.y);
        st.w = Math.max(MIN_W, Math.min(w0s + q.x - g0.x, maxW));
        st.h = Math.max(MIN_H, Math.min(h0s + q.y - g0.y, maxH));
        layoutWin(st);
      };
      const up = () => { release(); grip.classList.remove("held"); grip.removeEventListener("pointermove", move); grip.removeEventListener("pointerup", up); grip.removeEventListener("pointercancel", up); };
      grip.addEventListener("pointermove", move);
      grip.addEventListener("pointerup", up);
      grip.addEventListener("pointercancel", up);
    });

    const [path, name] = KINDS[w.kind] || [];
    if (path) {
      st.slot = ctx.slot(body, path, name, {
        box: st.box,
        window: { key: w.key, kind: w.kind, id: w.id, clip: w.clip || "", close: () => vm.closeWindow(st.key), raise: () => vm.raiseWindow(st.key) },
      });
    }
    wins.set(w.key, st);
    layoutWin(st);
    return st;
  }

  // the list: open / close / stacking
  own(host, effect(() => {
    const list = vm.view.windows.value || [];
    untracked(() => {
      const keys = new Set(list.map((w) => w.key));
      list.forEach((w, i) => {
        let st = wins.get(w.key);
        if (!st) st = create(w);
        if (!st.wanted) { st.wanted = true; }
        st.clip = w.clip;
        st.el.style.zIndex = String(i + 1);
        st.el.classList.add("open");
      });
      for (const [k, st] of wins) if (!keys.has(k) && st.wanted) { st.wanted = false; st.el.classList.remove("open"); }
      // the front roll (R-ROLL-8)
      let front = "";
      const last = list[list.length - 1];
      if (last && last.kind === "roll") front = last.id;
      for (const [, st] of wins) {
        if (st.kind === "roll" && st.id === front && st.wanted) st.el.setAttribute("data-front-roll", "");
        else st.el.removeAttribute("data-front-roll");
      }
      if (vm.view.frontRoll.peek() !== front) vm.view.frontRoll.value = front;
      kick();
    });
  }));

  // the titles, from the model (a rename from anywhere retitles it)
  own(host, effect(() => {
    const list = vm.view.windows.value || [];
    vm.lang && vm.lang.value;
    for (const w of list) {
      const st = untracked(() => wins.get(w.key));
      if (!st) continue;
      st.title = titleOf(vm, w);
      drawTitle(st);
    }
  }));

  // the layer resized: keep every window inside (WindowLayer::layout)
  own(host, effect(() => {
    const b = ctx.box.value;
    untracked(() => {
      for (const [, st] of wins) {
        st.x = Math.max(0, Math.min(st.x, Math.max(0, b.w - st.w)));
        st.y = Math.max(0, Math.min(st.y, Math.max(0, b.h - TITLE_H)));
        layoutWin(st);
      }
    });
  }));

  own(host, () => { for (const [, st] of wins) if (st.slot && st.slot.dispose) st.slot.dispose(); wins.clear(); });
}
