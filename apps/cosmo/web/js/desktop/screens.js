/*
 * Cosmo by arstro — screens.js: which screen THIS page shows, and the native transitions between
 * them (App.cpp: Screen Home | Loading | Editor, beginOpenTransition / renderTransition /
 * showHome / showEditor / requestHome, SplashScreen before the first model).
 *
 * The App owns its screen as view state and drives it from the service's events (App.cpp,
 * linux_main.cpp:582-685); so does this page. It watches `vm.screen` (the session's screen -
 * vm.goHome() sends `screen home`, so every page of the session goes) and the load events,
 * and runs the phases itself:
 *
 *   first model          Home (or the editor) under a background scrim 1 -> 0, 220 ms (App.cpp:1359)
 *   project.opening      Intro 460 -> Loading (bar fades in 160) -> Reveal 520 -> Editor (App.cpp:28-38),
 *                        the clicked recent flying from its grid rect to the centre as the whole card
 *   project new / back   the editor under the 220 ms scrim (App::showEditor)
 *   editor -> Home       ReturnEnter 300 / ReturnLoad 200 / ReturnExit 320, the wordmark flying home
 *                        over 480 ms; Home's own wordmark hidden until it lands
 * Input is swallowed while a transition runs (App.cpp:66, 867, 1801); on Home only the search
 * field takes keys (App.cpp:1818).
 *
 * The Home actions' flows (spec §4.8, §5): New Project = the board's file browser in Save mode
 * -> `project new`; Open Project… -> `project open`; Import Catalog… = pick images, then the
 * new project's name -> `project new` + `import` (the transition runs from the import's
 * project.opening, not from the intermediate editor); a recent card -> `project open` with the
 * card's live rect for the flight. If the session already has THAT project open, the card opens
 * this page's editor on it instead of reloading it for everyone.
 *
 * Adds to ctx (for the shell / menus): requestHome() - the wordmark's "go home" with the
 * native's unsaved-changes question -, pick(opts), confirm(opts), screen() -> this page's screen.
 */
import { h, own } from "../core/dom.js";
import { effect, untracked } from "../core/signal.js";
import { mountHome, coverOf } from "./home.js";
import { Transition, Splash } from "./loading.js";
import { mountDialogHost } from "../dialogs/host.js";
import { pick } from "../dialogs/picker.js";
import { confirm } from "../dialogs/confirm.js";
import { modalOpen } from "../ui/modal.js";

const stem = (p) => String(p || "").split("/").pop().replace(/\.[^.]*$/, "");

export function mountScreens(root, vm, ctx = {}, { mountEditor } = {}) {
  const el = h("div.scr-root");
  root.append(el);
  const homeLayer = h("div.scr-layer.scr-home.off", { inert: true });
  const editorLayer = h("div.scr-layer.scr-editor.off", { inert: true });
  const fadeEl = h("div.scr-fade");
  el.append(homeLayer, editorLayer, fadeEl);

  let editorMounted = false;
  function ensureEditor() {
    if (editorMounted) return;
    editorMounted = true;
    try { if (mountEditor) mountEditor(editorLayer, vm, ctx); }
    catch (e) { console.error(e); if (ctx.toast) ctx.toast("The editor failed to load: " + e.message, true); }
  }
  const setLayer = (layer, on) => { layer.classList.toggle("off", !on); layer.inert = !on; };
  const setHome = (on) => setLayer(homeLayer, on);
  const setEditor = (on) => { if (on) ensureEditor(); setLayer(editorLayer, on); };

  let shown = null;                  // null (splash) | "home" | "editor" | "open" | "return"
  let pendingOpen = null;            // {path, rect, card, coverUrl, at} - a card this page clicked
  let importing = 0;                 // Import Catalog: ignore the `project new` editor switch until this time
  let discardOnReturn = false;
  let thumbsAtOpen = null;
  let pendingPath = null, pendingPathAt = 0;   // the project this page asked to open (for its cover)
  let firstThumbAsked = false;
  let coverOff = null;
  /** The native's first choice of loading cover is the recent card's cached cover
   *  (linux_main.cpp:582): the project's first image through the adapter's `cover`. */
  function coverFromRecents(path, name) {
    if (coverOff) { coverOff(); coverOff = null; }
    const rs = vm.recents.peek();
    const r = rs.find((x) => path && x.path === path) || (!path && name ? rs.find((x) => x.name === name) : null);
    if (!r || !r.firstImagePath) return;
    const sig = coverOf(vm, r.firstImagePath);
    coverOff = effect(() => { const u = sig.value; if (u) untracked(() => trans.setCover(u)); });
  }

  // the dialog host sits over Home and the editor (its scrim covers the whole window); the
  // transition's canvas goes above it - no dialog stays up through a transition
  mountDialogHost(el, vm, ctx);
  const trans = new Transition(el, {
    onPhase: () => {},
    editorVisible: (on) => setEditor(on),
    homeVisible: (on) => setHome(on),
    onReturnLoad: () => {
      // native rebuilds the launcher here (refreshHome); the web resets its list and, for a
      // discarded project, closes it now (spec §10)
      home.reset();
      if (discardOnReturn) { discardOnReturn = false; vm.closeProject().catch(() => {}); }
    },
    onDone: (kind) => {
      if (kind === "home") {
        home.setWordmarkHidden(false);
        shown = "home";
        setEditor(false); setHome(true);
        settle(vm.screen.peek());
      } else {
        shown = "editor";
        setHome(false); setEditor(true);
        settle(vm.screen.peek());
      }
    },
  });

  const home = mountHome(homeLayer, vm, ctx, {
    newProject: () => guardDirty(newProject),
    openProject: () => guardDirty(openProject),
    importCatalog: () => guardDirty(importCatalog),
    openRecent: (r, rect, card, coverUrl) => openRecent(r, rect, card, coverUrl),
    settings: () => { vm.view.dialog.value = { name: "settings" }; },
  });

  let splash = new Splash(el);

  // ---------------------------------------------------------------- showing a screen
  function fadeIn() {
    // App.cpp:1359 / 1391: mScreenFade 1 -> 0 over 220 ms EaseOutCubic
    fadeEl.classList.add("snap");
    fadeEl.style.opacity = "1";
    void fadeEl.offsetWidth;
    fadeEl.classList.remove("snap");
    fadeEl.style.opacity = "0";
  }
  function showHome() {
    shown = "home";
    home.setWordmarkHidden(false);
    setEditor(false); setHome(true);
    fadeIn();
  }
  function showEditor() {
    shown = "editor";
    setHome(false); setEditor(true);
    fadeIn();
  }
  function beginOpen({ name, total = 0, from = null, card = null, coverUrl = null, complete = false } = {}) {
    if (shown === "open" && trans.active && !trans.returning && performance.now() - trans.loadStart < 1500) {
      // the event and the model can both announce one opening; keep the one that started
      if (name) { trans.name = name; trans.card.name = name; }
      if (total) trans.setTotal(total);
      if (from && !trans.from && performance.now() - trans.loadStart < 200) {
        trans.from = from;                                         // the model came first: adopt the card
        trans.card = { ...trans.card, ...(card || {}), name: trans.name };
        if (coverUrl) trans.setCover(coverUrl);
      }
      return;
    }
    if (vm.view.dialog.peek()) vm.view.dialog.value = null;       // App.cpp:1422 closes the modal
    shown = "open";
    thumbsAtOpen = new Map(vm.thumbs.peek());
    firstThumbAsked = false;
    trans.open({ name, from, card, coverUrl, complete });
    const pp = pendingPath && performance.now() - pendingPathAt < 4000 ? pendingPath : null;
    pendingPath = null;
    if (!coverUrl) coverFromRecents(pp || vm.project.peek().path, name);
    if (total) trans.setTotal(total);
    seedFromModel();
  }
  function beginReturn() {
    if (shown !== "editor") return;
    if (vm.view.dialog.peek()) vm.view.dialog.value = null;       // App::showHome closes Confirm / Info
    shown = "return";
    home.setWordmarkHidden(true);
    trans.back();
  }

  /** The session's screen changed (or this page chose Home): move this page there. */
  function settle(s) {
    if (s === "connecting") return;
    if (splash) return;                                    // the splash hands over when it has gone
    if (shown === "open") {
      if (s === "home") { trans.cancel(); showHome(); }  // the project went away mid-load
      else if (s === "editor") trans.finish();
      return;
    }
    if (shown === "return") return;                      // it lands on Home, then settles again
    if (s === "loading") {
      const mine = pendingOpen && performance.now() - pendingOpen.at < 4000 ? pendingOpen : null;
      beginOpen({ name: stem(vm.project.peek().path) || (mine && mine.card && mine.card.name) || "", total: vm.load.peek().total,
                  from: mine && mine.rect, card: mine && mine.card, coverUrl: mine && mine.coverUrl });
      return;
    }
    if (s === "home") {
      if (shown === "editor") beginReturn();
      else if (shown !== "home") showHome();
      return;
    }
    if (s === "editor") {
      if (shown === "editor") return;
      if (shown === "home" && performance.now() < importing) return;   // Import Catalog: wait for the load
      showEditor();
    }
  }

  own(el, effect(() => {
    const s = vm.screen.value;
    untracked(() => {
      if (splash && s !== "connecting") {
        const sp = splash;
        sp.setStatus("Ready");
        sp.setProgress(1);
        sp.leave(() => {
          splash = null;
          const now = vm.screen.peek();
          if (now === "loading") settle(now);
          else if (now === "editor") showEditor();
          else showHome();
        });
        return;
      }
      settle(s);
    });
  }));

  // ---------------------------------------------------------------- the load's events (linux_main.cpp:582-685)
  const S = vm.session;
  // the service's load events (read only; vm.on when the view-model offers it)
  const onEvent = (name, fn) => vm.on(name, fn);
  const load = () => vm.load.peek();       // {active, done, total, started, status, stage, entryStage, fraction}
  function seedFromModel() {
    const l = load();
    if (l.total) trans.setTotal(l.total);
    if (l.total) trans.setInFlight(l.started, l.total);
    if (l.done && l.total) trans.setProgress(l.done, l.total);
    if (l.stage === "reading") trans.setStatus("Reading project\u2026");
    else if (l.stage === "saving") trans.setStatus("Saving project\u2026");
    else if (l.status) trans.setStatus("Loading  " + l.status);
  }
  const opening = () => shown === "open" && trans.active && !trans.returning;
  const offs = [
    onEvent("project.opening", (d) => {
      const now = performance.now();
      const mine = pendingOpen && now - pendingOpen.at < 4000 ? pendingOpen : null;
      pendingOpen = null;
      importing = 0;
      beginOpen({ name: (d && d.text) || "", total: (d && d.b) || 0, from: mine && mine.rect, card: mine && mine.card,
                  coverUrl: mine && mine.coverUrl });
    }),
    onEvent("load.stage", (d) => {
      if (!opening()) return;
      trans.setInFlight(load().started, load().total);
      if (d.text === "reading") trans.setStatus("Reading project…");
      else if (d.text === "saving") trans.setStatus("Saving project…");
    }),
    onEvent("entry.started", (d) => {
      if (!opening()) return;
      if (d.text) trans.setStatus("Loading  " + d.text);
      trans.setInFlight(d.b || 0, load().total);
    }),
    onEvent("entry.progress", (d) => {
      if (!opening()) return;
      const l = load();
      trans.setFraction(l.fraction);
      const stage = d.text || l.entryStage || "";
      if (stage && l.status) trans.setStatus(l.status + "  \u2014  " + stage);
    }),
    onEvent("load.progress", (d) => {
      if (!opening()) return;
      if (d.text) trans.setStatus("Loading  " + d.text);
      trans.setProgress(d.a || 0, d.b || 0);
      trans.setInFlight(load().started, d.b || 0);
    }),
    onEvent("entry.decoded", (d) => {
      if (!opening()) return;
      trans.setUsable();
      // no cover yet: the first decoded photo's filmstrip thumb (linux_main.cpp:587), asked
      // for explicitly - the session only fetches thumbs it has not seen for that node + slot
      if (!trans.coverReady && !firstThumbAsked) {
        firstThumbAsked = true;
        const slot = d.b;
        const ask = (tries) => {
          const n = vm.nodes.peek().find((x) => x.slot === slot && x.kind === "image");
          if (n) S.call("thumbs", { node: n.node }).catch(() => {});
          else if (tries > 0) setTimeout(() => ask(tries - 1), 80);
        };
        ask(10);
      }
    }),
    onEvent("load.finished", () => { if (opening()) trans.finish(); }),
  ];
  own(el, () => offs.forEach((f) => f()));
  // the model's fraction moves between events (a late client, or a throttled push)
  own(el, effect(() => {
    const l = vm.load.value;
    untracked(() => { if (opening() && l.total) { trans.setFraction(l.fraction); trans.setInFlight(l.started, l.total); } });
  }));
  // the loading cover when the card had none: the first photo's filmstrip thumb (linux_main.cpp:578-590)
  own(el, effect(() => {
    const th = vm.thumbs.value;
    untracked(() => {
      if (!opening() || trans.coverReady) return;
      for (const [node, t] of th) {
        if (thumbsAtOpen && thumbsAtOpen.get(node) === t) continue;       // a thumb of the previous project
        trans.setCover(t.url);
        break;
      }
    });
  }));

  // ---------------------------------------------------------------- Home's actions
  /** Leaving a project with unsaved edits asks first (App::requestHome's question) - here also
   *  when Home replaces a project this session still has open. */
  async function guardDirty(then) {
    const p = vm.project.peek();
    if (!p.dirty || !p.path && !vm.imageCount.peek()) return then();
    const i = await confirm(vm, { title: "Unsaved changes", message: "Save your changes to this project before leaving?",
      buttons: [{ label: "Cancel" }, { label: "Discard", destructive: true }, { label: "Save", primary: true }] });
    if (i === 1) return then();
    if (i === 2) { if (await saveProject()) return then(); }
  }
  async function saveProject() {
    const p = vm.project.peek();
    try {
      if (p.path) await vm.save();
      else {
        const path = await pick(vm, { mode: "save", title: "Save Project", name: "Untitled.cmp", kinds: ["project"] });
        if (!path) return false;
        await vm.saveAs(path);
      }
      return true;
    } catch { return false; }
  }
  async function newProject() {
    const path = await pick(vm, { mode: "save", title: "New Project", name: "Untitled.cmp", kinds: ["project"], ok: "Create" });
    if (!path) return;
    try { await vm.newProject(path); } catch { /* toasted */ }
  }
  async function openProject() {
    const path = await pick(vm, { mode: "open", title: "Open Project", kinds: ["project"], ok: "Open" });
    if (!path) return;
    pendingPath = path; pendingPathAt = performance.now();
    try { await vm.openProject(path); } catch { /* toasted */ }
  }
  async function importCatalog() {
    const images = await pick(vm, { mode: "images", title: "Import Catalog", kinds: ["image"], ok: "Import" });
    if (!images || !images.length) return;
    const path = await pick(vm, { mode: "save", title: "Save New Project", name: "Imported.cmp", kinds: ["project"], ok: "Create" });
    if (!path) return;
    importing = performance.now() + 5000;
    try {
      await vm.newProject(path);
      await vm.importPhotos(images);
    } catch { importing = 0; settle(vm.screen.peek()); }
  }
  function openRecent(r, rect, card, coverUrl) {
    const p = vm.project.peek();
    if (p.path && p.path === r.path && !load().active) {
      // the session still has it loaded: back to the editor on it, for the session, no reload
      beginOpen({ name: r.name, from: rect, card, coverUrl, complete: true });
      vm.backToEditor();
      return;
    }
    guardDirty(() => {
      pendingOpen = { path: r.path, rect, card, coverUrl: coverUrl || coverOf(vm, r.firstImagePath).peek(), at: performance.now() };
      pendingPath = r.path; pendingPathAt = performance.now();
      vm.openProject(r.path).catch(() => { pendingOpen = null; });
    });
  }

  // ---------------------------------------------------------------- the way back (App::requestHome)
  async function requestHome() {
    if (shown !== "editor") return;
    const p = vm.project.peek();
    if (!p.dirty) { vm.goHome(); return; }
    const i = await confirm(vm, { title: "Unsaved changes", message: "Save your changes to this project before leaving?",
      buttons: [{ label: "Cancel" }, { label: "Discard", destructive: true }, { label: "Save", primary: true }] });
    if (i === 1) { discardOnReturn = true; vm.goHome(); }
    else if (i === 2) { if (await saveProject()) vm.goHome(); }
  }

  // ---------------------------------------------------------------- input while a transition runs / on Home
  const keys = (e) => {
    if (modalOpen()) return;                                 // the top modal routes it (modal.js)
    if (splash || trans.active) { e.stopPropagation(); e.preventDefault(); return; }
    if (shown === "home" && !home.isSearch(e.target)) {
      e.stopPropagation();
      if (e.key === " " || e.key.startsWith("Arrow")) e.preventDefault();
    }
  };
  window.addEventListener("keydown", keys, true);
  own(el, () => window.removeEventListener("keydown", keys, true));

  Object.assign(ctx, {
    requestHome,
    pick: (opts) => pick(vm, opts),
    confirm: (opts) => confirm(vm, opts),
    screen: () => shown || "splash",
  });
  return { requestHome, el };
}
