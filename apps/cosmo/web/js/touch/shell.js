/*
 * Cosmo by arstro — touch/shell.js: the touch (phone) shell - PhoneApp in the browser.
 *
 * `mount(el, vm, ctx)` builds the touch counterpart of the desktop App over the SAME view-model
 * API: one core (cosmo-cc on the board), this page's own view-model, this view (R-TOUCH-1). It
 * owns no model state: the screen is the model's (loadActive -> Loading, an open project ->
 * Editor, else Home; looking at Home while the session edits is this page's own choice,
 * vm.view.homeWanted), and every change leaves through a vm intent - the same command lines the
 * desktop sends - so an edit on a phone shows on a desktop page of the session and back.
 *
 * Ports PhoneApp::setScreen / syncFromModel / render (touch/PhoneApp.cpp:1320-1470): screens
 * switch under a #141414 overlay fading 1 -> 0 over 320 ms EaseOutCubic; the modal layer (sheets,
 * drawer, file browser, fullscreen curve) sits above every screen.
 *
 * Deliberate deviations: in a window wider than tall the editor uses the two-pane landscape
 * layout (R-TOUCH-3) instead of the native's interim 430 px portrait letterbox; long-press never
 * opens the browser's context menu.
 */
import { h, bindEffect, own } from "../core/dom.js";
import { signal, untracked } from "../core/signal.js";
import { reducedMotion } from "../core/motion.js";
import { EASE } from "./tk.js";
import { mountHome, mountLoading } from "./home.js";
import { mountEditor } from "./editor.js";
import { createSheets } from "./sheets.js";

export function mount(el, vm, ctx) {
  const root = h("div.touch");
  el.append(root);
  root.addEventListener("contextmenu", (e) => e.preventDefault());

  const W = signal(el.clientWidth || window.innerWidth), H = signal(el.clientHeight || window.innerHeight);
  const ro = new ResizeObserver(() => { W.value = root.clientWidth; H.value = root.clientHeight; });
  ro.observe(root);
  own(root, () => ro.disconnect());

  const screens = h("div.t-screens");
  const layer = h("div.t-layer-modal");
  const fade = h("div.t-fadeover");
  root.append(screens, fade, layer);
  const sheets = createSheets(layer, vm, ctx);

  // The project name while a load runs: the model's name is empty until it is decoded; this
  // page knows it when it asked, the `project.opening` event says it otherwise (§2.2).
  const loadingName = signal("");
  const offOpening = vm.on("project.opening", (d) => { if (d && d.text) loadingName.value = d.text; });
  own(root, offOpening);
  const env = {
    W, H, sheets, loadingName,
    opening(path, name) { loadingName.value = name || (path.split("/").pop() || "").replace(/\.[^.]+$/, ""); },
  };

  const views = {};
  const make = {
    home: () => mountHome(screens, vm, ctx, env).el,
    loading: () => mountLoading(screens, vm, ctx, env).el,
    editor: () => mountEditor(screens, vm, ctx, env).el,
  };
  let shown = null;
  bindEffect(root, () => {
    const s = vm.screen.value;
    untracked(() => {
      const want = s === "connecting" ? null : s;
      if (want === shown) return;
      if (want && !views[want]) views[want] = make[want]();
      for (const [k, v] of Object.entries(views)) v.classList.toggle("on", k === want);
      if (shown !== null || want) crossFade();
      shown = want;
      if (want !== "home") document.activeElement && document.activeElement.blur && document.activeElement.blur();
    });
  });
  /** PhoneApp::setScreen: a #141414 overlay at α 1 fading to 0 over 320 ms EaseOutCubic. */
  function crossFade() {
    if (reducedMotion()) return;
    fade.getAnimations().forEach((a) => a.cancel());
    fade.animate([{ opacity: 1 }, { opacity: 0 }], { duration: 320, easing: EASE.out });
  }
  return root;
}
