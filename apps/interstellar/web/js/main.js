/*
 * Interstellar — main.js: boot one page = one view of the session (R-SVC-4, MVVM).
 *
 *   NTWB bridge (/ntwb/ntwb.js) -> InterstellarSession (the model's proxy) -> this page's view-model
 *                                                                           -> Home | the edit page
 *
 * The edit page is the window's EditScreen in miniature: the top bar with the three tabs, the monitor
 * and its transport, the inspector of the tab beside it, and a deck below with the timeline and the
 * console. Home and the edit page cross-fade as the model's `screen` changes, the deck's tabs slide -
 * nothing changes in one frame (arstro.design.rule §1). On a phone the same pieces stack.
 *
 * `js/core/signal.js` and `js/core/dom.js` are cosmo's, copied beside this page by
 * `interstellar-cc ntwb install` (borrowed, not forked); the fonts likewise.
 */
import { InterstellarSession } from "./model/session.js";
import { createViewModel } from "./vm/edit.js";
import { effect } from "./core/signal.js";
import { bindClass } from "./core/dom.js";
import { h } from "./views/util.js";
import { topbar } from "./views/topbar.js";
import { home } from "./views/home.js";
import { monitor } from "./views/monitor.js";
import { timeline } from "./views/timeline.js";
import { inspector } from "./views/inspector.js";
import { cmdConsole } from "./views/console.js";

const toasts = h("div.toasts");
document.body.append(toasts);
const shown = new Map();     // text -> when: one refusal reaches us as the call's error, `command.rejected`
                             // and `error` - the same sentence is said once
export function toast(text, err = false) {
  const now = performance.now();
  if (now - (shown.get(text) || -1e9) < 1500) return;
  shown.set(text, now);
  const t = h("div.toast" + (err ? ".err" : ""), { text });
  toasts.append(t);
  requestAnimationFrame(() => requestAnimationFrame(() => t.classList.add("on")));
  setTimeout(() => { t.classList.remove("on"); setTimeout(() => t.remove(), 320); }, err ? 5200 : 3200);
}

const bridge = NTWB.connect();
const session = new InterstellarSession(bridge);
const vm = createViewModel(session, { toast });
window.interstellar = { vm, session, bridge };   // for the browser's console and the page tests

// the deck: Timeline | Console, a sliding highlight under the current tab (view state, never sent)
const decks = ["timeline", "console"];
const deckBtns = decks.map((d) => h("button.tab", { onclick: () => { vm.view.deck.value = d; } }, d === "timeline" ? "Timeline" : "Console"));
const highlight = h("i.tab-highlight");
const deckTabs = h("div.tabs", highlight, ...deckBtns);
const timelinePage = h("div.deck-page", timeline(vm));
const consolePage = h("div.deck-page", cmdConsole(vm));
effect(() => {
  const i = decks.indexOf(vm.view.deck.value);
  deckBtns.forEach((b, k) => b.classList.toggle("on", k === i));
  highlight.style.transform = `translateX(${i * 100}%)`;
  timelinePage.classList.toggle("on", i === 0);
  consolePage.classList.toggle("on", i === 1);
});

const homePage = h("section.page.home-page.scroll", home(vm));
const editPage = h("section.page.edit-page",
  h("div.edit-top", monitor(vm), inspector(vm)),
  h("div.deck", deckTabs, h("div.deck-pages", timelinePage, consolePage)));
bindClass(homePage, "on", () => vm.has.value && vm.screen.value === "home");
bindClass(editPage, "on", () => vm.screen.value === "edit");

const waiting = h("div.waiting", h("span.spinner"), h("span.waiting-text"));
effect(() => {
  const s = session.status.value;
  const loading = vm.screen.value === "loading";
  waiting.classList.toggle("on", s !== "running" || !vm.has.value || loading);
  waiting.lastChild.textContent = s === "running" ? (loading ? "Opening the project…" : "Waiting for the project…")
    : s === "failed" ? "Interstellar stopped — see Arstro Remote's log" : "Starting Interstellar on the machine…";
});

document.getElementById("app").append(topbar(vm), h("main.pages", homePage, editPage, waiting));

// the window's keys, where they do not fight a text field: Space play/pause, J/K/L shuttle,
// ←/→ a frame, B split (blade), Delete / Shift+Delete delete / ripple
document.addEventListener("keydown", (e) => {
  if (vm.screen.peek() !== "edit" || e.ctrlKey || e.metaKey || e.altKey) return;
  const t = e.target;
  if (t && (t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA")) return;
  const k = e.key.toLowerCase();
  if (k === " ") vm.play();
  else if (k === "j") vm.shuttle("back");
  else if (k === "k") vm.shuttle("stop");
  else if (k === "l") vm.shuttle("forward");
  else if (k === "arrowleft") vm.step(-1);
  else if (k === "arrowright") vm.step(1);
  else if (k === "b") vm.split();
  else if (k === "delete" || k === "backspace") vm.remove(e.shiftKey);
  else return;
  e.preventDefault();
});

// a refusal says why, in the service's words (it is also model.lastError and the console's red line)
effect(() => {
  const r = session.lastRejection.value;
  if (r && vm.view.deck.peek() !== "console") toast(r.text, true);
});
bridge.onError((e) => toast(String(e), true));
