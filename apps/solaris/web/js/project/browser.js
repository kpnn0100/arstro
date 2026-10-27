/*
 * Solaris — project/browser.js: Browser (app/widgets/Browser.{h,cpp}) — the left column: samples, instruments and
 * effects, the song's sounds, the presets (R-BROWSE-1, R-BROWSE-3, R-BROWSE-4, R-PRESET-1, R-PRESET-3, R-EDM-9).
 *
 * Drawn on one canvas, line for line the native's onPaint: four tabs as wide as their names (+4 characters' room) with
 * a highlight that SLIDES (kSlideMs, EaseOutCubic, x and width); the list below them TRAVELS (AnimatedRows keyed by
 * generation|kind|value|label: a tab or a folder changed is a new generation — the whole list cross-fades, the old one
 * fading where it was scrolled to) and scrolls (EasedScroll). Hover is eased (HoverFade, motion.hover).
 *
 * Rows: Samples — the sample folders from Settings, then Search… and Favourites; a folder clicked is listed through
 *   vm.browse(path, ["audio", "midi"]) (folders always) and the native's own line `browse <path>` is sent too, with a
 *   row back up. Instruments — the registry's instruments then its effects. Song — MIDI (New MIDI, the patterns) then the
 *   samples the song plays. Presets — per type, its presets.
 * Gestures (Browser::handleGesture): a click on a tab sets it; on a folder/dir/up navigates; on a sound auditions it
 *   (`audition <file>` / `audition stop`); on its star `sample favourite <file> on|off`; New MIDI `pattern new --name …`.
 *   A double-click places the item at the playhead (vm.dnd.activate) — a pattern opens its piano roll. A drag of a
 *   draggable row (audio, midi, instrument, effect, pattern, preset) reports vm.dnd.move / drop; Escape or a lost
 *   pointer cancels (vm.dnd.cancel). Right-click: a sound's tag/favourite menu, a pattern's menu — each one line.
 * Fixtures: vm.nativeView.project.browserTab picks the tab; the Samples tab adopts the model's browser.path.
 */
import { computed, effect, jsonEqual } from "../core/signal.js";
import { own } from "../core/dom.js";
import { Anim, Ease, frames, motion, EasedScroll, AnimatedRows } from "../kit/anim.js";
import { canvasIn, color, rgba, withAlpha, lerpColor, hoverWash, primaryAlpha, colorsVersion, rr } from "../kit/draw.js";
import { FONT, measure, ellipsize, baseline } from "../kit/text.js";
import { glyph } from "../kit/glyphs.js";
import { useCss } from "../kit/css.js";
import * as Words from "../vm/words.js";

const WIDTH = 234, TABS_H = 29.25, ROW_H = 22.75, PAD_X = 9.75, R_CONTROL = 2, R_HAIRLINE = 1;
const LIST_TOP = TABS_H + 1;
const SAMPLES = 0, INSTRUMENTS = 1, SONG = 2, PRESETS = 3;
const TAB_NAMES = ["Samples", "Instruments", "Song", "Presets"];
const DRAG_SLOP = 4;
const draggable = (it) => !!it && ["audio", "instrument", "effect", "pattern", "midi", "preset"].includes(it.kind);
const baseName = (p) => { const s = String(p || "").lastIndexOf("/"); return s < 0 ? String(p || "") : p.slice(s + 1); };
const fade = (c, a) => withAlpha(c, c[3] * a);
// lucide's folder (cosmo's icon::folder), 24-unit box
const FOLDER = typeof Path2D !== "undefined"
  ? new Path2D("M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z")
  : null;

export function mount(host, vm, ctx) {
  useCss("project/browser.css");
  host.classList.add("sl-browser");
  const cv = canvasIn(host, ctx);
  const say = vm.say || Words.say, sayn = vm.sayn || Words.sayn, shown = vm.shown || Words.shown, tr = vm.tr || Words.tr;
  const upper = vm.upper || Words.upper;
  const q = vm.q;

  // ---- state (Browser.h) ----
  let tab = SAMPLES, path = "", search = "", entries = [], gen = 0, ghostScroll = 0;
  let folders = [], types = [], presets = [], lib = new Map(), songMidi = [], songSounds = [];
  let audFile = "", audPlaying = false, audProgress = 0;
  let rows = [];
  const motionRows = new AnimatedRows();
  const scroll = new EasedScroll();
  const tabX = new Anim(0), tabW = new Anim(0);
  let tabInit = false;
  const stars = new Map();  // file -> {amt, last}
  const auds = new Map();   // file -> {amt, want, last, placed, progress}
  const hovers = new Map(); // id -> Anim
  let hovered = -1;
  let press = null, dragging = false, dragItem = null;
  let W = WIDTH, H = 0;

  // fixtures: the native's tab (and its folder, the model's browser.path)
  const nv = vm.nativeView && vm.nativeView.peek ? vm.nativeView.peek() : null;
  const fixture = !!nv;
  if (nv && nv.project && Number.isInteger(nv.project.browserTab) && nv.project.browserTab >= 0 && nv.project.browserTab < 4) tab = nv.project.browserTab;

  const src = computed(() => {
    const m = vm.model.value;
    if (!m) return null;
    return {
      folders: (m.settings && m.settings.folders) || [], types: m.deviceTypes || [], presets: m.presets || [],
      audition: m.audition || {}, browser: m.browser || {}, library: m.library || [], strips: m.strips || [],
      patterns: m.patterns || [], clips: (m.clips || []).filter((c) => c.kind === "audio").map((c) => c.src), sig: m.sig || "4/4",
    };
  }, jsonEqual);

  function lengthText(beats, perBar) {
    const bars = beats / Math.max(1, perBar);
    if (Math.abs(bars - Math.round(bars)) < 1e-9 && bars >= 1) return sayn(Math.round(bars), "{1} bar", "{1} bars", Math.round(bars));
    if (Math.abs(beats - Math.round(beats)) < 1e-9) return sayn(Math.round(beats), "{1} beat", "{1} beats", Math.round(beats));
    return say("{1} beats", String(beats));
  }

  function bind(m) {
    if (!m) return;
    folders = m.folders; types = m.types; presets = m.presets;
    audFile = m.audition.file || ""; audPlaying = !!m.audition.playing; audProgress = Number(m.audition.progress) || 0;
    if (fixture && !path && !search && tab === SAMPLES && m.browser.path) path = m.browser.path;
    if (search) { if (m.browser.search === search) entries = m.browser.entries || []; }
    else if (path && m.browser.path === path && (m.browser.entries || []).length) entries = m.browser.entries;
    lib = new Map(); for (const e of m.library) lib.set(e.path, e);
    const stripName = new Map(m.strips.map((s) => [s.id, s.name]));
    const perBar = Math.max(1, parseInt(m.sig, 10) || 4);
    songMidi = m.patterns.map((pt) => {
      const who = (pt.strips || []).map((s) => stripName.get(s) || s).join(", ");
      const n = (pt.notes || []).length;
      const note = shown(lengthText(Number(pt.length) || 0, perBar)) + " · " + shown(sayn(n, "{1} note", "{1} notes", n)) + " · " + (who || shown(say("unplayed")));
      return { kind: "pattern", label: pt.name, value: pt.id, note, strip: pt.lastStrip || "" };
    });
    const seen = new Set();
    songSounds = [];
    for (const s of m.clips) if (!seen.has(s)) { seen.add(s); songSounds.push([baseName(s), s]); }
    rebuild();
  }

  function rebuild() {
    rows = [];
    const it = (kind, label, value = "", note = "") => ({ kind, label, value, note });
    if (tab === SAMPLES && search) {
      rows.push(it("clearsearch", search === "favourites" ? say("Favourites") : say("Search · {1}", search)));
      for (const e of entries) if (e.kind === "audio") rows.push(it("audio", e.name, e.path));
    } else if (tab === SAMPLES) {
      if (!path) {
        for (const f of folders) rows.push(it("folder", baseName(f), f, f));
        if (folders.length || lib.size) { rows.push(it("search", say("Search samples…"))); rows.push(it("favourites", say("Favourites"))); }
      } else {
        const slash = path.lastIndexOf("/");
        rows.push(it("up", baseName(path), slash < 0 ? "" : path.slice(0, slash)));
        for (const e of entries) if (e.kind === "dir" || e.kind === "audio" || e.kind === "midi") rows.push(it(e.kind, e.name, e.path));
      }
    } else if (tab === INSTRUMENTS) {
      for (const kind of ["instrument", "effect"]) {
        rows.push(it("header", kind === "instrument" ? say("Instruments") : say("Effects")));
        for (const t of types) if (t.kind === kind) rows.push(it(kind, t.label, t.name, t.name));
      }
    } else if (tab === PRESETS) {
      for (const t of types) {
        let any = false;
        for (const x of presets) if (x.type === t.name) {
          if (!any) rows.push(it("header", t.label));
          any = true;
          const p = it("preset", x.name, x.type, x.factory ? say("factory") : "");
          p.role = t.kind;
          rows.push(p);
        }
      }
    } else {
      rows.push(it("header", say("MIDI")));
      rows.push(it("newmidi", say("New MIDI")));
      for (const p of songMidi) rows.push(p);
      if (songSounds.length) rows.push(it("header", say("Samples")));
      for (const [l, s] of songSounds) rows.push(it("audio", l, s));
    }
    const g = gen + "|";
    const keyed = rows.map((r) => [g + r.kind + "|" + r.value + "|" + r.label, r]);
    if (tab === SAMPLES && search && rows.length === 1) {
      const e = it("empty", search === "favourites" ? say("No favourites yet.") : say("Nothing found."));
      e.note = search === "favourites" ? say("A sound's star makes it one.") : say("Try part of a name, or a tag.");
      keyed.push([g + "empty|" + e.label, e]);
    }
    if (!rows.length) {
      const e = it("empty", "");
      if (tab === SAMPLES && !path) { e.label = say("No sample folders yet."); e.note = say("Add yours in Settings — click here."); }
      else if (tab === SAMPLES) { e.label = say("Nothing to play here."); e.note = say("Audio files and folders show up in this list."); }
      else if (tab === SONG) { e.label = say("This song plays no samples yet."); e.note = say("Drag one in from Samples."); }
      else if (tab === PRESETS) { e.label = say("No presets yet."); e.note = say("Save one from a device's window — Presets."); }
      keyed.push([g + "empty|" + e.label, e]);
    }
    motionRows.sync(keyed, ROW_H);
    layout();
    kick();
  }

  function navigate() {
    ghostScroll = scroll.value();
    ++gen;
    scroll.reset();
  }

  function layout() {
    const b = ctx.box.peek();
    W = b.w || WIDTH; H = b.h || 0;
    scroll.setExtent(LIST_TOP, Math.max(0, H - LIST_TOP), rows.length * ROW_H + PAD_X);
  }

  // ---- geometry ----
  function tabRect(t) {
    let sum = 0, before = 0;
    for (let k = 0; k < TAB_NAMES.length; ++k) { const w = TAB_NAMES[k].length + 4; if (k < t) before += w; sum += w; }
    const room = W - 2 * PAD_X;
    return { x: PAD_X + room * before / sum, y: 4, w: room * (TAB_NAMES[t].length + 4) / sum, h: TABS_H - 8 };
  }
  function rowY(i) { const r = motionRows.byIndex(i); return !r ? i * ROW_H : r.placed ? r.liveY() : r.yTarget; }
  function rowRect(i) { return { x: 0, y: LIST_TOP + rowY(i) - scroll.value(), w: W, h: ROW_H }; }
  const inside = (r, p) => p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
  function rowAt(p) {
    if (p.y < LIST_TOP) return -1;
    for (let i = 0; i < rows.length; ++i) if (inside(rowRect(i), p)) return rows[i].kind === "header" ? -1 : i;
    return -1;
  }
  function tabAt(p) { for (let t = 0; t < TAB_NAMES.length; ++t) if (inside(tabRect(t), p)) return t; return -1; }
  function starRect(i) {
    if (i < 0 || i >= rows.length || rows[i].kind !== "audio") return null;
    const r = rowRect(i);
    return { x: W - PAD_X - 13, y: r.y + (r.h - 13) * 0.5, w: 13, h: 13 };
  }
  function local(e) { const p = ctx.screenPoint(e), b = ctx.box.peek(); return { x: p.x - b.x, y: p.y - b.y }; }

  // ---- hover (HoverFade) ----
  function setHovered(id) {
    if (id === hovered) return;
    const now = performance.now(), m = motion();
    if (hovered !== -1) { const a = hovers.get(hovered); if (a) a.animateTo(0, m.hover, Ease.EaseOutCubic, now); }
    hovered = id;
    if (id !== -1) { let a = hovers.get(id); if (!a) { a = new Anim(0); hovers.set(id, a); } a.animateTo(1, m.hover, Ease.EaseOutCubic, now); }
    kick();
  }
  const hoverAmount = (id) => { const a = hovers.get(id); return a ? a.value : 0; };

  // ---- actions ----
  const send = (line) => vm.cmd(line);
  function setTab(t) { if (t < 0 || t >= TAB_NAMES.length || t === tab) return; tab = t; navigate(); rebuild(); }
  function goTo(target) {
    path = target;
    entries = [];
    navigate();
    if (path) {
      vm.note && vm.note("browse " + q(path)); // the native's line (Browser.cpp:451)
      const want = path;
      Promise.resolve(vm.browse(want, ["audio", "midi"])).then((r) => {
        if (path !== want || search || !r) return;
        entries = r.entries || [];
        rebuild();
      }).catch(() => {});
    }
    rebuild();
  }
  function click(p, e) {
    const t = tabAt(p);
    if (t >= 0) { setTab(t); return; }
    const r = rowAt(p);
    if (tab === SAMPLES && !folders.length && !search && (r < 0 || (rows[r].kind !== "search" && rows[r].kind !== "favourites"))) { vm.openSettings && vm.openSettings(); return; }
    if (r < 0) return;
    const it = rows[r];
    const sr = starRect(r);
    if (it.kind === "audio" && sr && inside(sr, p)) {
      const l = lib.get(it.value);
      send("sample favourite " + q(it.value) + (l && l.favourite ? " off" : " on"));
      return;
    }
    if (it.kind === "search") {
      const pt = ctx.screenPoint(e);
      ctx.menu.openRename("", pt.x, pt.y, (text) => {
        if (!text) return;
        search = text; entries = []; navigate();
        send("browse --search " + q(text));
        rebuild();
      });
      return;
    }
    if (it.kind === "favourites" || it.kind === "clearsearch") {
      search = it.kind === "favourites" ? "favourites" : "";
      path = ""; entries = []; navigate();
      if (search) send("browse --favourites");
      rebuild();
      return;
    }
    if (it.kind === "folder" || it.kind === "dir" || it.kind === "up") {
      let target = it.value;
      if (it.kind === "up" && folders.includes(path)) target = "";
      goTo(target);
    } else if (it.kind === "audio") {
      send(audPlaying && audFile === it.value ? "audition stop" : "audition " + q(it.value));
    } else if (it.kind === "newmidi") {
      send("pattern new --name " + q("MIDI " + (songMidi.length + 1)));
    }
  }
  function doubleClick(p) {
    const r = rowAt(p);
    if (r < 0) return;
    const it = rows[r];
    if (it.kind === "pattern") { vm.openRoll && vm.openRoll(it.value); return; }
    if (draggable(it)) vm.dnd.activate(it);
  }
  function rightClick(p, e) {
    const r = rowAt(p);
    const pt = ctx.screenPoint(e);
    if (r >= 0 && rows[r].kind === "audio") {
      const file = rows[r].value, l = lib.get(file), fav = !!(l && l.favourite);
      const items = [{ label: say("Tag…"), action: () => ctx.menu.openRename("", pt.x, pt.y, (tag) => { if (tag) send("sample tag " + q(file) + " " + q(tag)); }) }];
      for (const tag of (l && l.tags) || []) items.push({ label: say("Remove tag · {1}", tag), action: () => send("sample untag " + q(file) + " " + tag) });
      items.push({ label: fav ? say("Not a favourite") : say("Favourite"), action: () => send("sample favourite " + q(file) + (fav ? " off" : " on")) });
      ctx.menu.open(items, pt.x, pt.y);
      return;
    }
    if (r < 0 || rows[r].kind !== "pattern") return;
    const id = rows[r].value, name = rows[r].label;
    ctx.menu.open([
      { label: say("Rename…"), action: () => ctx.menu.openRename(name, pt.x, pt.y, (n) => { if (n) send("set " + id + ".name=" + q(n)); }) },
      { label: say("Piano Roll"), action: () => vm.openRoll && vm.openRoll(id) },
      { label: say("Duplicate"), action: () => send("pattern duplicate " + id) },
      { label: say("Copy ID"), action: () => (ctx.copy ? ctx.copy(id) : vm.copy(id)) },
      { label: say("Delete"), action: () => send("pattern delete " + id) },
    ], pt.x, pt.y);
  }

  // ---- pointer ----
  function endDrag(cancelled, e) {
    const item = dragItem;
    dragging = false; dragItem = null; press = null;
    host.classList.remove("sl-browser-dragging");
    if (!item) return;
    if (cancelled) vm.dnd.cancel();
    else vm.dnd.drop(item, ctx.screenPoint(e));
  }
  const onDown = (e) => {
    if (e.button !== 0) return;
    const p = local(e);
    press = { row: rowAt(p), x: p.x, y: p.y, id: e.pointerId };
    if (vm.view.interacting) vm.view.interacting.value = true;
  };
  const onMove = (e) => {
    const p = local(e);
    if (press && !dragging && e.pointerId === press.id && Math.hypot(p.x - press.x, p.y - press.y) > DRAG_SLOP && press.row >= 0 && draggable(rows[press.row])) {
      dragging = true;
      dragItem = rows[press.row];
      host.classList.add("sl-browser-dragging");
      try { host.setPointerCapture(e.pointerId); } catch { /* gone */ }
      setHovered(-1);
    }
    if (dragging) { vm.dnd.move(dragItem, ctx.screenPoint(e)); return; }
    const t = tabAt(p);
    setHovered(t >= 0 ? 100 + t : rowAt(p));
  };
  const onUp = (e) => {
    if (vm.view.interacting) vm.view.interacting.value = false;
    if (dragging) { endDrag(false, e); return; }
    if (press && e.pointerId === press.id) click(local(e), e);
    press = null;
  };
  const onCancel = () => {
    if (vm.view.interacting) vm.view.interacting.value = false;
    if (dragging) endDrag(true); else press = null;
  };
  const onLeave = () => { if (!dragging) { setHovered(-1); press = null; } };
  const onDbl = (e) => doubleClick(local(e));
  const onCtx = (e) => { e.preventDefault(); rightClick(local(e), e); };
  const onWheel = (e) => {
    const d = e.deltaMode === 1 ? e.deltaY * 16 : e.deltaY;
    if (scroll.scrollBy(d)) { e.preventDefault(); kick(); }
  };
  const onKey = (e) => { if (dragging && e.key === "Escape") { e.preventDefault(); e.stopPropagation(); endDrag(true); } };
  host.addEventListener("pointerdown", onDown);
  host.addEventListener("pointermove", onMove);
  host.addEventListener("pointerup", onUp);
  host.addEventListener("pointercancel", onCancel);
  host.addEventListener("pointerleave", onLeave);
  host.addEventListener("dblclick", onDbl);
  host.addEventListener("contextmenu", onCtx);
  host.addEventListener("wheel", onWheel, { passive: false });
  window.addEventListener("keydown", onKey, true);
  own(host, () => {
    host.removeEventListener("pointerdown", onDown);
    host.removeEventListener("pointermove", onMove);
    host.removeEventListener("pointerup", onUp);
    host.removeEventListener("pointercancel", onCancel);
    host.removeEventListener("pointerleave", onLeave);
    host.removeEventListener("dblclick", onDbl);
    host.removeEventListener("contextmenu", onCtx);
    host.removeEventListener("wheel", onWheel);
    window.removeEventListener("keydown", onKey, true);
    if (dragging) endDrag(true);
  });

  // ---- advance (Browser::advance) ----
  function advance(now) {
    const m = motion();
    let moving = false;
    const want = tabRect(tab);
    if (!tabInit) { tabX.set(want.x); tabW.set(want.w); tabInit = true; }
    else {
      if (Math.abs(tabX.target - want.x) > 0.01) tabX.animateTo(want.x, m.slide, Ease.EaseOutCubic, now);
      if (Math.abs(tabW.target - want.w) > 0.01) tabW.animateTo(want.w, m.slide, Ease.EaseOutCubic, now);
    }
    moving = tabX.update(now) || moving;
    moving = tabW.update(now) || moving;
    motionRows.advance(now);
    moving = motionRows.moving() || moving;
    for (const it of rows) if (it.kind === "audio") {
      let s = stars.get(it.value);
      if (!s) { s = { amt: new Anim(0), last: -1 }; stars.set(it.value, s); }
      const l = lib.get(it.value);
      const w = l && l.favourite ? 1 : 0;
      if (s.last < 0) s.amt.set(w);
      else if (w !== s.last) s.amt.animateTo(w, m.crossFade, Ease.EaseOutCubic, now);
      s.last = w;
    }
    for (const s of stars.values()) moving = s.amt.update(now) || moving;
    if (audPlaying && audFile && !auds.has(audFile)) auds.set(audFile, { amt: new Anim(0), want: false, last: false, placed: false, progress: 0 });
    for (const [file, a] of [...auds]) {
      a.want = audPlaying && file === audFile;
      if (a.want) a.progress = audProgress;
      if (!a.placed) { a.amt.set(0); a.placed = true; }
      if (a.want !== a.last) { a.amt.animateTo(a.want ? 1 : 0, m.crossFade, Ease.EaseOutCubic, now); a.last = a.want; }
      moving = a.amt.update(now) || moving;
      if (!a.want && !a.amt.animating && a.amt.value <= 0.001) auds.delete(file);
    }
    moving = scroll.advance(now) || moving;
    for (const [id, a] of [...hovers]) { moving = a.update(now) || moving; if (id !== hovered && !a.animating && a.value <= 0.001) hovers.delete(id); }
    return moving;
  }

  // ---- paint (Browser::onPaint) ----
  function starGlyph(g, b, c, lit) {
    const cx = b.x + b.w * 0.5, cy = b.y + b.h * 0.53, ro = b.w * 0.5, ri = ro * 0.42;
    g.beginPath();
    for (let k = 0; k < 10; ++k) {
      const ang = -Math.PI / 2 + k * Math.PI / 5, r = k % 2 === 0 ? ro : ri;
      const x = cx + r * Math.cos(ang), y = cy + r * Math.sin(ang);
      if (k === 0) g.moveTo(x, y); else g.lineTo(x, y);
    }
    g.closePath();
    if (lit > 0.001) { g.fillStyle = rgba(fade(c, lit)); g.fill(); }
    g.strokeStyle = rgba(c); g.lineWidth = 1.1; g.lineJoin = "miter"; g.stroke();
  }
  function bar(g, x, y, w, h, r, c) { g.beginPath(); g.roundRect(x, y, Math.max(0, w), Math.max(0, h), rr(r, w, h)); g.fillStyle = rgba(c); g.fill(); }
  function waveGlyph(g, b, c) {
    const hs = [0.35, 0.8, 1.0, 0.6, 0.3];
    for (let i = 0; i < 5; ++i) { const bh = b.h * hs[i]; bar(g, b.x + i * b.w / 5 + 0.5, b.y + (b.h - bh) * 0.5, b.w / 5 - 1, bh, R_CONTROL, c); }
  }
  function notesGlyph(g, b, c) {
    const y = [0.7, 0.25, 0.5], x = [0.0, 0.3, 0.62], w = [0.4, 0.4, 0.38];
    for (let i = 0; i < 3; ++i) bar(g, b.x + x[i] * b.w, b.y + y[i] * b.h - 1, w[i] * b.w, 2.2, R_CONTROL, c);
  }
  function folderGlyph(g, b, c, sw) {
    if (!FOLDER) return;
    g.save();
    g.translate(b.x, b.y);
    g.scale(b.w / 24, b.h / 24);
    g.strokeStyle = rgba(c); g.lineWidth = sw * 24 / b.w; g.lineJoin = "round"; g.lineCap = "round";
    g.stroke(FOLDER);
    g.restore();
  }
  function txt(g, s, x, base, px, fam, c, tracking = 0) {
    g.font = `${px}px "${fam}"`;
    g.fillStyle = rgba(c);
    if ("letterSpacing" in g) g.letterSpacing = tracking ? tracking + "px" : "0px";
    g.textBaseline = "alphabetic"; g.textAlign = "left";
    g.fillText(s, x, base);
    if (tracking && "letterSpacing" in g) g.letterSpacing = "0px";
  }

  function paint() {
    const g = cv.begin();
    W = cv.w || W; H = cv.h || H;
    const fg = color("foreground"), muted = color("muted-foreground"), border = color("border"), primary = color("primary");
    g.fillStyle = rgba(color("header-bg")); g.fillRect(0, 0, W, H);
    g.strokeStyle = rgba(border); g.lineWidth = 1;
    g.beginPath(); g.moveTo(W - 0.5, 0); g.lineTo(W - 0.5, H); g.stroke();
    g.beginPath(); g.moveTo(0, TABS_H); g.lineTo(W, TABS_H); g.stroke();

    // tabs: the highlight SLIDES
    bar(g, PAD_X, 4, W - 2 * PAD_X, TABS_H - 8, R_HAIRLINE, color("segmented-bg"));
    const t0 = tabRect(0);
    bar(g, tabX.value + 1, t0.y + 1, tabW.value - 2, t0.h - 2, R_HAIRLINE, color("secondary"));
    for (let k = 0; k < TAB_NAMES.length; ++k) {
      const r = tabRect(k), hv = hoverAmount(100 + k);
      const c = k === tab ? fg : lerpColor(muted, fg, 0.5 * hv);
      const fam = k === tab ? FONT.medium : FONT.sans;
      const name = ellipsize(tr(TAB_NAMES[k]), r.w - 4, 10, fam);
      txt(g, name, r.x + (r.w - measure(name, 10, FONT.sans)) * 0.5, baseline(r.y + r.h * 0.5, 10), 10, fam, c);
    }

    const top = LIST_TOP;
    g.save();
    g.beginPath(); g.rect(0, top, W, H - top); g.clip();
    for (const row of motionRows.rows()) {
      const a = row.placed ? row.liveAlpha() : 0;
      if (a <= 0.001) continue;
      const left = row.gone && parseInt(row.key, 10) !== gen;
      const r = { x: 0, y: top + row.liveY() - (left ? ghostScroll : scroll.value()), w: W, h: ROW_H };
      if (r.y + r.h < top || r.y > H) continue;
      const it = row.data;
      const cy = r.y + r.h * 0.5;
      if (it.kind === "empty") {
        txt(g, ellipsize(shown(it.label), W - 2 * PAD_X, 11, FONT.sans), PAD_X, r.y + 28, 11, FONT.sans, fade(muted, a));
        txt(g, ellipsize(shown(it.note), W - 2 * PAD_X, 10, FONT.sans), PAD_X, r.y + 44, 10, FONT.sans, fade(muted, 0.75 * a));
        continue;
      }
      if (it.kind === "header") {
        const tk = 0.13 * 9;
        txt(g, ellipsize(upper(shown(it.label)), W - 2 * PAD_X, 9, FONT.semibold, tk), PAD_X, baseline(cy + 3, 9), 9, FONT.semibold, fade(muted, a), tk);
        continue;
      }
      if (it.kind === "audio") {
        const au = auds.get(it.value);
        if (au && au.amt.value > 0.001) {
          const fa = au.amt.value * a, done = Math.min(1, Math.max(0, au.progress));
          bar(g, 4, r.y + 1, (W - 8) * done, r.h - 2, R_CONTROL, primaryAlpha(0.3 * fa));
          bar(g, 4, r.y + r.h - 3, (W - 8) * done, 2, 999, primaryAlpha(fa));
        }
      }
      const hv = row.gone || row.index < 0 ? 0 : hoverAmount(row.index);
      if (hv > 0.001) bar(g, 4, r.y + 1, W - 8, r.h - 2, R_CONTROL, hoverWash(hv * a));
      const ib = { x: PAD_X, y: cy - 6, w: 12, h: 12 };
      const ic = fade(lerpColor(muted, fg, 0.4 * hv), a);
      if (it.kind === "folder" || it.kind === "dir") folderGlyph(g, ib, ic, 1.1);
      else if (it.kind === "up" || it.kind === "clearsearch") { glyph.line(g, ib.x + 8, ib.y + 2, ib.x + 3, ib.y + 6, ic, 1.3); glyph.line(g, ib.x + 3, ib.y + 6, ib.x + 8, ib.y + 10, ic, 1.3); }
      else if (it.kind === "search") {
        g.beginPath(); g.arc(ib.x + 5, ib.y + 5, 3.6, 0, Math.PI * 2); g.strokeStyle = rgba(ic); g.lineWidth = 1.2; g.stroke();
        glyph.line(g, ib.x + 7.6, ib.y + 7.6, ib.x + 11, ib.y + 11, ic, 1.3);
      }
      else if (it.kind === "favourites") starGlyph(g, ib, ic, 0);
      else if (it.kind === "audio") waveGlyph(g, ib, ic);
      else if (it.kind === "instrument" || (it.kind === "preset" && it.role === "instrument")) glyph.speaker(g, ib, ic, 1.1);
      else if (it.kind === "pattern" || it.kind === "midi") notesGlyph(g, ib, ic);
      else if (it.kind === "newmidi") { glyph.line(g, ib.x + 6, ib.y + 2, ib.x + 6, ib.y + 10, ic, 1.3); glyph.line(g, ib.x + 2, ib.y + 6, ib.x + 10, ib.y + 6, ic, 1.3); }
      else { g.beginPath(); g.arc(ib.x + 6, ib.y + 6, 4, 0, Math.PI * 2); g.strokeStyle = rgba(ic); g.lineWidth = 1.1; g.stroke(); }
      const mono = it.kind === "audio";
      const px = mono ? 10 : 11, fam = mono ? FONT.mono : FONT.sans;
      let labelW = W - (ib.x + ib.w) - 2 * PAD_X;
      if (it.kind === "audio") {
        const sr = { x: W - PAD_X - 13, y: cy - 6.5, w: 13, h: 13 };
        const st = stars.get(it.value), lit = st ? st.amt.value : 0;
        starGlyph(g, sr, fade(lerpColor(muted, primary, lit), a * (0.45 + 0.55 * Math.max(lit, hv))), lit);
        labelW = Math.max(0, labelW - 19);
        const l = lib.get(it.value);
        if (l && l.tags && l.tags.length) {
          const meta = ellipsize(l.tags.join(" · "), W * 0.4, 9, FONT.mono);
          const mw = measure(meta, 9, FONT.mono);
          txt(g, meta, sr.x - 6 - mw, baseline(cy, 9), 9, FONT.mono, fade(muted, a));
          labelW = Math.max(0, labelW - mw - 8);
        }
      }
      if ((it.kind === "pattern" || it.kind === "preset") && it.note) {
        const meta = ellipsize(shown(it.note), W * 0.62, 9, FONT.mono);
        const mw = measure(meta, 9, FONT.mono);
        txt(g, meta, W - PAD_X - mw, baseline(cy, 9), 9, FONT.mono, fade(muted, a));
        labelW = Math.max(0, labelW - mw - 8);
      }
      const lc = it.kind === "up" || it.kind === "newmidi" ? lerpColor(muted, fg, it.kind === "newmidi" ? hv : 0) : fg;
      txt(g, ellipsize(shown(it.label), labelW, px, fam), ib.x + ib.w + 8, baseline(cy, px), px, fam, fade(lc, a));
    }
    g.restore();
    scroll.drawBar(g, W - 5);
  }

  let stopped = false;
  const tick = (now) => {
    if (stopped) return false;
    const moving = advance(now);
    paint();
    return moving;
  };
  function kick() { if (!stopped) frames(tick); }
  cv.onResize(() => { layout(); kick(); });

  const disposers = [
    effect(() => { bind(src.value); }),
    effect(() => { ctx.box.value; layout(); kick(); }),
    effect(() => { void colorsVersion.value; void Words.drawn.value; void Words.version.value; if (vm.lang) void vm.lang.value; kick(); }),
  ];
  own(host, () => { stopped = true; for (const d of disposers) if (typeof d === "function") d(); cv.el.remove(); });
  kick();
}
