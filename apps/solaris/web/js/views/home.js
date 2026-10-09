/*
 * Solaris — home.js: Home (the window's HomeScreen): the recent songs as cards, and a song to start
 * or open by its path ON THE MACHINE (the files are the service's, never the browser's).
 */
import { keyed, bindText } from "../core/dom.js";
import { h } from "./util.js";

export function home(vm) {
  const path = h("input.path.mono", { placeholder: "/path/on/this/machine/song.slp", spellcheck: "false" });
  const go = (fn) => () => { const p = path.value.trim(); if (p) fn(p); };
  path.addEventListener("keydown", (e) => { if (e.key === "Enter") go(vm.open)(); });

  const grid = h("div.recents");
  keyed(grid, () => vm.recents.value, (r) => r.path, (r) => {
    const card = h("button.recent", { onclick: () => vm.open(r.path) },
      h("div.recent-name"), h("div.recent-meta.mono"), h("div.recent-path.mono"));
    return card;
  }, (el, r) => {
    el.querySelector(".recent-name").textContent = r.name || r.path;
    el.querySelector(".recent-meta").textContent = r.missing ? "missing" : `${r.bpm} bpm · ${Math.round(r.lengthBeats)} beats · ${r.strips} strips`;
    el.querySelector(".recent-path").textContent = r.path;
    el.classList.toggle("missing", !!r.missing);
    el.disabled = !!r.missing;
  });
  const empty = h("p.recents-empty", "No recent songs yet.");
  const recentsTitle = h("h2", "Recent songs");
  bindText(empty, () => (vm.recents.value.length ? "" : "No recent songs yet — start one above."));

  return h("div.home-inner",
    h("div.home-head", h("h1.wordmark-big", "solaris"),
      h("p.home-sub", "The song, its engine and its sound stay on this machine. This page draws the song and sends it the same command lines the window does.")),
    h("div.home-actions", path,
      h("button.pill.primary", { onclick: go(vm.create), title: "project new <path>" }, "New song"),
      h("button.pill", { onclick: go(vm.open), title: "project open <path>" }, "Open")),
    recentsTitle, grid, empty);
}
