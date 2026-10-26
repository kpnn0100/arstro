/*
 * Interstellar — home.js: Home (the window's HomeScreen): the recent projects as cards, and a project
 * to start or open by its path ON THE MACHINE (the files are the service's, never the browser's).
 */
import { keyed, bindText, formatBytes } from "../core/dom.js";
import { h } from "./util.js";

export function home(vm) {
  const path = h("input.path.mono", { placeholder: "/path/on/this/machine/film.isp", spellcheck: "false" });
  const fps = h("input.small.mono", { type: "number", min: 1, max: 120, step: 1, value: 24, title: "frames a second (--fps)" });
  const res = h("input.small.mono", { value: "1920x1080", title: "the timeline's raster (--res WxH)", spellcheck: "false" });
  const go = (fn) => () => { const p = path.value.trim(); if (p) fn(p); };
  path.addEventListener("keydown", (e) => { if (e.key === "Enter") go(vm.open)(); });

  const grid = h("div.recents");
  keyed(grid, () => vm.recents.value, (r) => r.path, (r) => h("button.recent", { onclick: () => vm.open(r.path) },
    h("div.recent-name"), h("div.recent-meta.mono"), h("div.recent-path.mono")), (el, r) => {
    el.querySelector(".recent-name").textContent = r.name || r.path;
    el.querySelector(".recent-meta").textContent =
      `${r.sourceCount || 0} source${r.sourceCount === 1 ? "" : "s"}` + (r.sizeBytes ? ` · ${formatBytes(r.sizeBytes)}` : "");
    el.querySelector(".recent-path").textContent = r.path;
  });
  const empty = h("p.recents-empty");
  bindText(empty, () => (vm.recents.value.length ? "" : "No recent projects yet — start one above."));

  return h("div.home-inner",
    h("div.home-head", h("h1.wordmark-big", "interstellar"),
      h("p.home-sub", "The project, its rack and its media stay on this machine. This page draws them, shows the picture you ask for, and sends the same command lines the window does.")),
    h("div.home-actions", path,
      h("label.inline", fps, h("span", "fps")), h("label.inline", res),
      h("button.pill.primary", { onclick: () => { const p = path.value.trim(); if (p) vm.create(p, fps.value, res.value.trim()); }, title: "project new <path> --fps --res" }, "New project"),
      h("button.pill", { onclick: go(vm.open), title: "project open <path>" }, "Open")),
    h("h2", "Recent projects"), grid, empty);
}
