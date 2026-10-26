/*
 * Interstellar — timeline.js: the Cut tab's timeline (the window's Timeline): the ruler, the tracks
 * top-down as the window draws them (video by order, highest on top, then audio), the clips at their
 * time, the markers and the playhead. A click on the ruler or a lane moves the playhead
 * (`playhead <t>`); a drag on the ruler scrubs (notifies, no reply awaited); a click on a clip
 * selects it (`clip select`). The zoom is this page's own (Ctrl/⌘ + wheel). Every edit is one
 * command line, the window's: split at the playhead, delete (ripple), a marker, a track.
 */
import { keyed, bindClass, bindEffect, bindStyle, drag } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { h, icon, ICONS, ease } from "./util.js";

const TRACK_H = 36;     // shell::trackH (35.75), rounded to the pixel grid
const RULER_H = 23;     // shell::rulerH (22.75)
const TAIL_PX = 320;    // room after the last clip, to drop the playhead past it

/** The ruler's label step for a zoom: the smallest "nice" step at least 64 px apart. */
function labelStep(pps) {
  for (const s of [0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600]) if (s * pps >= 64) return s;
  return 1200;
}
function label(t) {
  const m = Math.floor(t / 60), s = t - m * 60;
  return m ? `${m}:${String(Math.round(s)).padStart(2, "0")}` : (Number.isInteger(s) ? `${s}s` : `${s.toFixed(1)}s`);
}

export function timeline(vm) {
  const pps = () => vm.view.pxPerSec.value;

  // ── the toolbar: one command line per button ──
  const split = h("button.tool", { title: "Split the selected clip at the playhead (clip split)", onclick: () => vm.split() }, icon(ICONS.split), "Split");
  const del = h("button.tool", { title: "Delete the selected clip (clip delete)", onclick: () => vm.remove(false) }, icon(ICONS.trash), "Delete");
  const ripple = h("button.tool", { title: "Delete and close the gap (clip delete --ripple)", onclick: () => vm.remove(true) }, "Ripple");
  for (const b of [split, del, ripple]) bindEffect(b, () => { b.disabled = !vm.selectedClip.value; });
  const tlSelect = h("select.tl-select", { title: "The editor's current timeline (timeline open)" });
  tlSelect.addEventListener("change", () => vm.openTimeline(tlSelect.value));
  keyed(tlSelect, () => vm.timelines.value, (t) => t.id, (t) => h("option", { value: t.id }), (el, t) => { el.textContent = "  ".repeat(t.depth || 0) + t.name; },
        { animate: false });
  bindEffect(tlSelect, () => { const t = vm.timeline.value; if (t) tlSelect.value = t.id; tlSelect.disabled = vm.timelines.value.length < 2; });
  const zoomOut = h("button.tbtn", { title: "Zoom out (Ctrl + wheel)", onclick: () => { vm.view.pxPerSec.value = Math.max(4, pps() / 1.5); } }, "−");
  const zoomIn = h("button.tbtn", { title: "Zoom in (Ctrl + wheel)", onclick: () => { vm.view.pxPerSec.value = Math.min(800, pps() * 1.5); } }, "+");
  const toolbar = h("div.tl-toolbar",
    tlSelect,
    h("button.tool", { title: "Add a video track (track add --kind video)", onclick: () => vm.addTrack("video") }, icon(ICONS.plus), "Video"),
    h("button.tool", { title: "Add an audio track (track add --kind audio)", onclick: () => vm.addTrack("audio") }, icon(ICONS.plus), "Audio"),
    h("span.sep"), split, del, ripple,
    h("button.tool", { title: "Drop a marker at the playhead (marker add)", onclick: () => vm.addMarker() }, icon(ICONS.marker), "Marker"),
    h("span.grow"), zoomOut, zoomIn);

  // ── the heads: one per track, aligned with its lane ──
  const heads = h("div.tl-heads-inner");
  keyed(heads, () => vm.tracks.value, (t) => t.id, (t) => h("div.tl-head", h("span.tl-kind"), h("span.tl-name")), (el, t) => {
    el.style.transform = `translateY(${t.row * TRACK_H}px)`;
    el.querySelector(".tl-kind").textContent = t.audio ? "A" : "V";
    el.querySelector(".tl-name").textContent = t.name;
    el.classList.toggle("audio", t.audio);
    el.classList.toggle("muted", t.mute);
  });
  const headsBox = h("div.tl-heads", h("div.tl-heads-corner"), h("div.tl-heads-clip", heads));

  // ── the content: ruler, lanes, clips, markers, playhead ──
  const ruler = h("div.tl-ruler");
  const labels = h("div.tl-labels");
  const markersLayer = h("div.tl-markers");
  ruler.append(labels, markersLayer);
  const lanes = h("div.tl-lanes");
  keyed(lanes, () => vm.tracks.value, (t) => t.id, () => h("div.tl-lane"), (el, t) => {
    el.style.transform = `translateY(${t.row * TRACK_H}px)`;
    el.classList.toggle("alt", t.row % 2 === 1);
    el.classList.toggle("audio", t.audio);
  });
  const clipsLayer = h("div.tl-clips");
  const rowOf = () => new Map(vm.tracks.value.map((t) => [t.id, t.row]));
  keyed(clipsLayer, () => {
    const rows = rowOf();
    return vm.clips.value.filter((c) => rows.has(c.track)).map((c) => ({ ...c, row: rows.get(c.track) }));
  }, (c) => c.id, (c) => {
    const el = h("button.tl-clip", { onclick: (e) => { e.stopPropagation(); vm.selectClip(c.id); } }, h("span.tl-clip-name"), h("span.tl-clip-src.mono"));
    return el;
  }, (el, c) => {
    el.__item = c;
    const p = pps();
    el.style.transform = `translate(${c.at * p}px, ${c.row * TRACK_H + 3}px)`;
    el.style.width = `${Math.max(2, c.duration * p)}px`;
    el.querySelector(".tl-clip-name").textContent = c.name;
    el.querySelector(".tl-clip-src").textContent = c.srcName || c.src;
    el.classList.toggle("audio", c.audio);
    el.classList.toggle("offline", c.offline);
    el.classList.toggle("nested", c.nested);
    el.title = `${c.name} · ${c.srcName || c.src} · at ${c.at.toFixed(2)} s, ${c.duration.toFixed(2)} s${c.offline ? " · offline" : ""}`;
  });
  effect(() => {   // the selection ring slides with the model's selectedClip (every page sees the same)
    const id = vm.selectedClipId.value;
    for (const el of clipsLayer.children) el.classList.toggle("selected", el.__key === id);
  });
  const playhead = h("div.tl-playhead");
  const content = h("div.tl-content", lanes, clipsLayer, playhead);
  const scroll = h("div.tl-scroll.scroll", ruler, content);

  // sizes follow the model and the zoom
  bindEffect(content, () => {
    const p = pps();
    const w = Math.max(scroll.clientWidth || 600, vm.duration.value * p + TAIL_PX);
    content.style.width = ruler.style.width = `${w}px`;
    content.style.height = `${Math.max(1, vm.tracks.value.length) * TRACK_H}px`;
    content.style.setProperty("--sec", `${p}px`);
    ruler.style.setProperty("--sec", `${p}px`);
    // labels for the whole width (a long film at a far zoom is still a few hundred)
    const step = labelStep(p), n = Math.ceil(w / (step * p)) + 1;
    labels.replaceChildren(...Array.from({ length: Math.min(n, 2000) }, (_, i) =>
      h("span.tl-label", { style: { transform: `translateX(${i * step * p}px)` } }, label(i * step))));
    // keyed lists re-run their update when the zoom moves (positions are in pixels)
    for (const el of clipsLayer.children) if (el.__item) {
      const c = el.__item;
      el.style.transform = `translate(${c.at * p}px, ${c.row * TRACK_H + 3}px)`;
      el.style.width = `${Math.max(2, c.duration * p)}px`;
    }
  });
  keyed(markersLayer, () => vm.markers.value, (k) => k.id, (k) => h("span.tl-marker", { title: k.name + (k.note ? " — " + k.note : "") }),
        (el, k) => { el.__item = k; el.style.transform = `translateX(${k.at * pps()}px)`; });
  effect(() => { const p = pps(); for (const el of markersLayer.children) if (el.__item) el.style.transform = `translateX(${el.__item.at * p}px)`; });

  // the playhead: extrapolated between `transport` pushes while playing; eased when it jumps
  let anchor = { t: 0, at: performance.now(), playing: false, rate: 1 };
  const x = ease((v) => { playhead.style.transform = `translateX(${v}px)`; }, 160);
  effect(() => {
    const t = vm.transport.value;
    anchor = { t: t.playhead, at: performance.now(), playing: t.playing, rate: t.shuttle || 1 };
  });
  let follow = true;
  const tick = () => {
    const t = anchor.playing ? anchor.t + ((performance.now() - anchor.at) / 1000) * anchor.rate : anchor.t;
    const px = Math.max(0, t) * pps();
    if (anchor.playing) x.jump(px); else x.set(px);
    if (anchor.playing && follow && (px > scroll.scrollLeft + scroll.clientWidth - 40 || px < scroll.scrollLeft)) scroll.scrollLeft = Math.max(0, px - 80);
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);

  // ── pointer: seek / scrub / select / zoom ──
  const timeAt = (clientX) => Math.max(0, (clientX - content.getBoundingClientRect().left) / pps());
  content.addEventListener("click", (e) => { if (e.target === content || e.target.classList.contains("tl-lane")) vm.seek(timeAt(e.clientX)); });
  let last = 0;
  drag(ruler, {
    onStart: (e) => { last = timeAt(e.clientX); vm.scrub(last); },
    onMove: (e) => { last = timeAt(e.clientX); vm.scrub(last); },
    onEnd: () => vm.seek(last),
  });
  scroll.addEventListener("wheel", (e) => {
    if (!(e.ctrlKey || e.metaKey)) return;
    e.preventDefault();
    const before = (e.clientX - content.getBoundingClientRect().left) / pps();
    vm.view.pxPerSec.value = Math.min(800, Math.max(4, pps() * (e.deltaY < 0 ? 1.15 : 1 / 1.15)));
    requestAnimationFrame(() => { scroll.scrollLeft = Math.max(0, before * pps() - (e.clientX - scroll.getBoundingClientRect().left)); });
  }, { passive: false });
  scroll.addEventListener("scroll", () => { heads.style.transform = `translateY(${-scroll.scrollTop}px)`; follow = true; });
  scroll.addEventListener("pointerdown", () => { follow = false; });

  const empty = h("div.tl-empty");
  bindClass(empty, "on", () => vm.has.value && !vm.clips.value.length);
  bindEffect(empty, () => {
    empty.textContent = !vm.tracks.value.length ? "No tracks yet — add a video track, then place a source from the rack (Cut › Sources)."
                                                 : "No clips yet — place a source from the rack at the playhead (Cut › Sources).";
  });
  bindStyle(headsBox, "--ruler-h", () => `${RULER_H}px`);
  return h("div.timeline", toolbar, h("div.tl-body", headsBox, scroll, empty));
}
