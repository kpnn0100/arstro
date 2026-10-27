/*
 * Interstellar — dialogs/picker.js: the machine's file picker - the web's stand-in for the GTK file
 * choosers the window's host shows for App's pickers (linux_main.cpp:136-338, 520-548).
 *
 *   pick(vm, ctx, {mode, title, kinds, name, ensure, ok}) -> Promise<path | path[] | null>
 *     mode "open"     one file of `kinds`                                  -> its path
 *     mode "files"    several files of `kinds` (Add Footage), checkboxes  -> [paths]
 *     mode "folder"   a folder (Select / Search)                          -> the folder shown
 *     mode "save"     a folder + a name (default `name`); `ensure(name)` makes sure of the extension
 *                     (the GTK host's per-picker rule); an existing name asks "Replace?" first, as
 *                     gtk_file_chooser_set_do_overwrite_confirmation does    -> the full path
 *   kinds: the adapter's `browse` kinds - project | video | image | audio | lut | timeline | captions |
 *          preset | file (anything)
 *
 * The folders are the MACHINE's (adapter `browse {path, kinds}` -> {path, parent, entries}). The design
 * is cosmo's picker (apps/cosmo/web/js/dialogs/picker.js - the worked example), Interstellar's tokens:
 * the shared modal skeleton (ui/modal.js: card 400, popover, 150 / 120 ms, scrim .55), a background
 * box of 10 rows × 26 px, glyphs at whiteAlpha(.42) (cosmo's folder / image / save, Interstellar's
 * film / speaker), a hover wash, the chosen row in primaryAlpha(.14), the path in JetBrains Mono 10
 * front-ellipsized, ConfirmDialog's buttons. Keys: Esc = Cancel, Enter = the confirm button,
 * Backspace (outside the name field) = up. The last folder used is kept per kind group for this page
 * (view state); the first pick starts in the open project's folder, else the machine's home.
 */
import { h, formatBytes } from "../core/dom.js";
import { openModal } from "../ui/modal.js";
import { icon } from "../ui/icons.js";
import { useCss } from "../kit/css.js";
import { glyph } from "../kit/glyphs.js";
import { ellipsize, width as textWidth, placeText, FAMILY } from "../kit/textfit.js";

const W = 400, PAD = 20, LIST_Y = 70, ROW = 26, ROWS = 10, LIST_H = ROWS * ROW + 2, BTN_H = 28;
const LAST = {};
const NS = "http://www.w3.org/2000/svg";

function T(parent, text, x, base, px, family = FAMILY.sans, cls = "") {
  const el = placeText(h("div.ip-t" + (cls ? "." + cls : ""), { text }), x, base, px, family);
  if (parent) parent.append(el);
  return el;
}
function frontEllipsis(s, maxW, px, family) {
  if (textWidth(s, px, family) <= maxW) return s;
  const cps = Array.from(s);
  let lo = 0, hi = cps.length;
  while (lo < hi) {                                   // the longest TAIL that fits after "…"
    const mid = (lo + hi + 1) >> 1;
    if (textWidth("…" + cps.slice(cps.length - mid).join(""), px, family) <= maxW) lo = mid; else hi = mid - 1;
  }
  return "…" + cps.slice(cps.length - lo).join("");
}
function kindGlyph(kind) {
  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("width", 11); svg.setAttribute("height", 11);
  svg.setAttribute("overflow", "visible");
  const c = "rgba(var(--white-rgb), 0.42)", b = { x: 0, y: 0, w: 11, h: 11 };
  if (kind === "video") svg.append(glyph.film(b, c));
  else if (kind === "audio") svg.append(glyph.speaker(b, c));
  else return icon(kind === "image" ? "image" : kind === "dir" || kind === "up" ? "folder" : "save", 11, 1.1);
  return svg;
}
function button(parent, { x, y, w, label, kind, onClick }) {
  const b = h("button.ip-btn.ip-" + kind, { style: { transform: `translate(${x}px, ${y}px)`, width: w + "px", height: BTN_H + "px" } });
  T(b, label, (w - textWidth(label, 12, FAMILY.medium)) / 2, BTN_H / 2 + 4, 12, FAMILY.medium, "ip-btn-label");
  b.addEventListener("click", (e) => { e.stopPropagation(); if (!b.classList.contains("disabled")) onClick(); });
  parent.append(b);
  return b;
}

export function pick(vm, ctx, { mode = "open", title = "", kinds, name = "", ensure, ok } = {}) {
  useCss("css/dialogs.css");
  useCss("css/dialogs/picker.css");
  kinds = kinds || (mode === "files" ? ["video", "image"] : mode === "folder" ? [] : ["project"]);
  const group = mode === "folder" ? "folder" : kinds.join(",");
  const okLabel = ok || { open: "Open", files: "Add", folder: "Select", save: "Save" }[mode] || "OK";
  const save = mode === "save", multi = mode === "files";
  const hgt = LIST_Y + LIST_H + (save ? 12 + 26 : 0) + 20 + BTN_H + PAD;
  const layer = ctx.layers.modals;

  return new Promise((resolve) => {
    let settled = false;
    const finish = (v) => { if (settled) return; settled = true; md.close(); resolve(v); };
    const md = openModal(layer, { width: W, height: hgt, cls: "ip-picker", onOutside: () => finish(null), onKey });
    const c = md.card;
    T(c, title, PAD, PAD + 16, 14, FAMILY.semibold, "ip-fg");
    const pathEl = T(c, "", PAD, 58, 10, FAMILY.mono, "ip-path");
    c.append(h("div.ip-box", { style: { transform: `translate(${PAD}px, ${LIST_Y}px)`, width: W - 2 * PAD + "px", height: LIST_H + "px" } }));
    const list = h("div.ip-list.scroll", { style: { transform: `translate(${PAD + 1}px, ${LIST_Y + 1}px)`, width: W - 2 * PAD - 2 + "px", height: LIST_H - 2 + "px" } });
    const rowsEl = h("div.ip-rows");
    list.append(rowsEl);
    const note = h("div.ip-note");
    c.append(list, note);
    let field = null;
    if (save) {
      const y = LIST_Y + LIST_H + 12;
      T(c, "Name", PAD, y + 17, 11, FAMILY.sans, "ip-muted");
      field = h("input.ip-field", { type: "text", spellcheck: "false", autocomplete: "off",
        style: { transform: `translate(${PAD + 56}px, ${y}px)`, width: W - 2 * PAD - 56 + "px", height: "26px" } });
      field.value = name;
      c.append(field);
      field.addEventListener("input", sync);
      requestAnimationFrame(() => { field.focus(); const dot = field.value.lastIndexOf("."); field.setSelectionRange(0, dot > 0 ? dot : field.value.length); });
    }
    const fy = hgt - PAD - BTN_H;
    const countEl = multi ? T(c, "", PAD, fy + 18, 10, FAMILY.sans, "ip-muted") : null;
    const wOk = textWidth(okLabel, 12, FAMILY.medium) + 28, wCancel = textWidth("Cancel", 12, FAMILY.medium) + 28;
    const okBtn = button(c, { x: W - PAD - wOk, y: fy, w: wOk, label: okLabel, kind: "primary", onClick: confirmPick });
    button(c, { x: W - PAD - wOk - 8 - wCancel, y: fy, w: wCancel, label: "Cancel", kind: "plain", onClick: () => finish(null) });

    let cur = null, selected = new Set(), seq = 0;
    const isFile = (e) => e.kind !== "dir" && e.kind !== "up";
    function sync() {
      let enabled = true;
      if (mode === "open") enabled = selected.size === 1;
      else if (multi) enabled = selected.size > 0;
      else if (mode === "folder") enabled = !!cur;
      else if (save) enabled = !!cur && !!field.value.trim();
      okBtn.classList.toggle("disabled", !enabled);
      if (countEl) {
        const n = cur ? cur.entries.filter(isFile).length : 0;
        countEl.textContent = `${selected.size} of ${n} selected`;
      }
      for (const r of rowsEl.children) {
        const on = !!r.dataset.path && selected.has(r.dataset.path);
        r.classList.toggle("sel", on);
        if (r.check) r.check.classList.toggle("on", on);
      }
      return enabled;
    }
    function setNote(s) {
      note.textContent = s;
      placeText(note, PAD + (W - 2 * PAD) / 2 - textWidth(s, 11) / 2, LIST_Y + LIST_H / 2 + 4, 11);
      note.classList.toggle("on", !!s);
    }
    async function go(path) {
      const my = ++seq;
      rowsEl.classList.add("fading");
      if (!cur) setNote("Loading…");
      let res;
      try { res = await vm.session.browse(path, mode === "folder" ? ["project"] : kinds); }
      catch (e) {
        if (my !== seq) return;
        if (path && !cur) return go(null);            // a remembered folder that went away: start at home
        rowsEl.classList.remove("fading");
        setNote("Cannot open this folder: " + (e.message || e));
        return;
      }
      if (my !== seq) return;
      cur = res;
      LAST[group] = res.path;
      if (!multi) selected = new Set();
      pathEl.textContent = frontEllipsis(res.path, W - 2 * PAD, 10, FAMILY.mono);
      render();
    }
    function render() {
      rowsEl.textContent = "";
      const items = [];
      if (cur.parent) items.push({ name: "..", path: cur.parent, kind: "up" });
      for (const e of cur.entries) if (e.kind === "dir" || mode !== "folder") items.push(e);
      items.forEach((e, i) => {
        const r = h("div.ip-row", { style: { transform: `translateY(${i * ROW}px)` } });
        let x = 7;
        if (multi && isFile(e)) { r.check = h("div.ip-check", { style: { transform: `translate(${x}px, 6.5px)` } }); r.append(r.check); }
        if (multi) x += 20;
        r.append(h("div.ip-row-ic", { style: { transform: `translate(${x}px, 7.5px)` } }, kindGlyph(e.kind)));
        x += 18;
        const sizeS = e.size ? formatBytes(e.size) : "";
        const room = W - 2 * PAD - 2 - 14 - (sizeS ? textWidth(sizeS, 10, FAMILY.mono) + 10 : 0) - x;
        T(r, ellipsize(e.name, room, 11, isFile(e) ? FAMILY.sans : FAMILY.medium), x, 16, 11, isFile(e) ? FAMILY.sans : FAMILY.medium, "ip-row-name");
        if (sizeS) T(r, sizeS, W - 2 * PAD - 2 - 14 - textWidth(sizeS, 10, FAMILY.mono), 15.5, 10, FAMILY.mono, "ip-row-size");
        if (isFile(e)) r.dataset.path = e.path;
        r.addEventListener("click", () => activate(e, false));
        r.addEventListener("dblclick", () => activate(e, true));
        rowsEl.append(r);
      });
      rowsEl.style.height = items.length * ROW + "px";
      list.scrollTop = 0;
      const nothing = { open: "Nothing to open in this folder", files: "No footage in this folder", folder: "No folders here", save: "This folder is empty" }[mode];
      setNote(items.some(isFile) || items.some((e) => e.kind === "dir") ? "" : nothing);
      requestAnimationFrame(() => rowsEl.classList.remove("fading"));
      sync();
    }
    function activate(e, dbl) {
      if (!isFile(e)) { go(e.path); return; }
      if (mode === "open") { selected = new Set([e.path]); sync(); if (dbl) confirmPick(); }
      else if (multi) { if (selected.has(e.path)) selected.delete(e.path); else selected.add(e.path); sync(); }
      else if (save) { field.value = e.name; sync(); if (dbl) confirmPick(); }
    }
    async function confirmPick() {
      if (!sync() || md.closing) return;
      if (mode === "open") return finish([...selected][0]);
      if (multi) return finish([...selected]);
      if (mode === "folder") return finish(cur.path);
      let nm = field.value.trim();
      if (ensure) nm = ensure(nm);
      const full = cur.path.replace(/\/$/, "") + "/" + nm;
      if (cur.entries.some((e) => e.name === nm)) {
        const i = await ctx.confirm({ title: "Replace?", message: `"${nm}" already exists in this folder.`,
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
    const proj = (vm.model.peek() || {}).projectPath || "";
    const projDir = proj.includes("/") ? proj.slice(0, proj.lastIndexOf("/")) : null;
    go(LAST[group] || projDir || null);
  });
}
