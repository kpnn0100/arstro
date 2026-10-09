/*
 * Solaris — main.js: boot one page = one view of the session (R-SVC-7, MVVM).
 *
 *   NTWB bridge (/ntwb/ntwb.js) -> SolarisSession (the model's proxy) -> this page's view-model
 *                                                                      -> Home | the song
 *
 * The song page is the window's in miniature: the song bar on top, the lanes, and a deck below
 * with the mixer and the console. Home and the song cross-fade as the model's `screen` changes,
 * the deck's tabs slide - nothing changes in one frame (arstro.design.rule §1).
 *
 * `js/core/signal.js` and `js/core/dom.js` are cosmo's, copied beside this page by
 * `solaris-cc ntwb install` (borrowed, not forked); the fonts likewise.
 */
import { SolarisSession } from "./model/session.js";
import { createViewModel } from "./vm/song.js";
import { effect } from "./core/signal.js";
import { bindClass } from "./core/dom.js";
import { h } from "./views/util.js";
import { topbar } from "./views/topbar.js";
import { home } from "./views/home.js";
import { lanes } from "./views/lanes.js";
import { mixer } from "./views/mixer.js";
import { cmdConsole } from "./views/console.js";

const toasts = h("div.toasts");
document.body.append(toasts);
export function toast(text, err = false) {
  const t = h("div.toast" + (err ? ".err" : ""), { text });
  toasts.append(t);
  requestAnimationFrame(() => requestAnimationFrame(() => t.classList.add("on")));
  setTimeout(() => { t.classList.remove("on"); setTimeout(() => t.remove(), 320); }, err ? 5200 : 3200);
}

const bridge = NTWB.connect();
const session = new SolarisSession(bridge);
const vm = createViewModel(session, { toast });
window.solaris = { vm, session, bridge };     // for the console of the browser and the page tests

// the deck: Mixer | Console, a sliding highlight under the current tab (view state, never sent)
const tabs = ["mixer", "console"];
const tabBtns = tabs.map((t) => h("button.tab", { onclick: () => { vm.view.tab.value = t; } }, t === "mixer" ? "Mixer" : "Console"));
const highlight = h("i.tab-highlight");
const tabBar = h("div.tabs", highlight, ...tabBtns);
const mixerPage = h("div.deck-page", mixer(vm));
const consolePage = h("div.deck-page", cmdConsole(vm));
effect(() => {
  const i = tabs.indexOf(vm.view.tab.value);
  tabBtns.forEach((b, k) => b.classList.toggle("on", k === i));
  highlight.style.transform = `translateX(${i * 100}%)`;
  mixerPage.classList.toggle("on", i === 0);
  consolePage.classList.toggle("on", i === 1);
});

const homePage = h("section.page.home-page.scroll", home(vm));
const songPage = h("section.page.song-page", lanes(vm), h("div.deck", tabBar, h("div.deck-pages", mixerPage, consolePage)));
bindClass(homePage, "on", () => vm.has.value && vm.screen.value !== "project");
bindClass(songPage, "on", () => vm.screen.value === "project");

const waiting = h("div.waiting", h("span.spinner"), h("span.waiting-text"));
effect(() => {
  const s = session.status.value;
  waiting.classList.toggle("on", s !== "running" || !vm.has.value);
  waiting.lastChild.textContent = s === "running" ? "Waiting for the song…" : s === "failed" ? "Solaris stopped — see Arstro Remote's log" : "Starting Solaris on the machine…";
});

document.getElementById("app").append(topbar(vm), h("main.pages", homePage, songPage, waiting));

// a refusal says why, in the service's words (it is also model.lastError and the console's red line)
effect(() => {
  const r = session.lastRejection.value;
  if (r && vm.view.tab.peek() !== "console") toast(r.text, true);
});
bridge.onError((e) => toast(String(e), true));
