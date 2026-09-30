/*
 * Cosmo by arstro — editor.js: the VIEW-MODEL of one page (R-NTWB-7).
 *
 * One per browser tab. It stands between the session's model (session.js - cosmo-cc on the
 * board, shared by every client of the Arstro Remote session) and this page's views, and it
 * holds the two things MVVM separates:
 *
 *   SHARED - derived from the model, identical in every client of the session: the project,
 *   the photos and their tree, the selection / edit target, every parameter, history, load,
 *   export, settings. A client never keeps its own copy of any of it; it asks the service to
 *   change it (a command line) and waits for the model to come back - to every client.
 *
 *   OWN - this page's view state, never sent anywhere: layout (desktop / touch), which group the
 *   filmstrip shows, the open tab and panels, Before / Split / After and the seam, zoom and pan,
 *   the rail, menus and dialogs, the white-balance pick mode, and a drag in flight (its value is
 *   drawn here at once and sent as `set` notifies; the model catches up - "a gesture in flight
 *   outranks the model", arstro.design.rule gotcha 9).
 *
 * Views read signals from here and call its intents; nothing else in the page talks to the
 * session. That one rule is what makes two clients of one session look different and agree.
 */
import { signal, computed, effect, batch, jsonEqual } from "../core/signal.js";
import { quotePath } from "../model/session.js";
import { num, setLine, parseParams } from "../model/params.js";
import { toEngine, toTrack } from "../model/controls.js";

export const ROOT = -1;          // "All Photos": nodes whose parent is -1 are its children
export const TABS = ["basic", "mask", "mixer", "grade", "xform"];
export const UI_SCALES = [75, 90, 100, 125, 150, 175, 200];    // AppSettings.cpp: the offered scales

function loadNumber(key, fallback) {
  try { const v = Number(localStorage.getItem(key)); return Number.isFinite(v) && v > 0 ? v : fallback; } catch { return fallback; }
}
function keepNumber(key, sig) {
  effect(() => { const v = sig.value; try { localStorage.setItem(key, String(v)); } catch { /* private mode */ } });
}

export function createViewModel(session, { toast } = {}) {
  const S = session;
  const m = S.model;

  // ------------------------------------------------------------------ SHARED (from the model)
  const ready = computed(() => !!m.value);
  const nodes = computed(() => (m.value ? m.value.nodes || [] : []), jsonEqual);
  const nodeById = computed(() => new Map(nodes.value.map((n) => [n.node, n])));
  /** Children of a group node (ROOT = the top level), in tree order. */
  const childrenOf = (group) => nodes.value.filter((n) => n.parent === group);
  const project = computed(() => {
    const v = m.value;
    return v ? { name: v.projectName || "", path: v.projectPath || "", dirty: !!v.dirty } : { name: "", path: "", dirty: false };
  }, jsonEqual);
  const selectedIds = computed(() => new Set(nodes.value.filter((n) => n.selected).map((n) => n.node)));
  const selectedNode = computed(() => (m.value ? m.value.selectedNode : -1));
  const editGroup = computed(() => (m.value ? m.value.editGroup : -1));
  const currentSlot = computed(() => (m.value ? m.value.currentSlot : -1));
  const imageCount = computed(() => (m.value ? m.value.imageCount : 0));
  /** The node whose params the panels show: the edit group, else the selected image. */
  const editTarget = computed(() => {
    const g = editGroup.value;
    if (g >= 0) return nodeById.value.get(g) || null;
    const sel = selectedNode.value;
    return sel >= 0 ? nodeById.value.get(sel) || null : null;
  });
  const params = computed(() => (m.value ? m.value.p : parseParams("")));
  /** The target's OWN params (differs from `params` only under a group offset - the stacked
   *  reach a slider draws in green). The model sends it only when it differs. */
  let ownText = null, ownParsed = null;
  const ownParams = computed(() => {
    const t = m.value && m.value.ownParams;
    if (!t) return params.value;
    if (t !== ownText) { ownText = t; ownParsed = parseParams(t); }
    return ownParsed;
  });
  const history = computed(() => {
    const v = m.value || {};
    return { canUndo: !!v.canUndo, canRedo: !!v.canRedo, label: v.historyLabel || "", nodes: v.historyNodes || 0,
             current: v.historyCurrent ?? -1, entries: v.history || null };
  }, jsonEqual);
  const load = computed(() => {
    const v = m.value || {};
    const o = S.opening.value;
    return { active: !!v.loadActive, done: v.loadDone || 0, total: v.loadTotal || 0, started: v.loadStarted || 0,
             status: v.loadStatus || "", stage: v.loadStage || "", entryStage: v.loadEntryStage || "",
             fraction: (v.loadPermille || 0) / 1000, name: v.projectName || (o && o.name) || "" };
  }, jsonEqual);
  const exporting = computed(() => {
    const v = m.value || {};
    return { active: !!v.exportActive, done: v.exportDone || 0, total: v.exportTotal || 0, failures: v.exportFailures || 0,
             name: v.exportName || "", outDir: v.exportOutDir || "" };
  }, jsonEqual);
  const settings = computed(() => {
    const v = m.value || {};
    return { previewEdge: v.settingsPreviewEdge, threads: v.settingsThreads, useGpu: !!v.settingsUseGpu,
             cpuPercent: v.settingsCpuPercent, uiScale: v.settingsUiScale || 100, touchUi: !!v.settingsTouchUi,
             gpuAvailable: !!v.gpuAvailable, gpuActive: !!v.gpuActive };
  }, jsonEqual);
  const recents = computed(() => (m.value ? m.value.recents || [] : []), jsonEqual);
  const presets = computed(() => (m.value ? m.value.presets || null : null), jsonEqual);
  const lastError = computed(() => (m.value ? m.value.lastError || "" : ""));
  /** Whether anything is the edit target (R-NTWB-3) - with none the panels freeze. */
  const hasEditTarget = computed(() => {
    const v = m.value;
    if (!v) return false;
    return typeof v.hasEditTarget === "boolean" ? v.hasEditTarget : v.editGroup >= 0 || v.currentSlot >= 0;
  });
  /** The selected photo's full-resolution size, {0,0} when none (R-CROP-1: aspect crops). */
  const source = computed(() => ({ w: (m.value && m.value.sourceWidth) || 0, h: (m.value && m.value.sourceHeight) || 0 }),
                          (a, b) => a.w === b.w && a.h === b.h);
  /** The newest `metadata` answer (R-INFO): which node, its name and the label/value rows. */
  const metadata = computed(() => {
    const v = m.value || {};
    return { node: v.metadataNode ?? -1, name: v.metadataName || "", rows: v.metadata || [] };
  }, jsonEqual);

  // ------------------------------------------------------------------ OWN (this page only)
  const view = {
    layout: signal("desktop"),                 // set by main.js from the window
    homeWanted: signal(false),                 // this client looks at Home while the session edits
    group: signal(ROOT),                       // which group the filmstrip / breadcrumb show
    tab: signal("basic"),
    compare: signal("after"),                  // before | split | after
    seam: signal(0.5),                         // split position, 0..1 of the photo
    zoom: signal({ z: 1, x: 0.5, y: 0.5 }),    // 1 = fit; x, y = the photo point at the centre
    railWanted: signal(true),
    wbArmed: signal(false),
    menu: signal(null),                        // open top-bar menu id
    contextMenu: signal(null),                 // {x, y, node}
    dialog: signal(null),                      // {name, ...props}
    maskIndex: signal(-1),                     // the mask the Mask tab edits
    // The desktop shell's UI scale (App's root transform, R-SCALE). In the window it is a
    // setting of the machine; here it belongs to THIS screen - a 4K desktop and a laptop on one
    // session each want their own - so it is kept per browser, not sent as `settings set`.
    uiScale: signal(loadNumber("cosmo.uiScale", 100)),
  };
  keepNumber("cosmo.uiScale", view.uiScale);
  /** Drafts of a drag in flight: field -> value shown here before the model catches up. */
  const drafts = signal(new Map());

  /** The screen THIS page shows. The session's screen decides loading / editor, but looking at
   *  Home is a view choice: one client may browse recents while another keeps editing. */
  const screen = computed(() => {
    const v = m.value;
    if (!v) return "connecting";
    // The session's screen, not `loadActive`: an `add` loads photos while the editor stays up.
    if (v.screen === "loading") return "loading";
    if (v.screen === "home" || !v.projectPath && !v.imageCount) return "home";
    return view.homeWanted.value ? "home" : "editor";
  });

  // Navigation stays valid: a group that disappeared (ungrouped, deleted, new project) falls
  // back to the top level.
  const group = computed(() => {
    const g = view.group.value;
    return g === ROOT || nodeById.value.has(g) ? g : ROOT;
  });
  const cells = computed(() => childrenOf(group.value));
  const breadcrumb = computed(() => {
    const chain = [];
    let g = group.value;
    while (g !== ROOT) {
      const n = nodeById.value.get(g);
      if (!n) break;
      chain.unshift(n);
      g = n.parent;
    }
    return chain;
  });

  // ------------------------------------------------------------------ intents -> commands
  function fail(e) {
    const text = e && e.message ? e.message : String(e);
    if (toast) toast(text, true);
    throw e;
  }
  /** One command, answered; a rejection is shown in the service's own words. */
  const cmd = (line) => S.command(line).catch(fail);
  const cmdQuiet = (line) => S.command(line).catch((e) => { if (toast) toast(e.message || String(e), true); });
  const note = (line) => S.notify(line);
  const q = quotePath;

  // A gesture (R-PREVIEW-1): while it is on, `set` renders the level that fits the latency
  // budget; off starts the refine walk. One per drag, not per notify.
  let gestureDepth = 0;
  function gestureOn() { if (gestureDepth++ === 0) note("gesture on"); }
  function gestureOff() { if (gestureDepth > 0 && --gestureDepth === 0) note("gesture off"); }

  /** Set engine fields. `live` = mid-drag: shown from the draft now, sent as a notify. The
   *  final value of a drag goes as a call, so a rejection is seen. */
  function setFields(fields, { live = false } = {}) {
    const line = setLine(fields);
    if (live) {
      const d = new Map(drafts.peek());
      for (const [k, v] of Object.entries(fields)) d.set(k, v);
      drafts.value = d;
      note(line);
      return Promise.resolve();
    }
    return cmd(line).finally(() => clearDrafts(Object.keys(fields)));
  }
  function clearDrafts(keys) {
    const d = new Map(drafts.peek());
    for (const k of keys) d.delete(k);
    drafts.value = d;
  }
  /** A scalar's value as this view shows it: the draft while dragging, else the model's. */
  function scalar(field, source = params) {
    const d = drafts.value;
    if (d.has(field)) return parseFloat(d.get(field));
    return source.value[field];
  }
  /** Slider helpers over a catalogue control: track position <-> engine value. */
  const track = (c, source = params) => toTrack(c, scalar(c.field, source));
  function setTrack(c, t, opts) { return setFields({ [c.field]: num(toEngine(c, t)) }, opts); }

  const intents = {
    cmd, cmdQuiet, note, gestureOn, gestureOff, setFields, clearDrafts, scalar, track, setTrack,
    // selection & tree (the model's selection is the session's: every client sees it)
    select: (node, mode) => cmd(`select ${node}${mode ? " " + mode : ""}`),
    selectNext: () => cmd("select next"),
    selectPrev: () => cmd("select prev"),
    /** Arrow keys: step through the cells of the group this client shows (App::stepSelection). */
    step(dir) {
      const list = cells.peek();
      if (!list.length) return;
      const sel = selectedIds.peek();
      let cur = list.findIndex((c) => sel.has(c.node));
      if (cur < 0) { const t = editTarget.peek(); cur = t ? list.findIndex((c) => c.node === t.node) : -1; }
      const next = cur < 0 ? (dir > 0 ? 0 : list.length - 1) : cur + dir;
      if (next < 0 || next >= list.length || next === cur) return;
      return cmd(`select ${list[next].node}`);
    },
    openGroup: (node) => { view.group.value = node; },           // navigation is view state
    bypass: (node, on) => cmd(`bypass ${node} ${on ? "on" : "off"}`),
    groupNew: (name) => cmd(name ? `group new "${name}"` : "group new"),
    ungroup: (node) => cmd(`group ungroup ${node}`),
    rename: (node, name) => cmd(`group rename ${node} "${name}"`),
    remove: (node) => cmd(node === undefined ? "delete" : `delete ${node}`),
    // history
    undo: () => cmdQuiet("undo"),
    redo: () => cmdQuiet("redo"),
    historyJump: (i) => cmd(`history jump ${i}`),
    // project
    newProject: (path) => { view.homeWanted.value = false; return cmd(`project new ${q(path)}`); },
    openProject: (path) => { view.homeWanted.value = false; return cmd(`project open ${q(path)}`); },
    save: () => cmd("project save"),
    saveAs: (path) => cmd(`project save ${q(path)}`),
    closeProject: () => cmd("project close"),
    goHome: () => { view.homeWanted.value = true; },
    backToEditor: () => { view.homeWanted.value = false; },
    importPhotos: (paths) => { view.homeWanted.value = false; return cmd(`import ${paths.map(q).join(" ")}`); },
    addPhotos: (paths) => cmd(`add ${paths.map(q).join(" ")}`),
    // presets, export, settings, metadata, white balance, masks
    applyPreset: (path) => cmd(`preset apply "${path}"`),
    savePreset: (name) => cmd(`preset save "${name}"`),
    exportTo: ({ outDir, format = "jpg", quality = 90, longEdge = 0 }) =>
      cmd(`export --outdir ${q(outDir)} --format ${format} --quality ${quality}${longEdge ? " --long-edge " + longEdge : ""}`),
    settingsSet: (fields) => cmd("settings set " + Object.entries(fields).map(([k, v]) => `${k}=${v}`).join(" ")),
    askMetadata: (node) => cmd(node === undefined ? "metadata" : `metadata ${node}`),   // answer: vm.metadata
    wbPick: (x, y) => { view.wbArmed.value = false; return cmd(`wb pick --x ${num(x)} --y ${num(y)}`); },
    maskAdd: (blob) => cmd(`set mask=${blob}`),
    maskSet: (i, fields, opts = {}) => {
      const line = `mask set ${i} ` + Object.entries(fields).map(([k, v]) => `${k}=${v}`).join(" ");
      if (opts.live) { note(line); return Promise.resolve(); }
      return cmd(line);
    },
    maskDelete: (i) => cmd(`mask delete ${i}`),
    browse: (path) => S.browse(path),
    /** The service's events (each with its formatEvent() line) - for views that react to a fact
     *  (a load starting, an export finishing) rather than to state. */
    on: (name, fn) => S.on(name, fn),
  };

  return {
    session: S, view, drafts,
    // shared
    ready, nodes, nodeById, childrenOf, project, selectedIds, selectedNode, editGroup, currentSlot, imageCount,
    editTarget, params, ownParams, history, load, exporting, settings, recents, presets, lastError,
    hasEditTarget, source, metadata,
    frame: S.frame, thumbs: S.thumbs, controls: S.controls, presence: S.presence, status: S.status,
    // own, derived
    screen, group, cells, breadcrumb,
    ...intents,
    batch,
  };
}
