/*
 * Interstellar — edit/topbar.js: the Edit page's 29.25 px chrome. Ports app/widgets/EditTopBar,
 * TabSwitcher and VersionSwitcher, and App::buildMenus / refresh{Settings,Workspace,Colour,Preset}Menu
 * (App.cpp:311-488) - the menu bar the App fills.
 *
 *      interstellar.  File Edit Settings Workspace Preset Colour  « project • »  [Grade|Cut|Deliver]  ‹ main ▾ ›   [save]
 *
 * EditTopBar (EditTopBar.cpp): the wordmark (13 px SemiBold, tracked −0.03×size, the accent dot at the
 * MEASURED end; a hover wash; a click asks to go Home = requestHome), the menu strip after it
 * (cosmo's MenuStrip, ui/menustrip.js, height 21), the project name ellipsized against the room left
 * before the tabs with an eased "edited" dot (200 ms), the tab switcher centred on the window, the
 * version switcher right after it (180…296 wide, sliding left before it would reach the save
 * button), and the save button (cosmo's IconButton: the icon eases muted → primary while dirty).
 *
 * TabSwitcher: a segmentedBg tray, a SOLID accent highlight that TRAVELS (220 ms) and labels whose
 * colour follows how much of the highlight covers them; hover cross-fades per segment.
 *
 * VersionSwitcher: ‹ and › step through the versions in tree order (dimmed at the ends); the body
 * shows the version - lock + "@commit · frozen" tags giving way before the name - and opens the
 * dropdown: VERSIONS + count, every version indented by depth with a tree elbow and its badges
 * (dangling, Δoverrides, frozen, @commit), then New version… / Pin|Unpin colour / Freeze|Thaw cut /
 * Rebase onto base. It opens 160 ms (closes 120 ms), placed against the root and clamped, the list
 * scrolling (EasedScroll) under a fixed action block; while open it takes every press (a click
 * outside closes it). A version switch cross-fades the chrome's label (200 ms).
 *
 * Every choice is one command line: `timeline open|pin|unpin|freeze|thaw|rebase <tl>`, `timeline new
 * <name> --base <tl>` (EditScreen.cpp's onNewVersion), `project save`, and the menus' own lines.
 * Web-only: "N viewing" (presence, NTWB-12) left of the save button when another page shares the session.
 *
 * Provides to the vm: "prompt" (NamePrompt, edit/nameprompt.js - ctx.prompt) and "versions"
 * ({isOpen, close} - the shell's Esc). Fixture view keys honoured: versionsOpen, menuOpen.
 */
import { h, own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { q, bindName } from "../kit/cmd.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { EasedScroll, wheelPx } from "../kit/easedscroll.js";
import { Painter, canvasSvg, col, css, fade, lerp, brighten, primaryA, whiteA, hoverWash, Anim, HoverFade,
         frameLoop, localPoint, inRect, Ease, F } from "../kit/paint.js";
import { menuStrip } from "../ui/menustrip.js";
import { namePrompt } from "./nameprompt.js";
import { requestHome, openRelinkMenu, openInputColourMenu, pickFootage, pickProjectToOpen, pickSaveAs,
         pickPresetToImport, pickStillToExport, pickTimelineToImport, pickTimelineToExport,
         pickCaptionsToImport, pickCaptionsToExport } from "./menus.js";

// EditTopBar.cpp:15-21
const PAD = 9.75, WORD_PX = 13, NAME_PX = 12, GROUP_GAP = 19.5, MENU_H = 21, MENU_GAP = 13, WORD = "interstellar";
// TabSwitcher.h:23-25
const TAB_H = 21, SEG_W = 64, TAB_PAD = 2, TABS = ["Grade", "Cut", "Deliver"];
// VersionSwitcher.h:30-33, .cpp:14-20
const VS_H = 21, ARROW_W = 20, ROW_H = 24.375, MENU_W = 280, ACTIONS = 4, VS_PAD = 4, HEADER_H = 24, DIVIDER_H = 9,
      EDGE = 8, GAP_BELOW = 6, VS_NAME_PX = 11, VER_MIN = 180, VER_MAX = 296;
const SAVE = 20.5;
const shortCommit = (c) => (c && c.length > 7 ? c.slice(0, 7) : c || "");

export function mount(host, vm, ctx) {
  useCss("css/edit/topbar.css");
  host.classList.add("tb");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  // ---------------------------------------------------------------- NamePrompt (ctx.prompt)
  const np = namePrompt(ctx.layers.modals);
  own(host, () => np.destroy());
  vm.provide("prompt", (o) => np.ask(o));

  // ---------------------------------------------------------------- state
  let W = 0, Hb = 29.25, rootW = 1440, rootH = 900, nowMs = 0;
  let projectName = "", dirty = false, bound = false, dirtyInit = false, dirtyApplied = false;
  const dirtyAmt = new Anim(0), homeHover = new Anim(0), saveHover = new Anim(0);
  let homeHovered = false, saveHovered = false, savePressed = false;
  // tabs
  let tabSel = vm.tab.peek(), tabInit = false, tabPending = false;
  const tabPos = new Anim(tabSel), tabHover = new HoverFade();
  // versions
  let timelines = [], current = "", shownId = "", prevShown = null, hasPrev = false, swapPending = false;
  let wantOpen = false, appliedOpen = false;
  const swap = new Anim(1), openAmt = new Anim(0), vhover = new HoverFade();
  const vscroll = new EasedScroll(() => loop.kick());
  // layout results
  const L = { tabsX: 0, tabsY: 0, tabsW: 0, verX: 0, verY: 0, verW: 232, saveX: 0, saveY: 0, menusX: 0, menusW: 0 };

  // ---------------------------------------------------------------- the menu bar (App::buildMenus)
  const selectedBind = () => vm.selectedBind.peek() || "";
  const run = (line) => vm.cmd(line);
  const model = () => vm.model.peek() || {};
  const curName = () => { const m = model(); const t = (m.timelines || []).find((x) => x.id === m.currentTimeline); return t ? t.name : ""; };
  const srcFlag = () => (vm.tab.peek() === 1 && model().sourceView ? " --source" : "");
  const strip = menuStrip({
    height: MENU_H,
    menus: [
      { title: "File", items: [
        { label: "Home", action: () => requestHome(vm, ctx) },
        { label: "Open...      (Ctrl+O)", action: () => pickProjectToOpen(vm, ctx) },
        { label: "Save         (Ctrl+S)", action: () => run("project save") },
        { label: "Save As...   (Ctrl+Shift+S)", action: () => pickSaveAs(vm, ctx) },
        { label: "Add Footage...", action: () => pickFootage(vm, ctx) },
        { label: "Export Still...", action: () => pickStillToExport(vm, ctx) },
        { label: "Import Timeline...", action: async () => { const f = await pickTimelineToImport(vm, ctx); if (f) run("interchange import " + q(f)); } },
        { label: "Export Timeline...", action: async () => {
          const name = curName();
          if (!name) return;
          const f = await pickTimelineToExport(vm, ctx, name + ".fcpxml");
          if (f) run("interchange export " + q(name) + " --out " + q(f));
        } },
        { label: "Import Captions...", action: async () => { const f = await pickCaptionsToImport(vm, ctx); if (f) run("caption import " + q(f)); } },
        { label: "Export Captions...", action: async () => {
          const name = curName();
          if (!name || !(model().captions || []).length) return;
          const f = await pickCaptionsToExport(vm, ctx, name + ".srt");
          if (f) run("caption export " + q(f) + " --timeline " + q(name));
        } },
        { label: "Relink Media...", action: () => openRelinkMenu(vm, ctx, rootW * 0.25, 40) },
        { label: "Render...", action: () => vm.setTab(2) },
      ] },
      { title: "Edit", items: [
        { label: "Undo         (Ctrl+Z)", action: () => run("undo") },
        { label: "Redo         (Ctrl+Y)", action: () => run("redo") },
        { label: "Copy Grade   (Ctrl+C)", action: () => { const b = selectedBind(); if (b) run("grade copy " + q(b)); } },
        { label: "Paste Grade to Selected (Ctrl+V)", action: () => { const b = selectedBind(); if (b) run("grade paste " + q(b)); } },
        { label: "Paste Grade to All Sources", action: () => run("grade paste --all") },
        { label: "Group Selection  (Ctrl+G)", action: () => run("rack group new") },
        { label: "Mark In      (I)", action: () => run("mark in" + srcFlag()) },
        { label: "Mark Out     (O)", action: () => run("mark out" + srcFlag()) },
        { label: "Clear In / Out", action: () => run("mark clear" + srcFlag()) },
        { label: "Insert       (,)", action: () => run("edit insert") },
        { label: "Overwrite    (.)", action: () => run("edit overwrite") },
        { label: "Ungroup", action: () => { const b = selectedBind(); if (b) run("rack ungroup " + q(b)); } },
        { label: "Duplicate as Variant (Ctrl+D)", action: () => { const b = selectedBind(); if (b) run("rack duplicate " + q(b)); } },
      ] },
      { title: "Settings", items: [] },
      { title: "Workspace", items: [] },
      { title: "Preset", items: [] },
      { title: "Colour", items: [] },
    ],
    onOpenChanged: () => {},
  });
  strip.el.classList.add("tb-strip");
  host.append(strip.el);
  own(host, () => strip.destroy && strip.destroy());

  const resetWorkspace = () => {
    vm.setTab(0);
    const t = vm.provided("timeline"); if (t && t.resetView) t.resetView();
    const mo = vm.provided("monitor"); if (mo && mo.resetZoom) mo.resetZoom();
  };
  let settingsFor = null, workspaceFor = null, colourFor = null, presetFor = null;
  function refreshMenus(m) {
    // App::refreshSettingsMenu (R-DLV-5)
    const s = m.settings ? m.settings.autosave : undefined;
    if (s !== settingsFor) {
      settingsFor = s;
      const mark = (v) => (s === v ? "•  " : "     ");
      strip.setItems(2, [
        { label: "Engine Settings...", action: () => vm.openSettings() },
        { label: mark(60) + "Autosave Every Minute", action: () => run("settings set autosave=60") },
        { label: mark(300) + "Autosave Every 5 Minutes", action: () => run("settings set autosave=300") },
        { label: mark(0) + "Autosave Off", action: () => run("settings set autosave=0") },
      ]);
    }
    // App::refreshWorkspaceMenu (R-MEDIA-2, R-DLV-1)
    const key = m.screen === "edit" ? (m.useProxies ? 2 : 1) + (m.captionsShown ? 0 : 2) : 0;
    if (key !== workspaceFor) {
      workspaceFor = key;
      const items = [
        { label: "Grade        (1)", action: () => vm.setTab(0) },
        { label: "Cut          (2)", action: () => vm.setTab(1) },
        { label: "Deliver      (3)", action: () => vm.setTab(2) },
        { label: "Reset Workspace", action: resetWorkspace },
      ];
      if (key) {
        items.push({ label: (m.useProxies ? "•  " : "     ") + "Use Proxies", action: () => run("proxy use " + (m.useProxies ? "off" : "on")) });
        items.push({ label: "     Make Proxies for All Video", action: () => run("proxy make") });
        items.push({ label: (m.captionsShown ? "•  " : "     ") + "Show Captions", action: () => run("view captions " + (m.captionsShown ? "off" : "on")) });
      }
      strip.setItems(3, items);
    }
    // App::refreshColourMenu (R-COLOR-3, R-CLR-1/4)
    const ck = (m.workingSpace || "") + (m.matteView ? "|matte" : "");
    if (ck !== colourFor) {
      colourFor = ck;
      const mark = (id) => (m.workingSpace === id ? "•  " : "     ");
      strip.setItems(5, [
        { label: mark("rec709") + "Rec.709 Working Space", action: () => run("colour working rec709") },
        { label: mark("acescct") + "ACEScct Working Space", action: () => run("colour working acescct") },
        { label: "Input Colour of Selected...", action: () => { const b = selectedBind(); if (b) openInputColourMenu(vm, ctx, b, rootW * 0.5, 40); } },
        { label: "     Grab Still  (Ctrl+Alt+G)", action: () => vm.grabStill() },
        { label: (m.matteView ? "•  " : "     ") + "Show Matte  (Shift+H)", action: () => {
          const on = !model().matteView;
          run("view matte " + (on ? "on" : "off"));
          if (on) vm.setTab(0);
        } },
      ]);
    }
    // App::refreshPresetMenu
    const names = (m.presets || []).map((x) => x.name);
    const pk = JSON.stringify(names);
    if (pk !== presetFor) {
      presetFor = pk;
      const items = [
        { label: "Save Preset...", action: async () => {
          const b = selectedBind();
          if (!b) return;
          const name = await ctx.prompt({ title: "Save preset", message: "The selected source's grade, saved to the library as an .apf.", value: "", ok: "Save" });
          if (name) run("preset save " + q(name) + " --node " + q(b));
        } },
        { label: "Import Preset...", action: () => pickPresetToImport(vm, ctx) },
      ];
      for (const n of names) items.push({ label: "Apply  " + n, action: () => { const b = selectedBind(); if (b) run("preset apply " + q(n) + " --node " + q(b)); } });
      strip.setItems(4, items);
    }
  }

  // ---------------------------------------------------------------- bind (EditTopBar / VersionSwitcher::bind)
  own(host, effect(() => {
    const m = vm.model.value;
    if (!m) return;
    projectName = m.projectName || "";
    dirty = !!m.dirty;
    bound = true;
    if (shownId && m.currentTimeline !== shownId) {
      const ci = timelines.findIndex((t) => t.id === current);
      if (ci >= 0) { prevShown = timelines[ci]; hasPrev = true; swapPending = true; }
    }
    timelines = m.timelines || [];
    current = m.currentTimeline || "";
    shownId = current;
    refreshMenus(m);
    loop.kick();
  }));
  own(host, effect(() => {                           // EditScreen::setTab: the switcher follows, versions close
    const t = vm.tab.value;
    if (t !== tabSel) { tabSel = t; tabPending = true; }
    wantOpen = false;
    loop.kick();
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; Hb = b.h || 29.25; const s = ctx.size.value; rootW = s.w; rootH = s.h; fontEpoch.value; loop.kick(); }));
  own(host, effect(() => { vm.presence.value; loop.kick(); }));
  own(host, effect(() => {                           // the native shots' view state
    const fx = vm.view.fixture.value;
    if (!fx) return;
    if (fx.versionsOpen) { wantOpen = true; loop.kick(); }
    if (typeof fx.menuOpen === "number" && fx.menuOpen >= 0) requestAnimationFrame(() => strip.setOpen(fx.menuOpen));
    if (fx.namePromptOpen) requestAnimationFrame(() => newVersion());
  }));
  vm.provide("versions", { isOpen: () => wantOpen, close: () => { wantOpen = false; loop.kick(); } });

  // ---------------------------------------------------------------- layout (EditTopBar::layout)
  let wordW = 82;
  function layout() {
    wordW = measure(WORD, WORD_PX, F.semibold, -0.03 * WORD_PX) + measure(".", WORD_PX, F.semibold, -0.03 * WORD_PX);
    L.menusX = PAD + wordW + MENU_GAP;
    L.menusW = strip.width;
    strip.el.style.transform = `translate(${L.menusX}px, ${(Hb - MENU_H) * 0.5}px)`;
    const menusRight = L.menusX + L.menusW;
    const tabsW = TAB_PAD * 2 + SEG_W * TABS.length;
    let tabsX = (W - tabsW) * 0.5;
    const saveX = W - PAD - SAVE, maxRight = saveX - GROUP_GAP;
    if (tabsX + tabsW + 13 + VER_MIN > maxRight) tabsX = Math.max(menusRight + GROUP_GAP, maxRight - VER_MIN - 13 - tabsW);
    tabsX = Math.max(tabsX, menusRight + GROUP_GAP);
    L.tabsX = tabsX; L.tabsY = (Hb - TAB_H) * 0.5; L.tabsW = tabsW;
    L.verX = tabsX + tabsW + 13; L.verY = (Hb - VS_H) * 0.5;
    L.verW = Math.min(VER_MAX, Math.max(VER_MIN, maxRight - (tabsX + tabsW + 13)));
    L.saveX = saveX; L.saveY = (Hb - SAVE) * 0.5;
  }
  const wordmarkRect = () => ({ x: 0, y: 0, w: wordW + 2 * PAD, h: Hb });
  const nameRect = () => { const x = L.menusX + L.menusW + GROUP_GAP; return { x, y: 0, w: Math.max(0, L.tabsX - GROUP_GAP - x), h: Hb }; };
  const segRect = (i) => ({ x: L.tabsX + TAB_PAD + i * SEG_W, y: L.tabsY + TAB_PAD, w: SEG_W, h: TAB_H - 2 * TAB_PAD });
  const saveRect = () => ({ x: L.saveX, y: L.saveY, w: SAVE, h: SAVE });

  // ---------------------------------------------------------------- versions geometry (local = switcher coords)
  const curIndex = () => timelines.findIndex((t) => t.id === current);
  const prevRect = () => ({ x: 0, y: 0, w: ARROW_W, h: VS_H });
  const nextRect = () => ({ x: L.verW - ARROW_W, y: 0, w: ARROW_W, h: VS_H });
  const bodyRect = () => ({ x: ARROW_W, y: 0, w: L.verW - 2 * ARROW_W, h: VS_H });
  function menuLayout() {                            // VersionSwitcher.cpp:49-70
    const o = { x: L.verX, y: L.verY };
    const M = { w: MENU_W, x: 0 };
    if (o.x + M.x + M.w > rootW - EDGE) M.x = rootW - EDGE - M.w - o.x;
    if (o.x + M.x < EDGE) M.x = EDGE - o.x;
    M.y = VS_H + GAP_BELOW;
    const avail = Math.max(0, rootH - (o.y + M.y) - 2 * EDGE);
    const actionsH = ACTIONS * ROW_H, fixed = 2 * VS_PAD + HEADER_H + DIVIDER_H + actionsH, content = timelines.length * ROW_H;
    M.listTop = M.y + VS_PAD + HEADER_H;
    M.listH = Math.min(Math.max(avail - fixed, Math.min(content, ROW_H)), Math.max(content, ROW_H));
    if (M.listH > content) M.listH = content;
    M.dividerY = M.listTop + M.listH + DIVIDER_H * 0.5;
    M.actionsTop = M.listTop + M.listH + DIVIDER_H;
    M.total = fixed + M.listH;
    return M;
  }
  const menuRect = () => { const M = menuLayout(); return { x: M.x, y: M.y, w: M.w, h: M.total }; };
  const rowRect = (i) => { const M = menuLayout(); return { x: M.x, y: M.listTop + i * ROW_H - vscroll.value, w: M.w, h: ROW_H }; };
  const actionRect = (a) => { const M = menuLayout(); return { x: M.x, y: M.actionsTop + a * ROW_H, w: M.w, h: ROW_H }; };
  function actionEnabled(a) {
    const ci = curIndex();
    if (ci < 0) return a === 0 ? timelines.length > 0 : false;
    const t = timelines[ci], derived = !!t.base;
    return a === 0 ? true : a === 1 ? derived || t.colourPinned : a === 2 ? derived || t.cutFrozen : a === 3 ? derived : false;
  }
  function actionLabel(a) {
    const ci = curIndex(), t = ci >= 0 ? timelines[ci] : null;
    return a === 0 ? "New version…" : a === 1 ? (t && t.colourPinned ? "Unpin colour" : "Pin colour to base")
         : a === 2 ? (t && t.cutFrozen ? "Thaw cut" : "Freeze cut") : a === 3 ? "Rebase onto base" : "";
  }
  function vHoverAt(pt) {                            // 0 prev, 1 body, 2 next, 10+a action, 100+i row
    if (wantOpen) {
      const M = menuLayout();
      if (inRect(pt, { x: M.x, y: M.listTop, w: M.w, h: M.listH })) for (let i = 0; i < timelines.length; ++i) if (inRect(pt, rowRect(i))) return 100 + i;
      for (let a = 0; a < ACTIONS; ++a) if (inRect(pt, actionRect(a))) return 10 + a;
    }
    if (inRect(pt, prevRect())) return 0;
    if (inRect(pt, nextRect())) return 2;
    if (inRect(pt, bodyRect())) return 1;
    return -1;
  }
  function stepVersion(dir) {
    const ci = curIndex(), j = ci + dir;
    if (ci < 0 || j < 0 || j >= timelines.length) return;
    run("timeline open " + q(timelines[j].id));
  }
  async function newVersion() {                      // EditScreen.cpp: onNewVersion
    const base = current;
    const typed = await ctx.prompt({ title: "New version", message: "A derived timeline: its base, plus your changes. Letters, digits and _.", value: "", ok: "Create" });
    const name = typed ? bindName(typed) : "";
    if (!name) return;
    run("timeline new " + q(name) + (base ? " --base " + q(base) : ""));
  }
  function versionClick(pt) {                        // VersionSwitcher::handleGesture Click
    const id = vHoverAt(pt);
    if (wantOpen) {
      if (id >= 100) {
        const t = timelines[id - 100];
        if (t.id !== current) run("timeline open " + q(t.id));
        wantOpen = false;
      } else if (id >= 10) {
        const a = id - 10;
        if (!actionEnabled(a)) return;
        const ci = curIndex(), tl = ci >= 0 ? q(timelines[ci].id) : "";
        wantOpen = false;                            // close BEFORE acting: the action may open a modal
        if (a === 0) newVersion();
        else if (a === 1) run((timelines[ci].colourPinned ? "timeline unpin " : "timeline pin ") + tl);
        else if (a === 2) run((timelines[ci].cutFrozen ? "timeline thaw " : "timeline freeze ") + tl);
        else if (a === 3) run("timeline rebase " + tl);
      } else if (!inRect(pt, menuRect())) {
        wantOpen = false;
        if (id === 0) stepVersion(-1); else if (id === 2) stepVersion(1);
      }
      loop.kick();
      return;
    }
    if (id === 0) stepVersion(-1);
    else if (id === 2) stepVersion(1);
    else if (id === 1) { wantOpen = true; loop.kick(); }
  }

  // ---------------------------------------------------------------- advance
  function advance(now) {
    nowMs = now;
    let moving = false;
    // EditTopBar::advance
    moving = homeHover.update(now) || moving;
    moving = saveHover.update(now) || moving;
    if (!dirtyInit) { dirtyAmt.set(dirty ? 1 : 0); dirtyApplied = dirty; dirtyInit = bound; }
    if (dirty !== dirtyApplied) { dirtyAmt.animateTo(dirty ? 1 : 0, tok.ms("motion-select"), Ease.EaseOutCubic, now); dirtyApplied = dirty; }
    moving = dirtyAmt.update(now) || moving;
    // TabSwitcher::advance
    if (!tabInit) { tabPos.set(tabSel); tabInit = true; tabPending = false; }
    else if (tabPending) { tabPos.animateTo(tabSel, tok.ms("motion-slide"), Ease.EaseOutCubic, now); tabPending = false; }
    moving = tabPos.update(now) || moving;
    moving = tabHover.advance(now) || moving;
    // VersionSwitcher::advance
    if (wantOpen !== appliedOpen) {
      openAmt.animateTo(wantOpen ? 1 : 0, wantOpen ? tok.ms("motion-dropdown") : tok.ms("motion-modal-close"), Ease.EaseOutCubic, now);
      if (wantOpen) {
        const M = menuLayout();
        vscroll.setExtent(M.listTop, M.listH, timelines.length * ROW_H);
        const ci = curIndex();
        if (ci >= 0) vscroll.reveal(ci * ROW_H, ROW_H);
      }
      appliedOpen = wantOpen;
      vlayer.classList.toggle("on", wantOpen);
    }
    moving = openAmt.update(now) || moving;
    if (swapPending) { swap.set(0); swap.animateTo(1, tok.ms("motion-cross-fade"), Ease.EaseOutCubic, now); swapPending = false; }
    moving = swap.update(now) || moving;
    if (hasPrev && !swap.animating) hasPrev = false;
    const M = menuLayout();
    vscroll.setExtent(M.listTop, M.listH, timelines.length * ROW_H);
    moving = vhover.advance(now) || moving;
    return moving || vscroll.moving;
  }

  // ---------------------------------------------------------------- paint
  function paint() {
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), primary = col("primary");
    const rc = tok.px("radius-control"), hair = tok.px("radius-hairline");
    p.begin();
    // EditTopBar::onPaint
    p.rect(0, 0, W, Hb, 0, { fill: col("background") });
    p.line(0, Hb, W, Hb, border, 1);
    const sp = -0.03 * WORD_PX, base = baseline(Hb * 0.5, WORD_PX);
    const ww = measure(WORD, WORD_PX, F.semibold, sp);
    const hv = homeHover.value;
    if (hv > 0.001) p.rect(PAD - 5, (Hb - 20) * 0.5, wordW + 10, 20, rc, { fill: hoverWash(hv) });
    p.text(WORD, PAD, base, WORD_PX, F.semibold, fg, sp);
    p.text(".", PAD + ww, base, WORD_PX, F.semibold, primary, sp);
    const nr = nameRect(), d = dirtyAmt.value;
    const name = ellipsize(projectName, nr.w - 12, NAME_PX, F.medium);
    if (name) {
      p.text(name, nr.x, baseline(Hb * 0.5, NAME_PX), NAME_PX, F.medium, fg);
      if (d > 0.001) p.circle(nr.x + measure(name, NAME_PX, F.medium) + 7, Hb * 0.5, 2.5, { fill: primaryA(0.9 * d) });
    }
    // TabSwitcher::onPaint
    {
      const x = L.tabsX, y = L.tabsY, w = L.tabsW, hh = TAB_H, n = TABS.length;
      p.rect(x, y, w, hh, rc, { fill: col("segmented-bg"), stroke: border, sw: 1 });
      const pos = Math.min(Math.max(tabPos.value, 0), n - 1);
      for (let i = 0; i < n; ++i) { const a = tabHover.amount(i); if (a > 0.001) { const r = segRect(i); p.rect(r.x, r.y, r.w, r.h, hair, { fill: hoverWash(a) }); } }
      p.rect(x + TAB_PAD + pos * SEG_W, y + TAB_PAD, SEG_W, hh - 2 * TAB_PAD, hair, { fill: primary });
      for (let i = 0; i < n; ++i) {
        const cover = Math.max(0, 1 - Math.abs(pos - i));
        let c = lerp(muted, fg, 0.55 * tabHover.amount(i));
        c = lerp(c, col("primary-foreground"), cover);
        const r = segRect(i), label = ellipsize(TABS[i], r.w - 8, 10, F.medium);
        p.text(label, r.x + (r.w - measure(label, 10, F.medium)) * 0.5, baseline(r.y + r.h * 0.5, 10), 10, F.medium, c);
      }
    }
    paintVersions(fg, muted, border, primary, rc, hair);
    // the save button (cosmo IconButton: hoverBg whiteAlpha(0.05), idle muted → primary by the dirty amount)
    {
      const r = saveRect(), sh = saveHover.value, vis = Math.max(sh, savePressed ? 1 : 0);
      if (vis > 0.001) p.rect(r.x, r.y, r.w, r.h, 2, { fill: whiteA(0.05 * vis) });
      p.icon("save", r.x, r.y, r.w, brighten(lerp(muted, primary, d), 0.4 * sh));
    }
    // web-only: presence (NTWB-12), left of the save button
    const pr = vm.presence.peek();
    const n = pr && pr.clients ? (Array.isArray(pr.clients) ? pr.clients.length : +pr.clients) : 0;
    if (n > 1) {
      const s = n + " viewing", tw = measure(s, 10, F.sans);
      p.text(s, L.saveX - 10 - tw, baseline(Hb * 0.5, 10), 10, F.sans, muted);
    }
    p.end();
    paintDropdown(fg, muted, border, primary, rc);
  }

  function paintVersions(fg, muted, border, primary, rc, hair) {   // VersionSwitcher::onPaint / paintBody
    const ox = L.verX, oy = L.verY, w = L.verW, hh = VS_H;
    p.save();
    p.cur.setAttribute("transform", `translate(${ox} ${oy})`);
    p.rect(0, 0, w, hh, rc, { fill: col("segmented-bg"), stroke: border, sw: 1 });
    const ci = curIndex(), canPrev = ci > 0, canNext = ci >= 0 && ci + 1 < timelines.length;
    for (let k = 0; k < 2; ++k) {
      const r = k === 0 ? prevRect() : nextRect(), a = vhover.amount(k === 0 ? 0 : 2);
      if (a > 0.001) p.rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, hair, { fill: hoverWash(a) });
      const en = k === 0 ? canPrev : canNext;
      const c = en ? lerp(muted, fg, 0.6 * a) : fade(muted, 0.45);
      const gb = { x: r.x + (r.w - 10) * 0.5, y: r.y + (r.h - 10) * 0.5, w: 10, h: 10 };
      p.add(k === 0 ? glyph.chevronLeft(gb, css(c)) : glyph.chevronRightSmall(gb, css(c)));
    }
    p.line(ARROW_W, 3, ARROW_W, hh - 3, border, 1);
    p.line(w - ARROW_W, 3, w - ARROW_W, hh - 3, border, 1);
    const body = bodyRect(), bhv = Math.max(vhover.amount(1), openAmt.value);
    if (bhv > 0.001) p.rect(body.x + 1, body.y + 1, body.w - 2, body.h - 2, hair, { fill: hoverWash(bhv) });
    const sw = swap.value;
    if (ci < 0) paintBody(null, 1, fg, muted, primary);
    else {
      if (hasPrev && sw < 0.999) paintBody(prevShown, 1 - sw, fg, muted, primary);
      paintBody(timelines[ci], hasPrev ? sw : 1, fg, muted, primary);
    }
    p.restore();
  }
  function paintBody(tl, alpha, fg, muted, primary) {
    const body = bodyRect(), cy = VS_H * 0.5, right = body.x + body.w;
    let x = body.x + 8;
    if (!tl) { p.text("no version", x, baseline(cy, 10), 10, F.sans, fade(muted, alpha)); return; }
    const locked = tl.colourPinned || tl.cutFrozen, caretW = 10;
    const commit = tl.colourPinned ? "@" + shortCommit(tl.pinCommit) : "";
    const tags = [];
    if (tl.colourPinned && tl.cutFrozen) tags.push(commit + " · frozen");
    if (tl.colourPinned) tags.push(commit); else if (tl.cutFrozen) tags.push("frozen");
    tags.push("");
    const full = tl.name || tl.id, nameW = measure(full, VS_NAME_PX, F.medium), x0 = x + (locked ? 13 : 0);
    let tag = "";
    for (const cand of tags) {
      tag = cand;
      const tw = cand ? measure(cand, 9, F.mono) + 8 : 0;
      if (right - 8 - caretW - 4 - tw - x0 >= Math.min(nameW, 72)) break;
    }
    const tagTextW = tag ? measure(tag, 9, F.mono) : 0, tagW = tag ? tagTextW + 8 : 0;
    if (locked) { p.add(glyph.lock({ x, y: cy - 5.5, w: 9, h: 11 }, css(fade(primary, alpha)))); x += 13; }
    const nm = ellipsize(full, right - 8 - caretW - 4 - tagW - x, VS_NAME_PX, F.medium);
    p.text(nm, x, baseline(cy, VS_NAME_PX), VS_NAME_PX, F.medium, fade(fg, alpha));
    if (tag) p.text(tag, right - 8 - caretW - 4 - tagTextW, baseline(cy, 9), 9, F.mono, fade(primary, alpha));
    p.add(glyph.caretDown({ x: right - 8 - caretW, y: cy - 5, w: caretW, h: 10 }, css(muted)));
  }

  // the dropdown, in the shell's menu layer (root px = this bar's px: the bar sits at the root's 0, 0)
  const vlayer = h("div.tb-vmenu");
  const vsvg = canvasSvg();
  vlayer.append(vsvg);
  ctx.layers.menus.append(vlayer);
  own(host, () => vlayer.remove());
  const vp = new Painter(vsvg);
  function paintDropdown(fg, muted, border, primary, rc) {   // VersionSwitcher::onOverlay
    const a = openAmt.value;
    vp.begin();
    if (a > 0.001) {
      const M = menuLayout(), ox = L.verX, oy = L.verY;
      vp.save();
      vp.cur.setAttribute("transform", `translate(${ox} ${oy})`);
      const revealH = M.total * (0.92 + 0.08 * a);
      const card = { x: M.x, y: M.y - 4 * (1 - a), w: M.w, h: revealH };
      vp.clip(card.x - 12, card.y - 2, card.w + 24, card.h + 16);
      vp.rect(card.x, card.y + 3, card.w, card.h, rc, { fill: [0, 0, 0, 0.35 * a] });
      vp.rect(card.x, card.y, card.w, card.h, rc, { fill: fade(col("popover"), a), stroke: fade(border, a), sw: 1 });
      const dy = card.y - M.y;
      vp.text("VERSIONS", M.x + 10, M.y + dy + VS_PAD + HEADER_H * 0.5 + 9 * 0.35, 9, F.semibold, fade(muted, a), 0.13 * 9);
      const count = String(timelines.length);
      vp.text(count, M.x + M.w - 10 - measure(count, 9, F.mono), M.y + dy + VS_PAD + HEADER_H * 0.5 + 9 * 0.35, 9, F.mono, fade(muted, a));
      vp.clip(M.x, M.listTop + dy, M.w, M.listH);
      for (let i = 0; i < timelines.length; ++i) {
        if (!vscroll.bandVisible(i * ROW_H, ROW_H)) continue;
        const t = timelines[i], r = rowRect(i);
        r.y += dy;
        const cur = t.id === current, hv = vhover.amount(100 + i);
        if (cur) vp.rect(r.x + 3, r.y + 1, r.w - 6, r.h - 2, rc, { fill: primaryA(0.14 * a) });
        if (hv > 0.001) vp.rect(r.x + 3, r.y + 1, r.w - 6, r.h - 2, rc, { fill: hoverWash(hv * a) });
        let x = r.x + 10 + (t.depth || 0) * tok.px("space-indent");
        const cy = r.y + r.h * 0.5;
        if (t.depth > 0) {
          const ec = fade(border, a * 2);
          vp.line(x - 8, cy - 7, x - 8, cy, ec, 1);
          vp.line(x - 8, cy, x - 3, cy, ec, 1);
        }
        let right = r.x + r.w - 10;
        const badge = (s, c, fam) => { const bw = measure(s, 9, fam); right -= bw; vp.text(s, right, baseline(cy, 9), 9, fam, fade(c, a)); right -= 7; };
        if (t.danglingDeltas > 0) badge(t.danglingDeltas + " dangling", col("destructive"), F.sans);
        if (t.overrides > 0) badge("Δ" + t.overrides, muted, F.mono);
        if (t.cutFrozen) badge("frozen", muted, F.sans);
        if (t.colourPinned) badge("@" + shortCommit(t.pinCommit), primary, F.mono);
        if (t.colourPinned || t.cutFrozen) { vp.add(glyph.lock({ x, y: cy - 5, w: 8, h: 10 }, css(fade(cur ? primary : muted, a)))); x += 12; }
        const fam = cur ? F.medium : F.sans;
        vp.text(ellipsize(t.name || t.id, right - x, VS_NAME_PX, fam), x, baseline(cy, VS_NAME_PX), VS_NAME_PX, fam, fade(cur ? primary : fg, a));
      }
      vp.restore();
      const bar = vscroll.bar(M.x + M.w - 2);
      if (bar) vp.rect(bar.x, bar.y + dy, bar.w, bar.h, tok.px("radius-pill"), { fill: whiteA(0.14 * a) });
      vp.line(M.x + 8, M.dividerY + dy, M.x + M.w - 8, M.dividerY + dy, fade(border, a), 1);
      for (let k = 0; k < ACTIONS; ++k) {
        const r = actionRect(k);
        r.y += dy;
        const en = actionEnabled(k), hv = en ? vhover.amount(10 + k) : 0;
        if (hv > 0.001) vp.rect(r.x + 3, r.y + 1, r.w - 6, r.h - 2, rc, { fill: hoverWash(hv * a) });
        const c = en ? lerp(fg, col("white"), 0.3 * hv) : fade(muted, 0.7);
        let label = actionLabel(k);
        if (k === 3) { const ci = curIndex(); if (ci >= 0 && timelines[ci].danglingDeltas > 0) label += " · " + timelines[ci].danglingDeltas + " dangling"; }
        vp.text(ellipsize(label, r.w - 20, VS_NAME_PX, F.sans), r.x + 10, baseline(r.y + r.h * 0.5, VS_NAME_PX), VS_NAME_PX, F.sans, fade(c, a));
      }
      vp.restore();
      vp.restore();
    }
    vp.end();
  }

  function step(now) {
    if (!(W > 0)) return false;
    layout();
    const more = advance(now);
    paint();
    return more;
  }

  // ---------------------------------------------------------------- input
  const hoverAnim = (anim, on) => anim.animateTo(on ? 1 : 0, tok.ms("motion-hover"), Ease.EaseOutCubic, performance.now());
  host.addEventListener("pointermove", (e) => {
    if (e.target.closest && e.target.closest(".tb-strip")) return;
    const pt = localPoint(host, e);
    const over = inRect(pt, wordmarkRect());
    if (over !== homeHovered) { homeHovered = over; hoverAnim(homeHover, over); }
    const sv = inRect(pt, saveRect());
    if (sv !== saveHovered) { saveHovered = sv; hoverAnim(saveHover, sv); }
    let seg = -1;
    for (let i = 0; i < TABS.length; ++i) if (inRect(pt, segRect(i))) seg = i;
    tabHover.setHovered(seg === tabSel ? -1 : seg);
    if (!wantOpen) vhover.setHovered(vHoverAt({ x: pt.x - L.verX, y: pt.y - L.verY }));
    loop.kick();
  });
  host.addEventListener("pointerleave", () => {
    if (homeHovered) { homeHovered = false; hoverAnim(homeHover, false); }
    if (saveHovered) { saveHovered = false; hoverAnim(saveHover, false); }
    savePressed = false;
    tabHover.clear();
    if (!wantOpen) vhover.clear();
    loop.kick();
  });
  host.addEventListener("pointerdown", (e) => { if (inRect(localPoint(host, e), saveRect())) { savePressed = true; loop.kick(); } });
  host.addEventListener("pointerup", () => { if (savePressed) { savePressed = false; loop.kick(); } });
  host.addEventListener("click", (e) => {
    if (e.target.closest && e.target.closest(".tb-strip")) return;
    const pt = localPoint(host, e);
    if (inRect(pt, wordmarkRect())) { requestHome(vm, ctx); return; }
    if (inRect(pt, saveRect())) { run("project save"); return; }
    for (let i = 0; i < TABS.length; ++i) if (inRect(pt, segRect(i))) { vm.setTab(i); return; }
    versionClick({ x: pt.x - L.verX, y: pt.y - L.verY });
  });
  // the open dropdown takes every press; a click outside closes it
  vlayer.addEventListener("pointermove", (e) => {
    const pt = localPoint(vlayer, e);
    vhover.setHovered(vHoverAt({ x: pt.x - L.verX, y: pt.y - L.verY }));
    loop.kick();
  });
  vlayer.addEventListener("click", (e) => { const pt = localPoint(vlayer, e); versionClick({ x: pt.x - L.verX, y: pt.y - L.verY }); });
  vlayer.addEventListener("wheel", (e) => {
    e.preventDefault();
    const M = menuLayout(), pt = localPoint(vlayer, e);
    if (inRect({ x: pt.x - L.verX, y: pt.y - L.verY }, { x: M.x, y: M.listTop, w: M.w, h: M.listH })) { vscroll.scrollBy(wheelPx(e)); loop.kick(); }
  }, { passive: false });

  loop.kick();
}
