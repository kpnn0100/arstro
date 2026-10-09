/*
 * Solaris — lanes.js: the song's lanes (the window's Timeline, a compact first version): a ruler with
 * the bars, one row per lane (and per strip whose clips are on no lane - Timeline's rule, vm/song.js),
 * every clip as a plate in its strip's colour with its pattern's notes, and the playhead.
 *
 * Like the window it keeps a LIVE eased copy of everything the model moves (a clip's beat, length and
 * row; the zoom; the playhead when it jumps), so nothing changes in one frame: one rAF loop eases
 * them and writes the styles. Gestures are command lines: a ruler click is `transport seek <beat>`
 * on the step you can see (Timeline's snap table); Ctrl+wheel zooms this page only (view state).
 */
import { keyed, bindEffect } from "../core/dom.js";
import { h } from "./util.js";
import { effect } from "../core/signal.js";
import { trackVar } from "../vm/song.js";

const ROW_H = 44, RULER_H = 24, MIN_PPB = 4.7, MAX_PPB = 637, MS = 200;

/** A number that eases to where it is told (EaseOutCubic over `ms`). */
class Tween {
  constructor(v, ms = MS) { this.v = this.from = this.to = v; this.t0 = 0; this.ms = ms; }
  set(to, now, jump = false) {
    if (to === this.to) return;
    if (jump || matchMedia("(prefers-reduced-motion: reduce)").matches) { this.v = this.from = this.to = to; return; }
    this.from = this.v; this.to = to; this.t0 = now;
  }
  get(now) {
    if (this.v === this.to) return this.v;
    const k = Math.min(1, (now - this.t0) / this.ms);
    this.v = k >= 1 ? this.to : this.from + (this.to - this.from) * (1 - Math.pow(1 - k, 3));
    return this.v;
  }
}

/** The step a click lands on at this zoom (Timeline.cpp's table: a bar below 11.5 px/beat, a beat from
 *  11.5, 1/2 from 22.4, 1/4 from 55, 1/8 from 107, 1/16 from 209, 1/32 from 408). */
function snapStep(ppb, beatsPerBar) {
  if (ppb < 11.5) return beatsPerBar;
  if (ppb < 22.4) return 1;
  if (ppb < 55) return 0.5;
  if (ppb < 107) return 0.25;
  if (ppb < 209) return 0.125;
  if (ppb < 408) return 0.0625;
  return 0.03125;
}

function notesSvg(c) {
  const NS = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("class", "clip-notes");
  svg.setAttribute("preserveAspectRatio", "none");
  if (!c.notes || !c.notes.length || !(c.length > 0)) return svg;
  let lo = 127, hi = 0;
  for (const n of c.notes) { lo = Math.min(lo, n.pitch); hi = Math.max(hi, n.pitch); }
  const span = Math.max(1, hi - lo + 1);
  svg.setAttribute("viewBox", `0 0 ${c.length} ${span}`);
  const loop = c.patternLength > 0 ? c.patternLength : c.length;
  for (let k = 0; k * loop < c.length; k++) {
    for (const n of c.notes) {
      const x = k * loop + n.at;
      if (x >= c.length) continue;
      const r = document.createElementNS(NS, "rect");
      r.setAttribute("x", x);
      r.setAttribute("y", hi - n.pitch);
      r.setAttribute("width", Math.max(0.06, Math.min(n.length, c.length - x)));
      r.setAttribute("height", 0.8);
      svg.append(r);
    }
  }
  return svg;
}

export function lanes(vm) {
  const heads = h("div.lane-heads");
  const headsInner = h("div.lane-heads-inner");
  heads.append(headsInner);
  const ruler = h("div.ruler");
  const rulerInner = h("div.ruler-inner");
  const rulerHead = h("div.ruler-playhead");
  ruler.append(rulerInner);
  rulerInner.append(rulerHead);
  const stage = h("div.lane-stage.scroll");
  const content = h("div.stage-content");
  const rowsLayer = h("div.rows-layer");
  const clipsLayer = h("div.clips-layer");
  const playhead = h("div.playhead");
  content.append(rowsLayer, clipsLayer, playhead);
  stage.append(content);
  const empty = h("div.lanes-empty", "No clips yet — `clip add --instrument drums --at 0 --length 8` in the console makes one.");

  const now0 = performance.now();
  const ppb = new Tween(vm.view.pxPerBeat.peek(), 220);
  let anchor = null; // {beat, x}: the beat under the pointer stays under it while a zoom eases

  // ── rows: lane heads on the left, stripes on the stage, keyed and eased into place ──
  const rowLive = new Map(); // key -> {head, stripe, top: Tween}
  keyed(headsInner, () => vm.rows.value, (r) => r.key, (r) => {
    const head = h("div.lane-head", h("i.lane-colour"), h("span.lane-name"));
    const stripe = h("div.lane-row");
    rowsLayer.append(stripe);
    rowLive.set(r.key, { head, stripe, top: new Tween(r.index * ROW_H) });
    return head;
  }, (el, r) => {
    const live = rowLive.get(r.key);
    live.top.set(r.index * ROW_H, performance.now());
    el.querySelector(".lane-name").textContent = r.name;
    el.querySelector(".lane-colour").style.background = trackVar(r.colour);
    live.stripe.classList.toggle("alt", r.index % 2 === 1);
  });

  // ── clips: plates keyed by id; beat, length and row eased ──
  const clipLive = new Map(); // id -> {el, at, len, row: Tween, key}
  keyed(clipsLayer, () => vm.clips.value, (c) => c.id, (c) => {
    const el = h("div.clip", h("div.clip-name"));
    clipLive.set(c.id, { el, at: new Tween(c.at), len: new Tween(c.length), row: new Tween(c.row || 0), notesKey: "" });
    return el;
  }, (el, c) => {
    const live = clipLive.get(c.id);
    const t = performance.now();
    live.at.set(c.at, t);
    live.len.set(c.length, t);
    live.row.set(c.row || 0, t);
    el.querySelector(".clip-name").textContent = c.name || c.id;
    el.style.setProperty("--clip", trackVar(c.colour));
    el.classList.toggle("note", c.kind === "note");
    el.classList.toggle("offline", !!c.offline);
    el.classList.toggle("linked", !!c.linked);
    el.title = `${c.id} · ${c.name} · beat ${c.at} · ${c.length} beats`;
    const key = JSON.stringify([c.notes, c.length, c.patternLength]);
    if (key !== live.notesKey) {
      live.notesKey = key;
      const old = el.querySelector(".clip-notes");
      if (old) old.remove();
      el.append(notesSvg(c));
    }
  });

  bindEffect(empty, () => { empty.classList.toggle("on", vm.clips.value.length === 0); });

  // ── the ruler's bar numbers, thinned to the room they have (Timeline paintRuler) ──
  let rulerKey = "";
  function rebuildRuler(p, totalBeats) {
    const bpb = vm.beatsPerBar.peek();
    let every = 1;
    while (every * bpb * p < 30 && every < 256) every *= 2;
    const key = `${every}|${Math.ceil(totalBeats / bpb)}`;
    if (key === rulerKey) return;
    rulerKey = key;
    for (const n of [...rulerInner.querySelectorAll(".bar-label")]) n.remove();
    for (let bar = 0; bar * bpb < totalBeats; bar += every) {
      const l = h("span.bar-label.mono", String(bar + 1));
      l.dataset.beat = String(bar * bpb);
      rulerInner.append(l);
    }
  }

  // ── the playhead: extrapolated between `transport` pushes while playing, eased when it jumps ──
  let tAnchor = { pos: 0, at: now0, playing: false };
  const head = new Tween(0, 180);
  effect(() => {
    const t = vm.transport.value;
    const n = performance.now();
    tAnchor = { pos: t.position, at: n, playing: t.playing };
    head.set(t.position, n, t.playing);
  });

  // ── one frame: ease, then write ──
  function frame(now) {
    const p = ppb.get(now);
    const song = vm.song.peek();
    const bpb = vm.beatsPerBar.peek();
    let end = song ? song.lengthBeats : 0;
    for (const c of vm.clips.peek()) end = Math.max(end, c.at + c.length);
    const totalBeats = Math.max(end + 2 * bpb, (stage.clientWidth || 800) / p + bpb);
    const w = totalBeats * p;
    const rows = vm.rows.peek().length;
    content.style.width = w + "px";
    content.style.height = Math.max(rows * ROW_H, stage.clientHeight || 0) + "px";
    rulerInner.style.width = w + "px";
    content.style.setProperty("--ppb", p + "px");
    content.style.setProperty("--bar", p * bpb + "px");
    if (anchor) {
      stage.scrollLeft = anchor.beat * p - anchor.x;
      if (p === ppb.to) anchor = null;
    }
    rebuildRuler(p, totalBeats);
    for (const l of rulerInner.querySelectorAll(".bar-label")) l.style.transform = `translateX(${Number(l.dataset.beat) * p}px)`;
    for (const [key, live] of rowLive) {
      if (!live.head.isConnected) { live.stripe.remove(); rowLive.delete(key); continue; }
      const y = live.top.get(now);
      live.head.style.transform = `translateY(${y}px)`;
      live.stripe.style.transform = `translateY(${y}px)`;
    }
    for (const [id, live] of clipLive) {
      if (!live.el.isConnected) { clipLive.delete(id); continue; }
      const x = live.at.get(now) * p, cw = Math.max(3, live.len.get(now) * p - 1), y = live.row.get(now) * ROW_H;
      live.el.style.transform = `translate(${x}px, ${y + 3}px)`;
      live.el.style.width = cw + "px";
    }
    const s = vm.song.peek();
    const bpm = s ? s.bpm : 120;
    const pos = tAnchor.playing ? tAnchor.pos + ((now - tAnchor.at) / 1000) * (bpm / 60) : head.get(now);
    playhead.style.transform = `translateX(${pos * p}px)`;
    rulerHead.style.transform = `translateX(${pos * p}px)`;
    ruler.scrollLeft = stage.scrollLeft;
    heads.scrollTop = stage.scrollTop;
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);

  // ── gestures ──
  ruler.addEventListener("click", (e) => {
    const r = ruler.getBoundingClientRect();
    const p = ppb.get(performance.now());
    const step = snapStep(p, vm.beatsPerBar.peek());
    const beat = Math.max(0, (e.clientX - r.left + stage.scrollLeft) / p);
    vm.seek(Math.round(beat / step) * step);
  });
  stage.addEventListener("wheel", (e) => {
    if (!e.ctrlKey) return;
    e.preventDefault();
    const r = stage.getBoundingClientRect();
    const x = e.clientX - r.left;
    const p = ppb.get(performance.now());
    const next = Math.min(MAX_PPB, Math.max(MIN_PPB, ppb.to * (e.deltaY < 0 ? 1.25 : 0.8)));
    anchor = { beat: (stage.scrollLeft + x) / p, x };
    ppb.set(next, performance.now());
    vm.view.pxPerBeat.value = next;
  }, { passive: false });

  return h("div.lanes",
    h("div.lanes-corner", h("span", "Lanes")), ruler,
    heads, h("div.stage-wrap", stage, empty));
}
