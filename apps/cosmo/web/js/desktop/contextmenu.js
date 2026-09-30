/*
 * Cosmo by arstro — desktop/contextmenu.js: the right-click menu of the photo and the
 * filmstrip, and its inline group-rename mode.
 *
 * Ports widgets/ContextMenu.cpp and App::openEditContext (App.cpp:577-620). Items in order:
 * Add Photo…, Group Selection, Ungroup Selection, Disable / Enable Filter (a cell - the label
 * names the inverse of its bypass), Rename Group (a group cell), Image Information (not a
 * group), Delete (a cell) - so the photo menu has 4 items and a cell 6. Width max(150,
 * estimate + 22), rows 24, padding 4, clamped into the window; popover fill, 1 px border, r2;
 * appears over 130 ms (out 100 ms) EaseOutCubic rising 6 px; hover wash white @ 0.08.
 *
 * Modal while open: every press is taken; a click (or right click) on an item closes the menu
 * and runs it, anywhere else just closes it. No keys (Esc does not close it).
 *
 * Rename mode (DR-TREE-5), from "Rename Group" or the TopBar's group name: the menu morphs over
 * 260 ms EaseInOutCubic to 208 wide - items collapse into a "Rename" header in the first half,
 * the light field grows in the second - seeded select-all with the group's name ("Group" if it
 * has none). Typing replaces the selection or appends, Backspace clears it or drops the last
 * character, Enter commits `group rename <node> "<name>"` (if not empty), Esc cancels; every
 * other key is swallowed while it is open. The field shows the TAIL of a long name, like the
 * native, and blinks a 1.6 px caret once the selection is dropped.
 *
 * State: vm.view.contextMenu = {x, y, node (-1 = the photo), rename?, name?} (this page's);
 * setting it opens the menu, null closes it.
 */
import { untracked } from "../core/signal.js";
import { h, bindEffect } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { text, setText, place, est, gestures, local } from "./editor.js";
import { pushKeyLayer } from "./keys.js";

const ITEM_H = 24, PAD_X = 11, PAD_Y = 4, MIN_W = 150;
const HEADER_H = 22, FIELD_H = 30, FIELD_GAP = 6, FIELD_MX = 8, RENAME_W = 208;
const clamp01 = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);

export function mountContextMenu(layer, vm, ed) {
  const catcher = h("div.cm-catch");
  const menu = h("div.cm");
  const itemsEl = h("div.cm-items");
  const header = text("Rename", { x: PAD_X, y: PAD_Y + HEADER_H / 2 + 11 * 0.35, size: 11, cls: "muted cm-header" });
  const fieldSel = h("div.cm-sel");
  const fieldText = text("", { x: 8, y: 0, size: 11, cls: "cm-ftext" });
  const caret = h("div.cm-caret");
  const field = h("div.cm-field", {}, fieldSel, fieldText, caret);
  menu.append(itemsEl, header, field);
  layer.append(catcher, menu);

  const st = { open: false, items: [], x: 0, y: 0, node: -1, renaming: false, text: "", selectAll: true, preH: 0 };
  let popKeys = null, blinkRaf = 0;
  const appear = new Tween(0, () => paint());
  const rename = new Tween(0, () => paint());

  function menuRect() {
    const W = ed.size.peek().w, H = ed.size.peek().h;
    let w = MIN_W;
    if (st.renaming) w = RENAME_W;
    else for (const it of st.items) w = Math.max(w, est(it.label, 11) + 2 * PAD_X);
    const amt = rename.value, collapse = clamp01(amt * 2), grow = clamp01((amt - 0.5) * 2);
    const contentH = st.renaming ? st.preH + (HEADER_H - st.preH) * collapse + (FIELD_GAP + FIELD_H) * grow
                                 : st.items.length * ITEM_H;
    const hh = contentH + 2 * PAD_Y;
    return { x: Math.max(0, Math.min(st.x, W - w)), y: Math.max(0, Math.min(st.y, H - hh)), w, h: hh, collapse, grow };
  }
  function paint() {
    const a = appear.value;
    menu.style.visibility = a <= 0.001 && !st.open ? "hidden" : "visible";
    const r = menuRect();
    menu.style.opacity = a;
    menu.style.transform = `translate(${r.x}px, ${r.y - (1 - a) * 6}px)`;
    menu.style.width = r.w + "px";
    menu.style.height = r.h + "px";
    if (!st.renaming) { itemsEl.style.opacity = 1; header.style.opacity = 0; field.style.display = "none"; return; }
    itemsEl.style.opacity = 1 - r.collapse;
    header.style.opacity = clamp01((rename.value - 0.1) / 0.4);
    field.style.display = r.grow > 0.001 ? "block" : "none";
    const fw = r.w - 2 * FIELD_MX, fh = FIELD_H * r.grow;
    field.style.transform = `translate(${FIELD_MX}px, ${PAD_Y + HEADER_H + FIELD_GAP * r.grow}px)`;
    field.style.width = fw + "px";
    field.style.height = fh + "px";
    let shown = st.text;
    while (est(shown, 11) > fw - 16 && shown.length > 1) shown = [...shown].slice(1).join("");
    const tw = est(shown, 11);
    setText(fieldText, shown);
    place(fieldText, 8, fh / 2 + 11 * 0.35, 11);
    fieldSel.style.opacity = st.selectAll && shown ? 0.26 * r.grow : 0;
    Object.assign(fieldSel.style, { left: 8 - 2 + "px", top: "5px", width: tw + 4 + "px", height: Math.max(0, fh - 10) + "px" });
    const blink = 0.55 + 0.45 * Math.cos(performance.now() * 0.006);
    caret.style.opacity = st.selectAll ? 0 : r.grow * blink;
    Object.assign(caret.style, { left: 8 + tw + 1 + "px", top: "5px", height: Math.max(0, fh - 10) + "px" });
  }
  function blinkLoop() {
    if (!st.renaming || !st.open) { blinkRaf = 0; return; }
    paint();
    blinkRaf = requestAnimationFrame(blinkLoop);
  }

  function itemsFor(node) {                    // App::openEditContext
    const n = node >= 0 ? vm.nodeById.peek().get(node) : null;
    const items = [
      { label: "Add Photo...", run: () => ed.act.open() },
      { label: "Group Selection", run: () => ed.act.group() },
      { label: "Ungroup Selection", run: () => ed.act.ungroup() },
    ];
    if (n) items.push({ label: n.bypass ? "Enable Filter" : "Disable Filter", run: () => vm.bypass(n.node, !n.bypass).catch(() => {}) });
    if (n && n.kind === "group") {
      items.push({ label: "Rename Group", keep: true,
                   run: () => { vm.view.contextMenu.value = { ...vm.view.contextMenu.peek(), rename: true, name: n.name }; } });
    }
    if (!n || n.kind !== "group") {
      items.push({ label: "Image Information", run: () => { vm.view.dialog.value = { name: "info", node: n ? n.node : -1 }; } });   // info.js asks
    }
    if (n) items.push({ label: "Delete", run: () => ed.act.remove() });
    return items;
  }
  function renderItems() {
    itemsEl.replaceChildren(...st.items.map((it, i) =>
      h("div.cm-item", { style: { top: PAD_Y + i * ITEM_H + "px" }, "data-i": i },
        text(it.label, { x: PAD_X, y: ITEM_H / 2 + 11 * 0.35, size: 11, cls: "fg" }))));
  }

  function setOpen(on) {
    st.open = on;
    layer.classList.toggle("open", on);
    appear.to(on ? 1 : 0, on ? 130 : 100, Ease.EaseOutCubic);
    if (!on) {
      st.renaming && stopRename();
      for (const el of itemsEl.children) el.classList.remove("hover");
    }
  }
  function startRename(name, fromItems) {
    st.preH = fromItems ? st.items.length * ITEM_H : HEADER_H;
    if (!fromItems) { st.items = []; renderItems(); }
    st.renaming = true;
    st.text = name || "Group";
    st.selectAll = true;
    if (!fromItems) rename.set(0);
    rename.to(1, 260, Ease.EaseInOutCubic);
    if (!popKeys) popKeys = pushKeyLayer(onKey);
    if (!blinkRaf) blinkRaf = requestAnimationFrame(blinkLoop);
  }
  function stopRename() {
    if (popKeys) { popKeys(); popKeys = null; }
  }

  bindEffect(layer, () => {
    const cm = vm.view.contextMenu.value;
    untracked(() => {
      if (!cm) { if (st.open) setOpen(false); paint(); return; }
      if (cm.rename) {
        const fromItems = st.open && !st.renaming && st.node === cm.node && st.items.length > 0;
        st.node = cm.node;
        if (!fromItems) { st.x = cm.x; st.y = cm.y; }
        if (!st.open) { st.renaming = false; setOpen(true); }
        startRename(cm.name, fromItems);
      } else {
        stopRename();
        st.renaming = false;
        rename.set(0);
        st.x = cm.x; st.y = cm.y; st.node = cm.node;
        st.items = itemsFor(cm.node);
        renderItems();
        setOpen(st.items.length > 0);
      }
      paint();
    });
  });
  // Another client removing the node under an open menu closes it (its items would lie).
  bindEffect(layer, () => {
    const map = vm.nodeById.value;
    untracked(() => { if (st.open && st.node >= 0 && !map.has(st.node)) vm.view.contextMenu.value = null; });
  });

  ed.openContext = (x, y, node) => { vm.view.contextMenu.value = { x, y, node }; };
  ed.openRename = (node, name, x, y) => { vm.view.contextMenu.value = { x, y, node, rename: true, name }; };

  const close = () => { vm.view.contextMenu.value = null; };
  function commit() {
    const name = st.text, node = st.node;
    close();
    if (name) vm.rename(node, name).catch(() => {});
  }
  function onKey(e) {                          // ContextMenu::handleKey (rename mode only)
    if (e.key === "Enter") commit();
    else if (e.key === "Escape") close();
    else if (e.key === "Backspace") {
      if (st.selectAll) { st.text = ""; st.selectAll = false; } else st.text = [...st.text].slice(0, -1).join("");
    } else if (e.key.length === 1 && !e.ctrlKey && !e.altKey && !e.metaKey) {
      if (st.selectAll) { st.text = ""; st.selectAll = false; }
      st.text += e.key;
    }
    paint();
    return true;                               // every other key is swallowed while editing
  }

  // Pointer: modal. Presses are taken; the release decides (Click / RightClick).
  gestures(catcher, { click: () => close(), rightClick: () => close() });
  const release = (e, p, at) => {
    if (st.renaming) {
      const inField = at && field.contains(at);
      if (inField) { st.selectAll = false; paint(); }
      return;
    }
    const row = at && at.closest(".cm-item");
    if (!row || !menu.contains(row)) { close(); return; }
    const it = st.items[+row.dataset.i];
    if (!it) return;
    if (!it.keep) close();
    Promise.resolve().then(it.run).catch((err) => console.error(err));
  };
  gestures(menu, { click: release, rightClick: release });
  menu.addEventListener("pointermove", (e) => {
    if (st.renaming) return;
    const p = local(menu, e);
    const i = Math.floor((p.y - PAD_Y) / ITEM_H);
    for (const el of itemsEl.children) el.classList.toggle("hover", +el.dataset.i === i && p.x >= 0 && p.x <= menu.offsetWidth);
  });
  menu.addEventListener("pointerleave", () => { for (const el of itemsEl.children) el.classList.remove("hover"); });

  paint();
}
