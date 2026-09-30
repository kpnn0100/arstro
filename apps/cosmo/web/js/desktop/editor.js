/*
 * Cosmo by arstro — desktop/editor.js: the editor screen of the desktop shell, and the small
 * toolkit every shell widget shares.
 *
 * Ports App::layout (App.cpp:341-386) - TopBar 29.25 high across the top; LeftRail 196 wide
 * (eased to 0 and back, 200 ms EaseOutCubic), CenterStage filling what is left, RightColumn
 * 324 wide - re-laid out every frame while the rail moves, so the photo refits continuously.
 * The rail's OPEN state is derived, as in the App: what the user wants (vm.view.railWanted,
 * flipped only by the TopBar toggle) AND room for a 260 px canvas (App.cpp:379), so it folds
 * itself under 780 logical px and comes back when the room does.
 *
 * Z-order (App.cpp:80-298): TopBar, LeftRail, CenterStage, RightColumn (B's rightcol.js),
 * HistoryView, ContextMenu, then the dialog host (C's dialogs/host.js). An open menu raises the
 * TopBar above all of them (App.cpp:469), as the native does for hit-testing.
 *
 * The toolkit: `est()` is the native's text estimate (TextMetrics.h:19-22 - UTF-8 bytes x px x
 * 0.6), which positions every centred / right-aligned label; `text()` places a label by its
 * alphabetic BASELINE like drawText; `gestures()` is Artboard's GestureRecognizer (5 px drag
 * slop, 300 ms double click that replaces the second click, right click on release);
 * `menuActions()` is what the menus, the context menu and the keyboard all run.
 *
 * Deviations: none in the layout. The editor's actions that need a native host dialog use C's
 * picker (dialogs/picker.js) or set vm.view.dialog; the ones the core cannot do yet (Reset
 * Workspace, Paste, quick export) say so in a toast instead of doing something else.
 */
import { signal, computed, effect, untracked } from "../core/signal.js";
import { h, bindEffect, own } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { mountTopBar } from "./topbar.js";
import { mountLeftRail } from "./leftrail.js";
import { mountStage } from "./stage.js";
import { mountBreadcrumb } from "./breadcrumb.js";
import { mountFilmstrip } from "./filmstrip.js";
import { mountContextMenu } from "./contextmenu.js";
import { mountHistory } from "./history.js";

// App.h / TopBar.h / RightColumn.h / CenterStage.cpp constants.
export const TOPBAR_H = 29.25;
export const RAIL_W = 196;
export const RIGHT_W = 324;
export const CANVAS_FLOOR = 260;          // App::roomForRail
export const CRUMB_H = 22.75;
export const STRIP_H = 86;

// ------------------------------------------------------------------ text
const enc = new TextEncoder();
/** UTF-8 byte length - what the native's estimate counts ("…" is 3). */
export const bytes = (s) => enc.encode(String(s)).length;
/** estimateTextWidth (widgets/TextMetrics.h:19-22). */
export const est = (s, px) => bytes(s) * px * 0.6;

/** The native's faces by role (Theme.h:37-41). */
export const FONT = {
  sans: "var(--font-sans)", medium: "var(--font-sans-medium)", semibold: "var(--font-sans-semibold)",
  mono: "var(--font-mono)", monoMedium: "var(--font-mono-medium)",
};
// With line-height 0 the alphabetic baseline sits (ascent - descent) / 2 below the box top
// (hhea metrics of the vendored TTFs: Roboto 1900/-500 per 2048, JetBrains Mono 1020/-300 per
// 1000), so a label whose top is `baseline - K * size` lands exactly on drawText's baseline.
const K = { sans: 0.341797, medium: 0.341797, semibold: 0.341797, mono: 0.36, monoMedium: 0.36 };

// Cairo lays text out with HINTED metrics: every glyph advance is rounded to a whole device
// pixel (the mono readout's pitch is 7 px at 11 px, not 6.6). The browser keeps them
// fractional, so each label gets the difference spread over its glyphs as extra letter
// spacing: its width - and so where its last glyph ends - is the native's.
const FAMILY_VAR = { sans: "--font-sans", medium: "--font-sans-medium", semibold: "--font-sans-semibold",
                     mono: "--font-mono", monoMedium: "--font-mono-medium" };
let famNames = null, measurer = null, deviceScale = 1;
const advCache = new Map();
const labels = new Set();
function famName(font) {
  if (!famNames) {
    const cs = getComputedStyle(document.documentElement);
    famNames = {};
    for (const k of Object.keys(FAMILY_VAR)) famNames[k] = cs.getPropertyValue(FAMILY_VAR[k]).trim() || "sans-serif";
  }
  return famNames[font] || famNames.sans;
}
function advance(ch, size, font) {
  const key = font + "|" + size + "|" + ch;
  let a = advCache.get(key);
  if (a !== undefined) return a;
  if (!measurer) { measurer = document.createElement("canvas").getContext("2d"); }
  const spec = `${size}px ${famName(font)}`;
  measurer.font = spec;
  a = measurer.measureText(ch).width;
  if (document.fonts && document.fonts.check(spec)) advCache.set(key, a);
  else if (document.fonts) document.fonts.load(spec).then(refreshLabels, () => {});
  return a;
}
/** A canvas font string for a role ("12px \"Roboto SemiBold\""). */
export function canvasFont(size, font = "sans") { return `${size}px ${famName(font)}`; }
/** The extra letter spacing that turns the browser's advances into Cairo's rounded ones. */
export function hintSpacing(str, size, font = "sans", S = deviceScale) {
  // Spread over the advances BEFORE the last glyph, so every glyph up to the last one starts
  // where Cairo starts it (letter spacing after the last glyph moves nothing visible).
  const chars = [...String(str)];
  if (chars.length < 2) return 0;
  let diff = 0;
  for (let i = 0; i < chars.length - 1; i++) { const a = advance(chars[i], size, font); diff += Math.round(a * S) / S - a; }
  return diff / (chars.length - 1);
}
function spacing(el) {
  const t = el.__t;
  if (!t) return;
  el.style.letterSpacing = (t.ls || 0) + hintSpacing(el.textContent, t.size, t.font) + "px";
}
/** Re-space every live label (fonts arrived, or the UI scale settled on another value). */
export function refreshLabels() {
  for (const el of labels) { if (!el.isConnected) { labels.delete(el); continue; } spacing(el); }
}
/** The device pixels per logical px the native rounds to (UI scale x devicePixelRatio). */
export function setDeviceScale(s) {
  if (Math.abs(s - deviceScale) < 1e-6) return;
  deviceScale = s;
  refreshLabels();
}

/** A label placed like drawText(text, x, baseline, size, family, letterSpacing). */
export function text(str, { x = 0, y = 0, size = 11, font = "sans", color, ls, cls } = {}) {
  const el = h("span.t" + (cls ? "." + cls.split(" ").join(".") : ""));
  el.style.fontFamily = FONT[font];
  el.style.fontSize = size + "px";
  if (color) el.style.color = color;
  el.__t = { size, font, ls: ls || 0 };
  place(el, x, y, size, font);
  setText(el, str);
  labels.add(el);
  return el;
}
/** Change a text() label's string (keeps its hinted spacing right). */
export function setText(el, str) {
  str = String(str ?? "");
  if (el.textContent === str && el.style.letterSpacing) return;
  el.textContent = str;
  spacing(el);
}
/** Move a text() label to another baseline position. */
export function place(el, x, y, size, font = "sans") {
  el.style.left = x + "px";
  el.style.top = (y - K[font] * size) + "px";
}

// ------------------------------------------------------------------ pointer
/** The CSS scale an element is drawn at (the UI-scale root), measured, not assumed. */
export function scaleOf(el) {
  const w = el.offsetWidth;
  return w > 0 ? el.getBoundingClientRect().width / w : 1;
}
/** A pointer event's position in `el`'s own (logical) px. */
export function local(el, e) {
  const r = el.getBoundingClientRect();
  const s = el.offsetWidth > 0 ? r.width / el.offsetWidth : 1;
  return { x: (e.clientX - r.left) / s, y: (e.clientY - r.top) / s };
}
/** The native wheel delta: +1 per notch up (linux_main.cpp:1339-1355). */
export function wheelDelta(e) {
  const d = Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
  return e.deltaMode === 1 ? -d : e.deltaMode === 2 ? -d * 10 : -d / 100;
}

/**
 * Artboard's GestureRecognizer (GestureRecognizer.cpp) on one element. Handlers get
 * (event, p, at) with p = the point in the element's logical px (modifiers on the event) and, for
 * the release gestures, `at` = the element under the pointer AT RELEASE - clicks hit-test again
 * there, not at the press (Segment.cpp:340-440), and the pointer is captured meanwhile:
 *   down, dragStart, drag, drop, click, doubleClick, rightClick, up
 * A move beyond 5 logical px while pressed is a drag (no click follows); a click within 300 ms
 * and 5 px of the previous one is a doubleClick INSTEAD of a second click; the right button's
 * release is rightClick; every other button is the left one (linux_main.cpp:158).
 */
export function gestures(el, hd) {
  let press = null, last = null;
  el.addEventListener("pointerdown", (e) => {
    if (press) return;
    const p = local(el, e);
    press = { id: e.pointerId, x0: e.clientX, y0: e.clientY, s: scaleOf(el), right: e.button === 2, dragging: false, p0: p };
    if (hd.down && hd.down(e, p) === false) { press = null; return; }
    try { el.setPointerCapture(e.pointerId); } catch { /* already gone */ }
  });
  el.addEventListener("pointermove", (e) => {
    if (!press || e.pointerId !== press.id) return;
    const p = local(el, e);
    if (!press.dragging) {
      if (Math.hypot(e.clientX - press.x0, e.clientY - press.y0) / press.s <= 5) return;
      press.dragging = true;
      if (hd.dragStart) hd.dragStart(e, p, press.p0);
    }
    if (hd.drag) hd.drag(e, p, press.p0);
  });
  const end = (e, cancelled) => {
    if (!press || e.pointerId !== press.id) return;
    const pr = press;
    press = null;
    const p = local(el, e);
    try { if (el.hasPointerCapture(e.pointerId)) el.releasePointerCapture(e.pointerId); } catch { /* gone */ }
    if (hd.up) hd.up(e, p);
    if (pr.dragging) { if (hd.drop) hd.drop(e, p); last = null; return; }
    if (cancelled) return;
    const at = document.elementFromPoint(e.clientX, e.clientY);
    if (pr.right) { if (hd.rightClick) hd.rightClick(e, p, at); return; }
    const now = performance.now();
    if (last && now - last.t <= 300 && Math.hypot(e.clientX - last.x, e.clientY - last.y) / pr.s <= 5) {
      last = null;
      if (hd.doubleClick) hd.doubleClick(e, p, at);
      return;
    }
    last = { t: now, x: e.clientX, y: e.clientY };
    if (hd.click) hd.click(e, p, at);
  };
  el.addEventListener("pointerup", (e) => end(e, false));
  el.addEventListener("pointercancel", (e) => end(e, true));
  el.addEventListener("lostpointercapture", (e) => { if (press && e.pointerId === press.id) end(e, true); });
}

// ------------------------------------------------------------------ the pickers (C)
/** C's file browser, or null while it does not exist yet. */
export async function picker() {
  try { return (await import("../dialogs/picker.js")).pick || null; } catch { return null; }
}
export const isProjectPath = (p) => /\.(cmp|cosmoproj)$/i.test(p);
export const baseName = (p) => String(p || "").replace(/\/+$/, "").split("/").pop();

/**
 * What every menu item, context-menu item and shortcut of the editor runs (App.cpp:471-620,
 * linux_main.cpp:1385-1497). `ed` supplies the view pieces (history view, context menu).
 */
export function menuActions(vm, ed) {
  const toast = (t, err) => ed.toast && ed.toast(t, err);
  const later = (what) => toast(what + " is not available yet");
  const hasImages = () => vm.imageCount.peek() > 0;
  const clipboard = { params: null };          // Develop ▸ Copy Settings (App-local, per page)

  async function openFiles() {                 // File ▸ Open…, context ▸ Add Photo…, key O
    const pick = await picker();
    if (!pick) return later("The file picker");
    // The host's "Open image" dialog lists photos AND projects (linux_main.cpp:495).
    const got = await pick(vm, { mode: "images", title: "Open image", kinds: ["image", "project"], ok: "Open" });
    const paths = !got ? [] : Array.isArray(got) ? got : [got];
    if (!paths.length) return;
    const project = paths.find(isProjectPath);
    if (project) return vm.openProject(project).catch(() => {});
    const images = paths.filter((p) => !isProjectPath(p));     // `browse` lists only images and projects
    if (images.length) return vm.addPhotos(images).catch(() => {});
    toast("Nothing to open in that selection");
  }
  async function saveAs() {                    // File ▸ Save As…, Ctrl+Shift+S
    if (!hasImages()) return;
    const pick = await picker();
    if (!pick) return later("The file picker");
    const cur = vm.project.peek().path;
    // linux_main.cpp:332-337 names it workspace.cosmoproj; the web's projects are .cmp (the
    // picker appends it), both of which the core opens.
    let path = await pick(vm, { mode: "save", title: "Save Project", name: cur ? baseName(cur) : "workspace.cmp",
                                kinds: ["project"] });
    if (!path) return;
    if (Array.isArray(path)) path = path[0];
    if (!isProjectPath(path)) path += ".cmp";
    return vm.saveAs(path).catch(() => {});
  }
  function save() {                            // App::saveWorkspace: no images -> nothing
    if (!hasImages()) return;
    if (vm.project.peek().path) return vm.save().catch(() => {});
    return saveAs();
  }
  function selectedGroups() { return vm.nodes.peek().filter((n) => n.selected && n.kind === "group").map((n) => n.node); }
  async function ungroup() {
    for (const g of selectedGroups()) { try { await vm.ungroup(g); } catch { return; } }
  }
  function goHome(ask) {                       // wordmark = requestHome (asks); File ▸ Home = showHome
    if (ask && vm.project.peek().dirty) {
      vm.view.dialog.value = {
        name: "confirm", title: "Unsaved changes", message: "Save your changes to this project before leaving?",
        buttons: [
          { label: "Cancel" },
          { label: "Discard", destructive: true, run: () => vm.goHome() },
          { label: "Save", primary: true, run: async () => { await save(); vm.goHome(); } },
        ],
      };
      return;
    }
    vm.goHome();
  }
  return {
    home: () => goHome(false),
    // screens.js owns the way back (the return transition + the unsaved-changes question);
    // without it, the same question through the dialog host.
    requestHome: () => (ed.ctx && ed.ctx.requestHome ? ed.ctx.requestHome() : goHome(true)),
    open: openFiles,
    save, saveAs,
    exportDialog: () => { if (hasImages()) vm.view.dialog.value = { name: "export" }; },
    settings: () => { vm.view.dialog.value = { name: "settings" }; },
    resetWorkspace: () => later("Reset Workspace"),
    copySettings: () => { if (vm.editTarget.peek()) clipboard.params = vm.ownParams.peek(); },
    pasteSelected: () => { if (clipboard.params) later("Paste to Selected"); },
    pasteAll: () => { if (clipboard.params) later("Paste to All Images"); },
    group: () => { if (vm.selectedIds.peek().size) vm.groupNew().catch(() => {}); },
    ungroup,
    undo: () => vm.undo(),
    redo: () => vm.redo(),
    history: () => ed.openHistory(),
    presetSave: () => { if (hasImages()) vm.view.dialog.value = { name: "preset", mode: "save" }; },
    presetImport: () => { vm.view.dialog.value = { name: "preset", mode: "import" }; },
    quickExport: () => { if (hasImages()) later("Quick export (S)"); },
    remove: () => vm.remove().catch(() => {}),
  };
}

// ------------------------------------------------------------------ the editor
/** The logical size of `el` as a signal, for a caller that did not pass the shell's. */
function ownSize(el) {
  const size = signal({ w: el.offsetWidth || 1600, h: el.offsetHeight || 1000, s: 1 });
  const ro = new ResizeObserver(() => { size.value = { w: el.offsetWidth, h: el.offsetHeight, s: scaleOf(el) }; });
  ro.observe(el);
  own(el, () => ro.disconnect());
  return size;
}

export function mountEditor(el, vm, ctx = {}) {
  const root = h("div.ed");
  el.append(root);
  // The ctx is the shell's LIVE object (screens.js adds requestHome / pick / confirm / screen to
  // it after mounting), so it is kept by reference, not copied.
  const size = ctx.size || ownSize(root);
  if (!ctx.size) ctx.size = size;

  // App.h:431-441 - the wish and the effective state.
  const railOpen = computed(() => vm.view.railWanted.value && size.value.w - RAIL_W - RIGHT_W >= CANVAS_FLOOR);
  const railW = new Tween(0, (v) => {
    root.style.setProperty("--rail-w", v + "px");
    root.classList.toggle("rail-gone", v <= 0.5);
  });
  railW.set(railOpen.peek() ? RAIL_W : 0);
  bindEffect(root, () => {
    const open = railOpen.value;
    untracked(() => railW.to(open ? RAIL_W : 0, 200, Ease.EaseOutCubic));
  });
  bindEffect(root, () => {
    const { w, h: hh } = size.value;
    root.style.setProperty("--W", w + "px");
    root.style.setProperty("--H", hh + "px");
  });

  const top = h("div.ed-top");
  const rail = h("div.ed-rail");
  const center = h("div.ed-center");
  const right = h("div.ed-right");
  const hist = h("div.ed-layer.ed-history");
  const cmenu = h("div.ed-layer.ed-cmenu");
  const dialogs = h("div.ed-layer.ed-dialogs");
  root.append(top, rail, center, right, hist, cmenu, dialogs);

  const ed = {
    vm, root, size, railOpen,
    toast: ctx.toast,
    ctx,
    reveal: () => {},                       // the filmstrip replaces these as it mounts
    openContext: () => {},
    openRename: () => {},
    openHistory: () => {},
    historyOpen: signal(false),
  };
  ed.act = menuActions(vm, ed);

  mountTopBar(top, vm, ed);
  mountLeftRail(rail, vm, ed);
  const stage = mountStage(center, vm, ed);
  mountBreadcrumb(center, vm, ed);
  mountFilmstrip(center, vm, ed);
  mountHistory(hist, vm, ed);
  mountContextMenu(cmenu, vm, ed);
  ed.stage = stage;

  // The right column is B's; until it exists the column is an empty, correctly sized box.
  import("./rightcol.js")
    .then((m) => m.mountRightColumn(right, vm, ed.ctx))
    .catch((e) => { right.classList.add("placeholder"); console.info("cosmo: right column not mounted:", e.message); });
  // The dialog host is C's; the editor keeps working without it.
  import("../dialogs/host.js")
    .then((m) => m.mountDialogHost(dialogs, vm, ed.ctx))
    .catch((e) => console.info("cosmo: dialog host not mounted:", e.message));

  // Wheel routing (App.cpp:862-921) is per area (rail, strip, stage, history each listen on
  // their own element); Ctrl+wheel is the photo's zoom and is consumed everywhere - it must
  // never zoom the browser page instead.
  root.addEventListener("wheel", (e) => { if (e.ctrlKey) e.preventDefault(); }, { passive: false });
  // The browser's own context menu never opens over the editor (right click is the native's);
  // text fields keep theirs.
  root.addEventListener("contextmenu", (e) => { if (!e.target.closest("input, textarea, [contenteditable]")) e.preventDefault(); });

  return { root, ed, stage };
}
