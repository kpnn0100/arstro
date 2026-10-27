/*
 * Interstellar — edit/screen.js: Screen::Edit - three tabs over ONE monitor (R-UI-3). A port of
 * app/widgets/EditScreen.{h,cpp}.
 *
 *      ┌ EditTopBar 29.25 ────────────────────────────────────────────────────────────────┐
 *      │ left column  │            MONITOR (outside the tab pages)       │ right column     │
 *      │ (per tab)    ├──────────────────────────────────────────────────┤ (per tab)        │
 *      │              │  Transport (eased out in Grade)                  │                  │
 *      ├──────────────┴──────────────────────────────────────────────────┴──────────────────┤
 *      │ deck (per tab: GradeDeck · Timeline · RenderQueue)                                  │
 *      └─────────────────────────────────────────────────────────────────────────────────────┘
 *
 * LAYOUT (EditScreen::layout, EditScreen.cpp:150-195), W × H = the root's logical size:
 *   top = shell::topBarH (29.25)      deckH = shell::deckH(H) = clamp(176, 0.30 H, 264)     deckY = H - deckH
 *   lw = shell::leftW (234)           rw = shell::rightW (324)       colH = max(0, deckY - top)
 *   monW = max(0, W - lw - rw)        ta = the transport's EASED presence (0 in Grade, 1 in Cut / Deliver;
 *                                         motion::kCrossFadeMs EaseOutCubic, EditScreen::advance)
 *   monH = max(0, colH - transportH × ta)   (transportH = 39: the monitor takes the transport's room)
 * Draw order (= stacking): monitor, transport, the three pages, the top bar, the refusal chip; the
 * shell's menu and modal layers are above the whole screen.
 *
 * SLOTS - the module paths are FIXED (kit/slot.js loads each; a module not installed yet shows a quiet
 * placeholder in the native panel's colours). Each box is {x, y, w, h} in ROOT logical px (ctx.box):
 *
 *   edit/topbar.js       EditTopBar (+ TabSwitcher, VersionSwitcher, NamePrompt, the App's menu bar)
 *                                                                     (0, 0, W, top)
 *   edit/monitor.js      Monitor (+ ScopePanel / Scopes as edit/scopes.js, which grade/inspector.js
 *                        mounts in its own scope box with ctx.slot)    (lw, top, monW, monH)
 *   edit/transport.js    Transport                                    (lw, top + monH, monW, transportH); the
 *                        screen fades the host with `ta` and makes it inert below 0.001 (EditScreen.cpp:171-172)
 *   GRADE page   grade/rack.js       RackTree                         (0, top, lw, colH)
 *                grade/inspector.js  GradeInspector                   (W - rw, top, rw, colH)
 *                grade/deck.js       GradeDeck (PluginList / EffectPanel / NodeGraph inside it)  (0, deckY, W, deckH)
 *   CUT page     cut/bin.js          SourceBin                        (0, top, lw, colH)
 *                cut/inspector.js    ClipInspector                    (W - rw, top, rw, colH)
 *                cut/timeline.js     Timeline + KeyLane + KeyGraph    (0, deckY, W, deckH)
 *   DELIVER page deliver/checks.js   ChecksPanel                      (0, top, lw, colH)
 *                deliver/output.js   OutputSpec                       (W - rw, top, rw, colH)
 *                deliver/queue.js    RenderQueue                      (0, deckY, W, deckH)
 *   (Home / Loading are the shell's: home/home.js HomeScreen, home/loading.js LoadingView - the whole root.)
 *
 * A slot's host is positioned in its PARENT (the screen, or its page at y = top), and ctx.box gives
 * the same rect in root coordinates (menus and drags use root coordinates). The pages are FadePages
 * (kit/fadepage.js): a tab switch cross-fades them (200 ms) and input follows the chosen tab at once.
 *
 * CROSS-WIDGET CONTRACT - where the native wires one widget to another (EditScreen.cpp / App.cpp),
 * the web talks through the view-model (vm/app.js has the types) or ctx, never a static import:
 *   tab                          vm.tab (0 Grade · 1 Cut · 2 Deliver), vm.setTab(i). TabSwitcher.onSelect.
 *                                The topbar closes its version dropdown on a tab change (EditScreen::setTab).
 *   Rack ⇄ Deck groups           RackTree.onOpenGroup -> GradeDeck.openGroup: rack calls vm.openDeckGroup(ro);
 *                                deck shows vm.view.deckLevel. GradeDeck.onNavigate -> RackTree.setOpen: deck
 *                                calls vm.setRackOpen(ro, true); rack shows vm.view.rackOpen.
 *   onRackContext                rack row, filmstrip cell, bin row: menus.openRackContext(vm, ctx, i, x, y)
 *   onAddFootage                 rack, bin (and File › Add Footage): menus.pickFootage(vm, ctx)
 *   onCapture                    monitor caption button (Grade) and transport button: menus.openCaptureMenu(
 *                                vm, ctx, rect) with the button's ROOT rect {x, y, w, h}
 *   ref-frame preview            GradeDeck.onPreview -> vm.view.preview = {bind, t} | null; the vm's monitor
 *                                fetch shows it (seek <tc> in the caption)
 *   bin drag -> timeline drop    SourceBin.onDragSource -> vm.view.binDrag {bind, label, dur, x, y, phase};
 *                                the timeline hit-tests on "drop" and calls vm.dropSource(bind, track, at)
 *                                (label = cosmoName || bindName; dur = mediaDuration, or 5 s for a still -
 *                                EditScreen.cpp:79-80)
 *   source viewer                bin double-click: vm.cmd(`source view ${q(bind)}`); vm.sourceViewing and
 *                                vm.monitor.transportSourceMode follow the model
 *   scrubbing                    transport scrub / timeline playhead drag -> vm.view.scrubbing (no dissolve)
 *   scopes ⇄ monitor             the scopes read vm.monitor.frame; their CLIP switch writes vm.view.clipWarning,
 *                                the monitor reads it (ScopePanel::onClipWarning)
 *   grab still                   Colour menu, Ctrl+Alt+G: vm.grabStill() -> vm.view.deckView = "stills"
 *   Reset Workspace              Workspace menu: vm.setTab(GRADE); vm.provided("timeline")?.resetView();
 *                                vm.provided("monitor")?.resetZoom()
 *   key graph                    cut/timeline.js provides "keyGraph" (selectionList / selectionCount / now /
 *                                shown) for the shell's Ctrl+C / Ctrl+V and the key menus
 *   versions dropdown            edit/topbar.js provides "versions" ({isOpen, close}) - Esc closes it
 *   new effect selected          grade/inspector.js provides "plugins" ({select(id)})
 *   NamePrompt                   edit/topbar.js provides "prompt" (ctx.prompt's implementation)
 *   pictures                     vm.session.thumb / thumbNow / still / stillNow / peaks / peaksNow (session.js)
 *   Home / Settings              home/home.js: menus.pickProjectToCreate / pickProjectToOpen, vm.openRecent(i),
 *                                vm.openSettings(); requestHome: menus.requestHome(vm, ctx)
 *
 * THE CHIP (EditScreen::showRefusal / showNotice / onOverlay, EditScreen.cpp:197-268): a refused line
 * said over the monitor - ctx.toast(text, true); a finished action with a check - ctx.toast(text). Centred
 * on the monitor, 44 px above its foot, rising 6 px as it appears (150 ms EaseOutCubic), held 2.6 s, then
 * out over 300 ms; popover plate, destructive (0.8) or border outline, radius control, a warn / check
 * glyph (11 px at 12, 8.5), Roboto 11 at x 30, ellipsized to the monitor's width - 80.
 */
import { signal, effect, untracked } from "../core/signal.js";
import { h, own } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { GRADE } from "../vm/app.js";
import { SHELL, tok } from "../kit/tokens.js";
import { fadePage } from "../kit/fadepage.js";
import { slot } from "../kit/slot.js";
import { glyph } from "../kit/glyphs.js";
import { ellipsize, width as textWidth, baseline, FAMILY, fontEpoch } from "../kit/textfit.js";

/** Every slot of the Edit screen: module, native name, page (null = the screen itself), placeholder look. */
export const EDIT_SLOTS = [
  { id: "topbar", path: "edit/topbar.js", name: "EditTopBar", page: null, ph: "bg border-bottom" },
  { id: "monitor", path: "edit/monitor.js", name: "Monitor", page: null, ph: "monitor" },
  { id: "transport", path: "edit/transport.js", name: "Transport", page: null, ph: "card border-top" },
  { id: "rack", path: "grade/rack.js", name: "RackTree", page: 0, ph: "rail border-right" },
  { id: "gradeInspector", path: "grade/inspector.js", name: "GradeInspector", page: 0, ph: "card border-left" },
  { id: "gradeDeck", path: "grade/deck.js", name: "GradeDeck", page: 0, ph: "deck border-top" },
  { id: "bin", path: "cut/bin.js", name: "SourceBin", page: 1, ph: "rail border-right" },
  { id: "clipInspector", path: "cut/inspector.js", name: "ClipInspector", page: 1, ph: "card border-left" },
  { id: "timeline", path: "cut/timeline.js", name: "Timeline", page: 1, ph: "deck ruler" },
  { id: "checks", path: "deliver/checks.js", name: "ChecksPanel", page: 2, ph: "rail border-right" },
  { id: "output", path: "deliver/output.js", name: "OutputSpec", page: 2, ph: "card border-left" },
  { id: "queue", path: "deliver/queue.js", name: "RenderQueue", page: 2, ph: "deck border-top" },
];

/** EditScreen::layout as a pure function: every rect in ROOT logical px. `ta` = the transport's presence. */
export function editLayout(W, H, ta) {
  const top = SHELL.topBarH, deckH = SHELL.deckH(H), deckY = H - deckH;
  const lw = SHELL.leftW, rw = SHELL.rightW, colH = Math.max(0, deckY - top);
  const monW = Math.max(0, W - lw - rw);
  const monH = Math.max(0, colH - SHELL.transportH * ta);
  const left = { x: 0, y: top, w: lw, h: colH }, right = { x: W - rw, y: top, w: rw, h: colH }, deck = { x: 0, y: deckY, w: W, h: deckH };
  return {
    top, deckH, deckY, colH, monW, monH,
    topbar: { x: 0, y: 0, w: W, h: top },
    monitor: { x: lw, y: top, w: monW, h: monH },
    transport: { x: lw, y: top + monH, w: monW, h: SHELL.transportH },
    page: { x: 0, y: top, w: W, h: Math.max(0, H - top) },
    rack: left, gradeInspector: right, gradeDeck: deck,
    bin: left, clipInspector: right, timeline: deck,
    checks: left, output: right, queue: deck,
  };
}

const sameRect = (a, b) => a.x === b.x && a.y === b.y && a.w === b.w && a.h === b.h;
const place = (el, r, ox = 0, oy = 0) => {
  el.style.transform = `translate(${r.x - ox}px, ${r.y - oy}px)`;   // fractional px kept (no left/top snapping)
  el.style.width = r.w + "px";
  el.style.height = r.h + "px";
};

export function mountEditScreen(host, vm, ctx) {
  host.classList.add("ed-screen");
  const size = ctx.size;

  // the transport's presence (0 in Grade): its tween re-lays the screen out every frame
  let built = false;
  const ta = new Tween(vm.tab.peek() === GRADE ? 0 : 1, () => { if (built) layout(); });

  // ---------------------------------------------------------------- hosts: monitor, transport, pages, topbar
  const hosts = {}, boxes = {}, slots = {};
  const pageEls = [0, 1, 2].map((i) => h("div.ed-page.ed-page-" + i));
  const pages = pageEls.map((el) => fadePage(el));
  const mk = (s) => {
    const el = h("div.ed-slot.ed-slot-" + s.id);
    el.append(h("div.is-placeholder.ed-ph" + s.ph.split(" ").map((c) => ".ph-" + c).join(""), null, h("div.is-ph-label", { text: s.name })));
    hosts[s.id] = el;
    boxes[s.id] = signal({ x: 0, y: 0, w: 0, h: 0 }, sameRect);
    return el;
  };
  const byId = Object.fromEntries(EDIT_SLOTS.map((s) => [s.id, s]));
  host.append(mk(byId.monitor), mk(byId.transport));
  for (const s of EDIT_SLOTS) if (s.page !== null) pageEls[s.page].append(mk(s));
  host.append(...pageEls, mk(byId.topbar));
  const chip = h("div.ed-chip");
  host.append(chip);
  const HOLD_MS = 2600;                                 // a hold, not a tween (EditScreen.cpp:16)
  let chipText = "", chipError = true, shownAt = -1, holdTimer = 0, chipW = 0;
  const amt = new Tween(0, () => placeChip());

  // ---------------------------------------------------------------- tabs: the pages cross-fade (EditScreen::setTab)
  let tabsPlaced = false;
  own(host, effect(() => {
    const t = vm.tab.value;
    untracked(() => {
      pages.forEach((p, i) => (tabsPlaced ? p.setShown(i === t) : p.setShownImmediate(i === t)));
      tabsPlaced = true;
    });
  }));

  // ---------------------------------------------------------------- layout (every size change, every frame of a tween)
  let rects = null;
  function layout() {
    const s = size.peek();
    const L = editLayout(s.w, s.h, ta.value);
    rects = L;
    host.style.width = s.w + "px";
    host.style.height = s.h + "px";
    for (const el of pageEls) place(el, L.page);
    for (const sl of EDIT_SLOTS) {
      const r = L[sl.id];
      place(hosts[sl.id], r, sl.page === null ? 0 : L.page.x, sl.page === null ? 0 : L.page.y);
      boxes[sl.id].value = { x: r.x, y: r.y, w: r.w, h: r.h };
    }
    const a = ta.value;
    hosts.transport.style.opacity = String(a);
    hosts.transport.inert = a <= 0.001;                 // culled: a hidden widget takes no input
    hosts.transport.style.visibility = a <= 0.001 ? "hidden" : "";
    placeChip();
  }
  built = true;
  own(host, effect(() => { size.value; untracked(layout); }));
  own(host, effect(() => {
    const target = vm.tab.value === GRADE ? 0 : 1;   // EditScreen::setTab -> advance: one eased value
    untracked(() => ta.to(target, tok.ms("motion-cross-fade"), Ease.EaseOutCubic));
  }));

  // ---------------------------------------------------------------- the slots
  for (const s of EDIT_SLOTS) {
    const c = ctx.child(boxes[s.id]);
    slots[s.id] = slot(hosts[s.id], s.path, vm, c, { name: s.name });
  }

  // ---------------------------------------------------------------- the chip (EditScreen.cpp:197-268)
  function showChip(text, isError) {
    chipText = String(text);
    chipError = !!isError;
    buildChip();
    amt.to(1, tok.ms("motion-modal-open"), Ease.EaseOutCubic);
    shownAt = performance.now();
    clearTimeout(holdTimer);
    holdTimer = setTimeout(() => amt.to(0, tok.ms("motion-progress-out"), Ease.EaseOutCubic), HOLD_MS);
  }
  function buildChip() {
    if (!rects) return;
    const mon = rects.monitor;
    const msg = ellipsize(chipText, Math.max(0, mon.w - 80), 11, FAMILY.sans);
    const tw = textWidth(msg, 11, FAMILY.sans);
    chipW = tw + 40;
    const NS = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(NS, "svg");
    svg.setAttribute("width", chipW + 2); svg.setAttribute("height", 30);
    svg.setAttribute("viewBox", `-1 -1 ${chipW + 2} 30`);
    svg.setAttribute("overflow", "visible");
    const plate = document.createElementNS(NS, "rect");
    for (const [k, v] of Object.entries({ x: 0, y: 0, width: chipW, height: 28, rx: tok.px("radius-control"), ry: tok.px("radius-control") })) plate.setAttribute(k, v);
    plate.style.fill = "var(--popover)";
    plate.style.stroke = chipError ? "rgba(var(--destructive-rgb), 0.8)" : "var(--border)";
    plate.style.strokeWidth = "1";
    const ic = chipError ? glyph.warn({ x: 12, y: 8.5, w: 11, h: 11 }, "var(--destructive)") : glyph.check({ x: 12, y: 8.5, w: 11, h: 11 }, "var(--success)");
    const text = document.createElementNS(NS, "text");
    text.setAttribute("x", 30);
    text.setAttribute("y", baseline(14, 11));
    text.setAttribute("font-size", 11);
    text.setAttribute("font-family", `"${FAMILY.sans}"`);
    text.style.fill = "var(--foreground)";
    text.textContent = msg;
    svg.append(plate, ic, text);
    chip.replaceChildren(svg);
    chip.classList.toggle("err", chipError);
    chip.dataset.text = chipText;
    placeChip();
  }
  function placeChip() {
    if (!rects) return;
    const a = amt.value;
    const mon = rects.monitor;
    chip.style.opacity = String(a);
    chip.style.visibility = a <= 0.001 ? "hidden" : "";
    chip.style.transform = `translate(${mon.x + (mon.w - chipW) * 0.5 - 1}px, ${mon.y + mon.h - 44 + 6 * (1 - a) - 1}px)`;
  }
  own(host, effect(() => { fontEpoch.value; untracked(() => { if (chipText) buildChip(); }); }));
  own(host, () => clearTimeout(holdTimer));

  return {
    showChip,
    /** The live rects (EditScreen::layout's numbers) - for tests and the shell. */
    rects: () => rects,
    transportAmount: () => ta.value,
    tabFade: (i) => pages[i].fadeValue(),
    chipAmount: () => amt.value,
    slots, hosts, boxes,
  };
}
