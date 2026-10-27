/*
 * Solaris — vm/dnd.js: the drag that crosses widgets — an item of the BROWSER dropped on the LANES, or a sample on a
 * SAMPLER's window (ProjectScreen.cpp:100-164, R-BROWSE-3, R-EDM-8). ProjectScreen owns it natively; here it is
 * `vm.dnd`, so the three widgets never import each other. Points are ROOT logical px (ctx.screenPoint(e)).
 *
 *   An Item is Browser::Item: {kind: audio | midi | instrument | effect | pattern | preset | …, label, value, note,
 *   strip (a pattern's newest strip, R-BROWSE-4), role (a preset's: instrument | effect)}.
 *
 *   the BROWSER (project/browser.js) reports, during its own drag gesture:
 *     vm.dnd.move(item, pt)      every move — hints and the ghost follow
 *     vm.dnd.drop(item, pt)      the release → ONE command line (or nothing, said)  -> Promise<boolean>
 *     vm.dnd.cancel()            the drag ended without a drop (Escape, pointercancel)
 *     vm.dnd.activate(item)      a double-click: the same at the time bar, on a new lane  -> Promise<boolean>
 *   the TIMELINE (timeline/timeline.js) provides, while mounted (set it to null on unmount):
 *     vm.dnd.lanes = { probe(pt) -> {beat, row} | null,   // over the lanes (not the header column, not above top()):
 *                                                          // the SNAPPED beat (its snap()) and rowAt(y) — rowCount() =
 *                                                          // below the last lane (a new one); null elsewhere
 *                      rowCount(), rowLane(row) -> ln | "", rowTrack(row) -> strip | "" }
 *     and draws vm.dnd.hint: Signal<{on, beat, row, label}>          (Timeline::setDropHint; label is stored words)
 *   the WINDOW LAYER (windows/layer.js) provides vm.dnd.windows = { samplerAt(pt) -> dv | "" } (the topmost sampler
 *     window under pt) and lights vm.dnd.sampleHint: Signal<dv | "">  (WindowLayer::setSampleHint)
 *   the SCREEN draws the ghost: vm.dnd.ghost: Signal<{label, x, y} | null> (eased in and out, over everything)
 */
import { signal } from "../core/signal.js";
import { q, beatText } from "../kit/format.js";
import { say } from "./words.js";

export function createDnd(vm) {
  const hint = signal({ on: false, beat: 0, row: -1, label: "" }, (a, b) => a.on === b.on && a.beat === b.beat && a.row === b.row && a.label === b.label);
  const sampleHint = signal("");
  const ghost = signal(null, (a, b) => a === b || (!!a && !!b && a.label === b.label && a.x === b.x && a.y === b.y));
  const dnd = { hint, sampleHint, ghost, lanes: null, windows: null };

  const beats = (b) => beatText(b);
  function over(pt) {
    const hit = dnd.lanes ? dnd.lanes.probe(pt) : null;
    return hit && hit.row >= 0 ? hit : null;
  }

  /** ProjectScreen::place — the drop as ONE command line. */
  function place(it, beat, row) {
    const L = dnd.lanes;
    const count = L ? L.rowCount() : 0;
    const lane = L && row < count ? L.rowLane(row) : "";
    // below the last lane: a NEW lane, said explicitly; onto another instrument's TRACK a sample or a new instrument
    // cannot play through it: a new lane too (R-LANE-3)
    const track = !!L && row < count && !!L.rowTrack(row);
    const onLane = !lane || track ? " --lane new" : " --lane " + q(lane);
    const send = (line) => vm.cmd(line).then((r) => !!r);
    if (it.kind === "audio") return send("clip add --src " + q(it.value) + " --at " + beats(beat) + onLane);
    if (it.kind === "midi") return send("pattern import " + q(it.value) + (track ? " --lane " + q(lane) : " --instrument synth") + " --at " + beats(beat));
    if (it.kind === "instrument") return send("clip add --instrument " + it.value + " --at " + beats(beat) + " --length 4" + onLane);
    if (it.kind === "pattern") {
      if (track) return send("clip add --pattern " + it.value + " --lane " + q(lane) + " --at " + beats(beat));
      if (!it.strip) {
        vm.toast(say("Drop {1} on an instrument's track — nothing plays it yet.", vm.shown(it.label)));
        return Promise.resolve(false);
      }
      return send("clip add --pattern " + it.value + " --strip " + it.strip + " --at " + beats(beat) + (!lane ? " --lane new" : " --lane " + q(lane)));
    }
    if (it.kind === "preset" && it.role === "instrument")
      return send("clip add --instrument " + it.value + " --preset " + q(it.label) + " --at " + beats(beat) + " --length 4" + onLane);
    if (it.kind === "preset") { vm.toast("A preset of an effect loads onto its device — its window's Presets."); return Promise.resolve(false); }
    if (it.kind === "effect") vm.toast("An effect goes on a mixer strip — drop it in the mixer.");
    return Promise.resolve(false);
  }

  dnd.move = (it, pt) => {
    const sampler = it.kind === "audio" && dnd.windows ? dnd.windows.samplerAt(pt) || "" : "";
    sampleHint.value = sampler;
    const hit = sampler ? null : over(pt);
    const newLane = !!hit && hit.row >= (dnd.lanes ? dnd.lanes.rowCount() : 0);
    hint.value = {
      on: !!hit && it.kind !== "effect" && !(it.kind === "preset" && it.role !== "instrument"),
      beat: hit ? hit.beat : 0, row: hit ? hit.row : -1,
      label: it.label + (newLane ? "  ·  " + say("new lane") : ""),
    };
    ghost.value = { label: it.label, x: pt.x, y: pt.y };
  };
  dnd.cancel = () => {
    hint.value = { on: false, beat: 0, row: -1, label: "" };
    sampleHint.value = "";
    ghost.value = null;
  };
  dnd.drop = (it, pt) => {
    dnd.cancel();
    const sampler = it.kind === "audio" && dnd.windows ? dnd.windows.samplerAt(pt) || "" : "";
    if (sampler) return vm.cmd("set " + sampler + ".sample=" + q(it.value)).then((r) => !!r); // ONE line
    const hit = over(pt);
    return hit ? place(it, hit.beat, hit.row) : Promise.resolve(false);
  };
  dnd.activate = (it) => place(it, Math.floor(vm.position.peek() * 4) / 4, dnd.lanes ? dnd.lanes.rowCount() : 0);
  dnd.place = place;
  return dnd;
}
