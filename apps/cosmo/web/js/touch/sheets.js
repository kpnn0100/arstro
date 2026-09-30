/*
 * Cosmo by arstro — touch/sheets.js: the touch shell's modal layer.
 *
 * Ports PhoneApp.cpp's SheetLayer (:740-893: the overflow ("more") sheet, Engine Settings, the
 * History tree card, the "Reset workspace?" confirm), PresetDrawer (:677-738) and FileBrowser
 * (:1193-1319), and hosts the fullscreen curve editor (curve.js). Everything here is a deliberate
 * modal overlay with a scrim (R-TOUCH-2): scrim 0 -> .55 over 180 ms EaseOutCubic and a 240 ms
 * rise; closing reverses both over 140 ms EaseInCubic; a tap outside every zone closes.
 *
 * Deliberate deviations (screens_touch.md §14.12-14.14, §14.16):
 *  - a sheet slides back out as it closes (native only fades the scrim, then drops it);
 *  - Copy Settings / Paste to All Images are shown disabled: no command carries them (core gap G12);
 *  - Ungroup sends the edit group / the selected group (native sends -1, which ungroups nothing);
 *  - the History card shows the service's real tree (model `history`) and jumps with
 *    `history jump <i>`; the drawer lists the real presets (model `presets`, native: mock data)
 *    and double-tap applies one;
 *  - the file browser works over the board's folders (adapter `browse`) in three modes - Open
 *    Project, Import (pick photos or a whole folder) and New Project (folder + name, replace
 *    confirmation) - instead of the native's Open Image / Import Folder;
 *  - the sheets scroll when the screen is shorter than they are (landscape), and ConfirmDialog's
 *    three-button "Unsaved changes" and a preset-name prompt reuse the confirm card.
 */
import { h, bindEffect, bindText, bindClass, keyed, destroy, own } from "../core/dom.js";
import { signal, computed, untracked } from "../core/signal.js";
import { reducedMotion } from "../core/motion.js";
import { icon } from "../ui/icons.js";
import { tx, rect, ticon, gestures, pressWash, baselineBox, slideIn } from "./tk.js";
import { segmented, toggle } from "./controls.js";
import { curveFull } from "./curve.js";

const quiet = (p) => { if (p && p.catch) p.catch(() => {}); return p; };
const CLOSE_MS = { sheet: 140, card: 140, drawer: 240, full: 140, browser: 140 };

export function createSheets(layer, vm, ctx) {
  let current = null;        // {root, kind, done}

  /** Show one modal; `build(close)` returns the panel. Resolves with what `close(v)` gets. */
  function show(kind, build) {
    if (current) current.close(undefined, true);
    let resolve;
    const p = new Promise((r) => { resolve = r; });
    const root = h("div.t-modal." + kind);
    const scrim = h("div.t-scrim");
    root.append(scrim);
    let closed = false;
    const close = (value, fast) => {
      if (closed) return;
      closed = true;
      if (current && current.root === root) current = null;
      root.classList.remove("open");
      root.classList.add("closing");
      const ms = reducedMotion() || fast ? 0 : CLOSE_MS[kind];
      setTimeout(() => { destroy(root); root.remove(); }, ms + 30);
      resolve(value);
    };
    const panel = build(close);
    panel.classList.add("t-mpanel");
    root.append(panel);
    gestures(scrim, { tap: () => close(null) });
    layer.append(root);
    current = { root, kind, close };
    requestAnimationFrame(() => requestAnimationFrame(() => { if (!closed) root.classList.add("open"); }));
    return p;
  }

  // ---------------------------------------------------------------- bottom sheets
  /** A bottom sheet of height hh (POP r12 + the grab handle); taps on it outside a zone close. */
  function sheetPanel(hh, close) {
    const panel = h("div.t-sheet");
    panel.style.height = `min(${hh}px, calc(100% - 24px))`;
    panel.append(h("i.t-handle"));
    gestures(panel, { tap: () => close(null) });
    return panel;
  }
  const row = (label, { y, danger, disabled, onTap }) => {
    const r = h("div.t-srow" + (danger ? ".danger" : "") + (disabled ? ".dis" : ""));
    r.style.top = y + "px";
    r.append(tx(label, 24, 29, 15, "sans"), h("i.t-hl.srow"));
    if (!disabled) gestures(r, { down: () => pressWash(r), tap: onTap });
    return r;
  };

  function overflow() {
    return show("sheet", (close) => {
      const panel = sheetPanel(24 + 9 * 48 + 16, close);
      const list = h("div.t-slist.vscroll");
      const inner = h("div.t-slist-in");
      inner.style.height = 9 * 48 + 16 + "px";
      list.append(inner);
      const hist = vm.history.peek();
      const eg = vm.editGroup.peek();
      const selGroup = eg >= 0 ? eg : [...vm.selectedIds.peek()].find((n) => (vm.nodeById.peek().get(n) || {}).kind === "group");
      const items = [
        ["Undo", { disabled: !hist.canUndo, onTap: () => { quiet(vm.undo()); close(); } }],
        ["Redo", { disabled: !hist.canRedo, onTap: () => { quiet(vm.redo()); close(); } }],
        ["Copy Settings", { disabled: true }],                               // G12: no command
        ["Paste to All Images", { disabled: true }],                         // G12
        ["Group Selection", { disabled: !vm.imageCount.peek(), onTap: () => { quiet(vm.groupNew()); close(); } }],
        ["Ungroup Selection", { disabled: selGroup === undefined || selGroup < 0, onTap: () => { quiet(vm.ungroup(selGroup)); close(); } }],
        ["Show History Tree", { onTap: () => history() }],
        ["Engine Settings", { onTap: () => settings() }],
        ["Reset Workspace", { danger: true, onTap: () => resetConfirm() }],
      ];
      items.forEach(([label, o], i) => inner.append(row(label, { y: i * 48, ...o })));
      panel.append(list);
      return panel;
    });
  }

  function settings() {
    return show("sheet", (close) => {
      const panel = sheetPanel(260, close);
      const body = h("div.t-slist.vscroll");
      const inner = h("div.t-slist-in");
      inner.style.height = "236px";
      body.append(inner);
      const S = () => vm.settings.value;
      inner.append(tx("Engine Settings", 20, 16, 15, "semibold", "left", "t-fg"),
                   tx("PREVIEW QUALITY", 20, 50, 11, "medium", "left", "t-muted"));
      const q = segmented(["Draft", "Standard", "High"], {
        sel: () => { const e = S().previewEdge || 1600; return e <= 1200 ? 0 : e <= 2000 ? 1 : 2; },
        pick: (i) => quiet(vm.settingsSet({ previewEdge: [1000, 1600, 2400][i] })),
      });
      rect(q, 20, 60, "calc(100% - 40px)", 30);
      inner.append(q, tx("CPU THREADS", 20, 114, 11, "medium", "left", "t-muted"));
      const th = segmented(["Auto", "2", "4", "8"], {
        sel: () => ({ 0: 0, 2: 1, 4: 2, 8: 3 })[S().threads | 0] ?? 0,
        pick: (i) => quiet(vm.settingsSet({ threads: [0, 2, 4, 8][i] })),
      });
      rect(th, 20, 124, "calc(100% - 40px)", 30);
      const gpuL = tx("", 20, 190, 14, "sans");
      bindText(gpuL, () => (S().gpuAvailable ? "GPU Acceleration" : "GPU Acceleration . unavailable"));
      bindClass(gpuL, "t-muted", () => !S().gpuAvailable);
      bindClass(gpuL, "t-fg", () => S().gpuAvailable);
      const tg = toggle({ on: () => S().useGpu && S().gpuAvailable, flip: () => { if (S().gpuAvailable) quiet(vm.settingsSet({ useGpu: S().useGpu ? 0 : 1 })); } });
      tg.style.left = "calc(100% - 60px)"; tg.style.top = "174px";
      inner.append(th, gpuL, tg);
      panel.append(body);
      return panel;
    });
  }

  // ---------------------------------------------------------------- centred cards
  function card(cls, close, { x = 40, w = "calc(100% - 80px)", hh }) {
    const c = h("div.t-card" + (cls ? "." + cls : ""));
    c.style.left = x + "px"; c.style.width = w;
    if (hh !== undefined) { c.style.height = hh + "px"; c.style.top = `calc(50% - ${hh / 2}px)`; }
    gestures(c, { tap: () => {} });                 // a tap on the card is not "outside"
    return c;
  }

  function history() {
    return show("card", (close) => {
      const c = card("hist", close, { x: 24, w: "calc(100% - 48px)" });
      c.append(tx("History", 16, 30, 15, "semibold", "left", "t-fg"));
      const x = h("div.t-ibtn");
      rect(x, "calc(100% - 44px)", 8, 40, 32);
      const bi = ticon("back", 20, 1.6); bi.style.left = "8px"; bi.style.top = "4px";
      x.append(bi);
      gestures(x, { down: () => pressWash(x), tap: () => close() });
      c.append(x);
      const list = h("div.t-hlist.vscroll");
      c.append(list);
      const inner = h("div.t-hlist-in");
      list.append(inner);
      let seen = "";
      bindEffect(inner, () => {
        const hs = vm.history.value;
        const key = JSON.stringify(hs);
        if (key === seen) return;
        seen = key;
        const entries = hs.entries && hs.entries.length ? hs.entries : [{ parent: -1, label: hs.label || "Open" }];
        const cur = hs.entries && hs.entries.length ? hs.current : 0;
        inner.replaceChildren();
        entries.forEach((n, i) => {
          let depth = 0;
          for (let p = n.parent, g = 0; p >= 0 && g < 999; p = (entries[p] || {}).parent ?? -1, g++) depth++;
          const y = 16 + i * 34, nx = 24 + depth * 24;
          const r = h("div.t-hnode" + (i === cur ? ".cur" : ""));
          r.style.top = y - 16 + "px";
          if (n.parent >= 0) {
            const py = 16 + n.parent * 34;
            const ln = h("i.t-hconn");
            // from (nx-12, y-20) to (nx, y): a straight connector (PhoneApp.cpp:862)
            const dx = 12, dy = Math.min(20, y - py);
            ln.style.left = nx - dx + "px"; ln.style.top = 16 - dy + "px";
            ln.style.width = Math.hypot(dx, dy) + "px";
            ln.style.transform = `rotate(${Math.atan2(dy, dx)}rad)`;
            r.append(ln);
          }
          const dot = h("i.t-hdot"); dot.style.left = nx - 5 + "px"; r.append(dot);
          r.append(tx(n.label || "Edit", nx + 14, 21, 13, "sans"));
          if (hs.entries && hs.entries.length) gestures(r, { down: () => pressWash(r), tap: () => { quiet(vm.historyJump(i)); close(); } });
          inner.append(r);
        });
        inner.style.height = 16 + entries.length * 34 + "px";
      });
      return c;
    });
  }

  /** A confirm card (PhoneApp.cpp:869-879): title, message, buttons right-aligned. Resolves with the
   *  pressed button's value (null outside). buttons = [{label, value, kind: "destructive"|"primary"}]. */
  function confirm({ title, message, buttons }) {
    return show("card", (close) => {
      const c = card("confirm", close, { hh: 150 });
      // the message wraps (a phone card is narrower than the desktop's 400): 18 px lines, the
      // first baseline at +62 as the native, room for two above the buttons at +98
      const msg = tx(message, 20, 62, 13, "sans", "left", "t-muted t-wrap");
      msg.style.lineHeight = "18px";
      msg.style.top = 62 - (Math.floor((18 - 12 - 3) / 2) + 12) + "px";
      c.append(tx(title, 20, 34, 15, "semibold", "left", "t-fg t-ell"), msg);
      const n = buttons.length;
      const bw = n <= 2 ? "90px" : `calc((100% - 56px) / ${n})`;
      buttons.forEach((b, i) => {
        const k = n - 1 - i;                          // laid out right to left
        const el = h("div.t-cbtn" + (b.kind ? "." + b.kind : ""));
        el.style.width = bw;
        el.style.left = n <= 2 ? `calc(100% - ${20 + 90 * (k + 1) + 8 * k}px)` : `calc(20px + ${i} * ((100% - 56px) / ${n} + 8px))`;
        baselineBox(el, 36, 22.5, 13, b.kind ? "semibold" : "sans");
        el.textContent = b.label;
        gestures(el, { down: () => pressWash(el), tap: () => close(b.value) });
        c.append(el);
      });
      return c;
    });
  }
  function resetConfirm() {
    return confirm({ title: "Reset workspace?", message: "This clears all photos and edits.",
                     buttons: [{ label: "Cancel", value: false }, { label: "Reset", value: true, kind: "destructive" }] })
      .then((ok) => { if (ok) quiet(vm.closeProject()); });
  }

  /** A one-field prompt in the confirm card (the host's "Save preset" dialog, §5.6). */
  function prompt({ title, placeholder, confirm: ok = "Save", value = "" }) {
    return show("card", (close) => {
      const c = card("prompt", close, { hh: 170 });
      c.append(tx(title, 20, 34, 15, "semibold", "left", "t-fg"));
      const field = h("div.t-field");
      rect(field, 20, 54, "calc(100% - 40px)", 40);
      const input = h("input.t-input", { type: "text", placeholder, value, spellcheck: "false", autocomplete: "off" });
      field.append(input);
      c.append(field);
      const submit = () => { const v = input.value.trim(); if (v && !/["#\n]/.test(v)) close(v); };
      input.addEventListener("keydown", (e) => { if (e.key === "Enter") submit(); if (e.key === "Escape") close(null); });
      [["Cancel", () => close(null), ""], [ok, submit, "primary"]].forEach(([label, fn, kind], i) => {
        const b = h("div.t-cbtn" + (kind ? "." + kind : ""));
        b.style.width = "90px";
        b.style.left = `calc(100% - ${20 + 90 * (2 - i) + 8 * (1 - i)}px)`;
        b.style.top = "114px";
        baselineBox(b, 36, 22.5, 13, kind ? "semibold" : "sans");
        b.textContent = label;
        gestures(b, { down: () => pressWash(b), tap: fn });
        c.append(b);
      });
      setTimeout(() => input.focus(), 60);
      return c;
    });
  }

  // ---------------------------------------------------------------- preset drawer
  function drawer() {
    return show("drawer", (close) => {
      const panel = h("div.t-drawer");
      panel.append(tx("PRESETS", 16, 30, 11, "semibold", "left", "t-muted"));
      const back = h("div.t-ibtn");
      rect(back, 256, 0, 44, 44);
      const bi = ticon("back", 20, 1.75); bi.style.left = "4px"; bi.style.top = "14px";
      back.append(bi);
      gestures(back, { down: () => pressWash(back), tap: () => close() });
      panel.append(back);
      const list = h("div.t-dlist.vscroll");
      panel.append(list);
      const open = signal(0);
      const sel = signal("");
      const tree = computed(() => {
        const out = [], by = new Map();
        for (const p of vm.presets.value || []) {
          const f = p.folder || "Presets";
          if (!by.has(f)) { const n = { name: f, items: [] }; by.set(f, n); out.push(n); }
          by.get(f).items.push(p);
        }
        return out;
      });
      const empty = tx("No presets yet", 16, 76, 13, "sans", "left", "t-muted t-fade");
      bindClass(empty, "off", () => tree.value.length > 0);
      panel.append(empty);
      const rows = computed(() => {
        const r = [];
        tree.value.forEach((f, i) => {
          r.push({ key: "f:" + f.name, folder: true, i, name: f.name });
          if (open.value === i) for (const p of f.items) r.push({ key: "p:" + p.path, folder: false, p, name: p.name });
        });
        return r;
      });
      keyed(list, () => rows.value, (r) => r.key, (r) => {
        const el = h("div.t-drow" + (r.folder ? ".folder" : ".leaf"));
        if (r.folder) {
          const ch = icon("chevronRight", 20, 1.3);
          ch.classList.add("t-dchev");
          el.append(ch, tx(r.name, 40, 28, 13, "medium", "left", "t-fg"), h("i.t-hl.drow"));
          gestures(el, { down: () => pressWash(el), tap: () => { open.value = open.peek() === r.i ? -1 : r.i; } });
        } else {
          el.append(h("i.t-drule"), tx(r.name, 40, 27, 13, "sans", "left", "t-dname"));
          bindClass(el, "on", () => sel.value === r.p.path);
          gestures(el, {
            down: () => pressWash(el),
            tap: () => { sel.value = r.p.path; },
            doubleTap: () => { sel.value = r.p.path; quiet(vm.applyPreset(r.p.path)); close(); },
          });
        }
        return el;
      });
      return panel;
    });
  }

  // ---------------------------------------------------------------- file browser over `browse`
  let lastDir = "";
  /** mode "open" -> a .cmp path; "import" -> [image paths]; "new" -> a new .cmp path. */
  function browse({ mode, dir, name }) {
    return show("browser", (close) => {
      const c = h("div.t-card.browser");
      gestures(c, { tap: () => {} });
      const title = { open: "Open Project", import: "Import Catalog", new: "New Project" }[mode];
      c.append(tx(title, 16, 30, 15, "semibold", "left", "t-fg"));
      const x = h("div.t-ibtn");
      rect(x, "calc(100% - 46px)", 6, 44, 36);
      const bi = ticon("back", 20, 1.6); bi.style.left = "8px"; bi.style.top = "6px";
      x.append(bi);
      gestures(x, { down: () => pressWash(x), tap: () => close(null) });
      c.append(x);
      const pathL = tx("", 16, 52, 11, "mono", "left", "t-muted");
      c.append(pathL);
      const list = h("div.t-blist.vscroll");
      list.classList.add(mode);
      c.append(list);
      const at = signal(null);             // the browse answer
      const picked = signal(new Set());
      const go = (p) => vm.browse(p).then((r) => { at.value = r; lastDir = r.path; picked.value = new Set(); list.scrollTop = 0; slideIn(list, 24); })
        .catch((e) => ctx.toast(e.message || String(e), true));
      go(dir || lastDir || undefined);
      bindEffect(pathL, () => { const p = at.value ? at.value.path : ""; pathL.textContent = p.length > 33 ? "..." + p.slice(-30) : p; });
      const entries = computed(() => {
        const r = at.value;
        if (!r) return [];
        const want = mode === "import" ? "image" : "project";
        const out = r.parent ? [{ name: "..", path: r.parent, kind: "up" }] : [];
        for (const e of r.entries) if (e.kind === "dir" || e.kind === want) out.push(e);
        return out;
      });
      keyed(list, () => entries.value, (e) => e.path + "|" + e.kind, (e) => {
        const r = h("div.t-brow." + e.kind);
        if (e.kind === "dir" || e.kind === "up") { const f = ticon("folderOpen", 18, 1.6); f.classList.add("t-bic"); r.append(f); }
        else r.append(h("i.t-bsq"));
        r.append(tx(e.name, 52, 27, 13, "sans", "left", "t-bname"), h("i.t-hl.brow"));
        if (e.kind === "image") bindClass(r, "on", () => picked.value.has(e.path));
        gestures(r, {
          down: () => pressWash(r),
          tap: () => {
            if (e.kind === "dir" || e.kind === "up") return go(e.path);
            if (mode === "open" && e.kind === "project") return close(e.path);
            if (mode === "new" && e.kind === "project") { input.value = e.name; return; }
            if (mode === "import") { const s = new Set(picked.peek()); if (s.has(e.path)) s.delete(e.path); else s.add(e.path); picked.value = s; }
          },
        });
        return r;
      }, null, { animate: false });
      let input = null;
      if (mode === "new") {
        const field = h("div.t-field.bname");
        input = h("input.t-input", { type: "text", value: name || "Untitled.cmp", spellcheck: "false", autocomplete: "off" });
        field.append(input);
        c.append(field);
      }
      if (mode !== "open") {
        const btn = h("div.t-bbtn");
        baselineBox(btn, 36, 22.5, 13, "semibold");
        bindText(btn, () => {
          if (mode === "new") return "Create";
          const n = picked.value.size;
          const all = (at.value ? at.value.entries : []).filter((e) => e.kind === "image").length;
          return n ? `Import ${n} photo${n === 1 ? "" : "s"}` : `Import this folder (${all})`;
        });
        bindClass(btn, "off", () => {
          if (mode === "new") return !at.value;
          return !picked.value.size && !(at.value ? at.value.entries : []).some((e) => e.kind === "image");
        });
        gestures(btn, {
          down: () => { if (!btn.classList.contains("off")) pressWash(btn); },
          tap: async () => {
            if (btn.classList.contains("off") || !at.value) return;
            if (mode === "import") {
              const paths = picked.peek().size ? [...picked.peek()] : at.value.entries.filter((e) => e.kind === "image").map((e) => e.path);
              return close({ paths, dir: at.value.path });
            }
            let nm = input.value.trim();
            if (!nm || /["#\n/]/.test(nm)) return;
            if (!/\.cmp$/i.test(nm)) nm += ".cmp";
            const path = at.value.path.replace(/\/$/, "") + "/" + nm;
            if (at.value.entries.some((e) => e.path === path || e.name === nm)) {
              const ok = await confirmInline(c, nm);
              if (!ok) return;
            }
            close(path);
          },
        });
        c.append(btn);
      }
      return c;
    });
  }
  /** The overwrite question, inside the browser card (so the browser stays open behind it). */
  function confirmInline(host, nm) {
    return new Promise((res) => {
      const m = h("div.t-inline");
      m.append(tx(`Replace ${nm}?`, 16, 26, 14, "semibold", "left", "t-fg t-ell"));
      [["Cancel", false, ""], ["Replace", true, "destructive"]].forEach(([l, v, k], i) => {
        const b = h("div.t-cbtn" + (k ? "." + k : ""));
        b.style.width = "90px"; b.style.top = "40px";
        b.style.left = `calc(100% - ${16 + 90 * (2 - i) + 8 * (1 - i)}px)`;
        baselineBox(b, 36, 22.5, 13, k ? "semibold" : "sans");
        b.textContent = l;
        gestures(b, { tap: () => { m.remove(); res(v); } });
        m.append(b);
      });
      host.append(m);
    });
  }

  // ---------------------------------------------------------------- fullscreen curve
  function curve(o) { return show("full", (close) => curveFull(vm, o, () => close())); }

  function isOpen() { return !!current; }
  return { overflow, settings, history, confirm, prompt, drawer, browse, curve, isOpen, close: () => current && current.close(null) };
}
