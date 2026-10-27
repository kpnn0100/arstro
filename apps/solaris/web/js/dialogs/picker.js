/*
 * Solaris — dialogs/picker.js: the machine's file browser — the web's stand-in for the host's GTK file choosers
 * (linux_main.cpp:378-450: App's onPickSongToOpen / onPickSongToCreate / onPickSave / onPickFile / onPickDirectory /
 * onPickFolder). The design is Cosmo's web picker (apps/cosmo/web/js/dialogs/picker.js — cosmo's dialog skeleton:
 * a 400 card, a 26 px-row list in a #141414 box, a mono path line, ConfirmDialog buttons), made Solaris's: its kinds,
 * its glyphs, measured text, its words; its teal comes from the tokens.
 *
 *   pick(vm, layer, {mode, title, ok, name, kinds}, confirm) -> Promise<path | null>
 *     mode "open"     one file of `kinds` (none: any file)                   -> its path
 *     mode "save"     a folder + a name (`name` suggested; `kinds[0]`'s extension appended when missing — .slp for a
 *                     song; an existing name asks "Replace?" first, as GTK's overwrite confirmation)   -> the path
 *     mode "folder"   a folder (the one shown)                               -> its path
 *     title, ok       the dialog's title and its button (the host's: "Open song" / "Open", "New song" / "Create" …)
 *     kinds           the adapter's: song · audio · midi · video · preset · sfz · other (folders always listed)
 *   The folders are the MACHINE's — the adapter's `browse {path, kinds}` (cli/NtwbAdapter.h). Dot-files hidden;
 *   folders first. The last folder used is remembered per page for the next pick of the same kind (view state);
 *   the first starts at the open song's folder, else the home folder.
 * Keys: Escape = Cancel, Enter = the button, Backspace (outside the name field) = up a folder. Double-click = open /
 * choose; a row click selects.
 */
import { h } from "../core/dom.js";
import { formatBytes } from "../core/dom.js";
import { openModal } from "../ui/modal.js";
import { icon } from "../ui/icons.js";
import { FONT, measure, ellipsize, ellipsizeFront, textAt } from "../kit/text.js";

const W = 400, PAD = 20, LIST_Y = 70, ROW = 26, ROWS = 10, LIST_H = ROWS * ROW + 2, BTN_H = 28; // cosmo's picker (dialogs/picker.js:28)
const EXT = { song: ".slp", preset: ".slpreset" };
const LAST = {};                                // kind group -> folder (this page's memory)
const SVGNS = "http://www.w3.org/2000/svg";

function T(parent, str, x, base, px, fam, cls) {
  const el = textAt(h("div.dg-t" + (cls ? "." + cls : ""), { text: str }), x, base, px, fam);
  if (parent) parent.append(el);
  return el;
}
function box(parent, cls, x, y, w, hh, r = 2) {
  const s = document.createElementNS(SVGNS, "svg");
  s.setAttribute("class", "dg-box " + cls);
  s.setAttribute("width", Math.max(0, w)); s.setAttribute("height", Math.max(0, hh));
  const rect = document.createElementNS(SVGNS, "rect");
  for (const [k, v] of Object.entries({ x: 0, y: 0, width: "100%", height: "100%", rx: r, ry: r })) rect.setAttribute(k, v);
  s.append(rect);
  s.style.left = x + "px"; s.style.top = y + "px";
  if (parent) parent.append(s);
  return s;
}
function button(parent, { x, y, w, label, kind, onClick }) {
  const b = h("button.dg-btn.dg-" + kind, { "aria-label": label, style: { left: x + "px", top: y + "px", width: w + "px", height: BTN_H + "px" } });
  box(b, "dg-btn-fill", 0, 0, w, BTN_H);
  box(b, "dg-wash", 0, 0, w, BTN_H);
  T(b, label, (w - measure(label, 12, FONT.medium)) / 2, BTN_H / 2 + 4, 12, FONT.medium, "dg-btn-label"); // ConfirmDialog.cpp:186-189
  b.addEventListener("click", (e) => { e.stopPropagation(); if (!b.classList.contains("disabled")) onClick(e); });
  parent.append(b);
  return b;
}
/** Glyphs.h's speaker and film, and a plain page, in an 11 px box — for the kinds cosmo's icons have no word for. */
function glyphFor(kind) {
  if (kind === "dir" || kind === "up") return icon("folder", 11, 1.1);
  if (kind === "song") return icon("save", 11, 1.1);
  const s = document.createElementNS(SVGNS, "svg");
  s.setAttribute("viewBox", "0 0 100 100"); s.setAttribute("width", 11); s.setAttribute("height", 11); s.setAttribute("class", "icon");
  const p = (d, fill) => { const e = document.createElementNS(SVGNS, "path"); e.setAttribute("d", d); e.setAttribute("fill", fill ? "currentColor" : "none");
    if (!fill) { e.setAttribute("stroke", "currentColor"); e.setAttribute("stroke-width", "1.1"); e.setAttribute("vector-effect", "non-scaling-stroke"); } s.append(e); };
  if (kind === "audio") { p("M12 38 L32 38 L56 16 L56 84 L32 62 L12 62 Z", true); p("M70 32 Q82 50 70 68"); }                          // glyph::speaker
  else if (kind === "video") { p("M0.5 12 H99.5 V88 H0.5 Z"); for (const x of [22, 50, 78]) p(`M${x} 12 V26 M${x} 74 V88`); }            // glyph::film
  else if (kind === "midi") { p("M10 20 H90 V80 H10 Z"); for (const x of [30, 50, 70]) p(`M${x} 20 V80`); p("M24 20 V56 H36 V20 M64 20 V56 H76 V20", true); }
  else p("M22 8 H62 L80 26 V92 H22 Z M62 8 V26 H80");
  return s;
}

export function pick(vm, layer, { mode = "open", title = "", ok, name = "", kinds } = {}, confirm) {
  kinds = kinds || (mode === "folder" ? [] : []);
  const group = mode === "folder" ? "folder" : kinds.join(",") || "any";
  const okLabel = ok || { open: "Open", save: "Save", folder: "Select" }[mode] || "OK";
  const save = mode === "save";
  const hgt = LIST_Y + LIST_H + (save ? 12 + 26 : 0) + 20 + BTN_H + PAD;

  return new Promise((resolve) => {
    let settled = false;
    const finish = (v) => { if (settled) return; settled = true; m.close(); resolve(v); };
    const m = openModal(layer, { width: W, height: hgt, cls: "dg-picker", onOutside: () => finish(null), onKey });
    const c = m.card;
    const frame = document.createElementNS(SVGNS, "svg");
    frame.setAttribute("class", "dg-frame");
    const fr = document.createElementNS(SVGNS, "rect");
    for (const [k, v] of Object.entries({ x: 0, y: 0, width: "100%", height: "100%", rx: 2, ry: 2 })) fr.setAttribute(k, v);
    frame.append(fr);
    c.append(frame);
    T(c, title, PAD, PAD + 16, 14, FONT.semibold, "dg-fg");
    const pathEl = T(c, "", PAD, 58, 10, FONT.mono, "dg-path");
    box(c, "dg-tree-box", PAD, LIST_Y, W - 2 * PAD, LIST_H);
    const list = h("div.dg-list.scroll", { style: { left: PAD + 1 + "px", top: LIST_Y + 1 + "px", width: W - 2 * PAD - 2 + "px", height: LIST_H - 2 + "px" } });
    const rowsEl = h("div.dg-rows");
    const note = T(null, "", 0, 0, 11, FONT.sans, "dg-note");
    list.append(rowsEl);
    c.append(list, note);
    let field = null;
    if (save) {
      const y = LIST_Y + LIST_H + 12;
      T(c, "Name", PAD, y + 17, 11, FONT.sans, "dg-muted");
      const fb = box(c, "dg-field-box", PAD + 56, y, W - 2 * PAD - 56, 26);
      field = h("input.dg-field", { type: "text", spellcheck: "false", autocomplete: "off",
        style: { left: PAD + 56 + "px", top: y + "px", width: W - 2 * PAD - 56 + "px", height: "26px" } });
      field.value = name;
      c.append(field);
      field.addEventListener("input", sync);
      field.addEventListener("focus", () => fb.classList.add("focus"));
      field.addEventListener("blur", () => fb.classList.remove("focus"));
      requestAnimationFrame(() => { field.focus(); const dot = field.value.lastIndexOf("."); field.setSelectionRange(0, dot > 0 ? dot : field.value.length); });
    }
    const fy = hgt - PAD - BTN_H;
    const wOk = measure(okLabel, 12, FONT.medium) + 28, wCancel = measure("Cancel", 12, FONT.medium) + 28; // kBtnPadX 14 a side
    const okBtn = button(c, { x: W - PAD - wOk, y: fy, w: wOk, label: okLabel, kind: "primary", onClick: confirmPick });
    button(c, { x: W - PAD - wOk - 8 - wCancel, y: fy, w: wCancel, label: "Cancel", kind: "plain", onClick: () => finish(null) });

    let cur = null, selected = null, seq = 0;

    function sync() {
      let enabled = true;
      if (mode === "open") enabled = !!selected;
      else if (mode === "folder") enabled = !!cur;
      else if (save) enabled = !!cur && !!field.value.trim();
      okBtn.classList.toggle("disabled", !enabled);
      for (const r of rowsEl.children) r.classList.toggle("sel", !!r.dataset.path && r.dataset.path === selected);
      return enabled;
    }
    function setNote(s) {
      note.textContent = s;
      textAt(note, PAD + (W - 2 * PAD) / 2 - measure(s, 11) / 2, LIST_Y + LIST_H / 2 + 11 * 0.35, 11, FONT.sans);
      note.classList.toggle("on", !!s);
    }
    async function go(path) {
      const my = ++seq;
      rowsEl.classList.add("fading");
      setNote(cur ? "" : "Loading…");
      let res;
      try { res = await vm.browse(path, mode === "folder" ? ["dir"] : kinds); }
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
      selected = null;
      pathEl.textContent = ellipsizeFront(res.path, W - 2 * PAD, 10, FONT.mono);
      render();
    }
    function shows(e) {
      if (e.kind === "dir") return true;
      if (mode === "folder") return false;
      return !kinds.length || kinds.includes(e.kind);
    }
    function render() {
      rowsEl.textContent = "";
      const items = [];
      if (cur.parent) items.push({ name: "..", path: cur.parent, kind: "up" });
      for (const e of cur.entries) if (shows(e)) items.push(e);
      const rowW = W - 2 * PAD - 2;
      items.forEach((e, i) => {
        const r = h("div.dg-row", { style: { top: i * ROW + "px" } });
        box(r, "dg-row-wash", 0, 0, rowW, ROW, 0);
        r.append(h("div.dg-row-ic", { style: { left: "7px", top: "7.5px" } }, glyphFor(e.kind)));
        const x = 25;
        const sizeS = e.kind !== "dir" && e.kind !== "up" && e.size !== undefined ? formatBytes(e.size) : "";
        const sizeW = sizeS ? measure(sizeS, 10, FONT.mono) : 0;
        const room = rowW - 14 - (sizeS ? sizeW + 10 : 0) - x;
        const isDir = e.kind === "dir" || e.kind === "up";
        T(r, ellipsize(e.name, room, 11, isDir ? FONT.medium : FONT.sans), x, 16, 11, isDir ? FONT.medium : FONT.sans, "dg-row-name");
        if (sizeS) T(r, sizeS, rowW - 14 - sizeW, 15.5, 10, FONT.mono, "dg-row-size");
        if (!isDir) r.dataset.path = e.path;
        r.addEventListener("click", () => activate(e, false));
        r.addEventListener("dblclick", () => activate(e, true));
        rowsEl.append(r);
      });
      rowsEl.style.height = items.length * ROW + "px";
      list.scrollTop = 0;
      const songs = kinds.length === 1 && kinds[0] === "song";
      setNote(items.length > (cur.parent ? 1 : 0) ? "" : mode === "folder" ? "No folders here" : songs ? "No songs in this folder" : "This folder is empty");
      requestAnimationFrame(() => rowsEl.classList.remove("fading"));
      sync();
    }
    function activate(e, dbl) {
      if (e.kind === "dir" || e.kind === "up") { go(e.path); return; }
      if (mode === "open") { selected = e.path; sync(); if (dbl) confirmPick(); }
      else if (save) { field.value = e.name; selected = e.path; sync(); if (dbl) confirmPick(); }
    }
    async function confirmPick() {
      if (!sync() || m.closing) return;
      if (mode === "open") return finish(selected);
      if (mode === "folder") return finish(cur.path);
      let nm = field.value.trim();
      const ext = EXT[kinds[0]];
      if (ext && !nm.toLowerCase().endsWith(ext)) nm += ext;
      const full = cur.path.replace(/\/$/, "") + "/" + nm;
      if (cur.entries.some((e) => e.name === nm) && confirm) {
        const i = await confirm({ title: "Replace?", message: `"${nm}" already exists in this folder.`,
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
    const proj = vm.model.peek() && vm.model.peek().projectPath;
    const dirOf = (p) => { if (!p) return null; const i = p.lastIndexOf("/"); return i > 0 ? p.slice(0, i) : null; };
    go(LAST[group] || dirOf(proj) || null);
  });
}
