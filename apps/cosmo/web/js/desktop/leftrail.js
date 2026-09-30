/*
 * Cosmo by arstro — desktop/leftrail.js: the collapsible preset rail and its tree.
 *
 * Ports widgets/LeftRail.cpp and widgets/PresetTree.cpp. The rail is 196 wide, eased to 0 and
 * back by the editor (its content is CLIPPED, not scaled - the tree's rows are as wide as the
 * rail), filled leftRailBg, with the "PRESETS" header over a hairline at 24.7 and the tree from
 * 27.95 down. Rows are 20 px: indent 6.5 at depth 0 and 19.5 + 13 (d - 1) below, a 9 px
 * chevron (stroke 1.2) on folders, labels at indent + 14.625 on baseline y + 13.85; hover wash
 * whiteAlpha(0.06) (HoverFade 120 ms); the selected preset's row primary @ 0.10, faded in over
 * 140 ms EaseOutCubic each time the selection changes, its label in the accent.
 *
 * Data: vm.presets = [{path (relPath, what `preset apply` takes), name (file stem), folder
 * (relPath of its folder, "" at the top)}] - the core's PresetLibrary scan. Folders come from
 * those paths; each level is sorted folders first, then by name (PresetLibrary.cpp:10-48). The
 * expanded set and the selected preset are this page's view state; expanded folders are kept
 * by relPath across rebuilds. Click a folder = toggle it; double-click a preset = apply it
 * (and select it once the service took it); right click does nothing (App never wires it).
 * Without the core's preset list (older cosmo-cc), the rail shows only its header, as the
 * native does with an empty preset folder.
 *
 * Deviations: rows that appear on expand fade in and rows below slide to their new place
 * (R-G-1 - the native rebuilds the rows in one frame). The wheel scrolls the tree by the
 * native's raw delta - ONE px per notch (App.cpp:890, a native quirk the spec keeps).
 */
import { signal, computed } from "../core/signal.js";
import { h, bindEffect, keyed } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { icon } from "../ui/icons.js";
import { text, gestures, local, wheelDelta } from "./editor.js";

const ROW_H = 20;
const HEADER_H = 24.7;
const indentFor = (d) => (d <= 0 ? 6.5 : 19.5 + 13 * (d - 1));
const byName = (a, b) => (a < b ? -1 : a > b ? 1 : 0);

/** The PresetLibrary tree from the flat list: [{name, relPath, folder, kids}]. */
function buildTree(presets) {
  const root = { kids: new Map(), leaves: [] };
  for (const p of presets) {
    let lvl = root, rel = "";
    for (const part of (p.folder || "").split("/").filter(Boolean)) {
      rel = rel ? rel + "/" + part : part;
      if (!lvl.kids.has(part)) lvl.kids.set(part, { name: part, relPath: rel, kids: new Map(), leaves: [] });
      lvl = lvl.kids.get(part);
    }
    lvl.leaves.push({ name: p.name, relPath: p.path });
  }
  const level = (l) => [
    ...[...l.kids.values()].sort((a, b) => byName(a.name, b.name)).map((f) => ({ folder: true, name: f.name, relPath: f.relPath, kids: level(f) })),
    ...l.leaves.slice().sort((a, b) => byName(a.name, b.name)).map((x) => ({ folder: false, name: x.name, relPath: x.relPath })),
  ];
  return level(root);
}

export function mountLeftRail(el, vm, ed) {
  const expanded = signal(new Set());
  const selected = signal("");

  el.append(
    text("PRESETS", { x: 9.75, y: 15.5, size: 9, font: "semibold", ls: 1.17, cls: "muted" }),
    h("div.lr-line"));                             // at HEADER_H (desktop.css)
  const tree = h("div.lr-tree", { style: { top: HEADER_H + 3.25 + "px" } });
  const rowsEl = h("div.lr-rows");
  tree.append(rowsEl);
  el.append(tree);

  const roots = computed(() => buildTree(vm.presets.value || []));
  const rows = computed(() => {
    const out = [];
    const ex = expanded.value;
    const walk = (list, depth) => {
      for (const n of list) {
        const open = n.folder && ex.has(n.relPath);
        out.push({ key: (n.folder ? "d:" : "p:") + n.relPath, folder: n.folder, name: n.name, relPath: n.relPath, depth, open });
        if (open) walk(n.kids, depth + 1);
      }
    };
    walk(roots.value, 0);
    return out;
  });

  keyed(rowsEl, () => rows.value, (r) => r.key, (r) => {
    const row = h("div.lr-row" + (r.folder ? ".folder" : ""));
    const ind = indentFor(r.depth);
    if (r.folder) {
      const chev = h("span.lr-chev", { style: { left: ind + 4.875 - 4.5 + "px" } });
      row.append(chev);
    }
    row.append(text(r.name, { x: ind + 9.75 + 4.875, y: ROW_H / 2 + 11 * 0.35, size: 11, font: r.folder ? "medium" : "sans" }));
    return row;
  }, (row, r, i) => {
    row.style.transform = `translateY(${i * ROW_H}px)`;
    row.dataset.index = i;
    if (r.folder) {
      const chev = row.querySelector(".lr-chev");
      const want = r.open ? "chevronDown" : "chevronRight";
      if (chev.dataset.glyph !== want) { chev.dataset.glyph = want; chev.replaceChildren(icon(want, 9, 1.2)); }
    }
  }, { leaveMs: 120 });

  // Selection wash: restarts from 0 on every change (PresetTree::advance).
  bindEffect(rowsEl, () => {
    const sel = selected.value;
    rows.value;                                        // re-mark after a rebuild
    for (const row of rowsEl.children) {
      const on = row.__key === "p:" + sel;
      if (row.classList.contains("sel") !== on) {
        row.classList.toggle("sel", on);
        if (on) { row.classList.remove("sel-in"); void row.offsetWidth; row.classList.add("sel-in"); }
      }
    }
  });

  // Eased scroll (180 ms EaseOutCubic), target clamped to the content.
  let target = 0;
  const scroll = new Tween(0, (v) => { rowsEl.style.transform = `translateY(${-v}px)`; });
  const clampTarget = () => {
    const max = Math.max(0, rows.peek().length * ROW_H - tree.offsetHeight);
    target = Math.min(max, Math.max(0, target));
  };
  bindEffect(tree, () => { rows.value; clampTarget(); scroll.to(target, 180, Ease.EaseOutCubic); });
  el.addEventListener("wheel", (e) => {
    if (e.ctrlKey) return;
    e.preventDefault();
    target -= wheelDelta(e);                            // raw delta: 1 px per notch (native)
    clampTarget();
    scroll.to(target, 180, Ease.EaseOutCubic);
  }, { passive: false });

  const rowAt = (p) => {
    const i = Math.floor((p.y + scroll.value) / ROW_H);
    const list = rows.peek();
    return i >= 0 && i < list.length ? list[i] : null;
  };
  gestures(tree, {
    click: (e, p) => {
      const r = rowAt(p);
      if (!r || !r.folder) return;
      const next = new Set(expanded.peek());
      if (next.has(r.relPath)) next.delete(r.relPath); else next.add(r.relPath);
      expanded.value = next;
    },
    doubleClick: (e, p) => {
      const r = rowAt(p);
      if (!r || r.folder) return;
      vm.applyPreset(r.relPath).then(() => { selected.value = r.relPath; }).catch(() => {});
    },
  });
  // Hover follows the pointer by row (the rows move under a still pointer while scrolling).
  tree.addEventListener("pointermove", (e) => markHover(rowAt(local(tree, e))));
  tree.addEventListener("pointerleave", () => markHover(null));
  function markHover(r) {
    for (const row of rowsEl.children) row.classList.toggle("hover", !!r && row.__key === r.key);
  }
}
