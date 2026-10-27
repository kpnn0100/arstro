/*
 * Interstellar — shell.js: the App's frame. A port of app/App.{h,cpp}'s host-facing half:
 *
 *   - ONE root in LOGICAL px at 100 %, scaled by the page's UI scale (App::setUiScale /
 *     applyLogicalSize / rootTransform, App.cpp:191-219): s = vm.view.uiScale / 100, snapped to
 *     UI_SCALES, and a change EASES the drawn scale over 260 ms EaseOutCubic with the logical box
 *     re-derived every frame from the eased value (the first placement sets). The logical box is
 *     W / s × H / s, never smaller than App::minWidth() × App::minHeight() (App.h:82-83: the Edit
 *     screen's columns + the monitor's floor = 918, and max(Edit's 404.25, 560) high).
 *   - WEB-ONLY ADDITION (no native counterpart): when the window is smaller than that minimum at the
 *     chosen scale, the drawn scale goes DOWN until the minimum fits - s = min(s, W / minW, H / minH) -
 *     so a phone (390 × 844) shows the whole app, smaller, instead of a crop. The native window
 *     cannot get that small (the host asks GTK for minPhysicalWidth/Height), so it never needs it.
 *   - Screens Home / Loading / Edit follow model.screen and CROSS-FADE (motion::kScreenFadeMs 260 ms
 *     EaseInOutCubic, App::render); only the shown screen takes input; Loading takes none. Home and
 *     Loading are the slots home/home.js and home/loading.js; Edit is edit/screen.js.
 *   - The editor-wide keys (App::key / App::editKey, App.cpp:1060-1240), sending the same lines.
 *     Where a key needs a widget (the key graph's selection, the version dropdown) it asks through
 *     vm.provided(name) - see vm/app.js "providers".
 *   - The layers over every screen: `menus` (context menus, the menu strip's dropdowns) and `modals`
 *     (confirm, prompt, the file picker, Engine Settings), in root logical px.
 *   - The recovery offer (App::bindIfStale R-DLV-6): an autosave newer than the file is offered once.
 *
 * ctx - what every widget module's mount(host, vm, ctx) gets (PORTING §3):
 *   toast(text, isError = false)     the refusal chip over the monitor in Edit (EditScreen::showRefusal /
 *                                    showNotice), a toast elsewhere (the window says it on Edit's
 *                                    chip even on Home, where nobody sees it - D-15; the page does not)
 *   menu.open(items, x, y, opts) -> {close, isOpen}   cosmo's ContextMenu (ui/menu.js openMenu) at ROOT
 *                                    logical x, y; items [{label, action(menu)}] - the action runs after the
 *                                    menu closed, with the menu: (m) => m.enterRenameMode(name, onRename)
 *                                    morphs it into rename mode in place, as the native's Rename… does;
 *                                    an item with no action is inert text. One menu at a time.
 *   menu.openRename(name, x, y, onRename, {title}) -> {close}   ContextMenu's rename mode
 *   menu.isOpen() -> boolean
 *   confirm({title, message, buttons: [{label, primary?, destructive?, onClick?}]}) -> Promise<index | -1>
 *                                    cosmo's ConfirmDialog (ui/confirm.js), accent = the primary token
 *   prompt({title, message, value, ok, fields?: [{label, value}]}) -> Promise<string | string[] | null>
 *                                    NamePrompt (show / showFields): the topbar module provides the port
 *                                    (vm.provide("prompt", fn)); until then a plain one (dialogs/prompt.js)
 *   pick({mode, title, kinds, name, ext, ok}) -> Promise<path | path[] | null>   the board's file picker
 *                                    (dialogs/picker.js; modes open | files | folder | save)
 *   box: Signal<{x, y, w, h}>        THIS slot's rect: x, y in root logical px, w × h its size
 *   size: Signal<{w, h, s}>          the root's logical size and the drawn scale
 *   scale() -> number                the drawn scale now (eased × fitted)
 *   point(e) -> {x, y}               a pointer event in root logical px (for menus)
 *   layers: {menus, modals}          the elements those layers are
 *   slot(host, path, {name, box})    mount another module into `host` (kit/slot.js), its ctx's box = `box`
 */
import { signal, effect, untracked } from "./core/signal.js";
import { h, own, destroy } from "./core/dom.js";
import { Tween, Ease } from "./core/motion.js";
import { UI_SCALES, GRADE, CUT } from "./vm/app.js";
import { tok, SHELL } from "./kit/tokens.js";
import { slot } from "./kit/slot.js";
import { installed } from "./kit/files.js";
import { q, seconds, num } from "./kit/cmd.js";
import { mountEditScreen } from "./edit/screen.js";
import { pickSaveAs, pickProjectToOpen } from "./edit/menus.js";
import { pick } from "./dialogs/picker.js";

/** App::minWidth / minHeight (App.h:82-83): Edit's columns + the monitor's floor; Home's sidebar +
 *  padding + one card (HomeScreen.h:42-46: 300 + 2 × 32 + 220); Edit's column + 176 (EditScreen.h:73) vs 560. */
export function minSize() {
  const editW = SHELL.leftW + SHELL.rightW + SHELL.minMonitorW;
  const homeW = 300 + 2 * 32 + 220;
  const editH = SHELL.topBarH + SHELL.minMonitorH + SHELL.transportH + 176;
  return { w: Math.max(editW, homeW), h: Math.max(editH, 560) };
}
const snap = (v) => UI_SCALES.reduce((best, s) => (Math.abs(s - v) < Math.abs(best - v) ? s : best), 100);
const SCREENS = ["home", "loading", "edit"];

// the shared kit, borrowed from cosmo at install (web/BORROW); each with a fallback while it is not
// installed (asked through files.json first, so a missing module is not even requested)
const borrowed = (path, fallback) => installed("js/" + path)
  .then((has) => (has === false ? import(fallback) : import("./" + path).catch((e) => { console.warn("interstellar: " + path, e); return import(fallback); })));
const kit = {
  menu: borrowed("ui/menu.js", "./kit/menu-fallback.js"),
  confirm: borrowed("ui/confirm.js", "./kit/confirm-fallback.js"),
};

export function mount(el, vm, base = {}) {
  el.classList.add("is-shell");
  const root = h("div.is-root");
  el.append(root);

  // ---------------------------------------------------------------- the scale root
  const MIN = minSize();
  const size = signal({ w: MIN.w, h: MIN.h, s: 1 }, (a, b) => a.w === b.w && a.h === b.h && a.s === b.s);
  const scale = new Tween(1, () => apply());
  function apply() {
    const cw = Math.max(1, el.clientWidth), ch = Math.max(1, el.clientHeight);
    // eased user scale, then fitted DOWN so the minimum always fits (web-only, see the header)
    const s = Math.max(0.05, Math.min(scale.value, cw / MIN.w, ch / MIN.h));
    const w = Math.max(MIN.w, cw / s), hh = Math.max(MIN.h, ch / s);
    root.style.width = w + "px";
    root.style.height = hh + "px";
    root.style.transform = s === 1 ? "none" : `scale(${s})`;
    size.value = { w, h: hh, s };
  }
  let first = true;
  own(root, effect(() => {
    const s = snap(vm.view.uiScale.value) / 100;
    untracked(() => { if (first) { first = false; scale.set(s); } else scale.to(s, tok.ms("motion-reflow"), Ease.EaseOutCubic); });
  }));
  const ro = new ResizeObserver(apply);
  ro.observe(el);
  own(root, () => ro.disconnect());
  // Ctrl+wheel must never zoom the browser page over the app (it is the monitor's zoom, R-UI-13)
  root.addEventListener("wheel", (e) => { if (e.ctrlKey) e.preventDefault(); }, { passive: false });
  root.addEventListener("contextmenu", (e) => e.preventDefault());   // right-click is the app's (R-UI-9)

  // ---------------------------------------------------------------- screens and layers
  const screens = {};
  for (const name of SCREENS) screens[name] = h("div.is-screen.is-" + name);
  const menus = h("div.is-layer.is-menus");
  const modals = h("div.is-layer.is-modals");
  const toasts = h("div.is-toasts");
  root.append(screens.home, screens.loading, screens.edit, toasts, menus, modals);

  // ---------------------------------------------------------------- ctx
  let edit = null;                                   // the Edit screen's api (its chip)
  const shown = new Map();
  function toast(text, isError = false) {
    text = String(text || "");
    if (!text) return;
    const now = performance.now();
    if (now - (shown.get(text) || -1e9) < 1000) return;   // one refusal reaches us once, not per channel
    shown.set(text, now);
    if (vm.screen.peek() === "edit" && edit) { edit.showChip(text, isError); return; }
    const t = h("div.is-toast" + (isError ? ".err" : ""), { text });
    toasts.append(t);
    requestAnimationFrame(() => requestAnimationFrame(() => t.classList.add("on")));
    setTimeout(() => { t.classList.remove("on"); setTimeout(() => t.remove(), 360); }, 2600);
  }
  vm._setSink(toast);

  // ONE context menu per layer, as the native App has one ContextMenu (ui/menu.js): a new opening
  // replaces the last. While it is open it owns the keyboard (App::key: the menu takes the keys first).
  let current = null;
  const opening = (start) => {
    const holder = { real: null, cancelled: false, close() { this.cancelled = true; if (this.real) this.real.close(); },
                     isOpen() { return !this.cancelled && !!this.real && (!this.real.isOpen || this.real.isOpen()); } };
    current = holder;
    kit.menu.then((mod) => { if (!holder.cancelled) holder.real = start(mod); });
    return holder;
  };
  const menu = {
    open: (items, x, y, opts = {}) => opening((mod) => mod.openMenu(menus, items, x, y, opts)),
    openRename: (name, x, y, onRename, opts = {}) => opening((mod) => mod.openRename(menus, name, x, y, onRename, opts)),
    isOpen: () => !!current && current.isOpen(),
  };
  function confirm({ title, message, buttons = [{ label: "OK", primary: true }] } = {}) {
    return new Promise((resolve) => {
      let done = false;
      const finish = (i) => { if (!done) { done = true; resolve(i); } };
      kit.confirm.then((mod) => {
        // ui/confirm.js: the button's onClick runs after the dialog started closing; onClose(index | null)
        // once it is answered or dismissed. The accent is var(--primary) - Interstellar's, from tokens.css.
        mod.confirmDialog(modals, {
          title, message,
          buttons: buttons.map((b, i) => ({ ...b, onClick: () => { finish(i); if (b.onClick) b.onClick(); } })),
          onClose: (i) => finish(i === null || i === undefined ? -1 : i),
          onCancel: () => finish(-1),
        });
      });
    });
  }
  function prompt(opts = {}) {
    const p = vm.provided("prompt");
    if (p) return p(opts);
    return import("./dialogs/prompt.js").then((mod) => mod.prompt(modals, opts));
  }
  const ctx = {
    ...base,
    toast, menu, confirm, prompt,
    pick: (opts) => pick(vm, ctxFor(null), opts),
    size,
    scale: () => size.peek().s,
    point(e) {
      const r = root.getBoundingClientRect(), s = size.peek().s || 1;
      return { x: (e.clientX - r.left) / s, y: (e.clientY - r.top) / s };
    },
    layers: { menus, modals },
  };
  /** A slot's own ctx: the shared one with ITS box. */
  function ctxFor(box) {
    const c = { ...ctx, box: box || signal({ x: 0, y: 0, w: size.peek().w, h: size.peek().h }) };
    c.slot = (host, path, opts = {}) => slot(host, path, vm, ctxFor(opts.box || null), { name: opts.name });
    return c;
  }
  ctx.slot = (host, path, opts = {}) => slot(host, path, vm, ctxFor(opts.box || null), { name: opts.name });
  ctx.child = ctxFor;

  // ---------------------------------------------------------------- the three screens
  const fullBox = signal({ x: 0, y: 0, w: MIN.w, h: MIN.h }, (a, b) => a.w === b.w && a.h === b.h);
  own(root, effect(() => { const s = size.value; fullBox.value = { x: 0, y: 0, w: s.w, h: s.h }; }));
  placeholderHome(screens.home);
  placeholderLoading(screens.loading);
  slot(screens.home, "home/home.js", vm, ctxFor(fullBox), { name: "HomeScreen" });
  slot(screens.loading, "home/loading.js", vm, ctxFor(fullBox), { name: "LoadingView" });
  edit = mountEditScreen(screens.edit, vm, ctxFor(fullBox));

  // cross-fade (App::render, App.cpp:1523-1540): first placement sets, later changes ease
  const fades = {};
  for (const name of SCREENS) {
    const host = screens[name];
    fades[name] = new Tween(0, (v) => {
      host.style.opacity = String(v);
      host.style.visibility = v <= 0.001 && vm.screen.peek() !== name ? "hidden" : "";
    });
  }
  let placed = false;
  own(root, effect(() => {
    const want = vm.ready.value ? vm.screen.value : "home";
    untracked(() => {
      for (const name of SCREENS) {
        const on = name === want;
        screens[name].inert = !on || name === "loading";   // the transition is not interactive
        if (on) screens[name].style.visibility = "";
        if (!placed) fades[name].set(on ? 1 : 0);
        else if (on || fades[name].value > 0) fades[name].to(on ? 1 : 0, tok.ms("motion-screen-fade"), Ease.EaseInOutCubic);
      }
      placed = true;
    });
  }));

  // ---------------------------------------------------------------- the recovery offer (R-DLV-6)
  let recoveryAsked = "";
  own(root, effect(() => {
    const v = vm.model.value;
    if (!v || v.screen !== "edit" || !v.recoveryAvailable) return;
    const key = v.projectPath + "|" + v.recoveryTime;
    if (key === recoveryAsked) return;
    recoveryAsked = key;
    // Cosmo's dialog draws one line: the message is short on purpose (design rule R5) - App.cpp:1301-1306
    untracked(() => confirm({ title: "Recover unsaved changes?", message: "An autosave is newer than this project's file.",
      buttons: [{ label: "Discard", destructive: true, onClick: () => vm.cmd("project recover --discard") },
                { label: "Recover", primary: true, onClick: () => vm.cmd("project recover") }] }));
  }));

  // ---------------------------------------------------------------- the keyboard (App::key / editKey)
  let kDown = false;                                  // R-EDT-2: K held - J/L step a frame
  const textEditing = (e) => {
    const t = e.target;
    return !!(t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.tagName === "SELECT" || t.isContentEditable));
  };
  function onKeyUp(e) { if (e.key === "k" || e.key === "K") kDown = false; }
  function onKeyDown(e) {
    if (e.defaultPrevented) return;
    const scr = vm.ready.peek() ? vm.screen.peek() : "home";
    if (scr === "loading") { e.preventDefault(); return; }
    if (vm.view.dialog.peek()) return;                // a modal owns the keyboard
    if (scr === "home") return;                       // the launcher owns the keyboard (home/home.js)
    if (menu.isOpen()) return;                        // the menu owns the keyboard (its rename field types)
    const versions = vm.provided("versions");
    if (versions && versions.isOpen && versions.isOpen()) {
      if (e.key === "Escape") versions.close();
      e.preventDefault();
      return;
    }
    if (textEditing(e)) return;
    if (editorKey(e)) { e.preventDefault(); e.stopPropagation(); }
  }
  window.addEventListener("keydown", onKeyDown);
  window.addEventListener("keyup", onKeyUp);
  own(root, () => { window.removeEventListener("keydown", onKeyDown); window.removeEventListener("keyup", onKeyUp); });

  const fpsOf = (v) => (v.fps > 0 ? v.fps : 24);
  /** App::key's Edit branch (App.cpp:1082-1186). True = handled. */
  function editorKey(e) {
    const ctrl = e.ctrlKey || e.metaKey;
    if (ctrl && editKey(e)) return true;
    const v = vm.model.peek() || {};
    const tab = vm.tab.peek();
    const fps = fpsOf(v);
    const ph = vm.playhead.peek();
    const key = e.key.length === 1 ? e.key.toUpperCase() : e.key;
    // R-EDT-5: Alt+1…9 switches the multicam clip under the playhead to that angle
    const digit = /^Digit([1-9])$/.exec(e.code || "");
    if (e.altKey && digit && tab === CUT && v.multicamClip) {
      const n = Number(digit[1]);
      if (n <= (v.multicamAngles || []).length) vm.cmd("multicam angle " + n);
      return true;
    }
    if (ctrl || e.altKey) return false;
    const srcView = tab === CUT && !!v.sourceView;
    switch (key) {
      case " ": vm.cmd(vm.playing.peek() ? "pause" : "play"); return true;
      case "ArrowLeft": case "ArrowRight": {
        const d = (key === "ArrowLeft" ? -1 : 1) / fps;
        if (srcView) {
          const t = vm.transport.peek();
          const sp = t && t.sourceView === v.sourceView ? t.sourcePlayhead : v.sourcePlayhead;
          vm.cmd("source playhead " + seconds(Math.max(0, sp + d), fps));
        } else vm.cmd("playhead " + seconds(ph + d, fps));
        return true;
      }
      case "J": case "L": {
        const fwd = key === "L";
        if (kDown) vm.cmd("playhead " + seconds(Math.max(0, ph + (fwd ? 1 : -1) / fps), fps));
        else vm.cmd(fwd ? "shuttle forward" : "shuttle back");
        return true;
      }
      case "K":
        if (!e.repeat) { kDown = true; if (vm.playing.peek()) vm.cmd("shuttle stop"); }
        return true;
      case "I": case "O":
        vm.cmd("mark " + (key === "I" ? "in" : "out") + (srcView ? " --source" : ""));
        return true;
      case ",": vm.cmd("edit insert"); return true;
      case ".": vm.cmd("edit overwrite"); return true;
      case "Escape":
        if (srcView) { vm.cmd("source view none"); return true; }
        return false;
      case "Delete": case "Backspace":
        if (tab === CUT && v.selectedClip) { vm.cmd("clip delete " + q(v.selectedClip) + (e.shiftKey ? " --ripple" : "")); return true; }
        return false;
      case "H":
        if (e.shiftKey && tab === GRADE) { vm.cmd("view matte " + (v.matteView ? "off" : "on")); return true; }
        return false;
      case "M":
        if (tab === CUT && v.duration > 0) { vm.cmd("marker add " + vm.freshMarkerName() + " --at " + seconds(ph, fps)); return true; }
        return false;
      case "S":
        if (tab === CUT && v.selectedClip) { vm.cmd("clip split " + q(v.selectedClip) + " --at " + seconds(ph, fps)); return true; }
        return false;
      case "1": case "2": case "3":
        vm.setTab(Number(key) - 1);
        return true;
      default:
        return false;
    }
  }
  /** App::editKey (App.cpp:1190-1245): cosmo's accelerators and the Cut tab's clip / key clipboard. */
  function editKey(e) {
    const v = vm.model.peek() || {};
    const tab = vm.tab.peek();
    const key = e.key.length === 1 ? e.key.toUpperCase() : e.key;
    switch (key) {
      case "Z": vm.cmd(e.shiftKey ? "redo" : "undo"); return true;
      case "Y": vm.cmd("redo"); return true;
      case "S":
        if (e.shiftKey) pickSaveAs(vm, ctxFor(null)); else vm.cmd("project save");
        return true;
      case "O": pickProjectToOpen(vm, ctxFor(null)); return true;
      case "G":
        if (e.altKey) { vm.grabStill(); return true; }          // R-CLR-4: Ctrl+Alt+G, Resolve's grab
        vm.cmd("rack group new");
        return true;
      case "D": {
        const n = vm.gradeTarget.peek();                       // Duplicate as Variant (R-RACK-5)
        if (!n || n.group || n.failed || !n.bindName) return false;
        vm.cmd("rack duplicate " + q(n.bindName));
        return true;
      }
      case "C": case "V": case "X": {
        if (tab === CUT) {
          // keys first, when the key lane holds a selection or the clipboard holds keys (R-ANIM-7)
          const kg = vm.provided("keyGraph");
          if (kg && kg.shown && kg.shown()) {
            if (key === "C" && kg.selectionCount() > 0) { vm.cmd("key copy --keys " + q(kg.selectionList())); return true; }
            const now = kg.now();
            if (key === "V" && v.keyClipboardCount > 0 && now >= 0) { vm.cmd("key paste --at " + num(now)); return true; }
          }
          // a CLIP on the Cut tab (R-TL-6): copy / cut / paste at the playhead
          if (key === "V") { if (!v.hasClipClipboard) return false; vm.cmd("clip paste"); return true; }
          if (!v.selectedClip) return false;
          const id = q(v.selectedClip);
          vm.cmd("clip copy " + id).then((r) => { if (r.ok && key === "X") vm.cmd("clip delete " + id); });
          return true;
        }
        if (key === "X" || tab !== GRADE) return false;
        const b = vm.selectedBind.peek();
        if (!b) return false;
        vm.cmd((key === "C" ? "grade copy " : "grade paste ") + q(b));
        return true;
      }
      default: return false;
    }
  }

  // ---------------------------------------------------------------- dialogs by name (vm.view.dialog)
  let dialogApi = null;
  own(root, effect(() => {
    const spec = vm.view.dialog.value;
    untracked(() => {
      if (dialogApi) { try { dialogApi.close(); } catch (e) { console.error(e); } dialogApi = null; }
      if (!spec) return;
      import("./kit/files.js").then(({ installed }) => installed(`js/dialogs/${spec.name}.js`)).then((has) => {
        if (has === false) {
          toast(spec.name === "settings" ? "Engine Settings is not in this page yet" : spec.name + " is not in this page yet", true);
          vm.view.dialog.value = null;
          return;
        }
        return import(`./dialogs/${spec.name}.js`).then((mod) => {
          if (vm.view.dialog.peek() !== spec) return;
          dialogApi = mod.open(modals, vm, ctxFor(null), spec) || null;
        });
      }).catch((e) => { console.error(e); toast("This dialog failed to open: " + e.message, true); vm.view.dialog.value = null; });
    });
  }));

  apply();
  return { root, ctx, size, destroy: () => destroy(root) };
}

/** Home's frame until home/home.js exists: the background, the sidebar plate and its rule
 *  (HomeScreen::onPaint: background, leftRailBg 0..kSidebarW, a border at its edge). */
function placeholderHome(host) {
  host.append(h("div.is-placeholder.is-ph-home", null, h("div.is-ph-home-side"), h("div.is-ph-label", { text: "HomeScreen" })));
}
/** LoadingView::onPaint: the background. */
function placeholderLoading(host) {
  host.append(h("div.is-placeholder.is-ph-plain", null, h("div.is-ph-label", { text: "LoadingView" })));
}
