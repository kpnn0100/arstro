/*
 * Solaris — project/screen.js: ProjectScreen (widgets/ProjectScreen.{h,cpp}, R-UI-3, R-BROWSE-3, R-LANE-1) — the song
 * view's frame: the SongBar on top, the Browser on the left, the Timeline in the centre with the MixerDock docked under
 * it, the SessionView in the lanes' place when shown, and the window layer over them. Each part is a SLOT: a module of
 * its own, mounted into a box this screen lays out (kit/slot.js — absent: a quiet placeholder in the native's panel
 * colours; broken: "<Widget> failed to load").
 *
 * SLOTS (fixed paths, under web/js; the box is ROOT logical px, ctx.box — W × H the root's, top = SongBar::kHeight
 * 29.25, bw = the browser's LIVE width (234 open, 0 folded), rw = W − bw, dockH = min(the dock's LIVE height, H − top)):
 *   project/songbar.js    SongBar + App's menus (project/menus.js)  {0, 0, W, 29.25}          overflow visible (its menus)
 *   project/browser.js    Browser                                   {bw − 234, top, 234, H − top}  faded by bw / 234
 *   timeline/timeline.js  Timeline (+ Timeline*.cpp)                {bw, top, rw, H − top − dockH}  opacity 1 − session
 *   session/view.js       SessionView                               the timeline's box             opacity = session
 *   mixer/dock.js         MixerDock + AnalogFace                    {bw, top + H − top − dockH, rw, dockH}
 *   windows/layer.js      the WindowLayer (FloatWindow frames)      {0, top, W, H − top}  pointer-events: none — only
 *                         the windows themselves take input (WindowLayer::hitTestSelf is false); it mounts each window's
 *                         CONTENT by kind with ctx.slot(host, path, name, {box: <the content's rect, ROOT px>, window: {…}}):
 *       vm.view.windows[i].kind   key            content module         native widget   opened at (FloatWindow.cpp place)
 *       device                    dev:<dv>       device/panel.js        DevicePanel     820×580 an instrument with an analog
 *                                                                                       face, else 390×520
 *       roll                      roll:<pt>      piano/roll.js          PianoRoll       720×520 (`clip` = its clip)
 *       automation                auto:<au>      automation/panel.js    AutomationPanel 422.5×390
 *       loudness                  loudness       mixer/loudness.js      LoudnessPanel   390×357.5 "Loudness — Master"
 *       video                     video          video/panel.js         VideoPanel      448×299
 *     A new window cascades from the top right: x = clamp(W − w − 19.5 − 26k, 0, W − w), y = clamp(13 + 26k, 0, H − h),
 *     k = (opened count) mod 6; w, h clamped to the layer (− 13). Title bar FloatWindow::kTitleH 26. The layer marks the
 *     front roll's window element `data-front-roll` (the shell gives that window the keys) and writes
 *     vm.view.frontRoll. Placement and stacking are the view's — never sent.
 *   Other wave-2 modules, not slots of this screen: sheets/{settings,export,relink,conflicts}.js and history/view.js
 *   (the shell's modal layers), home/home.js (the shell's home screen), mixer/parammenu.js (ParamMenu — the menu a
 *   parameter row's right-click opens, imported dynamically by whoever opens it).
 *
 * THE DOCK (ProjectScreen.cpp:91-99, 178-184, 228-231): its height is the view's. dockTarget = the tab bar alone
 * (MixerDock::kTabsH 29.25) when folded, else max(kTabsH, min(what the user dragged it to — vm.view.dockWant, 429 at
 * first — , H − top − kLanesFloor)): it gives way before the lanes do — the lanes keep at least kLanesFloor = 130. A
 * fold, an unfold or the window changing EASES it (motion::kSlideMs, EaseOutCubic); a drag of its top edge
 * (vm.dockResize(h)) follows the pointer exactly — no ease. The chevron is vm.toggleDock(). LIVE: vm.view.dockHeight.
 * THE BROWSER folds by its WIDTH (View › Browser): eased kSlideMs; LIVE vm.view.browserWidth.
 * THE SESSION VIEW (R-LAUNCH-1, View › Session): cross-faded with the lanes, kCrossFadeMs EaseOutCubic; the one going
 * takes no input past the half-way. LIVE vm.view.sessionAmount.
 * SHOW IDS (R-UI-11): the model's settings.showIds eased once here (kCrossFadeMs) for every widget: vm.view.idsAmount.
 * THE DRAG (vm.dnd, vm/dnd.js): the ghost of the dragged item over everything — its label ellipsized at 220 px, 11 px
 * Roboto, a popover pill with the accent's 1 px border at (pointer + 12, + 10), 24 high, fading in and out over
 * kHoverMs — the label following the pointer exactly.
 */
import { signal, effect, untracked } from "../core/signal.js";
import { h } from "../core/dom.js";
import { Anim, Ease, frames, motion } from "../kit/anim.js";
import { slot } from "../kit/slot.js";
import { FONT, measure, ellipsize, svgBox, svgText } from "../kit/text.js";

export const TOP = 29.25;          // SongBar::kHeight
export const BROWSER_W = 234;      // Browser::kWidth (space::u(72))
export const DOCK_TABS_H = 29.25;  // MixerDock::kTabsH (space::u(9))
export const LANES_FLOOR = 130;    // ProjectScreen::kLanesFloor (space::u(40))

const same = (a, b) => a.x === b.x && a.y === b.y && a.w === b.w && a.h === b.h;
const boxSig = () => signal({ x: 0, y: 0, w: 0, h: 0 }, same);

export function mount(host, vm, ctx, { makeCtx }) {
  const M = motion();
  const overlay = h("div.sl-overlay");
  const hosts = {
    timeline: h("div.sl-slot"), session: h("div.sl-slot"), dock: h("div.sl-slot"), browser: h("div.sl-slot"),
    windows: h("div.sl-slot.sl-windows"), bar: h("div.sl-slot.sl-bar"),
  };
  host.append(hosts.timeline, hosts.session, hosts.dock, hosts.browser, hosts.windows, hosts.bar, overlay); // ProjectScreen's children, in order
  const boxes = { timeline: boxSig(), session: boxSig(), dock: boxSig(), browser: boxSig(), windows: boxSig(), bar: boxSig() };

  // ------------------------------------------------------------------ eased state (ProjectScreen::advance)
  const dockH = new Anim(0), browserW = new Anim(0), sessionAmt = new Anim(0), ids = new Anim(0), ghostA = new Anim(0);
  let dockInit = false, dockLast = 0, dragSeen = vm.view.dockDragged.peek();
  let browserInit = false, browserLast = true, sessionLast = false, idsInit = false, idsLast = false;
  let ghost = { label: "", x: 0, y: 0 }, ghostWanted = false;

  const size = () => ctx.box.peek();
  function dockTarget() {
    const avail = Math.max(0, size().h - TOP);
    if (!vm.view.dockOpen.peek()) return DOCK_TABS_H;
    return Math.max(DOCK_TABS_H, Math.min(vm.view.dockWant.peek(), avail - LANES_FLOOR)); // the dock gives way before the lanes do (R4)
  }

  function advance(now) {
    const dt = dockTarget();
    const dragged = vm.view.dockDragged.peek();
    if (!dockInit) { dockH.set(dt); dockLast = dt; dockInit = true; }
    else if (dragged !== dragSeen) { dragSeen = dragged; dockLast = dt; dockH.set(dt); }            // a drag: already there
    else if (dt !== dockLast) { dockH.animateTo(dt, M.slide, Ease.EaseOutCubic, now); dockLast = dt; } // toggled, or the window changed
    const bwWant = vm.view.browserOpen.peek() ? BROWSER_W : 0;
    if (!browserInit) { browserW.set(bwWant); browserLast = vm.view.browserOpen.peek(); browserInit = true; }
    else if (vm.view.browserOpen.peek() !== browserLast) { browserW.animateTo(bwWant, M.slide, Ease.EaseOutCubic, now); browserLast = vm.view.browserOpen.peek(); }
    if (vm.view.sessionOpen.peek() !== sessionLast) { // R-LAUNCH-1: a cross-fade, never a cut
      sessionAmt.animateTo(vm.view.sessionOpen.peek() ? 1 : 0, M.crossFade, Ease.EaseOutCubic, now);
      sessionLast = vm.view.sessionOpen.peek();
    }
    const idsWant = !!vm.settings.peek().showIds;
    if (!idsInit) { ids.set(idsWant ? 1 : 0); idsLast = idsWant; idsInit = true; }
    else if (idsWant !== idsLast) { ids.animateTo(idsWant ? 1 : 0, M.crossFade, Ease.EaseOutCubic, now); idsLast = idsWant; }
    const gw = ghostWanted ? 1 : 0;
    if (Math.abs(ghostA.value - gw) > 1e-6 && (!ghostA.animating || ghostA.target !== gw)) ghostA.animateTo(gw, M.hover, Ease.EaseOutCubic, now);
    let moving = false;
    for (const a of [dockH, browserW, sessionAmt, ids, ghostA]) moving = a.update(now) || moving;
    layout();
    return moving;
  }
  const kick = () => frames(advance);

  // ------------------------------------------------------------------ ProjectScreen::layout — the exact arithmetic
  function place(key, b, extra) {
    const el = hosts[key];
    el.style.transform = `translate(${b.x}px, ${b.y}px)`;
    el.style.width = b.w + "px";
    el.style.height = b.h + "px";
    if (extra) Object.assign(el.style, extra);
    boxes[key].value = b;
  }
  function layout() {
    const { w: W, h: H } = size();
    const top = TOP;
    place("bar", { x: 0, y: 0, w: W, h: TOP });
    const bw = browserW.value;
    place("browser", { x: bw - BROWSER_W, y: top, w: BROWSER_W, h: Math.max(0, H - top) }, { opacity: String(Math.min(1, Math.max(0, bw / BROWSER_W))) });
    const rw = Math.max(0, W - bw), dh = Math.min(dockH.value, Math.max(0, H - top));
    const lanes = { x: bw, y: top, w: rw, h: Math.max(0, H - top - dh) };     // from the LIVE dock height: the seam never tears
    const sa = sessionAmt.value;
    place("timeline", lanes, { opacity: String(1 - sa), visibility: sa < 0.999 ? "" : "hidden", pointerEvents: sa >= 0.5 ? "none" : "" });
    place("session", lanes, { opacity: String(sa), visibility: sa > 0.001 ? "" : "hidden", pointerEvents: sa < 0.5 ? "none" : "" });
    place("dock", { x: bw, y: top + Math.max(0, H - top - dh), w: rw, h: dh });
    place("windows", { x: 0, y: top, w: W, h: Math.max(0, H - top) });
    vm.view.dockHeight.value = dh;
    vm.view.browserWidth.value = bw;
    vm.view.sessionAmount.value = sa;
    vm.view.idsAmount.value = ids.value;
    paintGhost();
  }

  // ------------------------------------------------------------------ the ghost (ProjectScreen::onOverlay)
  const ghostEl = h("div.sl-ghost");
  const ghostSvg = svgBox(0, 24), ghostBody = document.createElementNS("http://www.w3.org/2000/svg", "rect"), ghostText = svgText("", 10, 12 + 11 * 0.35, 11, FONT.sans);
  ghostBody.setAttribute("class", "sl-ghost-body");
  ghostText.setAttribute("class", "sl-ghost-text");
  ghostSvg.append(ghostBody, ghostText);
  ghostEl.append(ghostSvg);
  overlay.append(ghostEl);
  function paintGhost() {
    const a = ghostA.value;
    ghostEl.style.display = a > 0.001 && ghost.label ? "" : "none";
    if (ghostEl.style.display === "none") return;
    const s = ellipsize(vm.shown(ghost.label), 220, 11, FONT.sans);
    const w = measure(s, 11, FONT.sans) + 20;
    ghostEl.style.transform = `translate(${ghost.x + 12}px, ${ghost.y + 10}px)`;
    ghostEl.style.opacity = String(a);
    ghostSvg.setAttribute("width", w);
    for (const [k, v] of Object.entries({ x: 0, y: 0, width: w, height: 24, rx: 2, ry: 2 })) ghostBody.setAttribute(k, v); // the border straddles, as Cairo
    if (ghostText.textContent !== s) ghostText.textContent = s;
  }
  effect(() => {
    const g = vm.dnd.ghost.value;
    untracked(() => {
      ghostWanted = !!g;
      if (g) ghost = { label: g.label, x: g.x, y: g.y };  // gone: it keeps its last place while it fades
      kick();
    });
  });

  // ------------------------------------------------------------------ what moves the layout
  effect(() => {
    void ctx.box.value; void vm.view.dockOpen.value; void vm.view.dockWant.value; void vm.view.dockDragged.value;
    void vm.view.browserOpen.value; void vm.view.sessionOpen.value; void vm.settings.value.showIds;
    untracked(kick);
  });
  advance(performance.now());

  // ------------------------------------------------------------------ the slots
  const quiet = (cls, parts) => (ph) => { ph.classList.add(cls); for (const p of parts || []) ph.append(h("div." + p)); };
  const mountOne = (key, path, name, placeholder) =>
    slot(hosts[key], path, vm, makeCtx(name, boxes[key], overlay), { name, placeholder });
  mountOne("timeline", "timeline/timeline.js", "Timeline", quiet("sl-ph-timeline", ["sl-ph-tl-head", "sl-ph-tl-ruler"]));
  mountOne("session", "session/view.js", "SessionView", quiet("sl-ph-session"));
  mountOne("dock", "mixer/dock.js", "MixerDock", quiet("sl-ph-dock", ["sl-ph-dock-tabs"]));
  mountOne("browser", "project/browser.js", "Browser", quiet("sl-ph-browser", ["sl-ph-browser-tabs"]));
  mountOne("windows", "windows/layer.js", "WindowLayer", quiet("sl-ph-windows"));
  mountOne("bar", "project/songbar.js", "SongBar", quiet("sl-ph-bar"));

  return { boxes, hosts, overlay, layout };
}
