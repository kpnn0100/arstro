/*
 * Solaris — shell.js: App.cpp in the page — the logical-px root, the screens, the app-wide keys, the modal layers, the
 * toast, the cursor and the keyboard's focus, and the `ctx` every widget module is handed (PORTING.md §3).
 *
 * THE ROOT (App::setSize / applyLogicalSize / render, App.cpp:281-296, 982-997). Everything is laid out in LOGICAL px
 * at 100 % inside one element scaled by the drawn scale, transform-origin 0 0. The logical size is the window's
 * divided by the drawn scale — there is NO logical minimum (the native at 150 % in a 1024×640 window lays out
 * 683×427: shots/ui-scale-150-1024x640). The page's uiScale (vm.view.uiScale — per page) EASES: 260 ms EaseOutCubic
 * (App::kScaleAnimMs) with the layout following every frame; the first placement does not travel.
 *   WEB-ONLY: the native window cannot be smaller than 960×600 (App::minWidth/minHeight → gtk_widget_set_size_request,
 *   linux_main.cpp:463); a browser window can. Below that floor the page is drawn as if the window were the floor,
 *   scaled DOWN to fit: fit = min(1, W / 960, H / 600), drawn = uiScale × fit, logical = window / drawn — so a phone
 *   (390×844) shows the whole app (960 logical px wide), never a crop. At or above 960×600, fit = 1: the native's
 *   arithmetic exactly.
 *
 * SCREENS (App::render, App.cpp:956-970): home | project from the model's `screen`, CROSS-FADED 260 ms EaseInOutCubic
 * (motion::kScreenFadeMs) — never cut; the first placement does not fade. The screen fading out takes no input.
 *   home     slot "home/home.js" (HomeScreen) — its box is the whole root
 *   project  project/screen.js (ProjectScreen — this shell's own; it lays out the song view's slots)
 *
 * LAYERS over the screens, in the native's paint order (App::paintPass): the sheets (vm.view.sheet → slot
 * "sheets/<name>.js": settings · export · relink · conflicts), the history tree (vm.view.history → "history/view.js"),
 * the dialog layer (cosmo's ConfirmDialog, the file picker), the context menu (App's one ContextMenu), the keyboard's
 * focus ring, the toast. A sheet / the history module is mounted on open with ctx.open = Signal<true>; on close
 * ctx.open turns false and the shell removes it motion.modalClose + 40 ms later (animate your close from ctx.open).
 *
 * KEYS (App::key, App.cpp:815-847): the focus keys first (Tab / Shift+Tab walk vm.a11y; while the focus is held Enter
 * and Space click it, the arrows step it, Escape lets go); a menu, a dialog, a picker or a sheet open: theirs; the
 * history open: Escape closes it, the undo / redo keys walk it; a text field typing: its own. Else the combo
 * (kit/keys.js) is looked up in the model's keymap: the front piano roll's keyboard first when it has the keys
 * (vm.view.rollKeys, R-ROLL-8 — vm.rolls.get(vm.view.frontRoll).action(id)), then the song's (menus.js songAction).
 * A key the app took is kept from the browser (Ctrl+S saves the song, not the page). Ctrl+wheel never zooms the page.
 *
 * THE TOAST (App::paintPass, App.cpp:1059-1074): 12 px Roboto, ellipsized to min(640, W − 64), 34 high, centred,
 * 24 above the bottom and rising 12 px into place; popover fill, a 1 px border — destructive @ .6 for a refusal, the
 * accent for a notice; in 150 ms EaseOutCubic, held 3200 ms from its start, out 300 ms EaseInOutCubic.
 *
 * MODEL REACTIONS (App::bindIfStale, App.cpp:561-594): a sync of the song's file said (a notice) and its conflicts shown;
 * a song opened with missing files says so once; an autosave newer than the song asks Recover / Open the Saved One;
 * high contrast eased (root class `hc`, the tokens' transition; vm.view.contrast LIVE); the language — the model's
 * setting resolved against the browser's — loaded and DISSOLVED in (180 ms linear, App::kLanguageDissolveMs, a view
 * transition); reduced motion (OS or setting) zeroes every CSS duration (root class `rm`).
 *
 * ctx — handed to every slot (a fresh one per slot; only `box`, `name`, `open` differ):
 *   toast(text, err = true)        App::showToast — err (the default, as the native's) = a refusal; false = a notice
 *   menu.open(items, x, y)         App::openMenu: [{label, action}] at ROOT px, labels stored words or data
 *   menu.openRename(name, x, y, onRename, {title})   the menu in rename mode (App's onRename / onEditText)
 *   menu.close()
 *   confirm({title, message, buttons: [{label, primary, destructive, onClick}]}) -> Promise<index | -1>
 *   prompt({value, x, y, title}) -> Promise<string | null>
 *   pick({mode: open | save | folder, title, ok, name, kinds}) -> Promise<path | null>   the machine's files
 *   copy(text)                     the clipboard + "Copied …" (App::copy)
 *   box                            Signal<{x, y, w, h}> the slot's rect in ROOT logical px (re-layout from it)
 *   scale()                        the drawn scale now (a canvas's backing store: × devicePixelRatio)
 *   screenPoint(e)                 a pointer / mouse event → {x, y} in ROOT logical px
 *   overlay                        the screen's overlay layer (over its widgets, under the sheets): popups, combos,
 *                                  a drag's ghost — absolutely positioned at ROOT px
 *   slot(host, path, name, {box, placeholder, ...more}) -> {ready, dispose}
 *                                  mount a sub-module into `host` (the window layer mounts each window's content): `box`
 *                                  = ITS Signal<{x, y, w, h}> in ROOT px (default: this slot's), `placeholder(el)` its
 *                                  stand-in, anything else is added to its ctx (e.g. window: {key, id, close(), raise()})
 *   open                           (sheets, history) Signal<bool>: false = close yourself now
 *   name                           the slot's widget name
 */
import { signal, effect, untracked } from "./core/signal.js";
import { h, own, destroy } from "./core/dom.js";
import { modalOpen } from "./ui/modal.js";
import { Anim, Ease, frames, motion } from "./kit/anim.js";
import { slot as mountSlot } from "./kit/slot.js";
import { comboOf, keyCode, keyActionFor } from "./kit/keys.js";
import { createPopups } from "./kit/popups.js";
import { invalidateColors } from "./kit/draw.js";
import { FONT, measure, ellipsize, svgBox, svgText, fontsReady } from "./kit/text.js";
import { loadTable, setLanguage } from "./vm/words.js";
import { MIN_W, MIN_H, UI_SCALES } from "./vm/app.js";
import { appActions } from "./project/menus.js";
import { mount as mountProject } from "./project/screen.js";

const SHEETS = { settings: ["sheets/settings.js", "SettingsSheet"], export: ["sheets/export.js", "ExportSheet"],
                 relink: ["sheets/relink.js", "RelinkSheet"], conflicts: ["sheets/conflicts.js", "ConflictsSheet"] };
const SCALE_ANIM_MS = 260;        // App::kScaleAnimMs
const LANGUAGE_DISSOLVE_MS = 180; // App::kLanguageDissolveMs

export function mount(app, vm) {
  const M = motion();
  const root = h("div.sl-root");
  app.append(root);

  // ------------------------------------------------------------------ the logical-px root
  const size = signal({ w: MIN_W, h: MIN_H, s: 1 }, (a, b) => a.w === b.w && a.h === b.h && a.s === b.s);
  const scale = new Anim(1);
  let scaleInit = false;
  function apply() {
    const W = Math.max(1, app.clientWidth), H = Math.max(1, app.clientHeight);
    const fit = Math.min(1, W / MIN_W, H / MIN_H);        // WEB-ONLY: below the native window's floor, scale down to fit
    const s = Math.max(0.05, scale.value * fit);
    const w = W / s, hh = H / s;
    root.style.width = w + "px";
    root.style.height = hh + "px";
    root.style.transform = s === 1 ? "none" : `scale(${s})`;
    size.value = { w, h: hh, s };
    vm.view.drawnScale.value = s;
  }
  const scaleStep = (now) => { const moving = scale.update(now); apply(); return moving; };
  effect(() => {
    const pct = UI_SCALES.includes(vm.view.uiScale.value) ? vm.view.uiScale.value : 100;
    untracked(() => {
      if (!scaleInit) { scaleInit = true; scale.set(pct / 100); apply(); }   // the first frame: placed
      else { scale.animateTo(pct / 100, SCALE_ANIM_MS, Ease.EaseOutCubic, performance.now()); frames(scaleStep); }
    });
  });
  new ResizeObserver(apply).observe(app);
  const box = (x, y, w, hh) => ({ x, y, w, h: hh });
  const rootBox = signal(box(0, 0, MIN_W, MIN_H), (a, b) => a.w === b.w && a.h === b.h);
  effect(() => { const z = size.value; rootBox.value = box(0, 0, z.w, z.h); });
  root.addEventListener("wheel", (e) => { if (e.ctrlKey) e.preventDefault(); }, { passive: false }); // the lanes' zoom, never the page's

  // ------------------------------------------------------------------ layers (App::paintPass order)
  const homeHost = h("div.sl-screen.sl-home");
  const projectHost = h("div.sl-screen.sl-project");
  const sheetLayer = h("div.sl-layer.sl-sheets");
  const historyLayer = h("div.sl-layer.sl-history");
  const modalLayer = h("div.sl-layer.sl-modal.dg-host");
  const menuLayer = h("div.sl-layer.sl-menus");
  const ring = h("div.sl-ring");
  const toastEl = h("div.sl-toast");
  const toastSvg = svgBox(0, 34), toastBody = document.createElementNS("http://www.w3.org/2000/svg", "rect"), toastText = svgText("", 16, 17 + 12 * 0.35, 12, FONT.sans);
  toastBody.setAttribute("class", "sl-toast-body");
  toastText.setAttribute("class", "sl-toast-text");
  toastSvg.append(toastBody, toastText);
  toastEl.append(toastSvg);
  const waiting = h("div.sl-waiting", {}, h("div.sl-waiting-text"));
  const presence = h("div.sl-presence");
  root.append(homeHost, projectHost, sheetLayer, historyLayer, modalLayer, menuLayer, ring, toastEl, presence, waiting);

  // ------------------------------------------------------------------ the toast
  const toast = { a: new Anim(0), text: "", refusal: true, shownAt: -1e9 };
  function showToast(text, err = true) {
    toast.text = text; toast.refusal = err !== false;
    toast.a.animateTo(1, M.toastIn, Ease.EaseOutCubic, performance.now());
    toast.shownAt = performance.now();
    paintToast();
    frames(toastStep);
  }
  function paintToast() {
    const a = toast.a.value, z = size.peek();
    toastEl.style.display = a > 0.001 && toast.text ? "" : "none";
    if (toastEl.style.display === "none") return;
    const px = 12;
    const s = ellipsize(vm.shown(toast.text), Math.min(640, z.w - 64), px, FONT.sans);
    const w = measure(s, px, FONT.sans) + 32, hh = 34;
    toastEl.style.transform = `translate(${(z.w - w) * 0.5}px, ${z.h - 24 - hh + (1 - a) * 12}px)`;
    toastEl.style.opacity = a;
    toastEl.classList.toggle("refusal", toast.refusal);
    toastSvg.setAttribute("width", w);
    for (const [k, v] of Object.entries({ x: 0, y: 0, width: w, height: hh, rx: 2, ry: 2 })) toastBody.setAttribute(k, v); // radius::control; the 1 px border straddles the edge, as Cairo strokes it
    if (toastText.textContent !== s) toastText.textContent = s;
  }
  function toastStep(now) {
    let moving = toast.a.update(now);
    if (toast.a.value > 0 && !toast.a.animating && now - toast.shownAt > M.toastHold) {
      toast.a.animateTo(0, M.toastOut, Ease.EaseInOutCubic, now);
      moving = true;
    }
    paintToast();
    return moving || toast.a.value > 0;
  }
  effect(() => { void size.value; void vm.lang.value; untracked(paintToast); });
  vm.setToaster(showToast);

  // ------------------------------------------------------------------ menus, dialogs, the picker
  const popups = createPopups(vm, { menuLayer, modalLayer });
  let pickerMod = null;
  const pick = (opts) => (pickerMod ||= import("./dialogs/picker.js")).then((m) => m.pick(vm, modalLayer, opts, popups.confirm),
    (e) => { console.error("solaris: the file picker failed to load:", e); showToast("The file picker failed to load: " + e.message); return null; });

  function screenPoint(e) {
    const r = root.getBoundingClientRect(), s = size.peek().s || 1;
    return { x: (e.clientX - r.left) / s, y: (e.clientY - r.top) / s };
  }
  function makeCtx(name, boxSig, overlay, extra = {}) {
    const ctx = {
      name, box: boxSig, overlay,
      toast: showToast,
      menu: { open: popups.open, openRename: popups.openRename, close: popups.close },
      confirm: popups.confirm, prompt: popups.prompt, pick,
      copy: (text) => vm.copy(text),
      scale: () => size.peek().s,
      screenPoint,
      ...extra,
    };
    ctx.slot = (host, path, widget, { box: subBox, placeholder, ...more } = {}) =>
      mountSlot(host, path, vm, makeCtx(widget || path, subBox || ctx.box, overlay, more), { name: widget || path, placeholder });
    return ctx;
  }
  const shellCtx = makeCtx("App", rootBox, null);
  const actions = appActions(vm, shellCtx);

  // ------------------------------------------------------------------ the screens, cross-faded
  const homeOverlay = h("div.sl-overlay");
  const homeSlotHost = h("div.sl-slot.sl-fill");
  homeHost.append(homeSlotHost, homeOverlay);
  const fade = { home: new Anim(1), project: new Anim(0), cur: null };
  function paintScreens() {
    for (const [k, el] of [["home", homeHost], ["project", projectHost]]) {
      const a = fade[k].value;
      el.style.opacity = a;
      el.style.visibility = a <= 0.001 && fade.cur !== k ? "hidden" : "";
      el.classList.toggle("sl-gone", fade.cur !== k); // the screen going takes no input
    }
  }
  const fadeStep = (now) => { const a = fade.home.update(now), b = fade.project.update(now); paintScreens(); return a || b; };
  effect(() => {
    if (!vm.ready.value) return;
    const want = vm.screen.value;
    untracked(() => {
      if (fade.cur === null) { fade.home.set(want === "home" ? 1 : 0); fade.project.set(want === "project" ? 1 : 0); }
      else if (want !== fade.cur) {
        const now = performance.now();
        fade.home.animateTo(want === "home" ? 1 : 0, M.screenFade, Ease.EaseInOutCubic, now);
        fade.project.animateTo(want === "project" ? 1 : 0, M.screenFade, Ease.EaseInOutCubic, now);
        frames(fadeStep);
      }
      fade.cur = want;
      paintScreens();
    });
  });
  paintScreens();

  // ------------------------------------------------------------------ sheets and the history tree (modal, over both screens)
  function modalSlot(layer, path, widget) {
    const host = h("div.sl-slot.sl-fill.sl-sheet");
    layer.append(host);
    const open = signal(true);
    const unA11y = vm.a11y.modal(() => []);  // a sheet's nodes alone (it registers its own modal list over this one)
    const s = mountSlot(host, path, vm, makeCtx(widget, rootBox, null, { open }), { name: widget, placeholder: (ph) => ph.classList.add("sl-ph-sheet") });
    const it = { host, open, state: "loading", close() {
      open.value = false;
      unA11y();
      host.classList.add("closing");
      setTimeout(() => { s.dispose(); host.remove(); }, M.modalClose + 40);
    } };
    // a sheet with no module yet (or a broken one): its name on the scrim, and a click or Escape closes it — a modal
    // that cannot be closed would hold the page
    s.ready.then((state) => {
      it.state = state;
      if (state === "mounted") return;
      host.classList.add("sl-sheet-stub");
      host.addEventListener("click", () => (layer === historyLayer ? vm.closeHistory() : vm.closeSheet()));
    });
    return it;
  }
  let sheet = null;
  effect(() => {
    const name = vm.view.sheet.value;
    untracked(() => {
      if (sheet && sheet.name === name) return;
      if (sheet) { sheet.it.close(); sheet = null; }
      if (name && SHEETS[name]) sheet = { name, it: modalSlot(sheetLayer, SHEETS[name][0], SHEETS[name][1]) };
      else if (name) { console.warn("solaris: no sheet named", name); vm.view.sheet.value = null; }
    });
  });
  let history = null;
  effect(() => {
    const on = vm.view.history.value;
    untracked(() => {
      if (on && !history) history = modalSlot(historyLayer, "history/view.js", "HistoryView");
      else if (!on && history) { history.close(); history = null; }
    });
  });

  // ------------------------------------------------------------------ the project screen (ProjectScreen)
  mountProject(projectHost, vm, makeCtx("ProjectScreen", rootBox, null), { makeCtx });

  // ------------------------------------------------------------------ Home, once the faces are in (measurements are real)
  fontsReady().then(() => {
    mountSlot(homeSlotHost, "home/home.js", vm, makeCtx("HomeScreen", rootBox, homeOverlay), {
      name: "HomeScreen",
      placeholder: (ph) => {
        ph.classList.add("sl-ph-home");
        ph.append(h("div.sl-ph-home-side"), h("div.sl-ph-home-head"));
      },
    });
  });

  // ------------------------------------------------------------------ App::bindIfStale's reactions to the model
  let syncSeen = null, missingSaidFor = null, recoveryAsked = "";
  effect(() => {
    const m = vm.model.value;
    if (!m) return;
    untracked(() => {
      const sync = m.sync || { count: 0 };
      if (syncSeen === null) syncSeen = sync.count || 0;
      if ((sync.count || 0) !== syncSeen) { // the song followed its file: said — and what it asks, shown
        if (sync.count > syncSeen && sync.said) showToast(sync.said, false);
        if (sync.count > syncSeen && (sync.conflicts || []).length && vm.view.sheet.peek() !== "conflicts") vm.openConflicts();
        syncSeen = sync.count || 0;
      }
      if ((m.projectPath || "") !== missingSaidFor) { // a song opened with files missing says so, once
        missingSaidFor = m.projectPath || "";
        const n = (m.missing || []).length;
        if (n > 0) showToast(vm.sayn(n, "{1} file is missing — File › Relink…", "{1} files are missing — File › Relink…", n));
      }
      if (!m.recovery) recoveryAsked = "";
      else if (m.recovery !== recoveryAsked) { recoveryAsked = m.recovery; askRecovery(m); }
    });
  });
  function askRecovery(m) {
    // R-SAFE-2 (App::askRecovery): the saved one is open until this is answered
    const path = m.recovery, s = m.recoveryAge, name = m.projectName || "";
    const n = Math.round(s < 90 ? s : s < 5400 ? s / 60 : s / 3600);
    const age = vm.fillIn(s < 90 ? vm.trn(n, "{1} second", "{1} seconds") : s < 5400 ? vm.trn(n, "{1} minute", "{1} minutes") : vm.trn(n, "{1} hour", "{1} hours"), String(n));
    popups.confirm({
      title: vm.say("Recover unsaved changes?"),
      message: vm.fillIn(vm.tr("Solaris kept changes to \"{1}\" that were never saved ({2} ago). Recover them, or open the song as it was last saved?"), name, age),
      buttons: [
        { label: vm.say("Open the Saved One"), onClick: () => vm.cmd("project discard-recovery " + vm.q(path)) },
        { label: vm.say("Recover"), primary: true, onClick: () => vm.cmd("project recover " + vm.q(path)) },
      ],
    });
  }

  // high contrast, eased (App.cpp:991-996): the tokens transition on :root; vm.view.contrast follows the same curve
  const contrast = new Anim(0);
  let contrastInit = false;
  const contrastStep = (now) => { const moving = contrast.update(now); vm.view.contrast.value = contrast.value; invalidateColors(); return moving; };
  effect(() => {
    const hc = vm.settings.value.highContrast ? 1 : 0;
    untracked(() => {
      document.documentElement.classList.toggle("hc", !!hc);
      if (!contrastInit) { contrastInit = true; contrast.set(hc); vm.view.contrast.value = hc; invalidateColors(); return; }
      contrast.animateTo(hc, SCALE_ANIM_MS, Ease.EaseInOutCubic, performance.now());
      frames(contrastStep);
    });
  });
  effect(() => { document.documentElement.classList.toggle("rm", !!vm.view.reducedMotion.value); });

  // the language: loaded, then dissolved in (App::advanceLanguage — the first placement does not dissolve)
  let langInit = false, langSeq = 0;
  effect(() => {
    const want = vm.langWant.value;
    if (!vm.ready.value) return;
    untracked(() => {
      const my = ++langSeq;
      loadTable(want).then((ok) => {
        if (my !== langSeq) return;
        const code = ok ? want : "en";
        if (code === vm.lang.peek()) { langInit = true; return; }
        if (!langInit || !document.startViewTransition || vm.view.reducedMotion.peek()) { langInit = true; setLanguage(code); return; }
        document.documentElement.style.setProperty("--sl-dissolve", LANGUAGE_DISSOLVE_MS + "ms");
        document.startViewTransition(() => setLanguage(code));
      });
    });
  });

  // ------------------------------------------------------------------ pointer: interacting, and who has the keys
  root.addEventListener("pointerdown", (e) => {
    vm.view.interacting.value = true;
    if (vm.screen.peek() === "project") vm.view.rollKeys.value = !!(e.target.closest && e.target.closest("[data-front-roll]")); // R-ROLL-8 (D-25)
  }, true);
  const up = () => { vm.view.interacting.value = false; };
  window.addEventListener("pointerup", up, true);
  window.addEventListener("pointercancel", up, true);
  window.addEventListener("blur", up);

  // ------------------------------------------------------------------ the keyboard's focus (R-ACCESS-1)
  const R = { x: new Anim(0), y: new Anim(0), w: new Anim(0), h: new Anim(0), a: new Anim(0), placed: false, target: null, wantA: 0 };
  const nodes = () => vm.a11y.nodes(vm.screen.peek()).filter((n) => n.enabled && n.bounds.w > 0 && n.bounds.h > 0);
  function ringStep(now) {
    let want = null;
    if (vm.view.focus.on.peek()) {
      want = nodes().find((n) => n.key === vm.view.focus.key.peek()) || null;
      if (!want) vm.view.focus.on.value = false; // a node gone lets the focus go
    }
    const a = want ? 1 : 0;
    if (a !== R.wantA) { R.a.animateTo(a, a > 0 ? M.hover : M.modalClose, Ease.EaseOutCubic, now); R.wantA = a; }
    if (want) {
      const b = want.bounds;
      if (!R.placed || (R.a.value <= 0.001 && !R.a.animating)) { R.x.set(b.x); R.y.set(b.y); R.w.set(b.w); R.h.set(b.h); R.placed = true; }
      else if (!R.target || b.x !== R.target.x || b.y !== R.target.y || b.w !== R.target.w || b.h !== R.target.h)
        for (const [k, v] of [["x", b.x], ["y", b.y], ["w", b.w], ["h", b.h]]) R[k].animateTo(v, M.slide, Ease.EaseOutCubic, now);
      R.target = { ...b };
    }
    let moving = false;
    for (const k of ["x", "y", "w", "h", "a"]) moving = R[k].update(now) || moving;
    ring.style.opacity = R.a.value * 0.9;
    // App.cpp:1056: a 2 px stroke CENTRED on the node's rect grown by 2 — so it covers 1 … 3 px outside the node
    ring.style.transform = `translate(${R.x.value - 3}px, ${R.y.value - 3}px)`;
    ring.style.width = R.w.value + 6 + "px";
    ring.style.height = R.h.value + 6 + "px";
    return moving || vm.view.focus.on.peek();
  }
  effect(() => { if (vm.view.focus.on.value) untracked(() => frames(ringStep)); });
  function clickNode(n) {
    if (n.activate) { n.activate(); return; }
    // the click it stands for — a press and a release at its centre, through the same DOM a pointer goes through
    const r = root.getBoundingClientRect(), s = size.peek().s;
    const cx = r.left + (n.bounds.x + n.bounds.w / 2) * s, cy = r.top + (n.bounds.y + n.bounds.h / 2) * s;
    const el = document.elementFromPoint(cx, cy);
    if (!el) return;
    const o = { bubbles: true, cancelable: true, clientX: cx, clientY: cy, button: 0, pointerId: 999, pointerType: "mouse", isPrimary: true };
    el.dispatchEvent(new PointerEvent("pointerdown", o));
    el.dispatchEvent(new PointerEvent("pointerup", o));
    el.dispatchEvent(new MouseEvent("click", o));
  }
  function focusKey(e) {
    if (e.ctrlKey || e.altKey || e.metaKey) return false;
    const k = keyCode(e);
    if (k === 9) {
      const list = nodes().map((n) => n.key);
      if (!list.length) return true;
      const i = list.indexOf(vm.view.focus.key.peek()), n = list.length;
      const at = vm.view.focus.on.peek() && i >= 0 ? (i + (e.shiftKey ? -1 : 1) + n) % n : e.shiftKey ? n - 1 : 0;
      vm.view.focus.key.value = list[at];
      vm.view.focus.on.value = true;
      frames(ringStep);
      return true;
    }
    if (!vm.view.focus.on.peek()) return false;
    const node = nodes().find((x) => x.key === vm.view.focus.key.peek());
    if (k === 27) { vm.view.focus.on.value = false; frames(ringStep); return true; }
    if ((k === 13 || k === 32) && node) { clickNode(node); return true; }
    if (k >= 37 && k <= 40 && node) { if (node.step) node.step(k === 37 || k === 40 ? -1 : 1, e.shiftKey); return true; }
    return false;
  }

  // ------------------------------------------------------------------ App::key
  const typing = (t) => t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable);
  window.addEventListener("keydown", (e) => {
    if (e.defaultPrevented) return;
    if (typing(e.target) || modalOpen() || vm.view.menuOpen.peek()) return;     // a field, a dialog, a picker, a menu: theirs
    if (focusKey(e)) { e.preventDefault(); return; }
    if (vm.view.sheet.peek()) {                                                 // a sheet takes its own keys
      if (sheet && sheet.it.state !== "mounted" && e.key === "Escape") { vm.closeSheet(); e.preventDefault(); }
      return;
    }
    const m = vm.model.peek();
    const combo = comboOf(e);
    if (vm.view.history.peek()) {
      // R-HIST-3: modal — Escape closes it; the undo and redo keys walk the tree it shows
      if (keyCode(e) === 27 && !e.ctrlKey && !e.altKey && !e.shiftKey) { vm.closeHistory(); e.preventDefault(); return; }
      const id = keyActionFor(m, combo, "song");
      if (id === "song.undo" || id === "song.redo") actions.songAction(id);
      e.preventDefault();
      return;
    }
    if (!combo) return;
    if (vm.screen.peek() === "project" && vm.view.rollKeys.peek()) {  // R-ROLL-8: the roll the pointer last pressed in
      const roll = vm.rolls.get(vm.view.frontRoll.peek());
      if (roll) {
        const id = keyActionFor(m, combo, "roll");
        if ((roll.action && roll.action(id)) || id === "roll.copy" || id === "roll.paste") { e.preventDefault(); return; }
      }
    }
    if (actions.songAction(keyActionFor(m, combo, "song"))) e.preventDefault();
  });

  // ------------------------------------------------------------------ waiting for the app, and who else is here
  effect(() => {
    const s = vm.status.value, ready = vm.ready.value;
    waiting.classList.toggle("on", s !== "running" || !ready);
    waiting.firstChild.textContent = s === "running" ? "Waiting for the song…" : s === "failed" ? "Solaris stopped — see Arstro Remote's log" : "Starting Solaris on the machine…";
  });
  effect(() => {
    const p = vm.presence.value || {};
    const n = p.clients || 0;
    presence.classList.toggle("on", n > 1);           // NTWB-12: edits are shared — say so when someone else is here
    if (n > 1) presence.textContent = n + " viewing";
  });

  own(root, () => { destroy(root); });
  return { root, size, ctx: shellCtx, actions, toast: showToast };
}
