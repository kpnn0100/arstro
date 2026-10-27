/*
 * Interstellar — main.js: boot one page = one view of the session (R-SVC-4, MVVM - NTWB-11).
 *
 *   NTWB bridge (/ntwb/ntwb.js) -> InterstellarSession (model/session.js: the model's proxy)
 *                               -> the view-model (vm/app.js: App.cpp's presentation state and intents)
 *                               -> the shell (shell.js: App.cpp's frame - scale root, screens, keys, layers)
 *                                    -> Home (home/home.js) | Loading (home/loading.js) | Edit (edit/screen.js
 *                                       and its twelve widget slots)
 *
 * The page is the native front end ported widget by widget (apps/interstellar/app/); every widget is
 * its own module, loaded into its box by kit/slot.js, so one that is missing or broken never takes the
 * page down. js/core/* and js/ui/* (and the CSS they read) are cosmo's, copied beside the page by
 * `interstellar-cc ntwb install` from web/BORROW - borrowed, not forked.
 *
 * Web-only, with no native counterpart: the connection line (the app process is Arstro Remote's, so
 * "starting" / "stopped" are this page's to say), and - on the fixture host only - the native shot's
 * view state applied to the view-model (window.__ntwbFixture.view), so a screenshot compares 1:1.
 */
import { InterstellarSession } from "./model/session.js";
import { createViewModel } from "./vm/app.js";
import { effect } from "./core/signal.js";
import { h } from "./core/dom.js";
import { mount } from "./shell.js";

const bridge = NTWB.connect();
const session = new InterstellarSession(bridge);
const vm = createViewModel(session);
const app = document.getElementById("app");
const shell = mount(app, vm, {});
window.interstellar = { vm, session, bridge, shell };   // for the browser's console and the page tests

// the connection, said while the app is not running (the shell has nothing to draw from yet)
const line = h("div.is-conn");
document.body.append(line);
effect(() => {
  const s = session.status.value;
  const on = s !== "running" || !vm.ready.value;
  line.classList.toggle("on", on);
  line.textContent = s === "running" ? "Waiting for Interstellar…" : s === "failed" ? "Interstellar stopped — see Arstro Remote's log"
    : s === "stopped" ? "Interstellar is not running" : s === "offline" ? "Connection lost — reconnecting…" : "Starting Interstellar on the machine…";
});
bridge.onError((e) => vm.say(String(e), true));

// the fixture host (tools/fixture_host.mjs): put this page's view where the native shot's was
function applyFixture(fx) {
  const v = fx && fx.view;
  if (!v) return;
  vm.view.fixture.value = v;
  if (typeof v.uiScale === "number" && v.uiScale > 0) vm.view.uiScale.value = Math.round(v.uiScale);
  if (typeof v.tabIndex === "number") vm.tab.value = v.tabIndex;
  if (v.grade) {
    if (typeof v.grade.deckView === "number") vm.view.deckView.value = ["sources", "stills", "nodes"][v.grade.deckView] || "sources";
    if (typeof v.grade.deckLevel === "string") vm.view.deckLevel.value = v.grade.deckLevel;
    if (typeof v.grade.scopeClipWarning === "boolean") vm.view.clipWarning.value = v.grade.scopeClipWarning;
  }
  if (v.monitor && v.monitor.edge > 0) vm.view.monitorEdge.value = v.monitor.edge;
}
if (window.__ntwbFixture) applyFixture(window.__ntwbFixture);
window.addEventListener("ntwb-fixture", (e) => applyFixture(e.detail));
