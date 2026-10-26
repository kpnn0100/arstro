/*
 * Interstellar — monitor.js: the picture and the transport under it (the window's Monitor and
 * Transport). The picture is THIS page's (the view-model asks for it: the timeline at the playhead
 * on Cut and Deliver, the Grade target's reference frame on Grade) - it arrives as a JPEG from the
 * core and cross-dissolves in (motion::kDissolveMs). The monitor tells the view-model its size, so
 * the picture is rendered at the edge it is shown at, never bigger.
 */
import { bindText, bindClass, bindEffect } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { h, icon, ICONS } from "./util.js";
import { timecode } from "../vm/edit.js";

export function monitor(vm) {
  // two images, the new one dissolving over the old, so a frame never pops
  const a = h("img.pic", { alt: "" }), b = h("img.pic", { alt: "" });
  let front = a;
  effect(() => {
    const f = vm.session.frame.value;
    if (!f) return;
    const next = front === a ? b : a;
    next.onload = () => { next.classList.add("on"); front.classList.remove("on"); front = next; };
    next.src = f.url;
  });
  const empty = h("div.pic-empty");
  bindClass(empty, "on", () => vm.pictureEmpty.value);
  bindText(empty, () => (vm.view.tab.value === "grade" ? (vm.gradeTarget.value ? "This source has no picture yet" : "Select a source in the rack")
                                                        : "Nothing on the timeline at the playhead"));
  const stage = h("div.pic-stage", a, b, empty);
  const ro = new ResizeObserver(() => {
    const r = stage.getBoundingClientRect();
    const edge = Math.round(Math.min(1920, Math.max(r.width, r.height) * Math.min(2, window.devicePixelRatio || 1)) / 32) * 32;
    if (edge >= 64 && edge !== vm.view.monitorEdge.peek()) vm.view.monitorEdge.value = edge;
  });
  ro.observe(stage);

  // ── the transport ──
  const playIcon = icon(ICONS.play, "play"), pauseIcon = icon(ICONS.pause, "pause");
  const playBtn = h("button.tbtn.play-btn", { title: "Play / pause (Space)", onclick: () => vm.play() }, playIcon, pauseIcon);
  bindClass(playBtn, "playing", () => vm.transport.value.playing);
  const tc = h("span.tc.mono", { title: "the playhead (HH:MM:SS:FF)" });
  const dur = h("span.tc-dur.mono");
  bindText(dur, () => "/ " + timecode(vm.transport.value.duration, vm.fps.value));
  // the clock between `transport` pushes: extrapolated while playing, so the numbers run at the frame
  // rate and land where the service says
  let anchor = { t: 0, at: performance.now(), playing: false, rate: 1 };
  effect(() => {
    const t = vm.transport.value;
    anchor = { t: t.playhead, at: performance.now(), playing: t.playing, rate: t.shuttle || 1 };
  });
  const scrub = h("input.scrub", { type: "range", min: 0, max: 1, step: 0.0001, value: 0, title: "scrub (playhead <t>)" });
  let scrubbing = false;
  const now = () => (anchor.playing ? anchor.t + ((performance.now() - anchor.at) / 1000) * anchor.rate : anchor.t);
  const tick = () => {
    const d = vm.transport.peek().duration || 0;
    const t = Math.min(d || Infinity, Math.max(0, now()));
    const text = timecode(t, vm.fps.peek());
    if (tc.textContent !== text) tc.textContent = text;
    if (!scrubbing) scrub.value = String(d > 0 ? t / d : 0);
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
  scrub.addEventListener("input", () => { scrubbing = true; vm.scrub(Number(scrub.value) * (vm.transport.peek().duration || 0)); });
  scrub.addEventListener("change", () => { scrubbing = false; vm.seek(Number(scrub.value) * (vm.transport.peek().duration || 0)); });
  bindEffect(scrub, () => { scrub.disabled = !(vm.transport.value.duration > 0); });

  const bar = h("div.transport",
    h("button.tbtn", { title: "Previous cut (playhead prev-cut)", onclick: () => vm.prevCut() }, icon(ICONS.prevCut)),
    h("button.tbtn", { title: "One frame back (←)", onclick: () => vm.step(-1) }, icon(ICONS.stepBack)),
    playBtn,
    h("button.tbtn", { title: "One frame on (→)", onclick: () => vm.step(1) }, icon(ICONS.stepFwd)),
    h("button.tbtn", { title: "Next cut (playhead next-cut)", onclick: () => vm.nextCut() }, icon(ICONS.nextCut)),
    h("div.tc-wrap", tc, dur),
    scrub);
  return h("div.monitor", stage, bar);
}
