/*
 * Solaris — topbar.js: the song bar (the window's SongBar): the wordmark, the song, the transport,
 * the tempo, undo / redo / save / close, and which session this page shows.
 */
import { bindText, bindClass, bindEffect } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { h, icon, ICONS, barBeat } from "./util.js";

export function topbar(vm) {
  const playIcon = icon(ICONS.play, "play");
  const stopIcon = icon(ICONS.stop, "stop");
  const playBtn = h("button.tbtn.play-btn", { title: "Play / stop (transport play | transport stop)", onclick: () => vm.play() }, playIcon, stopIcon);
  bindClass(playBtn, "playing", () => vm.transport.value.playing);

  const pos = h("span.position.mono", { title: "bar.beat.sixteenth — click the ruler to seek" });
  // the clock between `transport` pushes: extrapolated from the tempo while playing, so the numbers
  // run at the frame rate and land where the service says (the view's own eased copy)
  let anchor = { pos: 0, at: performance.now(), playing: false };
  effect(() => {
    const t = vm.transport.value;
    anchor = { pos: t.position, at: performance.now(), playing: t.playing };
  });
  const tick = () => {
    const s = vm.song.peek();
    const bpm = s ? s.bpm : 120;
    const p = anchor.playing ? anchor.pos + ((performance.now() - anchor.at) / 1000) * (bpm / 60) : anchor.pos;
    const text = barBeat(p, vm.beatsPerBar.peek());
    if (pos.textContent !== text) pos.textContent = text;
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);

  const bpm = h("input.bpm.mono", { type: "number", min: 20, max: 999, step: 0.1, title: "Tempo (set project.bpm=…)" });
  bindEffect(bpm, () => {
    const s = vm.song.value;
    if (s && document.activeElement !== bpm) bpm.value = String(s.bpm);
  });
  bpm.addEventListener("change", () => { const v = parseFloat(bpm.value); if (v > 0) vm.setBpm(v); });
  bpm.addEventListener("keydown", (e) => { if (e.key === "Enter") bpm.blur(); });

  const name = h("span.song-name");
  bindText(name, () => (vm.song.value ? vm.song.value.name : ""));
  const dirty = h("i.dirty", { title: "unsaved changes" });
  bindClass(dirty, "on", () => !!(vm.song.value && vm.song.value.dirty));

  const undo = h("button.tbtn", { onclick: () => vm.undo() }, icon(ICONS.undo));
  const redo = h("button.tbtn", { onclick: () => vm.redo() }, icon(ICONS.redo));
  bindEffect(undo, () => { const s = vm.song.value; undo.disabled = !(s && s.undo); undo.title = s && s.undo ? "Undo " + s.undo : "Nothing to undo"; });
  bindEffect(redo, () => { const s = vm.song.value; redo.disabled = !(s && s.redo); redo.title = s && s.redo ? "Redo " + s.redo : "Nothing to redo"; });
  const save = h("button.tbtn", { title: "Save (project save)", onclick: () => vm.save() }, icon(ICONS.save));
  const close = h("button.tbtn", { title: "Close the song (project close)", onclick: () => vm.close() }, icon(ICONS.close));

  const presence = h("span.presence", h("i"), h("span"));
  bindClass(presence, "live", () => vm.session.status.value === "running");
  bindText(presence.lastChild, () => {
    const s = vm.session.status.value;
    const p = vm.session.presence.value;
    if (s !== "running") return s;
    return (p.session || "main") + (p.clients > 1 ? ` · ${p.clients} pages` : "");
  });

  const songOnly = h("div.song-controls",
    h("div.song-title", name, dirty),
    h("div.transport", h("button.tbtn", { title: "To the start (transport seek 0)", onclick: () => vm.rewind() }, icon(ICONS.rewind)),
      playBtn, pos, h("label.bpm-wrap", bpm, h("span", "bpm"))),
    h("div.actions", undo, redo, save, close));
  bindClass(songOnly, "on", () => vm.screen.value === "project");

  return h("header.topbar",
    h("div.brand", h("img", { src: "icon.svg", alt: "" }), h("span.wordmark", "solaris")),
    songOnly,
    presence);
}
