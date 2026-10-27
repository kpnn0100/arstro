/*
 * Interstellar — vm/app.js: the VIEW-MODEL of one page (R-SVC-4; MVVM, NTWB-11).
 *
 * A port of what app/App.{h,cpp} keeps that is not one widget's: the model-derived values several
 * widgets read, the presentation state that crosses widgets (App.h "presentation state"), the
 * intents App.cpp turns into command lines, and App's monitor fetch (fetchFrame / fetchSource /
 * fetchViewer, App.cpp:1338-1500). The model is the session's (model/session.js) - shared by every
 * page; everything under `view` is THIS page's and is never sent.
 *
 * ── TYPES ─────────────────────────────────────────────────────────────────────────────────────────
 *   Signal<T>   core/signal.js: .value (tracked), .peek(); computed ones are read-only
 *   Model       the AppModel JSON (state print --json; field docs: apps/interstellar/docs/API.md)
 *   RackNode    Model.rack[i]          Clip  Model.clips[i]          Timeline  Model.timelines[i]
 *   Result      {ok: boolean, output?: string, revision?: number, error?: string}
 *
 * ── MODEL-DERIVED (read-only signals) ───────────────────────────────────────────────────────────────
 *   model: Signal<Model|null>          the whole model (null until the service published once)
 *   transport: Signal<object|null>     the `transport` state (playhead … meters), see session.js
 *   ready: Signal<boolean>             a model has arrived
 *   revision: Signal<number>
 *   screen: Signal<"home"|"loading"|"edit">     AppModel::screen ("home" before the first model)
 *   project: Signal<{name, path, dirty}>
 *   fps: Signal<number>                model.fps, 24 when 0 (App.cpp's `m.fps > 0 ? m.fps : 24.0`)
 *   playhead / playing / duration / frameSeq: Signal   the transport's while it is published, else the model's
 *   gradeTarget: Signal<RackNode|null> rack[selectedRack] (App::selectedBind's node)
 *   selectedBind: Signal<string>       its bind name, "" = none (App::selectedBind)
 *   selection: Signal<string[]>        bind names of every selected rack node (the multi-select highlight)
 *   currentTimeline: Signal<Timeline|null>
 *   sourceViewing: Signal<boolean>     Cut AND model.sourceView: the source viewer owns monitor + transport (R-EDT-1)
 *   settings: Signal<object>           model.settings
 *   recents: Signal<Recent[]>          newest first - App's stable sort on lastOpened; index i is openRecent(i)'s
 *   status / presence                  the bridge's ("running" …) / {session, clients}
 *
 * ── VIEW STATE (this page's; signals a widget may write where noted) ───────────────────────────────
 *   tab: Signal<0|1|2>                 GRADE | CUT | DELIVER; default GRADE (EditScreen.h mTab). Write via setTab(i).
 *   view.uiScale: Signal<number>       this page's UI scale %, one of UI_SCALES; kept per browser (localStorage
 *                                      "interstellar.uiScale"), like cosmo's view.uiScale - NOT `settings set`:
 *                                      a 4K desktop and a phone on one session each want their own
 *   view.dialog: Signal<{name,…}|null> the open dialog module (shell: js/dialogs/<name>.js) - "settings" for
 *                                      Engine Settings (openSettings)
 *   view.openingName: Signal<string>   the project the Loading screen names (LoadingView::setProjectName)
 *   view.monitorEdge: Signal<number>   WRITTEN BY edit/monitor.js: Monitor::wantedProxyEdge() (640…7680)
 *   view.scrubbing: Signal<boolean>    WRITTEN BY transport.js / cut/timeline.js while the playhead is dragged
 *                                      (App's `transport()->scrubbing() || timeline()->dragging()`)
 *   view.preview: Signal<{bind, t}|null>  WRITTEN BY grade/deck.js: the ref-frame slider's live preview
 *                                      (GradeDeck::onPreview, R-RACK-3); null = not previewing
 *   view.binDrag: Signal<BinDrag|null> WRITTEN BY cut/bin.js, READ BY cut/timeline.js (SourceBin::onDragSource):
 *                                      {bind, label, dur, x, y, phase: "move"|"drop"|"cancel"} - x, y in ROOT
 *                                      logical px; the timeline shows its ghost on "move", hit-tests and calls
 *                                      vm.dropSource on "drop", clears its ghost on "cancel"
 *   view.rackOpen: Signal<Set<string>> the rack tree's open groups (rackObj ids). Owned by grade/rack.js;
 *                                      grade/deck.js opens one with setRackOpen(ro, true) (GradeDeck::onNavigate)
 *   view.deckLevel: Signal<string>     the SOURCES strip's level ("" = all sources, else a group's rackObj).
 *                                      Owned by grade/deck.js; grade/rack.js sets it via openDeckGroup(ro)
 *                                      (RackTree::onOpenGroup -> GradeDeck::openGroup)
 *   view.deckView: Signal<"sources"|"stills"|"nodes">  the Grade deck's strip; grabStill() shows "stills"
 *   view.clipWarning: Signal<boolean>  WRITTEN BY the scopes (edit/scopes.js), READ BY the monitor
 *                                      (ScopePanel::onClipWarning -> Monitor::setClipWarning)
 *   view.lossless: Signal<boolean>     WRITTEN BY the scopes (edit/scopes.js) while they are shown: the monitor's
 *                                      picture AT REST (not playing, scrubbing or previewing) is then asked as a
 *                                      PNG (adapter `frame {format: "png"}`) - the exact 8-bit pixels the native's
 *                                      scopesOf(frame) reads (App::showScopes); a JPEG's would differ. Default off.
 *   view.fixture: Signal<object|null>  TESTS ONLY: on tools/fixture_host.mjs, the native shot's view.json
 *                                      ({tab, grade: {inspectorTab, scopeMode, deckView, deckLevel…}, cut: {pps,
 *                                      scroll…}, monitor: {zoom, edge}, deliver…}) - a widget may put its own
 *                                      view state there so its screenshot compares 1:1. null in a real session.
 *
 * ── THE MONITOR (vm.monitor: App's fetch, ported; read by edit/monitor.js, edit/scopes.js) ─────────
 *   monitor.state: Signal<"empty"|"loading"|"frame">        Monitor::State
 *   monitor.frame: Signal<{url, w, h, dissolve, source, t, seq, png}|null>  the picture to show; `dissolve` = a
 *                  content change at the same place (cross-dissolve 160 ms LINEAR), else a cut (playback); `png` = it is
 *                  the lossless one (view.lossless) - the scopes compute from it
 *   monitor.caption / monitor.timecode: Signal<string>      Monitor::setCaption / setTimecode
 *   monitor.subtitle: Signal<string>                        R-DLV-1: the caption under the playhead
 *   monitor.angles: Signal<{names, active, shown}>          R-EDT-5
 *   monitor.wipe: Signal<{on, vertical, at, label}>         R-CLR-5
 *   monitor.captureShown: Signal<boolean>                   Grade: the capture button on the caption
 *   monitor.transportSourceMode: Signal<boolean>            Transport::setSourceMode (Cut + a source viewed)
 *   monitor.refresh()                                       ask again (force), e.g. after a zoom
 *
 * ── INTENTS ─────────────────────────────────────────────────────────────────────────────────────────
 *   cmd(line, {sync?, quiet?}) -> Promise<Result>   App::dispatch: one command line, answered. It NEVER
 *          rejects: a refusal is SAID (the refusal chip over the monitor, EditScreen::showRefusal - its text
 *          "Refused: <line>" when the service gave none) and comes back as {ok: false, error}. `sync`:
 *          resolve only once the model has reached the command's revision (what App.cpp reads right after
 *          a dispatch); `quiet`: do not say a refusal (the caller says it differently)
 *   notify(line)                        a line with no reply: the per-frame `set` / `playhead` of a drag
 *   say(text, isError = false)          the chip (EditScreen::showRefusal / showNotice) - ctx.toast does the same
 *   afterRevision(rev) -> Promise       the model reached `rev` (or 2 s passed)
 *   setTab(i), setRackOpen(ro, open), openDeckGroup(ro)
 *   openProject(path) / newProject(path) / openRecent(i)      App::openProjectPicked / newProjectPicked / onOpenRecent
 *   footagePicked(paths) / saveAsPicked(path) / presetImportPicked(path) / stillExportPicked(path)
 *   frameSavePicked(path, bind)          `capture --out <p> [--source <bind>]`
 *   dropSource(src, track, at)           App::dropSource: `track add --kind video` first when track is ""
 *   freshMarkerName() -> string          App::freshMarkerName: m1, m2 … the first one free
 *   grabStill()                          App::grabStill (Colour › Grab Still, Ctrl+Alt+G)
 *   captureBind() -> string              App::captureBind: what the capture button captures ("" = the timeline)
 *   canCopyFrame() -> boolean            this page can put a picture on the clipboard (a secure context)
 *   copyFrame(bind) -> Promise<boolean>  Copy Frame: call it INSIDE the click (the clipboard wants the gesture)
 *   openSettings()                       Engine Settings… (view.dialog = {name: "settings"})
 *   api() -> Promise<object>             `api --json`, asked once: the address table (ranges, neutrals)
 *   provide(name, impl) -> dispose / provided(name) -> impl|null      what a widget lends the rest:
 *          "keyGraph"  {selectionList(): string, selectionCount(): number, now(): number, shown(): boolean}
 *                      cut/timeline.js (KeyGraph / KeyLane - the keys' Ctrl+C/V, the key menus)
 *          "timeline"  {resetView()}      cut/timeline.js   (Workspace › Reset Workspace)
 *          "monitor"   {resetZoom()}      edit/monitor.js   (same)
 *          "versions"  {isOpen(), close()} edit/topbar.js   (Esc closes the version dropdown)
 *          "plugins"   {select(id)}       grade/inspector.js (a new effect is selected)
 *          "prompt"    (opts) => Promise  edit/topbar.js's NamePrompt port, behind ctx.prompt
 *   session                             the model's proxy (pictures: session.thumb / still / peaks …)
 */
import { signal, computed, effect, untracked, jsonEqual } from "../core/signal.js";
import { q, seconds, timecode } from "../kit/cmd.js";

export const GRADE = 0, CUT = 1, DELIVER = 2;
export const TAB_NAMES = ["Grade", "Cut", "Deliver"];
export const UI_SCALES = [75, 90, 100, 125, 150, 175, 200];   // cosmo AppSettings::uiScales()
const DOT = "  ·  ";                                       // App.cpp's "  \xC2\xB7  "

function loadScale() {
  try { const v = Number(localStorage.getItem("interstellar.uiScale")); return UI_SCALES.includes(v) ? v : 100; } catch { return 100; }
}
const clipLen = (c) => (c.duration > 0 ? c.duration : (c.out - c.in) / Math.max(1e-6, c.speed || 1));   // App.cpp:831
const base = (p) => { const i = String(p).lastIndexOf("/"); return i < 0 ? String(p) : String(p).slice(i + 1); };

export function createViewModel(session) {
  const m = session.model;

  // ------------------------------------------------------------------ model-derived
  const ready = computed(() => !!m.value);
  const revision = computed(() => (m.value ? m.value.revision || 0 : 0));
  const screen = computed(() => (m.value && m.value.screen) || "home");
  const project = computed(() => {
    const v = m.value || {};
    return { name: v.projectName || "", path: v.projectPath || "", dirty: !!v.dirty };
  }, jsonEqual);
  const fps = computed(() => (m.value && m.value.fps > 0 ? m.value.fps : 24));
  const live = (key, fallback) => computed(() => {
    const t = session.transport.value;
    if (t && t[key] !== undefined) return t[key];
    return m.value && m.value[key] !== undefined ? m.value[key] : fallback;
  });
  const playhead = live("playhead", 0), playing = live("playing", false), duration = live("duration", 0), frameSeq = live("frameSeq", 0);
  const gradeTarget = computed(() => {
    const v = m.value;
    if (!v || v.selectedRack < 0 || !v.rack || v.selectedRack >= v.rack.length) return null;
    return v.rack[v.selectedRack];
  }, jsonEqual);
  const selectedBind = computed(() => (gradeTarget.value ? gradeTarget.value.bindName || "" : ""));
  const selection = computed(() => ((m.value && m.value.rack) || []).filter((r) => r.selected && r.bindName).map((r) => r.bindName), jsonEqual);
  const currentTimeline = computed(() => {
    const v = m.value;
    return v ? (v.timelines || []).find((t) => t.id === v.currentTimeline) || null : null;
  }, jsonEqual);
  const settings = computed(() => (m.value && m.value.settings) || {}, jsonEqual);
  const recents = computed(() => {
    const list = ((m.value && m.value.recents) || []).slice();
    return list.sort((a, b) => (b.lastOpened || 0) - (a.lastOpened || 0));   // stable: App.cpp:40
  }, jsonEqual);

  // ------------------------------------------------------------------ view state
  const tab = signal(GRADE);
  const view = {
    uiScale: signal(loadScale()),
    dialog: signal(null),
    openingName: signal(""),
    monitorEdge: signal(960),
    scrubbing: signal(false),
    preview: signal(null),
    binDrag: signal(null),
    rackOpen: signal(new Set()),
    deckLevel: signal(""),
    deckView: signal("sources"),
    clipWarning: signal(false),
    fixture: signal(null),
    lossless: signal(false),
  };
  effect(() => { const v = view.uiScale.value; try { localStorage.setItem("interstellar.uiScale", String(v)); } catch { /* private mode */ } });
  const sourceViewing = computed(() => tab.value === CUT && !!(m.value && m.value.sourceView));

  // ------------------------------------------------------------------ providers (what a widget lends)
  const providers = signal(new Map());
  function provide(name, impl) {
    const next = new Map(providers.peek());
    next.set(name, impl);
    providers.value = next;
    return () => {
      if (providers.peek().get(name) !== impl) return;
      const n2 = new Map(providers.peek());
      n2.delete(name);
      providers.value = n2;
    };
  }
  const provided = (name) => providers.value.get(name) || null;

  // ------------------------------------------------------------------ saying things, commands
  let sink = null;                       // the shell's chip / toast (shell.js sets it)
  function say(text, isError = false) {
    if (sink) sink(String(text), !!isError);
    else (isError ? console.warn : console.info)("interstellar:", text);
  }
  function afterRevision(rev) {
    return new Promise((resolve) => {
      if (!rev || revision.peek() >= rev) { resolve(); return; }
      let dispose = null;
      const done = () => { clearTimeout(timer); if (dispose) dispose(); resolve(); };
      const timer = setTimeout(done, 2000);
      dispose = effect(() => { if (revision.value >= rev) queueMicrotask(done); });
    });
  }
  async function cmd(line, { sync = false, quiet = false } = {}) {
    try {
      const r = (await session.command(line)) || {};
      if (sync) await afterRevision(r.revision);
      return { ok: true, output: r.output || "", revision: r.revision || 0 };
    } catch (e) {
      const text = (e && e.message) || String(e || "");
      const shown = text && text !== "failed" ? text : "Refused: " + line;   // App::dispatch (App.cpp:504)
      if (!quiet) say(shown, true);
      return { ok: false, error: shown };
    }
  }
  const notify = (line) => session.notify(line);

  // ------------------------------------------------------------------ intents (App.cpp, each a line)
  function setTab(i) {
    if (i < 0 || i > 2 || i === tab.peek()) return;
    tab.value = i;
  }
  function setRackOpen(ro, open) {
    const s = new Set(view.rackOpen.peek());
    if (open) s.add(ro); else s.delete(ro);
    view.rackOpen.value = s;
  }
  function openDeckGroup(ro) { view.deckLevel.value = ro || ""; view.deckView.value = "sources"; }

  const openProject = (path) => { if (!path) return Promise.resolve({ ok: false }); view.openingName.value = base(path); return cmd("project open " + q(path)); };
  const newProject = (path) => { if (!path) return Promise.resolve({ ok: false }); view.openingName.value = base(path); return cmd("project new " + q(path)); };
  function openRecent(i) {
    const rec = recents.peek();
    if (i < 0 || i >= rec.length) return Promise.resolve({ ok: false });
    view.openingName.value = rec[i].name;           // the loading screen names what is opening
    return cmd("project open " + q(rec[i].path));
  }
  const footagePicked = (paths) => (paths && paths.length ? cmd("rack add " + paths.map(q).join(" ")) : Promise.resolve({ ok: false }));
  const saveAsPicked = (path) => (path ? cmd("project save " + q(path)) : Promise.resolve({ ok: false }));
  const presetImportPicked = (path) => (path ? cmd("preset import " + q(path)) : Promise.resolve({ ok: false }));
  function stillExportPicked(path) {
    const v = m.peek();
    if (!path || !v || !v.currentTimeline) return Promise.resolve({ ok: false });
    return cmd(`export-still --timeline ${q(v.currentTimeline)} --out ${q(path)} --at ${seconds(playhead.peek(), v.fps > 0 ? v.fps : 24)}`);
  }
  const frameSavePicked = (path, bind) => (path ? cmd("capture --out " + q(path) + (bind ? " --source " + q(bind) : "")) : Promise.resolve({ ok: false }));

  /** App::dropSource (App.cpp:836-852): the rest of the source at `at`; with no track, a video track first. */
  async function dropSource(src, track, at) {
    const m0 = m.peek() || {};
    const f = m0.fps > 0 ? m0.fps : 24;
    let trk = track;
    if (!trk) {
      const before = new Set((m0.tracks || []).map((t) => t.id));
      const r = await cmd("track add --kind video", { sync: true });
      if (!r.ok) return r;
      const t = ((m.peek() || {}).tracks || []).find((x) => !x.audio && !before.has(x.id));
      if (!t) return { ok: false };
      trk = t.id;
    }
    return cmd(`clip add --track ${q(trk)} --src ${q(src)} --in 0 --at ${seconds(at, f)}`);
  }
  function freshMarkerName() {
    const taken = new Set(((m.peek() || {}).markers || []).map((k) => k.name));
    for (let k = 1; ; k++) if (!taken.has("m" + k)) return "m" + k;
  }
  /** App::grabStill (App.cpp:952-958). */
  function grabStill() {
    const b = selectedBind.peek();
    const p = tab.peek() === GRADE && b ? cmd("still grab " + q(b)) : cmd("still grab");
    view.deckView.value = "stills";                 // the gallery comes forward to show what was kept
    return p;
  }
  /** App::captureBind (App.cpp:548-555). */
  function captureBind() {
    const n = gradeTarget.peek();
    if (tab.peek() !== GRADE || !n) return "";
    return n.group || n.failed ? "" : n.bindName;
  }
  const canCopyFrame = () => !!(window.isSecureContext && navigator.clipboard && navigator.clipboard.write && typeof ClipboardItem === "function");
  /** Copy Frame (App::openCaptureMenu, R-UI-11): the adapter's full-resolution PNG onto THIS page's
   *  clipboard. Called synchronously from the click so the clipboard sees the user's gesture. */
  function copyFrame(bind) {
    if (!canCopyFrame()) { say("Copying a picture needs a secure page (https or localhost)", true); return Promise.resolve(false); }
    const blob = session.capture(bind || "");
    blob.catch(() => {});
    let write;
    try { write = navigator.clipboard.write([new ClipboardItem({ "image/png": blob })]); }
    catch (e) { write = Promise.reject(e); }
    return write.then(
      () => { say("Frame copied to the clipboard", false); return true; },
      async (e) => {
        const why = await blob.then(() => null, (err) => (err && err.message) || String(err));
        say(why || "Could not copy the frame", true);
        return false;
      });
  }
  const openSettings = () => { view.dialog.value = { name: "settings" }; };
  let apiDoc = null;
  function api() {
    if (!apiDoc) {
      apiDoc = cmd("api --json", { quiet: true }).then((r) => {
        if (!r.ok) throw new Error(r.error);
        return JSON.parse(r.output);
      });
      apiDoc.catch(() => { apiDoc = null; });
    }
    return apiDoc;
  }

  // ------------------------------------------------------------------ the monitor (App.cpp:1338-1500)
  const monitor = {
    state: signal("empty"),
    frame: signal(null),
    caption: signal(""),
    timecode: signal("00:00:00:00"),
    subtitle: computed(() => {
      const v = m.value;
      if (!v || !v.captionsShown || v.sourceView || v.matteView) return "";
      const t = playhead.value;
      return (v.captions || []).filter((k) => t >= k.at - 1e-6 && t < k.at + k.dur - 1e-6).map((k) => k.text).join("\n");
    }),
    angles: computed(() => {
      const v = m.value || {};
      return { names: v.multicamAngles || [], active: v.multicamAngle || 0, shown: tab.value === CUT && !v.sourceView && !!v.multicamClip };
    }, jsonEqual),
    wipe: computed(() => {
      const v = m.value || {};
      return { on: !!v.wipeRef, vertical: v.wipeVertical !== false, at: v.wipeAt ?? 0.5, label: v.wipeLabel || "" };
    }, jsonEqual),
    captureShown: computed(() => tab.value === GRADE),
    transportSourceMode: sourceViewing,
    refresh: () => { fetched = null; pump(); },
  };

  /** What the monitor should show now, as a request (or a state with none) - App::fetchFrame's choice. */
  const want = computed(() => {
    const v = m.value;
    if (!v || v.screen !== "edit") return null;
    const edge = view.monitorEdge.value;
    const t = tab.value;
    const n = gradeTarget.value;
    // Grade: the target ALONE, graded, at its reference frame or the slider's preview (R-UI-3, R-RACK-3)
    if (t === GRADE && n && !n.group && !n.failed && n.bindName) {
      const pv = view.preview.value;
      const previewing = !!pv && pv.t >= 0 && pv.bind === n.bindName;
      const at = previewing ? pv.t : n.frame;
      const f = n.mediaFps > 0 ? n.mediaFps : v.fps;
      let cap = n.bindName;
      if (n.video) cap += DOT + (previewing ? "seek " : "ref ") + timecode(at, f);
      if (v.sourceWidth > 0 && v.sourceHeight > 0) cap += DOT + v.sourceWidth + "×" + v.sourceHeight;
      if (v.useProxies && n.proxy) cap += DOT + "proxy";
      if (v.matteView) cap += DOT + "matte";
      for (const j of v.trackJobs || [])
        if ((j.state === "queued" || j.state === "running") && j.total > 0) cap += DOT + "tracking " + Math.floor((100 * j.done) / j.total) + "%";
      return { kind: "source", source: n.bindName, t: previewing ? at : -1, at, edge, previewing, caption: cap, tc: timecode(at, f),
               seq: v.frameSeq, rev: v.revision, png: view.lossless.value && !previewing };
    }
    // Cut with a source in the viewer (R-EDT-1)
    if (t === CUT && v.sourceView) {
      let f = v.fps;
      for (const r of v.rack || []) if (r.bindName === v.sourceView && r.mediaFps > 0) f = r.mediaFps;
      const at = session.transport.value && session.transport.value.sourceView === v.sourceView ? session.transport.value.sourcePlayhead : v.sourcePlayhead;
      let cap = "SOURCE" + DOT + v.sourceView;
      if (v.sourceIn >= 0 || v.sourceOut >= 0)
        cap += DOT + (v.sourceIn >= 0 ? timecode(v.sourceIn, f) : "—") + " – " + (v.sourceOut >= 0 ? timecode(v.sourceOut, f) : "—");
      for (const r of v.rack || []) if (v.useProxies && r.bindName === v.sourceView && r.proxy) cap += DOT + "proxy";
      return { kind: "viewer", source: v.sourceView, t: at, at, edge, caption: cap, tc: timecode(at, f), seq: v.frameSeq, rev: v.revision };
    }
    // the timeline at the playhead
    const ph = playhead.value;
    let cap = "";
    for (const tl of v.timelines || []) if (tl.id === v.currentTimeline) cap = tl.name || tl.id;
    if (v.width > 0 && v.height > 0) cap += (cap ? DOT : "") + v.width + "×" + v.height;
    if (playing.value && v.playbackEdge > 0) cap += DOT + "▶ " + (v.playbackFromCache ? "cached" : v.playbackEdge + " px");
    if (v.useProxies) cap += DOT + "proxies";
    const clipHere = (v.clips || []).some((c) => !c.audio && ph >= c.at - 1e-9 && ph < c.at + clipLen(c));
    // nothing cut here: the service shows the Grade target's reference frame - empty only without one
    if (!clipHere && !v.hasGradeTarget) return { kind: "empty", caption: cap, tc: timecode(ph, v.fps) };
    return { kind: "timeline", source: "", t: ph, at: ph, edge, caption: cap, tc: timecode(ph, v.fps), timeline: v.currentTimeline,
             seq: frameSeq.value, rev: v.revision, playing: playing.value,
             png: view.lossless.value && !playing.value && !view.scrubbing.value };
  }, jsonEqual);

  let fetched = null;          // the request the picture on screen answers
  let inFlight = false;
  effect(() => {
    const w = want.value;
    untracked(() => {
      if (!w) return;
      monitor.caption.value = w.caption;
      monitor.timecode.value = w.tc;
      if (w.kind === "empty") { monitor.state.value = "empty"; fetched = null; return; }
      pump();
    });
  });
  function pump() {
    const w = want.peek();
    if (inFlight || !w || w.kind === "empty") return;
    const f = fetched;
    const same = f && f.kind === w.kind && f.source === w.source && f.t === w.t && f.edge === w.edge && f.seq === w.seq && f.rev === w.rev &&
                 f.timeline === w.timeline && !!f.png === !!w.png;
    if (same) return;
    const samePlace = !!f && f.kind === w.kind && f.source === w.source && Math.abs(f.at - w.at) <= 1e-9;
    inFlight = true;
    const params = { edge: w.edge };
    if (w.png) params.format = "png";
    if (w.source) params.source = w.source;
    params.t = w.t;
    session.requestFrame(params).then((r) => {
      fetched = w;
      if (!r || r.empty || !r.url) {
        // a clip is there; its pixels are not (yet) - a source keeps its picture at the same place
        if (w.kind === "timeline" || !samePlace || monitor.state.peek() !== "frame") monitor.state.value = "loading";
        return;
      }
      // a new picture at the SAME place is a content change (a grade, a version) and dissolves; a new
      // time is the video moving, its own animation (gotcha 10)
      const dissolve = w.kind === "timeline" ? samePlace && !w.playing && !view.scrubbing.peek()
                     : w.kind === "source" ? samePlace && !w.previewing : samePlace;
      const old = monitor.frame.peek();
      monitor.frame.value = { url: r.url, w: r.w, h: r.h, dissolve, source: w.source, t: w.at, seq: r.meta && r.meta.seq, png: !!w.png };
      monitor.state.value = "frame";
      if (old) session.release(old.url);
    }, () => { fetched = w; }).finally(() => { inFlight = false; pump(); });
  }

  return {
    session, model: m, transport: session.transport, status: session.status, presence: session.presence,
    ready, revision, screen, project, fps, playhead, playing, duration, frameSeq, gradeTarget, selectedBind, selection,
    currentTimeline, sourceViewing, settings, recents,
    tab, view, monitor,
    provide, provided,
    cmd, notify, say, afterRevision, setTab, setRackOpen, openDeckGroup,
    openProject, newProject, openRecent, footagePicked, saveAsPicked, presetImportPicked, stillExportPicked, frameSavePicked,
    dropSource, freshMarkerName, grabStill, captureBind, canCopyFrame, copyFrame, openSettings, api,
    /** The shell's chip / toast: (text, isError) => void. Set once by shell.js. */
    _setSink(fn) { sink = fn; },
  };
}
