/*
 * Cosmo by arstro — desktop/topbar.js: the editor's top bar - wordmark, menu strip, project
 * name, the right-slot readout and the rail toggle.
 *
 * Ports widgets/TopBar.cpp, widgets/MenuStrip.cpp, widgets/IconButton.cpp and the menus of
 * App::buildMenus (App.cpp:471-497). Every number is the native's: the wordmark's "." sits at
 * 9.75 + estimate("cosmo", 13) = 48.75 (the visible gap in the renders is that estimate), the
 * strip starts at 69.55, titles are estimate + 16.25 wide with 1 px between them, a dropdown is
 * max(152, estimate + 22) wide with 24 px rows, the title highlight grows / slides / shrinks
 * over 190 ms EaseOutCubic and a dropdown opened from closed reveals top-down over 160 ms.
 * Centred and right-aligned texts are placed by the native's byte estimate, not measured.
 *
 * Menu state is this page's (vm.view.menu = the open menu's index). A press anywhere outside
 * the bar and the open dropdown closes it and then goes on to whatever is under it
 * (App.cpp:822-829); a click on a title toggles it, on another title switches (hovering does
 * not); no keyboard, separators or disabled items - exactly like the native.
 *
 * Deviations: none known. The group-name rename opens the context menu's rename mode at
 * (W - 236, 31.25) like App.cpp:73-77.
 */
import { computed, untracked } from "../core/signal.js";
import { h, bindEffect, bindText, bindClass, bindStyle } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { icon } from "../ui/icons.js";
import { text, setText, est, bytes, gestures, TOPBAR_H } from "./editor.js";

/** App::buildMenus - labels verbatim (their spaces are part of the label). */
export const MENUS = [
  { title: "File", items: [["Home", "home"], ["Open...", "open"], ["Save        (Ctrl+S)", "save"],
                           ["Save As...  (Ctrl+Shift+S)", "saveAs"], ["Export...", "exportDialog"]] },
  { title: "Settings", items: [["Engine Settings...", "settings"], ["Reset Workspace", "resetWorkspace"]] },
  { title: "Develop", items: [["Copy Settings", "copySettings"], ["Paste to Selected", "pasteSelected"],
                              ["Paste to All Images", "pasteAll"], ["Group Selection", "group"],
                              ["Ungroup Selection", "ungroup"]] },
  { title: "History", items: [["Undo   (Ctrl+Z)", "undo"], ["Redo   (Ctrl+Y)", "redo"],
                              ["Show History Tree...", "history"]] },
  { title: "Preset", items: [["Save Preset...", "presetSave"], ["Import Preset...", "presetImport"]] },
];

// (No top-level use of editor.js's toolkit: the modules import each other in a cycle.)
const stripX = () => 9.75 + est("cosmo.", 13) + 3.25 + 9.75;   // 69.55 (TopBar::layout)
const stripY = () => (TOPBAR_H - 21) / 2;                        // 4.125
const titleW = (i) => est(MENUS[i].title, 11) + 2 * 8.125;
const titleX = (i) => { let x = 0; for (let k = 0; k < i; k++) x += titleW(k) + 1; return x; };
const dropW = (i) => Math.max(152, ...MENUS[i].items.map(([l]) => est(l, 11) + 22));

/** "Group: <name>", cut with "…" while its estimate exceeds 220 (TopBar.cpp:20-27). */
function groupLabel(name) {
  let label = "Group: " + name;
  if (est(label, 11) <= 220) return label;
  const enc = new TextEncoder(), dec = new TextDecoder();
  let b = enc.encode(label);
  while (b.length > 8 && (b.length + 3) * 11 * 0.6 > 220) b = b.slice(0, b.length - 1);
  return dec.decode(b).replace(/�+$/, "") + "…";
}

export function mountTopBar(el, vm, ed) {
  el.append(h("div.tb-line"));

  // ---- wordmark (TopBar::onPaint / wordmarkRect)
  const wm = h("div.tb-wm", {},
    text("cosmo", { x: 9.75 - 5.75, y: TOPBAR_H / 2 + 13 * 0.35, size: 13, font: "semibold", ls: -0.39, cls: "fg" }),
    text(".", { x: 9.75 - 5.75 + est("cosmo", 13), y: TOPBAR_H / 2 + 13 * 0.35, size: 13, font: "semibold", ls: -0.39, cls: "accent" }));
  gestures(wm, { click: () => ed.act.requestHome() });

  // ---- project name, centred by the estimate
  const name = text("", { y: TOPBAR_H / 2 + 12 * 0.35, size: 12, font: "medium", cls: "fg tb-name" });
  bindEffect(name, () => {
    const n = vm.project.value.name;
    setText(name, n);
    name.style.left = `calc((var(--W) - ${bytes(n) * 12 * 0.6}px) / 2)`;
  });

  // ---- right slot: "Group: <name>" (editing a group) or "<file> (i/n)" (an image)
  const slot = computed(() => {
    const g = vm.editGroup.value;
    if (g >= 0) {
      const n = vm.nodeById.value.get(g);
      if (n) return { group: g, name: n.name };
    }
    const s = vm.currentSlot.value;
    if (s >= 0) {
      const n = vm.nodes.value.find((x) => x.slot === s && x.kind === "image");
      return { file: n ? n.name : "", i: s + 1, n: vm.imageCount.value };
    }
    return null;
  }, (a, b) => JSON.stringify(a) === JSON.stringify(b));

  const fileName = text("", { y: TOPBAR_H / 2 + 11 * 0.35, size: 11, font: "mono", cls: "muted" });
  const fileSuffix = text("", { y: TOPBAR_H / 2 + 11 * 0.35, size: 11, font: "mono", cls: "fg30" });
  const file = h("div.tb-file", {}, fileName, fileSuffix);
  const groupText = text("", { x: 6, y: TOPBAR_H / 2 + 11 * 0.35 - 5.625, size: 11, cls: "fg" });
  const groupBox = h("div.tb-group", {}, groupText);
  bindEffect(el, () => {
    const s = slot.value;
    file.classList.toggle("on", !!(s && !s.group && s.n > 0));
    groupBox.classList.toggle("on", !!(s && s.group !== undefined));
    if (s && s.group !== undefined) {
      const label = groupLabel(s.name);
      const w = est(label, 11) + 12;
      setText(groupText, label);
      groupBox.style.width = w + "px";
      groupBox.style.left = `calc(var(--W) - ${40 + w}px)`;
    } else if (s && s.n > 0) {
      const suffix = ` (${s.i}/${s.n})`;
      const nw = est(s.file, 11), sw = est(suffix, 11);
      setText(fileName, s.file);
      setText(fileSuffix, suffix);
      fileName.style.left = `calc(var(--W) - ${40 + nw + sw}px)`;
      fileSuffix.style.left = `calc(var(--W) - ${40 + sw}px)`;
    }
  });
  gestures(groupBox, {
    click: () => {
      const s = slot.peek();
      if (!s || s.group === undefined) return;
      const W = ed.size.peek().w;
      ed.openRename(s.group, s.name, W - 236, TOPBAR_H + 2);
    },
  });

  // ---- rail toggle (IconButton: panelLeft in the whole 20.5 box, stroke 1.4)
  const toggle = h("button.tb-rail", { "aria-label": "Toggle presets" }, icon("panelLeft", 20.5, 1.4));
  bindClass(toggle, "active", () => ed.railOpen.value);
  gestures(toggle, { click: () => { vm.view.railWanted.value = !vm.view.railWanted.peek(); } });

  // ---- menu strip
  const strip = mountMenuStrip(vm, ed, el);

  el.append(wm, strip, name, file, groupBox, toggle);
}

function mountMenuStrip(vm, ed, bar) {
  const strip = h("div.ms", { style: { left: stripX() + "px", top: stripY() + "px", width: titleX(MENUS.length - 1) + titleW(MENUS.length - 1) + "px" } });
  const hi = h("div.ms-hi");
  strip.append(hi);
  const hiC = new Tween(0, () => paintHi());
  const hiW = new Tween(0, () => paintHi());
  function paintHi() {
    const w = hiW.value;
    hi.style.opacity = w > 0.5 ? "1" : "0";
    hi.style.left = (hiC.value - w / 2) + "px";
    hi.style.width = Math.max(0, w) + "px";
  }

  const tabs = MENUS.map((m, i) => {
    const tab = h("div.ms-tab", { style: { left: titleX(i) + "px", width: titleW(i) + "px" } },
      text(m.title, { x: (titleW(i) - est(m.title, 11)) / 2, y: 21 / 2 + 11 * 0.35, size: 11, font: "medium" }));
    bindClass(tab, "open", () => vm.view.menu.value === i);
    strip.append(tab);
    return tab;
  });

  // The dropdown: drawn at its revealed height, items clipped top-down.
  const drop = h("div.ms-drop");
  const list = h("div.ms-list");
  drop.append(list);
  strip.append(drop);
  let shownMenu = -1;
  const reveal = new Tween(0, () => paintDrop());
  function paintDrop() {
    if (shownMenu < 0) { drop.classList.remove("on"); return; }
    const full = MENUS[shownMenu].items.length * 24 + 8;
    const r = Math.min(1, Math.max(0, reveal.value));
    const hh = full * r;
    drop.classList.toggle("on", hh >= 1);
    drop.style.height = hh + "px";
    list.style.setProperty("--reveal", r);
  }
  function fillDrop(i) {
    shownMenu = i;
    list.replaceChildren();
    if (i < 0) return;
    drop.style.left = titleX(i) + "px";
    drop.style.width = dropW(i) + "px";
    MENUS[i].items.forEach(([label, action], k) => {
      const row = h("div.ms-item", { style: { top: 4 + 24 * k + "px" } },
        text(label, { x: 11, y: 12 + 11 * 0.35, size: 11, cls: "fg" }));
      row.dataset.action = action;
      list.append(row);
    });
  }

  let prev = null;
  bindEffect(strip, () => {
    const open = vm.view.menu.value;
    untracked(() => {
      const fromClosed = prev === null && open !== null;
      if (open !== null) {
        const cx = titleX(open) + titleW(open) / 2;
        if (fromClosed) hiC.set(cx); else hiC.to(cx, 190, Ease.EaseOutCubic);
        hiW.to(titleW(open), 190, Ease.EaseOutCubic);
        fillDrop(open);
        if (fromClosed) { reveal.set(0); reveal.to(1, 160, Ease.EaseOutCubic); } else reveal.set(1);
      } else {
        hiW.to(0, 190, Ease.EaseOutCubic);
        fillDrop(-1);
        reveal.set(0);
      }
      bar.classList.toggle("raised", open !== null);   // App.cpp:469 - raise the TopBar
      prev = open;
    });
  });

  const close = () => { vm.view.menu.value = null; };
  // A press inside the bar / dropdown is the strip's; anywhere else it closes the menu first
  // and then carries on to whatever it landed on (App.cpp:822-829).
  const onDownAnywhere = (e) => {
    if (vm.view.menu.peek() === null) return;
    const inBar = strip.contains(e.target) && (e.target.closest(".ms-tab, .ms-drop") || inStripBox(e));
    if (!inBar) close();
  };
  function inStripBox(e) {
    const r = strip.getBoundingClientRect();
    return e.clientX >= r.left && e.clientX <= r.right && e.clientY >= r.top && e.clientY <= r.bottom;
  }
  window.addEventListener("pointerdown", onDownAnywhere, true);
  bindEffect(strip, () => () => window.removeEventListener("pointerdown", onDownAnywhere, true));

  gestures(strip, {
    click: (e, p, at) => {
      const open = vm.view.menu.peek();
      const item = at && at.closest(".ms-item");
      if (open !== null && item && drop.contains(item)) {
        close();
        const fn = ed.act[item.dataset.action];
        if (fn) Promise.resolve().then(fn).catch((err) => console.error(err));
        return;
      }
      const tab = at && at.closest(".ms-tab");
      if (tab) {
        const i = tabs.indexOf(tab);
        vm.view.menu.value = open === i ? null : i;
        return;
      }
      close();
    },
  });
  return strip;
}
