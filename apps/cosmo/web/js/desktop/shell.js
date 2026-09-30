/*
 * Cosmo by arstro — desktop/shell.js: the desktop App's frame - one UI-scale root, the
 * editor-wide keyboard and the gesture bracket, and the screens inside it.
 *
 * Ports App's root transform and scale tween (App.h:87-124, App.cpp:768-808) and its input
 * entry (App::pointer, App.cpp:810-860):
 *
 *   - Everything the shell draws is in LOGICAL px at 100 % and lives in one root scaled by
 *     s = uiScale / 100 (transform-origin 0 0). The logical size is max(584, W / s) x max(466,
 *     H / s): a window smaller than the minimum is cropped, never reflowed below it. The scale
 *     is this page's (vm.view.uiScale - a 4K desktop and a laptop on one session each want their
 *     own), snapped to the offered 75 / 90 / 100 / 125 / 150 / 175 / 200, and a change EASES the
 *     drawn scale over 260 ms EaseOutCubic with the layout recomputed every frame from the eased
 *     value (the startup apply does not animate).
 *   - Every press in the editor sends `gesture on` and its release `gesture off` (R-PREVIEW-1:
 *     a `set` during a gesture renders a coarse level) - at the root, for any press, so every
 *     draggable is covered by construction.
 *   - keys.js: the editor's shortcuts.
 *   - Screens: C's ./screens.js mountScreens(root, vm, ctx, {mountEditor}) runs Home / Loading /
 *     Editor with the native transitions; while it does not exist the editor is mounted as is.
 *
 * ctx handed down (adds to main.js's {toast}): size - signal {w, h, s} of the logical box and
 * the drawn scale; scale() - the drawn scale now.
 */
import { signal } from "../core/signal.js";
import { h, bindEffect, own } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { UI_SCALES } from "../vm/editor.js";
import { mountEditor as mountEditorView, setDeviceScale } from "./editor.js";
import { installKeys } from "./keys.js";

const MIN_W = 584, MIN_H = 466;                 // App::minLogicalWidth / minLogicalHeight at 100 %
const snap = (v) => UI_SCALES.reduce((best, s) => (Math.abs(s - v) < Math.abs(best - v) ? s : best), 100);

export function mount(el, vm, ctx = {}) {
  el.classList.add("desk");
  const root = h("div.desk-root");
  el.append(root);

  // ---- the UI-scale root
  const size = signal({ w: MIN_W, h: MIN_H, s: 1 }, (a, b) => a.w === b.w && a.h === b.h && a.s === b.s);
  const scale = new Tween(1, () => apply());
  function apply() {
    const s = scale.value;
    const w = Math.max(MIN_W, el.clientWidth / s), hh = Math.max(MIN_H, el.clientHeight / s);
    root.style.width = w + "px";
    root.style.height = hh + "px";
    root.style.transform = s === 1 ? "none" : `scale(${s})`;
    size.value = { w, h: hh, s };
    if (!scale.animating) setDeviceScale(s * (window.devicePixelRatio || 1));   // labels' hinted spacing
  }
  let first = true;
  bindEffect(root, () => {
    const s = snap(vm.view.uiScale.value) / 100;
    if (first) { first = false; scale.set(s); } else scale.to(s, 260, Ease.EaseOutCubic);
  });
  const ro = new ResizeObserver(apply);
  ro.observe(el);
  own(root, () => ro.disconnect());

  // ---- gesture on / off around every press in the editor (App.cpp:852-859)
  const presses = new Set();
  const down = (e) => {
    if (vm.screen.peek() !== "editor" || presses.has(e.pointerId)) return;
    presses.add(e.pointerId);
    vm.gestureOn();
  };
  const up = (e) => { if (presses.delete(e.pointerId)) vm.gestureOff(); };
  const lost = () => { for (const id of [...presses]) up({ pointerId: id }); };
  root.addEventListener("pointerdown", down, true);
  window.addEventListener("pointerup", up, true);
  window.addEventListener("pointercancel", up, true);
  window.addEventListener("blur", lost);
  own(root, () => {
    window.removeEventListener("pointerup", up, true);
    window.removeEventListener("pointercancel", up, true);
    window.removeEventListener("blur", lost);
    lost();
  });
  // Ctrl+wheel must never zoom the browser page over the shell (it is the photo's zoom).
  root.addEventListener("wheel", (e) => { if (e.ctrlKey) e.preventDefault(); }, { passive: false });

  // ---- the editor, once, wherever the screens put it
  let editor = null;
  const dctx = { ...ctx, size, scale: () => scale.value };
  function mountEditor(host, vmArg = vm, c = dctx) {
    if (editor) return editor;
    const live = c || dctx;
    if (!live.size) live.size = size;
    if (!live.scale) live.scale = dctx.scale;
    editor = mountEditorView(host, vmArg, live);
    return editor;
  }
  own(root, installKeys(vm, { editor: () => editor && editor.ed, ctx: dctx }));

  import("./screens.js")
    .then((m) => {
      try { m.mountScreens(root, vm, dctx, { mountEditor }); }
      catch (e) { console.error(e); if (!editor) mountEditor(root); }
    })
    .catch((e) => {
      console.info("cosmo: screens not mounted, showing the editor:", e.message);
      mountEditor(root);
    });
}
