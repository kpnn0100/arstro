/*
 * Interstellar — edit.js: this page's VIEW-MODEL (R-SVC-4; MVVM as arstro.ntwb.implement asks).
 *
 *   shared  = derived from the session's model (the project, its timeline, tracks and clips, the rack
 *             and the Grade target, the renders, the transport) - every page of the session shows
 *             the same;
 *   own     = `view.*` (the tab, the deck, the zoom, the console's history) and the monitor's
 *             picture - this page's alone, never sent. Two pages on two tabs show two pictures,
 *             exactly as the window's tab decides what its monitor shows (App.cpp);
 *   intents = one command line each, the same text the window's gestures send (app/NOTES.md), so a
 *             page can do nothing interstellar-cc cannot (arstro.rule §1).
 */
import { signal, computed, effect, jsonEqual } from "../core/signal.js";
import { token } from "../model/session.js";

/** Seconds as the grammar spells them: rounded to the microsecond, the fewest decimals. */
export function secs(t) {
  const v = Math.round(Math.max(0, t) * 1e6) / 1e6;
  return String(Number(v.toFixed(6)));
}

/** SMPTE-style timecode of a time at a frame rate: HH:MM:SS:FF. */
export function timecode(t, fps) {
  const f = Math.max(1, Math.round(fps || 24));
  const total = Math.max(0, Math.round((t || 0) * f));
  const ff = total % f, s = Math.floor(total / f);
  const p = (n) => String(n).padStart(2, "0");
  return `${p(Math.floor(s / 3600))}:${p(Math.floor(s / 60) % 60)}:${p(s % 60)}:${p(ff)}`;
}

export function createViewModel(session, { toast } = {}) {
  const m = session.model;
  const has = computed(() => !!m.value);

  // ── shared: what the model says, shaped for the views (recomputed only when it really changed) ──
  const screen = computed(() => (m.value && m.value.screen) || "home");
  const project = computed(() => {
    const x = m.value;
    if (!x || x.screen === "home") return null;
    return { name: x.projectName, path: x.projectPath, dirty: x.dirty, fps: x.fps, width: x.width, height: x.height,
             canUndo: x.canUndo, canRedo: x.canRedo, undo: x.undoLabel, redo: x.redoLabel };
  }, jsonEqual);
  const recents = computed(() => (m.value ? m.value.recents || [] : []), jsonEqual);
  const lastError = computed(() => (m.value ? m.value.lastError : ""));
  const fps = computed(() => (m.value && m.value.fps) || 24);

  const timelines = computed(() => (m.value ? (m.value.timelines || []).map((t) => ({ id: t.id, name: t.name, depth: t.depth })) : []), jsonEqual);
  const timeline = computed(() => {
    const x = m.value;
    if (!x) return null;
    const t = (x.timelines || []).find((tl) => tl.id === x.currentTimeline);
    return t ? { id: t.id, name: t.name } : null;
  }, jsonEqual);

  /** Tracks top-down as the window draws them: video tracks by order (highest on top), then audio. */
  const tracks = computed(() => {
    const x = m.value;
    if (!x) return [];
    const list = (x.tracks || []).map((t) => ({ id: t.id, name: t.name, audio: !!t.audio, order: t.order, mute: !!t.mute }));
    const video = list.filter((t) => !t.audio).sort((a, b) => b.order - a.order);
    const audio = list.filter((t) => t.audio).sort((a, b) => a.order - b.order);
    return video.concat(audio).map((t, i) => ({ ...t, row: i }));
  }, jsonEqual);

  const clips = computed(() => {
    const x = m.value;
    if (!x) return [];
    return (x.clips || []).map((c) => ({
      id: c.id, name: c.name, track: c.track, src: c.src, srcName: c.srcName, at: c.at, in: c.in, out: c.out,
      speed: c.speed, duration: c.duration, opacity: c.opacity, audio: !!c.audio, offline: !!c.offline, nested: !!c.nested,
    }));
  }, jsonEqual);
  const markers = computed(() => (m.value ? (m.value.markers || []).map((k) => ({ id: k.id, name: k.name, at: k.at, note: k.note })) : []), jsonEqual);
  const selectedClipId = computed(() => (m.value ? m.value.selectedClip || "" : ""));
  const selectedClip = computed(() => clips.value.find((c) => c.id === selectedClipId.value) || null, jsonEqual);
  const duration = computed(() => (m.value ? m.value.duration || 0 : 0));

  const rack = computed(() => {
    const x = m.value;
    if (!x) return [];
    return (x.rack || []).map((r, i) => ({
      index: i, node: r.node, bind: r.bindName, name: r.cosmoName || r.bindName, depth: r.depth || 0, group: !!r.group,
      bypass: !!r.bypass, pending: !!r.pending, failed: !!r.failed, offlineWhy: r.offlineWhy || "", video: !!r.video,
      media: r.media || "", selected: !!r.selected, usedBy: r.usedBy,
    }));
  }, jsonEqual);
  const gradeTarget = computed(() => {
    const x = m.value;
    if (!x || !x.hasGradeTarget || x.selectedRack < 0) return null;
    const r = (x.rack || [])[x.selectedRack];
    return r ? { node: r.node, bind: r.bindName, name: r.cosmoName || r.bindName, group: !!r.group } : null;
  }, jsonEqual);
  const gradeParams = computed(() => (m.value && m.value.gradeParams) || {}, jsonEqual);
  const renders = computed(() => (m.value ? (m.value.renders || []).map((r) => ({
    id: r.id, timeline: r.timeline, timelineName: r.timelineName, out: r.outPath, format: r.format,
    done: r.done, total: r.total, state: r.state, error: r.error })) : []), jsonEqual);

  // ── the transport: the model's at rest, the `transport` state while it moves ──
  const transport = computed(() => {
    const t = session.transport.value;
    const x = m.value;
    return {
      playing: t ? t.playing : !!(x && x.playing),
      playhead: t ? t.playhead : (x ? x.playhead : 0),
      duration: t ? t.duration : (x ? x.duration : 0),
      shuttle: t ? t.shuttle : 0,
      frameSeq: t ? t.frameSeq : (x ? x.frameSeq : 0),
      meterPeak: t ? t.meterPeak : [0, 0],
    };
  }, jsonEqual);

  // ── own: this page's view, never sent ──
  const view = {
    tab: signal("cut"),             // the window's three tabs over one monitor: cut | grade | deliver
    deck: signal("timeline"),       // the lower deck: timeline | console
    pxPerSec: signal(48),           // the timeline's zoom
    history: signal([]),            // the console's lines, newest last
    monitorEdge: signal(960),       // the long edge the monitor is drawn at (its size × the pixel ratio)
  };
  const addresses = signal([]);     // the core's address table (api --json): ranges and neutrals

  // ── intents: one command line each ──
  function run(line, { quiet = false } = {}) {
    return session.command(line).then(
      (r) => r,
      (e) => { if (!quiet && toast) toast(String(e && e.message ? e.message : e), true); throw e; });
  }
  const swallow = (p) => p.catch(() => {});
  const live = (line) => session.notify(line);

  const vm = {
    session, has, screen, project, recents, lastError, fps, timelines, timeline, tracks, clips, markers,
    selectedClipId, selectedClip, duration, rack, gradeTarget, gradeParams, renders, transport, view, addresses,
    run,
    // project
    open: (path) => swallow(run("project open " + token(path))),
    create: (path, fpsV, res) => swallow(run("project new " + token(path) + (fpsV ? " --fps " + fpsV : "") + (res ? " --res " + res : ""))),
    save: () => swallow(run("project save")),
    close: () => swallow(run("project close")),
    undo: () => swallow(run("undo")),
    redo: () => swallow(run("redo")),
    openTimeline: (id) => swallow(run("timeline open " + token(id))),
    // transport
    play: () => swallow(run(transport.peek().playing ? "pause" : "play")),
    scrub: (t) => live("playhead " + secs(t)),               // while the pointer moves: no reply awaited
    seek: (t) => swallow(run("playhead " + secs(t))),
    step: (frames) => swallow(run("playhead " + (frames >= 0 ? "+" : "-") + secs(Math.abs(frames) / fps.peek()))),
    nextCut: () => swallow(run("playhead next-cut")),
    prevCut: () => swallow(run("playhead prev-cut")),
    shuttle: (dir) => swallow(run("shuttle " + dir)),
    // cut
    selectClip: (id) => swallow(run("clip select" + (id ? " " + token(id) : ""))),
    split: () => { const c = selectedClip.peek(); if (c) swallow(run(`clip split ${token(c.id)} --at ${secs(transport.peek().playhead)}`)); },
    remove: (ripple) => { const c = selectedClip.peek(); if (c) swallow(run(`clip delete ${token(c.id)}${ripple ? " --ripple" : ""}`)); },
    setClip: (field, v, { drag = false } = {}) => {
      const c = selectedClip.peek();
      if (!c) return;
      const line = `set ${c.id}.${field}=${v}`;
      if (drag) live(line); else swallow(run(line));
    },
    renameClip: (name) => { const c = selectedClip.peek(); if (c && name) swallow(run(`rename ${token(c.id)} ${token(name)}`)); },
    // the whole source from its start (no --out: the rest of it), at the playhead, on the model's
    // target track (`edit target`) or else the top video track - as the window's source bin does
    place: (bind) => {
      const x = m.peek();
      const trk = (x && x.targetTrack) || (tracks.peek().find((t) => !t.audio) || {}).id;
      if (!trk) { if (toast) toast("Add a video track first (Timeline › + Video)", true); return; }
      swallow(run(`clip add --track ${token(trk)} --src ${token(bind)} --in 0 --at ${secs(transport.peek().playhead)}`));
    },
    addTrack: (kind) => swallow(run("track add --kind " + kind)),
    addMarker: () => swallow(run(`marker add m${markers.peek().length + 1} --at ${secs(transport.peek().playhead)}`)),
    // grade
    rackAdd: (paths) => swallow(run("rack add " + paths.map(token).join(" "))),
    rackSelect: (bind) => swallow(run("rack select " + token(bind))),   // the grammar names a node by its bind name
    setGrade: (address, v, { drag = false } = {}) => {
      const g = gradeTarget.peek();
      if (!g) return;
      const line = `set ${g.bind}.${address}=${Number(v.toFixed ? v.toFixed(4) : v)}`;
      if (drag) live(line); else swallow(run(line));
    },
    // deliver
    render: ({ out, format, tl }) => swallow(run(`render --timeline ${token(tl)} --out ${token(out)}` + (format ? " --format " + format : ""))),
    cancelRender: (id) => swallow(run("render cancel " + token(id))),
  };

  // the core's address table, once a session runs: the Grade sliders' ranges and neutrals
  session.bridge.onReady(() => session.api().then((a) => { addresses.value = a.addresses || []; }).catch(() => {}));

  // ── the monitor: this page asks for the picture its tab shows, one request in flight at a time ──
  // Cut: the timeline at the playhead. Grade: the target's reference frame. Deliver: the timeline.
  // A new request is due when what decides the picture changed (the model's frameSeq, the playhead,
  // the revision, the tab, the target, the size); while one is in flight the newest wish waits.
  const want = computed(() => {
    const x = m.value;
    if (!x || x.screen !== "edit") return null;
    const tab = view.tab.value;
    const g = gradeTarget.value;
    const source = tab === "grade" && g && !g.group ? g.bind : "";
    return { source, t: source ? -1 : transport.value.playhead, edge: view.monitorEdge.value,
             rev: x.revision, seq: transport.value.frameSeq };
  }, jsonEqual);
  const picture = { empty: signal(false), busy: false, again: null };
  function fetchPicture(w) {
    if (!w) return;
    if (picture.busy) { picture.again = w; return; }
    picture.busy = true;
    const p = { edge: w.edge };
    if (w.source) p.source = w.source; else p.t = w.t;
    session.requestFrame(p).then(
      (r) => { picture.empty.value = !!(r && r.empty); },
      () => {}).finally(() => {
        picture.busy = false;
        const next = picture.again;
        picture.again = null;
        if (next) fetchPicture(next);
      });
  }
  effect(() => fetchPicture(want.value));
  vm.pictureEmpty = picture.empty;

  return vm;
}
