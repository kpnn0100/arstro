/*
 * Interstellar — cut/menus.js: the menus App.cpp fills for the Cut timeline's right-clicks.
 *
 *   openClipContext(vm, ctx, clipId, x, y)       App::openClipContext (App.cpp:870-907): selects the clip,
 *                                                then Split at Playhead (only when the playhead is inside
 *                                                it), Copy, Cut, Paste at Playhead (with a clipboard),
 *                                                Delete, Ripple Delete; for a picture clip Add Dissolve to
 *                                                Next (a TOUCHING next clip on its track), Speed 50/100/200 %
 *                                                (not the current), Open Timeline <src> (nested) or Show
 *                                                Source in Grade
 *   openLaneContext(vm, ctx, trackId, t, x, y)   App::openLaneContext (App.cpp:910-933): Paste Here, Add Marker
 *                                                Here, Target for Insert / Overwrite, Place Timeline Here…,
 *                                                Add Video Track, Add Audio Track
 * x, y in ROOT logical px. Every item is one command line, the App's own.
 */
import { q, seconds, num } from "../kit/cmd.js";
import { openPlaceTimelineMenu } from "../edit/menus.js";

const clipLen = (c) => (c.duration > 0 ? c.duration : c.speed > 0 ? (c.out - c.in) / c.speed : c.out - c.in);

export function openClipContext(vm, ctx, id, x, y) {
  const m = vm.model.peek();
  if (!m) return;
  const clip = (m.clips || []).find((c) => c.id === id);
  if (!clip) return;
  if (m.selectedClip !== id) vm.cmd("clip select " + q(id));
  const fps = m.fps > 0 ? m.fps : 24, qid = q(id), run = (l) => vm.cmd(l), items = [];
  const ph = vm.playhead.peek();
  if (ph > clip.at + 1e-9 && ph < clip.at + clipLen(clip) - 1e-9)
    items.push({ label: "Split at Playhead  (S)", action: () => run("clip split " + qid + " --at " + seconds(ph, fps)) });
  items.push({ label: "Copy  (Ctrl+C)", action: () => run("clip copy " + qid) });
  items.push({ label: "Cut  (Ctrl+X)", action: async () => { if ((await vm.cmd("clip copy " + qid)).ok) run("clip delete " + qid); } });
  if (m.hasClipClipboard) items.push({ label: "Paste at Playhead  (Ctrl+V)", action: () => run("clip paste") });
  items.push({ label: "Delete  (Del)", action: () => run("clip delete " + qid) });
  items.push({ label: "Ripple Delete  (Shift+Del)", action: () => run("clip delete " + qid + " --ripple") });
  if (!clip.audio) {
    const next = (m.clips || []).find((x2) => x2.track === clip.track && x2.id !== clip.id && Math.abs(x2.at - (clip.at + clipLen(clip))) < 0.5 / fps);
    if (next) items.push({ label: "Add Dissolve to Next", action: () => run("transition add --between " + qid + "," + q(next.id) + " --dur 0.5") });
    for (const sp of [0.5, 1, 2])
      if (Math.abs(clip.speed - sp) > 1e-9) items.push({ label: "Speed " + Math.round(sp * 100) + "%", action: () => run("clip speed " + qid + " " + num(sp)) });
    if (clip.nested) items.push({ label: "Open Timeline " + clip.srcName, action: () => run("timeline open " + q(clip.srcName)) });
    else if (clip.srcName) items.push({ label: "Show Source in Grade", action: () => { run("rack select " + q(clip.srcName)); vm.setTab(0); } });
  }
  ctx.menu.open(items, x, y);
}

export function openLaneContext(vm, ctx, track, t, x, y) {
  const m = vm.model.peek();
  if (!m) return;
  const fps = m.fps > 0 ? m.fps : 24, run = (l) => vm.cmd(l), items = [];
  if (m.hasClipClipboard)
    items.push({ label: "Paste Here", action: () => run("clip paste --at " + seconds(t, fps) + (track ? " --track " + q(track) : "")) });
  items.push({ label: "Add Marker Here  (M at the playhead)", action: () => run("marker add " + vm.freshMarkerName() + " --at " + seconds(t, fps)) });
  const video = (m.tracks || []).some((tk) => tk.id === track && !tk.audio);
  if (video && track !== m.targetTrack) items.push({ label: "Target for Insert / Overwrite", action: () => run("edit target " + q(track)) });
  const placeable = (m.timelines || []).some((tl) => tl.placeable);
  if (video && placeable) items.push({ label: "Place Timeline Here...", action: () => openPlaceTimelineMenu(vm, ctx, track, t, x, y) });
  items.push({ label: "Add Video Track", action: () => run("track add --kind video") });
  items.push({ label: "Add Audio Track", action: () => run("track add --kind audio") });
  ctx.menu.open(items, x, y);
}
