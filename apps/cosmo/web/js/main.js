/*
 * Cosmo by arstro — main.js: boot one page = one view of the session (R-NTWB-7).
 *
 *   NTWB bridge (ntwb.js) -> CosmoSession (the model's proxy) -> this page's view-model
 *                                                              -> the shell for this screen
 *
 * The shell is chosen per page from the page's own window: the desktop shell (the App's
 * TopBar / LeftRail / CenterStage / RightColumn) when there is room for it and a fine pointer,
 * the touch shell (PhoneApp's one column, tray and tool bar) otherwise. A phone and a desktop
 * on one session therefore show different layouts of the same model. Crossing the breakpoint
 * cross-fades one shell into the other (R-G-1) - both exist while the fade runs.
 *
 * Shells are loaded as modules on demand and a shell that fails to load is reported, not
 * fatal: the page keeps showing whatever else still works.
 */
import { CosmoSession } from "./model/session.js";
import { createViewModel } from "./vm/editor.js";
import { effect } from "./core/signal.js";
import { h, destroy } from "./core/dom.js";

const app = document.getElementById("app");
const toasts = h("div.toasts");
document.body.append(toasts);

export function toast(text, err = false) {
  const t = h("div.toast" + (err ? ".err" : ""), { text });
  toasts.append(t);
  requestAnimationFrame(() => requestAnimationFrame(() => t.classList.add("on")));
  setTimeout(() => { t.classList.remove("on"); setTimeout(() => t.remove(), 260); }, err ? 5200 : 2600);
}

const bridge = NTWB.connect();
const session = new CosmoSession(bridge);
const vm = createViewModel(session, { toast });
window.cosmo = { vm, session, bridge };        // for the console and the page tests

// The desktop shell needs its minimum (584 x 466 logical px, App.cpp:744-759) and a pointer that
// can hit a 10 px row; anything smaller, or a coarse pointer on a small screen, gets the touch shell.
const desktopMq = matchMedia("(min-width: 584px) and (min-height: 466px) and ((pointer: fine) or (min-width: 1024px))");
function chooseLayout() { vm.view.layout.value = desktopMq.matches ? "desktop" : "touch"; }
desktopMq.addEventListener("change", chooseLayout);
chooseLayout();

const shells = {
  desktop: () => import("./desktop/shell.js"),
  touch: () => import("./touch/shell.js"),
};
let current = null;                              // {layout, el}

effect(() => {
  const layout = vm.view.layout.value;
  if (current && current.layout === layout) return;
  const prev = current;
  const el = h("div.shell.away", { "data-layout": layout });
  current = { layout, el };
  app.append(el);
  shells[layout]()
    .then((mod) => {
      if (current.el !== el) return;
      mod.mount(el, vm, { toast });
      requestAnimationFrame(() => requestAnimationFrame(() => el.classList.remove("away")));
    })
    .catch((e) => { console.error(e); toast("This layout failed to load: " + e.message, true); el.classList.remove("away"); });
  if (prev) {
    prev.el.classList.add("away");
    setTimeout(() => { destroy(prev.el); prev.el.remove(); }, 320);
  }
});

// Connection and rejection feedback that every shell shares.
effect(() => {
  const r = session.lastRejection.value;
  if (r && performance.now() - r.at < 1000) console.info("cosmo:", r.text);
});
bridge.onError((e) => toast(e, true));
