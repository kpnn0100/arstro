/*
 * Cosmo by arstro — picker.js: the board's file browser, in cosmo's dialog design - the web's
 * stand-in for the host's GTK file choosers (linux_main.cpp:739-798, 900-913, 427-490; spec §5).
 *
 *   pick(vm, {mode, title, name, kinds, ok}) -> Promise<path | paths | null>
 *     mode "open"    one file of `kinds` (default ["project"])        -> its path
 *     mode "images"  several files of `kinds` (default ["image"]), multi-select -> [paths]
 *     mode "folder"  a folder (Select-folder)                         -> the folder shown
 *     mode "save"    a folder + a name (default `name`; the kind's extension is appended when
 *                    missing; an existing name asks "Replace?" first)  -> the full path
 *
 * The folders are the BOARD's: ADAPTER `browse {path}` -> {path, parent, entries:[{name, path,
 * kind: dir|project|image, size?}]} (dirs first, dot-files hidden). Drawn in the shared modal
 * skeleton (card 400, popover, 150/120 ms) with the ExportDialog tree's vocabulary: a #141414
 * box, 26 px rows, 11 px folder / image glyphs at whiteAlpha(.42), hover wash, the selected row
 * in primaryAlpha(.14); path line JetBrains Mono 10 front-ellipsised; ConfirmDialog buttons.
 * Keys: Esc = Cancel, Enter = the confirm button, Backspace (outside the name field) = up.
 * The last folder used is remembered per page for the next pick of the same kind (view state).
 */
import { h } from "../core/dom.js";
import { openModal } from "../ui/modal.js";
import { icon } from "../ui/icons.js";
import { est, fitEnd, fitFront, cardBytes } from "./paint.js";
import { T, box, button, frame, checkbox } from "./kit.js";
import { hostOf, ensureHost } from "./host.js";
import { confirm } from "./confirm.js";

const W = 400, PAD = 20, LIST_Y = 70, ROW = 26, ROWS = 10, LIST_H = ROWS * ROW + 2, BTN_H = 28;
const EXT = { project: ".cmp" };
const LAST = {};                               // kind group -> folder (this page's memory)

export function pick(vm, { mode = "open", title, name = "", kinds, ok } = {}) {
  const host = hostOf(vm) || ensureHost(vm);
  kinds = kinds || (mode === "images" ? ["image"] : mode === "folder" ? [] : ["project"]);
  const group = mode === "folder" ? "folder" : kinds.join(",");
  const okLabel = ok || { open: "Open", images: "Import", folder: "Select", save: "Save" }[mode] || "OK";
  const save = mode === "save";
  const hgt = LIST_Y + LIST_H + (save ? 12 + 26 : 0) + 20 + BTN_H + PAD;

  return new Promise((resolve) => {
    let settled = false;
    const finish = (v) => { if (settled) return; settled = true; m.close(); resolve(v); };
    const m = openModal(host.layer, { width: W, height: hgt, cls: "dg-picker", onOutside: () => finish(null), onKey });
    const c = m.card;
    frame(c);
    T(c, title || "", PAD, PAD + 16, 14, "semibold", "dg-fg");
    const pathEl = T(c, "", PAD, 58, 10, "mono", "dg-path");
    // list box
    const listBox = box(c, "dg-tree-box", PAD, LIST_Y, W - 2 * PAD, LIST_H);
    const list = h("div.dg-list.scroll", { style: { left: PAD + 1 + "px", top: LIST_Y + 1 + "px", width: W - 2 * PAD - 2 + "px", height: LIST_H - 2 + "px" } });
    const rowsEl = h("div.dg-rows");
    const note = T(null, "", 0, 0, 11, "sans", "dg-note");
    list.append(rowsEl);
    c.append(list, note);
    void listBox;
    // save: the name field
    let field = null;
    if (save) {
      const y = LIST_Y + LIST_H + 12;
      T(c, "Name", PAD, y + 17, 11, "sans", "dg-muted");
      box(c, "dg-field-box", PAD + 56, y, W - 2 * PAD - 56, 26);
      field = h("input.dg-field", { type: "text", spellcheck: "false", autocomplete: "off",
        style: { left: PAD + 56 + "px", top: y + "px", width: W - 2 * PAD - 56 + "px", height: "26px" } });
      field.value = name;
      c.append(field);
      field.addEventListener("input", sync);
      requestAnimationFrame(() => { field.focus(); const dot = field.value.lastIndexOf("."); field.setSelectionRange(0, dot > 0 ? dot : field.value.length); });
    }
    // footer
    const fy = hgt - PAD - BTN_H;
    const countEl = mode === "images" ? T(c, "", PAD, fy + 18, 10, "sans", "dg-muted") : null;
    const wOk = est(okLabel, 12) + 28, wCancel = est("Cancel", 12) + 28;
    const okBtn = button(c, { x: W - PAD - wOk, y: fy, w: wOk, h: BTN_H, label: okLabel, kind: "primary", onClick: confirmPick });
    button(c, { x: W - PAD - wOk - 8 - wCancel, y: fy, w: wCancel, h: BTN_H, label: "Cancel", onClick: () => finish(null) });

    let cur = null;                           // the browse answer shown
    let selected = new Set();                 // paths (open: one; images: many)
    let seq = 0;

    function sync() {
      let enabled = true;
      if (mode === "open") enabled = selected.size === 1;
      else if (mode === "images") enabled = selected.size > 0;
      else if (mode === "folder") enabled = !!cur;
      else if (save) enabled = !!cur && !!field.value.trim();
      okBtn.classList.toggle("disabled", !enabled);
      if (countEl) {
        const n = cur ? cur.entries.filter((e) => e.kind === "image").length : 0;
        countEl.textContent = selected.size + " of " + n + " images selected";
      }
      for (const r of rowsEl.children) {
        const on = r.dataset.path && selected.has(r.dataset.path);
        r.classList.toggle("sel", !!on);
        if (r.check) r.check.set(on ? 1 : 0);
      }
      return enabled;
    }
    function shows(e) {
      if (e.kind === "dir") return true;
      if (mode === "folder") return false;
      if (mode === "images") return kinds.includes(e.kind);     // default ["image"]; File > Open... also lists projects
      return kinds.includes(e.kind);
    }
    function setNote(s) {
      note.textContent = s;
      const cx = PAD + (W - 2 * PAD) / 2;
      Object.assign(note.style, {});
      note.style.left = cx - est(s, 11) / 2 + "px";
      note.style.top = LIST_Y + LIST_H / 2 - 11 * 0.8418 + "px";
      note.classList.toggle("on", !!s);
    }
    async function go(path) {
      const my = ++seq;
      rowsEl.classList.add("fading");
      setNote(cur ? "" : "Loading…");
      let res;
      try { res = await vm.browse(path); }
      catch (e) {
        if (my !== seq) return;
        if (path && !cur) return go(null);          // a remembered folder that went away: start at home
        rowsEl.classList.remove("fading");
        setNote("Cannot open this folder: " + (e.message || e));
        return;
      }
      if (my !== seq) return;
      cur = res;
      LAST[group] = res.path;
      if (mode !== "images") selected = new Set();
      pathEl.textContent = fitFront(res.path, W - 2 * PAD, 10);
      render();
    }
    function render() {
      rowsEl.textContent = "";
      const items = [];
      if (cur.parent) items.push({ name: "..", path: cur.parent, kind: "up" });
      for (const e of cur.entries) if (shows(e)) items.push(e);
      const multi = mode === "images";
      items.forEach((e, i) => {
        const r = h("div.dg-row", { style: { top: i * ROW + "px" } });
        box(r, "dg-row-wash", 0, 0, W - 2 * PAD - 2, ROW, 0);
        let x = 7;
        if (multi && e.kind === "image") { r.check = checkbox(r, x, 6.5, 13); }
        if (multi) x += 20;
        const glyph = e.kind === "image" ? icon("image", 11, 1.1) : e.kind === "project" ? icon("save", 11, 1.1) : icon("folder", 11, 1.1);
        r.append(h("div.dg-row-ic", { style: { left: x + "px", top: "7.5px" } }, glyph));
        x += 18;
        const sizeS = e.size ? cardBytes(e.size) : "";
        const room = W - 2 * PAD - 2 - 14 - (sizeS ? est(sizeS, 10) + 10 : 0) - x;
        T(r, fitEnd(e.name, room, 11), x, 16, 11, e.kind === "dir" || e.kind === "up" ? "medium" : "sans", "dg-row-name");
        if (sizeS) T(r, sizeS, W - 2 * PAD - 2 - 14 - est(sizeS, 10), 15.5, 10, "mono", "dg-row-size");
        if (e.kind !== "dir" && e.kind !== "up") r.dataset.path = e.path;
        r.addEventListener("click", () => activate(e, false));
        r.addEventListener("dblclick", () => activate(e, true));
        rowsEl.append(r);
      });
      rowsEl.style.height = items.length * ROW + "px";
      list.scrollTop = 0;
      setNote(items.length ? "" : mode === "open" ? "No projects in this folder" : mode === "images" ? (kinds.includes("project") ? "No photos or projects in this folder" : "No images in this folder") : "This folder is empty");
      requestAnimationFrame(() => rowsEl.classList.remove("fading"));
      sync();
    }
    function activate(e, dbl) {
      if (e.kind === "dir" || e.kind === "up") { go(e.path); return; }
      if (mode === "open") {
        selected = new Set([e.path]);
        sync();
        if (dbl) confirmPick();
      } else if (mode === "images") {
        if (selected.has(e.path)) selected.delete(e.path); else selected.add(e.path);
        sync();
      } else if (save) {
        field.value = e.name;
        sync();
        if (dbl) confirmPick();
      }
    }
    async function confirmPick() {
      if (!sync() || m.closing) return;
      if (mode === "open") return finish([...selected][0]);
      if (mode === "images") return finish([...selected]);
      if (mode === "folder") return finish(cur.path);
      let nm = field.value.trim();
      const ext = EXT[kinds[0]];
      if (ext && !nm.toLowerCase().endsWith(ext)) nm += ext;
      const full = cur.path.replace(/\/$/, "") + "/" + nm;
      if (cur.entries.some((e) => e.name === nm)) {
        const i = await confirm(vm, { title: "Replace?", message: `"${nm}" already exists in this folder.`,
          buttons: [{ label: "Cancel" }, { label: "Replace", destructive: true }] });
        if (i !== 1) { field.focus(); return; }
      }
      finish(full);
    }
    function onKey(e) {
      if (e.key === "Escape") { finish(null); return true; }
      if (e.key === "Enter") { confirmPick(); return true; }
      if (e.key === "Backspace" && e.target !== field && cur && cur.parent) { go(cur.parent); return true; }
      return false;
    }
    const start = LAST[group] || dirOf(vm.project.peek().path) || null;
    go(start);
  });
}

function dirOf(p) { if (!p) return null; const i = p.lastIndexOf("/"); return i > 0 ? p.slice(0, i) : null; }
