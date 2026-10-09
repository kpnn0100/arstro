/*
 * Solaris — song.js: this page's VIEW-MODEL (R-SVC-7; MVVM as arstro.ntwb.implement asks).
 *
 *   shared  = derived from the session's model (the song, its lanes and clips, its mixers and
 *             strips, the transport) - every page of the session shows the same;
 *   own     = `view.*` (zoom, the tab, the console's history) - this page's alone, never sent;
 *   intents = one command line each, the same text the window's gestures send (app/NOTES.md),
 *             so a page can do nothing the CLI cannot (arstro.rule §1).
 */
import { signal, computed, jsonEqual } from "../core/signal.js";
import { token } from "../model/session.js";

/** A strip's or lane's colour index -> the track colour token (Theme.h surface::track). */
export function trackVar(i) {
  const n = Number.isFinite(i) ? ((i % 10) + 10) % 10 : 0;
  return `var(--track-${n})`;
}

/** Beats as the grammar spells them: rounded to the tick (1/960), the fewest decimals (Timeline::beatText). */
export function beats(b) {
  const t = Math.round(b * 960) / 960;
  return String(Number(t.toFixed(4)));
}

export function createViewModel(session, { toast } = {}) {
  const m = session.model;
  const has = computed(() => !!m.value);

  // ── shared: what the model says, shaped for the views (recomputed only when it really changed) ──
  const screen = computed(() => (m.value && m.value.screen) || "home");
  const song = computed(() => {
    const x = m.value;
    if (!x || x.screen !== "project") return null;
    return { name: x.projectName, path: x.projectPath, dirty: x.dirty, bpm: x.bpm, sig: x.sig,
             undo: x.undoLabel, redo: x.redoLabel, lengthBeats: x.lengthBeats, masterGain: x.masterGain };
  }, jsonEqual);
  const beatsPerBar = computed(() => {
    const s = song.value;
    const n = s ? parseInt(String(s.sig).split("/")[0], 10) : 4;
    return n > 0 ? n : 4;
  });
  const recents = computed(() => (m.value ? m.value.recents || [] : []), jsonEqual);
  const lastError = computed(() => (m.value ? m.value.lastError : ""));

  const stripsById = computed(() => {
    const map = new Map();
    for (const s of (m.value && m.value.strips) || []) map.set(s.id, s);
    return map;
  });

  /** The rows of the lanes, by the window's rule (Timeline.cpp `bind`): every lane in order, then
   *  one row per strip whose clips are on no lane. A lane with no colour wears its first clip's strip's. */
  const rows = computed(() => {
    const x = m.value;
    if (!x) return [];
    const out = [];
    const rowOfLane = new Map(), rowOfStrip = new Map();
    for (const l of x.lanes || []) {
      rowOfLane.set(l.id, out.length);
      out.push({ key: l.id, lane: l.id, name: l.name, colour: l.colour });
    }
    for (const c of x.clips || []) {
      if (c.lane === "" && !rowOfStrip.has(c.track)) {
        const st = (x.strips || []).find((s) => s.id === c.track);
        rowOfStrip.set(c.track, out.length);
        out.push({ key: "strip:" + c.track, lane: "", name: st ? st.name : c.track, colour: st ? st.colour : 0 });
      }
    }
    for (const r of out) {
      if (r.colour >= 0) continue;
      const first = (x.clips || []).find((c) => c.lane === r.lane);
      const st = first && (x.strips || []).find((s) => s.id === first.track);
      r.colour = st ? st.colour : 0;
    }
    return out.map((r, i) => ({ ...r, index: i }));
  }, (a, b) => jsonEqual(a.map((r) => [r.key, r.name, r.colour]), b.map((r) => [r.key, r.name, r.colour])));

  const clips = computed(() => {
    const x = m.value;
    if (!x) return [];
    const rowOfLane = new Map(), rowOfStrip = new Map();
    let i = 0;
    for (const l of x.lanes || []) rowOfLane.set(l.id, i++);
    for (const c of x.clips || []) if (c.lane === "" && !rowOfStrip.has(c.track)) rowOfStrip.set(c.track, i++);
    const patterns = new Map((x.patterns || []).map((p) => [p.id, p]));
    const colourOf = new Map((x.strips || []).map((s) => [s.id, s.colour]));
    return (x.clips || []).map((c) => {
      const p = c.pattern ? patterns.get(c.pattern) : null;
      return {
        id: c.id, name: c.name, kind: c.kind, at: c.at, length: c.length, linked: c.linked, offline: c.offline,
        row: c.lane === "" ? rowOfStrip.get(c.track) : (rowOfLane.has(c.lane) ? rowOfLane.get(c.lane) : 0),
        colour: colourOf.has(c.track) ? colourOf.get(c.track) : 0,
        notes: p ? p.notes : null, patternLength: p ? p.length : 0,
      };
    });
  }, jsonEqual);

  /** The mixers as pages, each with its strips in order (the model lists strips in PROCESSING order). */
  const mixers = computed(() => {
    const x = m.value;
    if (!x) return [];
    return (x.mixers || []).map((mx) => ({
      id: mx.id, name: mx.name,
      strips: (x.strips || []).filter((s) => s.mixer === mx.id).sort((a, b) => a.order - b.order)
        .map((s) => ({ id: s.id, name: s.name, kind: s.kind, gain: s.gain, pan: s.pan, mute: s.mute, solo: s.solo,
                       audible: s.audible, colour: s.colour, out: s.out, gainFormula: s.gainFormula, panFormula: s.panFormula })),
    }));
  }, jsonEqual);

  // ── the transport: the model's at rest, the `transport` state while it moves ──
  const transport = computed(() => {
    const t = session.transport.value;
    const mt = m.value ? m.value.transport : null;
    return {
      playing: t ? t.playing : !!(mt && mt.playing),
      position: t ? t.position : (mt ? mt.position : 0),
      loopFrom: mt ? mt.loopFrom : 0, loopTo: mt ? mt.loopTo : 0,
      masterPeak: t ? t.masterPeak : (mt ? mt.masterPeak : [0, 0]),
      peaks: t ? t.peaks : {},
    };
  }, jsonEqual);

  // ── own: this page's view, never sent ──
  const view = {
    pxPerBeat: signal(28),          // the window's default zoom (Timeline: 28 px/beat)
    tab: signal("mixer"),           // the lower deck: mixer | console
    history: signal([]),            // the console's lines, newest last
  };

  // ── intents: one command line each ──
  function run(line, { quiet = false } = {}) {
    return session.command(line).then(
      (r) => r,
      (e) => { if (!quiet && toast) toast(String(e && e.message ? e.message : e), true); throw e; });
  }
  const swallow = (p) => p.catch(() => {});
  const vm = {
    session, has, screen, song, beatsPerBar, recents, lastError, rows, clips, mixers, stripsById, transport, view,
    run,
    play: () => swallow(run(transport.peek().playing ? "transport stop" : "transport play")),
    seek: (b) => swallow(run("transport seek " + beats(Math.max(0, b)))),
    rewind: () => swallow(run("transport seek 0")),
    setBpm: (v) => swallow(run("set project.bpm=" + Number(v))),
    save: () => swallow(run("project save")),
    undo: () => swallow(run("undo")),
    redo: () => swallow(run("redo")),
    close: () => swallow(run("project close")),
    open: (path) => swallow(run("project open " + token(path))),
    create: (path) => swallow(run("project new " + token(path))),
    toggle: (id, field, now) => swallow(run(`set ${id}.${field}=${now ? "false" : "true"}`)),
  };
  return vm;
}
