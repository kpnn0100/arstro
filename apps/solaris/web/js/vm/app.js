/*
 * Solaris — vm/app.js: the VIEW-MODEL of one page (R-SVC-7, NTWB-11 MVVM) — what App.cpp and ProjectScreen keep
 * between the model and the widgets.
 *
 * MODEL (shared by every page of the one session — the manifest is `single: true`):
 *   vm.model            Computed<AppModel|null> the whole model JSON, `state print --json` (AppModelCodec.cpp) WITH what
 *                       moves live merged in (the adapter's `live` key, ≤ 25/s: meters, loudness, readings, the tempo
 *                       while playing … — else its `transport` key): what the native App binds every frame. Read it in
 *                       a computed with jsonEqual for what you draw, so a meter's push does not rebuild a list.
 *   vm.base             Signal<AppModel|null>   the model as last pushed on a revision (no live values merged)
 *   vm.transport        Signal<obj|null>        the adapter's `transport` key: what moves with no edit (≤ 20/s)
 *   vm.live             Signal<obj|null>        the adapter's `live` key as it came (NtwbAdapter::liveJson)
 *   vm.ready            Computed<bool>          the model has arrived
 *   vm.screen           Computed<"home"|"project">  AppModel::screen (App.cpp:957)
 *   vm.settings         Computed<SettingsModel> m.settings (jsonEqual)
 *   vm.position         Computed<number>        beats HEARD now (vm.model's transport.position — live while playing)
 *   vm.playing          Computed<bool>
 *   vm.status, vm.presence, vm.events, vm.lastRejection   the session's (model/session.js)
 *
 * INTENT — a command line, the same text the native widget sends (app/NOTES.md, docs/API.md):
 *   vm.cmd(line) -> Promise<{output, revision} | null>   App::dispatch: null when refused — the refusal is SAID first,
 *                    a toast with the service's own sentence (or "refused: <line>" when it gave none, App.cpp:440).
 *                    Never rejects (no unhandled rejection reaches the console).
 *   vm.modelAt(rev) -> Promise<AppModel>   the model once it caught up with a reply's revision (cmd's `revision`):
 *                    what the native reads right after dispatch, the web reads through this
 *   vm.note(line)    the line with no reply awaited — a drag's per-frame `set` (the release goes through cmd)
 *   vm.q(s)          App::quote, the one quoting of an argument; vm.beatText(b) Timeline::beatText
 *   vm.browse(path, kinds) -> Promise<{path, parent, entries}>   a folder of the machine (the adapter's `browse`)
 *   vm.on(name, fn) -> off   the service's events ({line, fields})
 *   vm.toast(text, err = true)   App::showToast — `err` = a refusal (red outline); false = a notice (the accent).
 *                    `text` may be stored words (say(...)): shown in the language drawn.
 *   vm.copy(text)    App::copy: the browser's clipboard, then "Copied <text>" (or "This window has no clipboard")
 *
 * WORDS (R-ACCESS-2, vm/words.js): vm.tr(en), vm.trc(ctx, en), vm.trn(n, one, other), vm.fillIn(tpl, ...a),
 *   vm.say(en, ...a), vm.sayn(n, one, other, ...a), vm.shown(stored), vm.english(stored), vm.upper(s), vm.N_(en);
 *   vm.lang: Signal<code> the language drawn; vm.langWant: Computed<code> the one the model's setting resolves to.
 *   (A widget draws `vm.tr("Play")` inside its effect — the effect re-runs when the language changes.)
 *
 * VIEW STATE — this page's, never sent (`vm.view.*`, all Signals unless marked LIVE = written by whoever eases it):
 *   uiScale        % the page is drawn at — per page, like Cosmo's (a phone and a 4K desktop on one session each want
 *                  their own; kept in this browser); App eases it (shell.js). Set it with vm.setUiScale(pct) — the
 *                  Settings sheet's UI-scale combo sets THIS (the machine's settings.uiScale is the window's own).
 *   drawnScale     LIVE  the scale drawn now (uiScale eased × the web's fit-down below 960×600, shell.js)
 *   contrast       LIVE  high contrast drawn now, 0 … 1 (the model's settings.highContrast, eased 260 ms)
 *   reducedMotion  Computed: the OS's "reduce motion" OR settings.reducedMotion (App.cpp:589-593)
 *   dockOpen, dockWant (px the user last dragged it to: 429 = space::u(132)), dockHeight LIVE (eased),
 *   browserOpen, browserWidth LIVE, sessionOpen, sessionAmount LIVE (0 lanes … 1 the grid), idsAmount LIVE (Show IDs,
 *                  eased once for every widget that draws ids)          — ProjectScreen's, see project/screen.js
 *   windows        [{key, kind, id, clip}] the open float windows, front LAST. kind: device (key dev:<dv>) | roll
 *                  (roll:<pt>, `clip` = the clip it was opened from) | automation (auto:<au>) | loudness | video
 *   frontRoll      the pattern of the piano roll in front of every window ("" none) — the window layer writes it
 *   rollKeys       the keys go to the front roll first (R-ROLL-8: a press in its window takes them; any other gives
 *                  them back — the shell watches presses; a roll opened takes them)
 *   selection      [clip ids] the clips selected on the lanes, in the song's order (R-CLIP-11) — the timeline writes
 *   selectedClip   the clip clicked last ('' none): Split / Quantize act on it — the timeline writes
 *   snapStep       the lanes' snap step in beats (0 at the deepest zoom: nothing snaps) — the timeline writes
 *   expectCopy     null | {instant} — "the next clips the model brings are copies just made: select them" (App's
 *                  Duplicate / Paste); the timeline reads it at its next model and sets it back to null
 *   clipboard      [clip ids] Copy Clip kept (R-CLIP-10, the window's own clipboard; nothing is sent)
 *   sheet          null | "settings" | "export" | "relink" | "conflicts"   the open sheet (shell mounts sheets/<name>.js)
 *   history        the history tree is open (shell mounts history/view.js)
 *   menuOpen       a context menu is open (ctx.menu sets it) or a song-bar menu (the song bar sets it from its strip's
 *                  onOpenChanged) — modal: the app keys wait
 *   interacting    a pointer is down anywhere (ProjectScreen::setInteracting: a gesture in flight outranks the model)
 *   focus          {key: Signal<string>, on: Signal<bool>} the keyboard's focus (R-ACCESS-1, shell.js walks vm.a11y)
 *
 * VIEW INTENTS: setUiScale(pct) · setDockOpen(b) · toggleDock() · dockResize(h) (the dock's top edge dragged: `h` =
 *   the height wanted, px from the screen's bottom — direct manipulation, no ease) · setBrowserOpen(b) ·
 *   setSessionOpen(b) · openDevice(dv) · openRoll(pt, ac?) · openAutomation(au) · openLoudness() · openVideo() ·
 *   closeWindow(key) · raiseWindow(key) · isWindowOpen(key) (reads the signal) · openSheet(name) · closeSheet() ·
 *   openSettings() (`devices list` first, App.cpp:539-543) · openRelink() (`project files --missing` first) ·
 *   openExport() · openConflicts() · openHistory() · closeHistory()
 *
 * FIXTURES: vm.nativeView — Signal<object | null>, the native App's view state of the shot a fixture page draws
 *   (vm/fixture.js lists its keys); a widget module reads its own part on mount. null in a real session.
 *
 * REGISTRIES that cross widgets without importing them:
 *   vm.dnd     the browser → lanes / sampler drag (vm/dnd.js)
 *   vm.a11y    the accessible list the keyboard walks (kit/a11y.js)
 *   vm.rolls   Map<pt, {action(id) -> bool, togglePlay() -> bool}> — a piano roll registers its keys (R-ROLL-8)
 *   vm.keymap  {actionFor(combo, group), hint(action)} over the model's keymap (kit/keys.js)
 */
import { signal, computed, effect, batch, jsonEqual } from "../core/signal.js";
import { q, beatText } from "../kit/format.js";
import { setAppReducedMotion, osReducedMotion } from "../kit/anim.js";
import { createA11y } from "../kit/a11y.js";
import { keyActionFor, keyHint } from "../kit/keys.js";
import * as W from "./words.js";
import { createDnd } from "./dnd.js";

export const UI_SCALES = [75, 90, 100, 125, 150, 175, 200];  // SettingsModel::uiScale's offered values (AppModel.h:295)
export const MIN_W = 960, MIN_H = 600;                       // App::minWidth / minHeight (App.h): the window's floor
export const DOCK_WANT = 429;                                // ProjectScreen mDockWant: space::u(132)

const snapScale = (v) => UI_SCALES.reduce((best, s) => (Math.abs(s - v) < Math.abs(best - v) ? s : best), 100);
function loadNumber(key, fallback) {
  try { const v = Number(localStorage.getItem(key)); return Number.isFinite(v) && v > 0 ? v : fallback; } catch { return fallback; }
}
function keepNumber(key, v) { try { localStorage.setItem(key, String(v)); } catch { /* a private window */ } }

/** The live values (NtwbAdapter::liveJson) laid over the model at their own paths; else the `transport` key's. */
export function mergeLive(base, live, transport) {
  if (!base) return base;
  const m = { ...base };
  if (live) {
    if (live.transport) m.transport = { ...(base.transport || {}), ...live.transport };
    const st = live.strips || {}, rd = live.readings || {};
    const devs = (ds) => (ds || []).map((d) => (rd[d.id] && d.readings
      ? { ...d, readings: d.readings.map((r, i) => (i < rd[d.id].length ? { ...r, value: rd[d.id][i] } : r)) } : d));
    m.strips = (base.strips || []).map((s) => {
      const o = st[s.id];
      const withDevs = (s.devices || []).some((d) => rd[d.id]);
      return o || withDevs ? { ...s, ...(o || {}), devices: withDevs ? devs(s.devices) : s.devices } : s;
    });
    if ((base.masterDevices || []).some((d) => rd[d.id])) m.masterDevices = devs(base.masterDevices);
    if (live.bindings) m.bindings = (base.bindings || []).map((b) => (b.address in live.bindings ? { ...b, live: live.bindings[b.address] } : b));
    if (live.loudness) m.loudness = { ...(base.loudness || {}), ...live.loudness };
    if (live.video) m.video = { ...(base.video || {}), ...live.video };
    if (live.audition) m.audition = { ...(base.audition || {}), ...live.audition };
    if (typeof live.midiMessages === "number") m.midiMessages = live.midiMessages;
  } else if (transport) {
    const t = transport;
    m.transport = { ...(base.transport || {}), playing: t.playing, position: t.position, loopFrom: t.loopFrom, loopTo: t.loopTo,
                    latencyMs: t.latencyMs, masterPeak: t.masterPeak };
    if (t.peaks) m.strips = (base.strips || []).map((s) => (t.peaks[s.id] ? { ...s, peak: t.peaks[s.id] } : s));
    m.audition = { ...(base.audition || {}), file: t.audition, playing: t.auditionPlaying, progress: t.auditionProgress };
  }
  return m;
}

export function createViewModel(session, { toast } = {}) {
  const S = session;
  const m = computed(() => mergeLive(S.model.value, S.live.value, S.transport.value));

  // ------------------------------------------------------------------ the model
  const ready = computed(() => !!m.value);
  const screen = computed(() => (m.value && m.value.screen === "project" ? "project" : "home"));
  const settings = computed(() => (m.value && m.value.settings) || {}, jsonEqual);
  const position = computed(() => (m.value && m.value.transport ? m.value.transport.position || 0 : 0));
  const playing = computed(() => !!(m.value && m.value.transport && m.value.transport.playing));
  const languages = computed(() => ((settings.value.languages || []).map((l) => l.code)), jsonEqual);
  const langWant = computed(() => W.resolve(settings.value.language || "auto", navigator.language || "", languages.value.length ? languages.value : ["en"]));

  // ------------------------------------------------------------------ view state (this page's)
  const view = {
    uiScale: signal(snapScale(loadNumber("solaris.uiScale", 100))),
    drawnScale: signal(1),
    contrast: signal(0),
    dockOpen: signal(true), dockWant: signal(DOCK_WANT), dockHeight: signal(0),
    browserOpen: signal(true), browserWidth: signal(0),
    sessionOpen: signal(false), sessionAmount: signal(0),
    idsAmount: signal(0),
    windows: signal([], jsonEqual),
    frontRoll: signal(""),
    rollKeys: signal(false),
    selection: signal([], jsonEqual),
    selectedClip: signal(""),
    snapStep: signal(0.25),
    expectCopy: signal(null),
    clipboard: signal([], jsonEqual),
    sheet: signal(null),
    history: signal(false),
    menuOpen: signal(false),
    interacting: signal(false),
    focus: { key: signal(""), on: signal(false) },
    /** The dock's last resize was a drag (no ease) — screen.js reads and clears it. */
    dockDragged: signal(0),
  };
  view.reducedMotion = computed(() => osReducedMotion() || !!settings.value.reducedMotion);
  effect(() => { setAppReducedMotion(!!settings.value.reducedMotion); });

  // ------------------------------------------------------------------ saying things
  let toaster = toast || null;
  const vm = {};
  function say(text, err = true) {
    if (toaster) toaster(text, err);
    else console.info("solaris:", W.shown(text));
  }

  // ------------------------------------------------------------------ intents
  /** App::dispatch: the line, answered; a refusal SAID (its own words), resolved null. */
  function cmd(line) {
    return S.command(line).then(
      (r) => r || { output: "", revision: 0 },
      (e) => {
        const why = e && e.message ? e.message : String(e || "");
        say(why ? why : W.say("refused: {1}", line), true);
        return null;
      });
  }

  function copy(text) {
    const done = () => say(W.say("Copied {1}", text), false);
    const none = () => say(W.say("This window has no clipboard"), true);
    const legacy = () => {
      try {
        const ta = document.createElement("textarea");
        ta.value = text; ta.setAttribute("readonly", ""); ta.style.cssText = "position:fixed;left:-9999px;top:0";
        document.body.append(ta); ta.select();
        const ok = document.execCommand("copy");
        ta.remove();
        if (ok) done(); else none();
      } catch { none(); }
    };
    if (navigator.clipboard && window.isSecureContext) navigator.clipboard.writeText(text).then(done, legacy);
    else legacy();
  }

  // the float windows (WindowLayer's keyed windows, FloatWindow.h)
  function openWindow(kind, id, extra = {}) {
    const key = kind === "device" ? "dev:" + id : kind === "roll" ? "roll:" + id : kind === "automation" ? "auto:" + id : kind;
    const list = view.windows.peek().filter((w) => w.key !== key);
    const prev = view.windows.peek().find((w) => w.key === key);
    list.push({ key, kind, id: id || "", clip: extra.clip !== undefined ? extra.clip : prev ? prev.clip || "" : "" });
    view.windows.value = list;
    return key;
  }

  /** The model once it has caught up with a reply's revision (a command's effect is in the NEXT `model` push, ≤ 40 ms
   *  later — the native reads its model right after dispatch; a port that does reads it through this). */
  function modelAt(revision) {
    return new Promise((resolve) => {
      const now = m.peek();
      if (!revision || (now && now.revision >= revision)) { resolve(now); return; }
      let stop = null;
      const t = setTimeout(() => { if (stop) stop(); resolve(m.peek()); }, 2000);
      stop = effect(() => {
        const x = m.value;
        if (x && x.revision >= revision) { clearTimeout(t); queueMicrotask(() => stop && stop()); resolve(x); }
      });
    });
  }

  Object.assign(vm, {
    session: S, model: m, base: S.model, transport: S.transport, live: S.live, status: S.status, presence: S.presence, events: S.events, lastRejection: S.lastRejection,
    ready, screen, settings, position, playing, view, batch,
    // intents
    cmd, modelAt, note: (line) => S.notify(line), q, beatText,
    browse: (path, kinds) => S.browse(path, kinds),
    on: (name, fn) => S.on(name, fn),
    toast: say, copy,
    /** The shell's toast (shell.js calls it once it is mounted). */
    setToaster(fn) { toaster = fn; },
    /** A native shot's view state when the page draws a fixture (vm/fixture.js); null in a real session. */
    nativeView: signal(null),
    // words
    lang: W.drawn, langWant, languages,
    tr: W.tr, trc: W.trc, trn: W.trn, fillIn: W.fillIn, say: W.say, sayn: W.sayn, shown: W.shown, english: W.english, upper: W.upper, N_: W.N_,
    // view intents
    setUiScale(pct) { const s = snapScale(pct); view.uiScale.value = s; keepNumber("solaris.uiScale", s); },
    setDockOpen(b) { view.dockOpen.value = !!b; },
    toggleDock() { view.dockOpen.value = !view.dockOpen.peek(); },
    /** MixerDock::onResize: the edge follows the pointer, and that is where it stays (ProjectScreen.cpp:92-99). */
    dockResize(h) {
      batch(() => {
        view.dockOpen.value = true;
        view.dockWant.value = Math.max(29.25 + 97.5, h); // MixerDock::kTabsH + 97.5
        view.dockDragged.value = view.dockDragged.peek() + 1;
      });
    },
    setBrowserOpen(b) { view.browserOpen.value = !!b; },
    setSessionOpen(b) { view.sessionOpen.value = !!b; },
    openDevice: (dv) => openWindow("device", dv),
    openRoll(pt, ac = "") { const k = openWindow("roll", pt, { clip: ac }); view.rollKeys.value = true; return k; }, // onRollOpened (D-25)
    openAutomation: (au) => openWindow("automation", au),
    openLoudness: () => openWindow("loudness", ""),
    openVideo: () => openWindow("video", ""),
    closeWindow(key) { view.windows.value = view.windows.peek().filter((w) => w.key !== key); },
    raiseWindow(key) {
      const list = view.windows.peek();
      const w = list.find((x) => x.key === key);
      if (w && list[list.length - 1] !== w) view.windows.value = list.filter((x) => x !== w).concat([w]);
    },
    isWindowOpen: (key) => view.windows.value.some((w) => w.key === key),
    openSheet(name) { view.sheet.value = name; },
    closeSheet() { view.sheet.value = null; },
    openSettings() { cmd("devices list"); view.sheet.value = "settings"; },
    openExport() { view.sheet.value = "export"; },
    openRelink() { cmd("project files --missing"); view.sheet.value = "relink"; },
    openConflicts() { view.sheet.value = "conflicts"; },
    openHistory() { if ((m.peek() && m.peek().history || []).length) view.history.value = true; },
    closeHistory() { view.history.value = false; },
    // registries
    a11y: createA11y(),
    rolls: new Map(),
    keymap: {
      actionFor: (combo, group) => keyActionFor(m.peek(), combo, group),
      hint: (action) => keyHint(m.peek(), action),
    },
  });
  vm.dnd = createDnd(vm);

  // a window whose object is gone closes — "a device removed by anyone takes its window with it" (WindowLayer::bind)
  effect(() => {
    const x = m.value;
    if (!x) return;
    const devs = new Set();
    for (const s of x.strips || []) for (const d of s.devices || []) devs.add(d.id);
    for (const d of x.masterDevices || []) devs.add(d.id);
    const pts = new Set((x.patterns || []).map((p) => p.id)), aus = new Set((x.automations || []).map((a) => a.id));
    const list = view.windows.peek();
    const keep = list.filter((w) => w.kind === "device" ? devs.has(w.id) : w.kind === "roll" ? pts.has(w.id) : w.kind === "automation" ? aus.has(w.id) : true);
    if (keep.length !== list.length) view.windows.value = keep;
  });

  return vm;
}
