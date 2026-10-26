/*
 * Interstellar — topbar.js: the window's EditTopBar: the wordmark, the project, the three tabs over
 * one monitor (Cut | Grade | Deliver - the PAGE's tab, like the window's: never sent), undo / redo /
 * save / close, and which session this page shows.
 */
import { bindText, bindClass, bindEffect } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { h, icon, ICONS } from "./util.js";

const TABS = [["cut", "Cut"], ["grade", "Grade"], ["deliver", "Deliver"]];

export function topbar(vm) {
  const name = h("span.project-name");
  bindText(name, () => (vm.project.value ? vm.project.value.name : ""));
  const dirty = h("i.dirty", { title: "unsaved changes" });
  bindClass(dirty, "on", () => !!(vm.project.value && vm.project.value.dirty));
  const tl = h("span.timeline-name.mono");
  bindText(tl, () => (vm.timeline.value ? vm.timeline.value.name : ""));

  const tabBtns = TABS.map(([k, label]) => h("button.seg", { onclick: () => { vm.view.tab.value = k; } }, label));
  const highlight = h("i.seg-highlight");
  const tabs = h("div.segmented", highlight, ...tabBtns);
  effect(() => {
    const i = TABS.findIndex(([k]) => k === vm.view.tab.value);
    tabBtns.forEach((b, j) => b.classList.toggle("on", i === j));
    highlight.style.transform = `translateX(${i * 100}%)`;
  });

  const undo = h("button.tbtn", { onclick: () => vm.undo() }, icon(ICONS.undo));
  const redo = h("button.tbtn", { onclick: () => vm.redo() }, icon(ICONS.redo));
  bindEffect(undo, () => { const p = vm.project.value; undo.disabled = !(p && p.canUndo); undo.title = p && p.canUndo ? "Undo " + (p.undo || "") : "Nothing to undo"; });
  bindEffect(redo, () => { const p = vm.project.value; redo.disabled = !(p && p.canRedo); redo.title = p && p.canRedo ? "Redo " + (p.redo || "") : "Nothing to redo"; });
  const save = h("button.tbtn", { title: "Save (project save)", onclick: () => vm.save() }, icon(ICONS.save));
  const close = h("button.tbtn", { title: "Close the project (project close)", onclick: () => vm.close() }, icon(ICONS.close));

  const presence = h("span.presence", h("i"), h("span"));
  bindClass(presence, "live", () => vm.session.status.value === "running");
  bindText(presence.lastChild, () => {
    const s = vm.session.status.value;
    const p = vm.session.presence.value;
    if (s !== "running") return s;
    return (p.session || "main") + (p.clients > 1 ? ` · ${p.clients} viewing` : "");
  });

  const projectOnly = h("div.project-controls",
    h("div.project-title", name, dirty, tl),
    tabs,
    h("div.actions", undo, redo, save, close));
  bindClass(projectOnly, "on", () => vm.screen.value === "edit");

  return h("header.topbar",
    h("div.brand", h("img", { src: "icon.svg", alt: "" }), h("span.wordmark", "interstellar")),
    projectOnly,
    presence);
}
