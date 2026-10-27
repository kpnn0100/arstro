/*
 * Interstellar — cut/timeline.js: the Cut tab's deck. Ports app/widgets/Timeline.{h,cpp}.
 *
 * ONE time origin: every time ↔ x (ruler, lanes, clips, transitions, markers, playhead, a drag's snap,
 * the key lane) goes through toX / toT, which read shell::headerWidth and the LIVE eased zoom (px/s)
 * and scroll. Clips are drawn by PROVENANCE: local plain, inherited dimmed (body × 0.55, "inherited"),
 * overridden with an accent edge and a 3 px bar ("overridden here"), dangling in destructive and
 * hatched ("base deleted its target"); offline is hatched in muted ("offline — media missing"); a
 * nested clip shows a second edge; an audio clip its waveform (the adapter's `peaks`, fading in when
 * it lands). The playhead is destructive.
 *
 * Motion (Timeline.cpp:740-880): zoom eases 220 ms, ANCHORED (the time under the pointer - or the
 * playhead for the ± buttons - stays put; the scroll is re-derived from the eased zoom every frame);
 * the wheel scrolls eased; a clip whose model place changes eases there (220 ms); a new clip fades
 * in, a deleted one fades out as a ghost; a restyle cross-fades the two looks; a track added or
 * dropped slides the lanes; the In/Out band, the target bar, the snap guide, the drop ghost and the
 * empty hint fade; the preview-cache bar's seconds cross-fade between stale / cached / building.
 *
 * Gestures (Timeline.cpp:395-640): a drag is direct manipulation that keeps its GRAB OFFSET (gotcha
 * 8), snaps within 8 px to 0, the playhead, markers and every other clip's edges (the accent guide
 * shows the edge caught), may change to a track of the same kind; on release ONE line:
 *   `clip move <c> --at <t> [--track <trk>]` · `clip trim <c> --in|--out <t>` (source time)
 *   Alt on a cut between two touching clips: `clip roll <left> --at <t>` · Alt on a body:
 *   `clip slip <c> --by <dt>` (the hint chip says "roll" / "slip +0.50 s · in …" while held)
 * A refused drop eases back to the model. A click selects (`clip select`), a click on the ruler or an
 * empty lane moves the playhead (`playhead <t>`, a ruler drag scrubs - notifies, the last a command);
 * Ctrl+wheel zooms ×1.25 about the pointer, ± step ×1.5 about the playhead; right-clicks open
 * cut/menus.js. A source dragged from the bin (vm.view.binDrag) shows a ghost on the video lane under
 * the pointer - "new video track" below the tracks or when there is none, a refusal over an audio
 * lane - and a drop calls vm.dropSource.
 *
 * The ANIMATION section below the tracks (KeyLane, R-ANIM-9) is ANOTHER module, cut/keylane.js,
 * mounted here with ctx.slot in the section's rect; this widget owns that rect (folded to its 22.75 px
 * header or open to keyLaneH(), eased 220 ms), the resize band on its top edge (`settings set
 * keyLaneHeight=<px>` on release, R-ANIM-8), and provides vm "timeline":
 *   { toX(t), toT(x), colW: Signal<number>, playhead: Signal<number> (the SHOWN playhead),
 *     keyOpen: Signal<number> (live eased 0..1), axis: Signal<number> (bumps on zoom/scroll/size),
 *     keysShown: Signal<boolean>, toggleKeys(), resetView(), relayout() }
 * and reads vm "keyLane" ({contentHeight()}) for the section's open height (until it exists: the
 * empty key lane's height, KeyLane::contentH with nothing marked). The section's HEADER is the key lane's
 * (KeyLane draws it natively).
 */
import { h, own } from "../core/dom.js";
import { signal, effect } from "../core/signal.js";
import { reducedMotion } from "../core/motion.js";
import { useCss } from "../kit/css.js";
import { tok, SHELL } from "../kit/tokens.js";
import { q, seconds, num, timecode } from "../kit/cmd.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { EasedScroll, wheelPx } from "../kit/easedscroll.js";
import { Painter, canvasSvg, col, css, fade, lerp, brighten, whiteA, primaryA, hoverWash, Anim, HoverFade, frameLoop,
         localPoint, inRect, Ease, F } from "../kit/paint.js";
import { openClipContext, openLaneContext } from "./menus.js";

// Timeline.h:83-87, .cpp:12-13, KeyLane.h:43
const ZOOM_STEP = 1.5, SNAP_PX = 8, EDGE_PX = 6, MAX_PPS = 480, CLIP_INSET_Y = 3, FOLLOW_STEP_S = 0.5, KEY_LANE_MIN_H = 80, KEY_HEAD_H = 22.75;
const STEPS = [1 / 24, 2 / 24, 6 / 24, 12 / 24, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800];
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const clipDur = (c) => (c.duration > 0 ? c.duration : c.speed > 0 ? (c.out - c.in) / c.speed : c.out - c.in);
const styleKey = (c) => c.id + "|" + c.provenance + (c.offline ? "|o" : "");
function tickLabel(t, step, fps) {                   // Timeline.cpp:17-31
  const fr = Math.round(t * fps), ifps = Math.max(1, Math.round(fps)), s = Math.floor(fr / ifps), f = fr % ifps;
  const p2 = (n) => String(n).padStart(2, "0");
  if (step >= 1) return s >= 3600 ? `${Math.floor(s / 3600)}:${p2(Math.floor(s / 60) % 60)}:${p2(s % 60)}` : `${Math.floor(s / 60)}:${p2(s % 60)}`;
  return `${s}:${p2(f)}`;
}
const mkAnim = () => ({ data: null, at: new Anim(0), dur: new Anim(0), lane: new Anim(0), alpha: new Anim(0), atT: 0, durT: 0, laneT: 0, alphaT: 1,
                        atL: 0, durL: 0, laneL: 0, alphaL: 1, placed: false, gone: false, fadeIn: false });

export function mount(host, vm, ctx) {
  useCss("css/cut/timeline.css");
  host.classList.add("tl");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());
  const HW = () => SHELL.headerWidth, RH = () => SHELL.rulerH, TH = () => SHELL.trackH;

  let W = 0, H = 0;
  // model mirror
  let tracks = [], transitions = [], markers = [], modelClips = new Map(), selected = "", duration = 0, fps = 24, playhead = 0, playing = false;
  let markIn = -1, markOut = -1, bandIn = 0, bandOut = 0, bandOn = false, bandApplied = false, targetTrack = "", targetApplied = "";
  let cacheSegSeconds = 1, everBound = false, populated = false, emptyWanted = true, emptyApplied = true, emptyInit = false;
  const anims = new Map(), trackAnims = new Map(), targetAmt = new Map(), waves = new Map(), cacheSegs = [], clipOrder = [];
  const bandAmt = new Anim(0), emptyAmt = new Anim(1), guideAmt = new Anim(0), dropAmt = new Anim(0), shownPlayhead = new Anim(0);
  let playheadLast = -1, guideWanted = false, guideApplied = false, guideTime = 0;
  // zoom + scroll
  const pps = new Anim(0);
  let ppsTarget = 0, ppsLast = 0, ppsInit = false, anchored = false, anchorTime = 0, anchorX = 0;
  const scroll = new Anim(0);
  let scrollTarget = 0, scrollLast = 0;
  const vscroll = new EasedScroll(() => loop.kick());
  // the animation section
  let keysWanted = true, keyLaneApplied = true, laneHTarget = 140, laneHLast = -1, laneDragging = false;
  const laneH = new Anim(140), keyLane = new Anim(1);
  // input
  const hover = new HoverFade(), sel = new HoverFade(), ui = new HoverFade();
  let drag = { kind: "none" };
  // the drop ghost
  let dropWanted = false, dropApplied = false, dropLabel = "", dropTrack = "", dropT = 0, dropDur = 0, dropLane = 0, dropNewTrack = false, dropBad = false;

  // ---------------------------------------------------------------- the provider the key lane reads
  const axis = signal(0), shownPh = signal(0), keyOpen = signal(1), keysShownSig = signal(true), colW = signal(SHELL.headerWidth);
  const api = {
    toX: (t) => toX(t), toT: (x) => toT(x), colW, playhead: shownPh, keyOpen, axis, keysShown: keysShownSig,
    toggleKeys: () => { keysWanted = !keysWanted; keysShownSig.value = keysWanted; loop.kick(); },
    resetView: () => { resetView(); loop.kick(); },
    relayout: () => loop.kick(),                    // the key lane's height moved: re-lay the section
  };
  vm.provide("timeline", api);

  // ---------------------------------------------------------------- the key lane's host
  const klHost = h("div.tl-keys");
  host.append(klHost);
  const klBox = signal({ x: 0, y: 0, w: 0, h: 0 }, (a, b) => a.x === b.x && a.y === b.y && a.w === b.w && a.h === b.h);
  if (ctx.slot) ctx.slot(klHost, "cut/keylane.js", { name: "KeyLane", box: klBox });

  // ---------------------------------------------------------------- bind (Timeline::bind)
  function bind() {
    const m = vm.model.value;
    if (!m) return;
    const tr = vm.transport.value || {};
    vm.session.peaksVersion.value;                   // an envelope landing re-binds
    for (const c of m.clips || [])
      if (c.audio && c.media && !waves.has(c.media)) {
        const got = vm.session.peaksNow(c.media);
        if (got && got.peaks && got.peaks.length) waves.set(c.media, { peaks: got.peaks, perSecond: got.perSecond || 100, amount: new Anim(0), placed: false });
      }
    fps = m.fps > 0 ? m.fps : 24;
    if (!laneDragging) laneHTarget = m.settings && m.settings.keyLaneHeight > 0 ? m.settings.keyLaneHeight : 140;
    duration = Math.max(0, tr.duration ?? m.duration ?? 0);
    playhead = tr.playhead ?? m.playhead ?? 0;
    playing = !!(tr.playing ?? m.playing);
    selected = m.selectedClip || "";
    transitions = m.transitions || [];
    markers = m.markers || [];
    markIn = m.markIn ?? -1; markOut = m.markOut ?? -1;
    bandOn = markIn >= 0 || markOut >= 0;
    if (bandOn) { bandIn = markIn >= 0 ? markIn : 0; bandOut = markOut >= 0 ? markOut : Math.max(bandIn, m.duration); }
    targetTrack = m.targetTrack || "";
    if (!targetTrack) {
      let best = 1 << 30;
      for (const tk of m.tracks || []) if (!tk.audio && tk.order < best) { best = tk.order; targetTrack = tk.id; }
    }
    cacheSegSeconds = m.previewCacheSegmentSeconds > 0 ? m.previewCacheSegmentSeconds : 1;
    const segs = m.previewCacheSegments || [];
    while (cacheSegs.length < segs.length) cacheSegs.push({ cached: new Anim(0), stale: new Anim(0), building: new Anim(0), cachedT: 0, staleT: 0, buildingT: 0, cachedL: 0, staleL: 0, buildingL: 0 });
    cacheSegs.forEach((cs, n) => { const st = n < segs.length ? segs[n] : 0; cs.cachedT = st === 1 ? 1 : 0; cs.staleT = st === 2 ? 1 : 0; cs.buildingT = st === 3 ? 1 : 0; });
    // display order: video top-down by descending order (V2 over V1), then audio
    const v = (m.tracks || []).filter((t) => !t.audio).sort((a, b) => b.order - a.order);
    const a = (m.tracks || []).filter((t) => t.audio).sort((x, y) => x.order - y.order);
    tracks = [...v, ...a];
    for (const ta of trackAnims.values()) { ta.gone = true; ta.alphaT = 0; }
    tracks.forEach((tk, i) => {
      let ta = trackAnims.get(tk.id);
      if (!ta) { ta = { data: tk, lane: new Anim(0), alpha: new Anim(0), laneT: i, laneL: 0, alphaT: 1, alphaL: 1, placed: false, gone: false }; trackAnims.set(tk.id, ta); }
      ta.data = tk; ta.gone = false; ta.laneT = i; ta.alphaT = 1;
    });
    modelClips = new Map();
    const live = new Set();
    for (const c of m.clips || []) {
      modelClips.set(c.id, c);
      if (!clipOrder.includes(c.id)) clipOrder.push(c.id);
      const key = styleKey(c);
      live.add(key);
      let an = anims.get(key);
      if (!an) {
        an = mkAnim();
        for (const o of anims.values())
          if (!o.gone && o.data && o.data.id === c.id && o.placed) {      // a new LOOK: from the old one's live geometry
            an.at.set(o.at.value); an.dur.set(o.dur.value); an.lane.set(o.lane.value);
            an.atL = an.at.value; an.durL = an.dur.value; an.laneL = an.lane.value;
            an.placed = true; an.fadeIn = true;
            break;
          }
        anims.set(key, an);
      }
      an.data = c; an.gone = false; an.atT = c.at; an.durT = clipDur(c); an.laneT = laneOfTrack(c.track); an.alphaT = 1;
    }
    for (const [k, an] of anims) if (!live.has(k)) { an.gone = true; an.alphaT = 0; }
    emptyWanted = !(m.clips || []).length;
    sel.setHovered(clipSlot(selected));
    everBound = true;
    loop.kick();
  }
  own(host, effect(bind));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));
  own(host, effect(() => {                          // the native shot's view (cut.pps / scroll / keysShown)
    const fx = vm.view.fixture.value;
    if (!fx || !fx.cut) return;
    const c = fx.cut;
    if (c.pps > 0) { pps.set(c.pps); ppsTarget = ppsLast = c.pps; ppsInit = true; }
    if (typeof c.scroll === "number") { scroll.set(c.scroll); scrollTarget = scrollLast = c.scroll; }
    if (typeof c.keysShown === "boolean") { keysWanted = keyLaneApplied = c.keysShown; keyLane.set(c.keysShown ? 1 : 0); keysShownSig.value = c.keysShown; }
    loop.kick();
  }));
  own(host, effect(() => {                          // a source dragged out of the bin (SourceBin::onDragSource)
    const d = vm.view.binDrag.value;
    if (!d) return;
    const b = ctx.box.peek(), pt = { x: d.x - b.x, y: d.y - b.y };
    if (d.phase === "move") dropHover(d.label, d.dur, pt);
    else if (d.phase === "cancel") dropCancel();
    else if (d.phase === "drop") { const r = dropAt(pt); if (r) vm.dropSource(d.bind, r.track, r.t); }
    loop.kick();
  }));

  // ---------------------------------------------------------------- the one origin + geometry
  function toX(t) { return HW() + t * pps.value - scroll.value; }
  function toT(x) { return (x - HW() + scroll.value) / Math.max(1e-6, pps.value); }
  const contentSeconds = () => duration + 2;
  // until cut/keylane.js provides its own: KeyLane::contentH() with nothing marked (kPad + kHintH + kPad, KeyLane.cpp:229-234)
  function keyLaneContentH() { const k = vm.provided("keyLane"); return k && k.contentHeight ? k.contentHeight() : 4.875 + 22.75 + 4.875; }
  function keyLaneHNow() {
    const room = H - RH() - TH() - KEY_HEAD_H;
    const asked = clamp(laneH.value, KEY_LANE_MIN_H, 600);
    return Math.max(0, Math.min(keyLaneContentH(), asked, room));
  }
  function animSectionRect() { const sh = KEY_HEAD_H + keyLaneHNow() * keyLane.value; return { x: 0, y: H - sh, w: W, h: sh }; }
  const keyLaneGrabRect = () => { const s = animSectionRect(); return { x: 0, y: s.y - 3, w: W, h: 6 }; };
  const rulerRect = () => ({ x: HW(), y: 0, w: Math.max(0, W - HW()), h: RH() });
  const lanesRect = () => ({ x: HW(), y: RH(), w: Math.max(0, W - HW()), h: Math.max(0, animSectionRect().y - RH()) });
  const laneTop = (laneLive) => RH() + laneLive * TH() - vscroll.value;
  const laneAtY = (y) => Math.floor((y - laneTop(0)) / TH());
  function laneOfTrack(id) { const i = tracks.findIndex((t) => t.id === id); return i < 0 ? 0 : i; }
  function clipSlot(id) { return clipOrder.indexOf(id); }
  function animFor(id) {
    let ghost = null;
    for (const an of anims.values()) if (an.data && an.data.id === id) { if (!an.gone) return an; ghost = an; }
    return ghost;
  }
  function liveAnim(id) { for (const an of anims.values()) if (!an.gone && an.data && an.data.id === id) return an; return null; }
  function clipRect(id) {
    const a = animFor(id);
    if (!a) return { x: 0, y: 0, w: 0, h: 0 };
    return { x: toX(a.at.value), y: laneTop(a.lane.value) + CLIP_INSET_Y, w: a.dur.value * pps.value, h: TH() - 2 * CLIP_INSET_Y };
  }
  const zoomOutRect = () => ({ x: 9.75, y: (RH() - 18) * 0.5, w: 18, h: 18 });
  const zoomInRect = () => ({ x: 9.75 + 20, y: (RH() - 18) * 0.5, w: 18, h: 18 });
  const fitPps = () => clamp(Math.max(60, lanesRect().w - 24) / Math.max(1, duration), 2, MAX_PPS);
  const maxScrollFor = (pp) => Math.max(0, contentSeconds() * pp - lanesRect().w);
  function zoomBy(factor, ax) {
    if (!ppsInit) return;
    const next = clamp(ppsTarget * factor, fitPps() * 0.5, MAX_PPS);
    if (Math.abs(next - ppsTarget) < 1e-9) return;
    anchorX = clamp(ax, HW(), W);
    anchorTime = toT(anchorX);
    anchored = true;
    ppsTarget = next;
  }
  function resetView() {
    if (!ppsInit) return;
    anchorX = HW(); anchorTime = 0; anchored = true; ppsTarget = fitPps();
  }
  function snap(start, dur, self, twoEdges) {       // Timeline.cpp:345-368
    const thr = SNAP_PX / Math.max(1e-6, pps.value);
    let best = thr, delta = 0, snapped = false, snapT = 0;
    const consider = (c) => {
      const d1 = c - start;
      if (Math.abs(d1) < best) { best = Math.abs(d1); delta = d1; snapped = true; snapT = c; }
      if (twoEdges) { const d2 = c - (start + dur); if (Math.abs(d2) < best) { best = Math.abs(d2); delta = d2; snapped = true; snapT = c; } }
    };
    consider(0);
    consider(playhead);
    for (const mk of markers) consider(mk.at);
    for (const [id, c] of modelClips) { if (id === self) continue; consider(c.at); consider(c.at + clipDur(c)); }
    return { v: start + delta, snapped, snapT };
  }
  function clipAt(pt) {                             // Timeline.cpp:372-393
    if (!inRect(pt, lanesRect())) return { id: "", edge: 0 };
    let hit = "";
    for (const an of anims.values()) {
      if (an.gone) continue;
      if (!inRect(pt, clipRect(an.data.id))) continue;
      hit = an.data.id;
      if (hit === selected) break;
    }
    if (!hit) return { id: "", edge: 0 };
    const r = clipRect(hit);
    let edge = 0;
    if (r.w >= 3 * EDGE_PX) { if (pt.x - r.x <= EDGE_PX) edge = -1; else if (r.x + r.w - pt.x <= EDGE_PX) edge = 1; }
    return { id: hit, edge };
  }
  function resetToModel(id) {
    const an = liveAnim(id), mc = modelClips.get(id);
    if (!an || !mc) return;
    an.atT = mc.at; an.durT = clipDur(mc); an.laneT = laneOfTrack(mc.track);
    an.atL = an.durL = an.laneL = -1e9;
  }

  // ---------------------------------------------------------------- the drop ghost (Timeline.cpp:646-715)
  function dropLocate(pt) {
    const t0 = Math.max(0, toT(pt.x)), s = snap(t0, dropDur, "", true);
    dropT = Math.max(0, s.v);
    guideWanted = s.snapped; if (s.snapped) guideTime = s.snapT;
    const videoLanes = tracks.filter((t) => !t.audio).length, lane = laneAtY(pt.y);
    dropNewTrack = false; dropBad = false;
    if (lane >= 0 && lane < tracks.length) { dropLane = lane; dropTrack = tracks[lane].id; if (tracks[lane].audio) { dropBad = true; dropTrack = "!"; } }
    else if (videoLanes === 0) { dropLane = 0; dropTrack = ""; dropNewTrack = true; }
    else { dropLane = tracks.length; dropTrack = ""; dropNewTrack = true; }
  }
  function dropHover(label, dur, pt) {
    dropLabel = label; dropDur = dur > 0 ? dur : 5;
    dropWanted = inRect(pt, lanesRect());
    if (dropWanted) dropLocate(pt); else guideWanted = false;
  }
  function dropAt(pt) {
    const inside = inRect(pt, lanesRect());
    if (inside) dropLocate(pt);
    dropWanted = false; guideWanted = false;
    if (!inside || dropBad) return null;
    return { track: dropTrack, t: dropT };
  }
  function dropCancel() { dropWanted = false; guideWanted = false; }
  function dragHint() {
    if (drag.kind === "roll") return "roll";
    if (drag.kind === "slip") return "slip " + (drag.value >= 0 ? "+" : "") + drag.value.toFixed(2) + " s";
    return "";
  }

  // ---------------------------------------------------------------- advance (Timeline::advance)
  function advance(now) {
    let moving = false;
    const ms = (n) => tok.ms(n);
    if (bandOn !== bandApplied) { bandAmt.animateTo(bandOn ? 1 : 0, ms("motion-hover"), Ease.EaseOutCubic, now); bandApplied = bandOn; }
    moving = bandAmt.update(now) || moving;
    if (targetTrack !== targetApplied) {
      if (targetApplied) { if (!targetAmt.has(targetApplied)) targetAmt.set(targetApplied, new Anim(0)); targetAmt.get(targetApplied).animateTo(0, ms("motion-select"), Ease.EaseOutCubic, now); }
      if (!targetAmt.has(targetTrack)) targetAmt.set(targetTrack, new Anim(0));
      const amt = targetAmt.get(targetTrack);
      if (!targetApplied) amt.set(1); else amt.animateTo(1, ms("motion-select"), Ease.EaseOutCubic, now);
      targetApplied = targetTrack;
    }
    for (const a of targetAmt.values()) moving = a.update(now) || moving;
    for (const w of waves.values()) {
      if (!w.placed) { w.amount.animateTo(1, ms("motion-cross-fade"), Ease.EaseOutCubic, now); w.placed = true; }
      moving = w.amount.update(now) || moving;
    }
    if (!ppsInit && everBound && lanesRect().w > 0) { const f = fitPps(); pps.set(f); ppsTarget = ppsLast = f; ppsInit = true; }
    if (ppsTarget !== ppsLast) { pps.animateTo(ppsTarget, ms("motion-zoom"), Ease.EaseOutCubic, now); ppsLast = ppsTarget; }
    const zooming = pps.animating;
    moving = pps.update(now) || moving;
    if (anchored) {
      const s = clamp(anchorTime * pps.value - (anchorX - HW()), 0, maxScrollFor(pps.value));
      scroll.set(s); scrollTarget = scrollLast = s;
      if (!zooming) anchored = false;
    } else {
      scrollTarget = clamp(scrollTarget, 0, maxScrollFor(pps.value));
      if (scrollTarget !== scrollLast) { scroll.animateTo(scrollTarget, ms("motion-scroll"), Ease.EaseOutCubic, now); scrollLast = scrollTarget; }
      moving = scroll.update(now) || moving;
    }
    if (!laneDragging && laneHTarget !== laneHLast) {
      if (laneHLast < 0) laneH.set(laneHTarget); else laneH.animateTo(laneHTarget, ms("motion-slide"), Ease.EaseOutCubic, now);
      laneHLast = laneHTarget;
    }
    moving = laneH.update(now) || moving;
    if (keysWanted !== keyLaneApplied) { keyLane.animateTo(keysWanted ? 1 : 0, ms("motion-slide"), Ease.EaseOutCubic, now); keyLaneApplied = keysWanted; }
    moving = keyLane.update(now) || moving;
    vscroll.setExtent(RH(), lanesRect().h, tracks.length * TH());
    for (const cs of cacheSegs)
      for (const k of ["cached", "stale", "building"]) {
        if (cs[k + "T"] !== cs[k + "L"]) { cs[k].animateTo(cs[k + "T"], ms("motion-cross-fade"), Ease.EaseOutCubic, now); cs[k + "L"] = cs[k + "T"]; }
        moving = cs[k].update(now) || moving;
      }
    for (const [key, an] of anims) {
      const dragged = drag.kind !== "none" && drag.kind !== "scrub" && !an.gone && drag.clip === an.data.id;
      if (an.fadeIn) { an.alpha.set(0); an.alpha.animateTo(1, ms("motion-cross-fade"), Ease.EaseOutCubic, now); an.alphaL = 1; an.fadeIn = false; }
      if (!an.placed) {
        an.at.set(an.atT); an.dur.set(an.durT); an.lane.set(an.laneT);
        an.atL = an.atT; an.durL = an.durT; an.laneL = an.laneT;
        if (populated && !reducedMotion()) { an.alpha.set(0); an.alpha.animateTo(1, ms("motion-cross-fade"), Ease.EaseOutCubic, now); } else an.alpha.set(1);
        an.alphaL = 1; an.placed = true;
      } else if (!dragged) {
        const cu = ms("motion-catch-up");
        if (an.atT !== an.atL) { an.at.animateTo(an.atT, cu, Ease.EaseOutCubic, now); an.atL = an.atT; }
        if (an.durT !== an.durL) { an.dur.animateTo(an.durT, cu, Ease.EaseOutCubic, now); an.durL = an.durT; }
        if (an.laneT !== an.laneL) { an.lane.animateTo(an.laneT, cu, Ease.EaseOutCubic, now); an.laneL = an.laneT; }
      }
      if (an.alphaT !== an.alphaL) { an.alpha.animateTo(an.alphaT, ms("motion-cross-fade"), Ease.EaseOutCubic, now); an.alphaL = an.alphaT; }
      for (const a of [an.at, an.dur, an.lane, an.alpha]) moving = a.update(now) || moving;
      if (an.gone && !an.alpha.animating && an.alpha.value <= 0.001) anims.delete(key);
    }
    for (const [id, ta] of trackAnims) {
      if (!ta.placed) {
        ta.lane.set(ta.laneT); ta.laneL = ta.laneT;
        if (populated && !reducedMotion()) { ta.alpha.set(0); ta.alpha.animateTo(1, ms("motion-cross-fade"), Ease.EaseOutCubic, now); } else ta.alpha.set(1);
        ta.alphaL = 1; ta.placed = true;
      }
      if (ta.laneT !== ta.laneL) { ta.lane.animateTo(ta.laneT, ms("motion-catch-up"), Ease.EaseOutCubic, now); ta.laneL = ta.laneT; }
      if (ta.alphaT !== ta.alphaL) { ta.alpha.animateTo(ta.alphaT, ms("motion-cross-fade"), Ease.EaseOutCubic, now); ta.alphaL = ta.alphaT; }
      moving = ta.lane.update(now) || moving;
      moving = ta.alpha.update(now) || moving;
      if (ta.gone && !ta.alpha.animating && ta.alpha.value <= 0.001) trackAnims.delete(id);
    }
    if (!emptyInit && everBound) { emptyAmt.set(emptyWanted ? 1 : 0); emptyApplied = emptyWanted; emptyInit = true; }
    if (emptyWanted !== emptyApplied) { emptyAmt.animateTo(emptyWanted ? 1 : 0, ms("motion-cross-fade"), Ease.EaseOutCubic, now); emptyApplied = emptyWanted; }
    moving = emptyAmt.update(now) || moving;
    if (everBound) populated = true;
    if (drag.kind !== "scrub" && playhead !== playheadLast) {
      if (playheadLast < -0.5) shownPlayhead.set(playhead);
      else if (playing && Math.abs(playhead - shownPlayhead.value) < FOLLOW_STEP_S) shownPlayhead.set(playhead);
      else shownPlayhead.animateTo(playhead, ms("motion-catch-up"), Ease.EaseOutCubic, now);
      playheadLast = playhead;
    }
    moving = shownPlayhead.update(now) || moving;
    if (guideWanted !== guideApplied) { guideAmt.animateTo(guideWanted ? 1 : 0, ms("motion-hover"), Ease.EaseOutCubic, now); guideApplied = guideWanted; }
    moving = guideAmt.update(now) || moving;
    if (dropWanted !== dropApplied) { dropAmt.animateTo(dropWanted ? 1 : 0, ms("motion-hover"), Ease.EaseOutCubic, now); dropApplied = dropWanted; }
    moving = dropAmt.update(now) || moving;
    moving = hover.advance(now) || moving;
    moving = ui.advance(now) || moving;
    moving = sel.advance(now, ms("motion-select")) || moving;
    // what the key lane reads, and its box (root px)
    shownPh.value = shownPlayhead.value;
    keyOpen.value = keyLane.value;
    colW.value = HW();
    axis.value = pps.value * 1e6 + scroll.value + W * 1e-3;
    const s = animSectionRect(), b = ctx.box.peek();
    Object.assign(klHost.style, { transform: `translate(${s.x}px, ${s.y}px)`, width: s.w + "px", height: s.h + "px" });
    klBox.value = { x: b.x + s.x, y: b.y + s.y, w: s.w, h: s.h };
    return moving || vscroll.moving;
  }

  // ---------------------------------------------------------------- paint (Timeline::onPaint)
  const sr = tok.raw("scrim-rgb").split(",").map((v) => +v / 255);
  const scrim = (a) => [sr[0], sr[1], sr[2], a];
  function paint() {
    const hw = HW(), rh = RH(), th = TH(), pp = Math.max(1e-6, pps.value), rc = tok.px("radius-control"), hair = tok.px("radius-hairline");
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), primary = col("primary"), destr = col("destructive");
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("deck-bg") });
    const ea = emptyAmt.value, laneDim = 1 - 0.55 * ea;
    // lanes
    const lr = lanesRect();
    p.clip(lr.x, lr.y, lr.w, lr.h);
    for (const ta of trackAnims.values()) {
      const y = laneTop(ta.lane.value), a = ta.alpha.value;
      if (y + th < lr.y || y > H || a <= 0.001) continue;
      p.rect(lr.x, y, lr.w, th, 0, { fill: fade(col("lane-bg"), laneDim * a) });
      if (Math.round(ta.laneT) % 2) p.rect(lr.x, y, lr.w, th, 0, { fill: fade(col("lane-alt-bg"), a) });
      p.line(lr.x, y + th - 0.5, lr.x + lr.w, y + th - 0.5, fade(border, a), 1);
    }
    { const xe = toX(duration); if (xe < lr.x + lr.w) p.rect(Math.max(lr.x, xe), lr.y, lr.x + lr.w - Math.max(lr.x, xe), lr.h, 0, { fill: scrim(0.35) }); }
    for (const mk of markers) { const x = toX(mk.at); if (x < lr.x || x > lr.x + lr.w) continue; p.line(x, lr.y, x, H, fade(col("marker"), 0.35), 1); }
    // clips, by provenance
    for (const an of anims.values()) {
      const a = an.alpha.value;
      if (a <= 0.001) continue;
      const c = an.data;
      const r = { x: toX(an.at.value), y: laneTop(an.lane.value) + CLIP_INSET_Y, w: an.dur.value * pp, h: th - 2 * CLIP_INSET_Y };
      if (r.x + r.w < lr.x || r.x > lr.x + lr.w || r.y + r.h < lr.y || r.y > H) continue;
      const slot = clipSlot(c.id), sa = sel.amount(slot), hv = hover.amount(slot);
      const dragged = !an.gone && drag.clip === c.id && drag.kind !== "none" && drag.kind !== "scrub";
      let body = a;
      if (c.provenance === "inherited") body *= 0.55;
      const plate = c.audio ? col("clip-audio") : col("clip-video");
      let bd = whiteA(0.10), fill = plate;
      if (c.provenance === "dangling") { fill = lerp(plate, destr, 0.12); bd = destr; }
      else if (c.provenance === "overridden") bd = primaryA(0.85);
      if (dragged) fill = brighten(fill, 0.08);
      p.rect(r.x, r.y, r.w, r.h, rc, { fill: fade(fill, body), stroke: fade(bd, a), sw: 1 });
      if (hv > 0.001) p.rect(r.x, r.y, r.w, r.h, rc, { fill: hoverWash(hv * a) });
      if (c.provenance === "overridden" && r.w > 4) p.rect(r.x, r.y, 3, r.h, hair, { fill: fade(primary, a) });
      if (c.nested && r.w > 8) {
        p.clip(r.x, r.y, r.w, r.h);
        p.line(r.x + 3, r.y + 2.5, r.x + r.w - 3, r.y + 2.5, fade(fg, 0.28 * a), 1);
        p.line(r.x + 3, r.y + r.h - 2.5, r.x + r.w - 3, r.y + r.h - 2.5, fade(fg, 0.28 * a), 1);
        p.restore();
      }
      if (c.audio && c.media) {
        const wv = waves.get(c.media), wa = wv ? wv.amount.value : 0;
        if (wa > 0.001 && wv.peaks.length && r.w > 2) {
          const pk = wv.peaks, per = wv.perSecond, cy = r.y + r.h * 0.62, amp = r.h * 0.34;
          const x0 = Math.max(r.x + 1, lr.x), x1 = Math.min(r.x + r.w - 1, lr.x + lr.w), inStart = an.at.value, top = [];
          for (let x = x0; x <= x1; x += 1) {
            const s0 = c.in + (toT(x) - inStart), s1 = c.in + (toT(x + 1) - inStart);
            const b0 = Math.floor(s0 * per), b1 = Math.max(b0 + 1, Math.ceil(s1 * per));
            let v = 0;
            for (let b = Math.max(0, b0); b < b1 && b < pk.length; ++b) v = Math.max(v, pk[b]);
            top.push([x, Math.min(1, v)]);
          }
          if (top.length > 1) {
            let d = `M${top[0][0].toFixed(2)} ${(cy - top[0][1] * amp).toFixed(2)}`;
            for (const [x, v] of top) d += `L${x.toFixed(2)} ${(cy - v * amp).toFixed(2)}`;
            for (let i = top.length - 1; i >= 0; --i) d += `L${top[i][0].toFixed(2)} ${(cy + top[i][1] * amp).toFixed(2)}`;
            p.path(d + "Z", { fill: fade(fg, 0.30 * a * wa) });
          }
        }
      }
      if (c.provenance === "dangling" || c.offline) {
        p.clip(r.x, r.y, r.w, r.h);
        const hc = fade(c.offline && c.provenance !== "dangling" ? muted : destr, 0.22 * a);
        for (let hx = r.x - r.h; hx < r.x + r.w; hx += 7) p.line(hx, r.y + r.h, hx + r.h, r.y, hc, 1);
        p.restore();
      }
      const tx = Math.max(r.x, lr.x) + (c.provenance === "overridden" && r.x >= lr.x ? 8 : 6), room = r.x + r.w - 5 - tx;
      if (room > 8) {
        const warnText = c.provenance === "dangling" || c.offline, label = c.srcName || c.name;
        p.text(ellipsize(label, room, 10, F.medium), tx, r.y + 11.5, 10, F.medium, fade(warnText ? destr : c.provenance === "inherited" ? muted : fg, a));
        let sub;
        if (c.provenance === "dangling") sub = "base deleted its target";
        else if (c.offline) sub = "offline — media missing";
        else if (c.provenance === "inherited") sub = "inherited";
        else if (c.provenance === "overridden") sub = "overridden here";
        else if (c.audio) sub = num(c.gain) + " dB";
        else sub = (c.angle > 0 ? "angle " + c.angle + " · " : c.nested ? "timeline · " : "") + timecode(clipDur(c), fps).slice(3);
        if (r.h >= 24) {
          const fam = c.provenance === "local" && !c.audio ? F.mono : F.sans;
          p.text(ellipsize(sub, room, 8.5, fam), tx, r.y + 22.5, 8.5, fam, fade(warnText ? destr : muted, a * 0.9));
        }
      }
      if (sa > 0.001) p.rect(r.x - 1, r.y - 1, r.w + 2, r.h + 2, rc, { stroke: primaryA(sa * a), sw: 1.5 });
      if (dragged && (drag.kind === "roll" || drag.kind === "slip")) {
        let hint = dragHint();
        if (drag.kind === "slip") hint += " · in " + timecode(Math.max(0, drag.origIn + drag.value), fps);
        const tw = measure(hint, 9, F.mono) + 10, hx = drag.kind === "roll" ? toX(drag.value) - tw * 0.5 : r.x + Math.max(0, (r.w - tw) * 0.5);
        const chip = { x: hx, y: r.y - 16 < lr.y ? r.y + r.h + 2 : r.y - 16, w: tw, h: 14 };
        p.rect(chip.x, chip.y, chip.w, chip.h, rc, { fill: col("popover"), stroke: primaryA(0.8), sw: 1 });
        p.text(hint, chip.x + 5, baseline(chip.y + chip.h * 0.5, 9), 9, F.mono, fg);
        if (drag.kind === "roll") p.line(toX(drag.value), r.y - 2, toX(drag.value), r.y + r.h + 2, primary, 2);
      }
    }
    // the drop ghost
    const da = dropAmt.value;
    if (da > 0.001) {
      const gr = { x: toX(dropT), y: laneTop(dropLane) + CLIP_INSET_Y, w: Math.max(8, dropDur * pp), h: th - 2 * CLIP_INSET_Y };
      const edge = dropBad ? destr : primary;
      p.rect(gr.x, gr.y, gr.w, gr.h, rc, { fill: fade(edge, 0.16 * da), stroke: fade(edge, 0.9 * da), sw: 1 });
      const what = dropBad ? "a video source goes on a video track" : dropNewTrack ? dropLabel + " · new video track" : dropLabel, room = gr.w - 12;
      if (room > 8) {
        p.text(ellipsize(what, room, 10, F.medium), gr.x + 6, gr.y + 11.5, 10, F.medium, fade(dropBad ? destr : fg, da));
        p.text(timecode(dropT, fps), gr.x + 6, gr.y + 22.5, 8.5, F.mono, fade(muted, da));
      }
    }
    // transitions: a bowtie over the cut
    for (const tr of transitions) {
      const b = animFor(tr.clipB);
      if (!b) continue;
      const cut = b.at.value, x0 = toX(cut - tr.dur * 0.5), x1 = toX(cut + tr.dur * 0.5);
      const y0 = laneTop(b.lane.value) + CLIP_INSET_Y + 2, y1 = y0 + th - 2 * CLIP_INSET_Y - 4;
      if (x1 < lr.x || x0 > lr.x + lr.w) continue;
      p.path(`M${x0} ${y0}L${x1} ${y1}L${x1} ${y0}L${x0} ${y1}Z`, { fill: fade(col("white"), 0.10 * b.alpha.value) });
      const c = fade(col("white"), 0.7 * b.alpha.value);
      p.line(x0, y0, x1, y1, c, 1);
      p.line(x0, y1, x1, y0, c, 1);
    }
    if (guideAmt.value > 0.001) { const gx = toX(guideTime); p.line(gx, lr.y, gx, H, primaryA(0.9 * guideAmt.value), 1); }
    if (ea > 0.001) {
      const s = "drag a source here";
      p.text(s, lr.x + (lr.w - measure(s, 12, F.sans)) * 0.5, lr.y + lr.h * 0.5 + 4, 12, F.sans, fade(muted, ea));
    }
    p.restore();
    // track headers
    p.rect(0, rh, hw, lr.h, 0, { fill: col("track-header-bg") });
    p.clip(0, rh, hw, lr.h);
    for (const ta of trackAnims.values()) {
      const tk = ta.data, y = laneTop(ta.lane.value), hA = ta.alpha.value;
      if (y + th < rh || y > H || hA <= 0.001) continue;
      p.group(hA);
      const cy = y + th * 0.5, inherited = tk.provenance === "inherited", dangling = tk.provenance === "dangling";
      if (tk.provenance === "overridden") p.rect(0, y + 4, 2, th - 8, 0, { fill: primary });
      const tam = targetAmt.get(tk.id), tav = tam ? tam.value : 0;
      if (tav > 0.001 && !tk.audio) p.rect(hw - 4, y + 6, 3, th - 12, hair, { fill: primaryA(0.9 * tav) });
      const gc = css(dangling ? destr : muted);
      p.add(tk.audio ? glyph.speaker({ x: 9.75, y: cy - 5.5, w: 11, h: 11 }, gc) : glyph.film({ x: 9.75, y: cy - 5.5, w: 11, h: 11 }, gc));
      let right = hw - 8;
      if (tk.mute) {
        const chip = { x: right - 14, y: cy - 6.5, w: 14, h: 13 };
        p.rect(chip.x, chip.y, chip.w, chip.h, hair, { fill: col("secondary"), stroke: border, sw: 1 });
        p.text("M", chip.x + (14 - measure("M", 8, F.semibold)) * 0.5, baseline(cy, 8), 8, F.semibold, muted);
        right = chip.x - 4;
      }
      const nx = 9.75 + 16;
      p.text(ellipsize(tk.name || tk.id, right - nx, 10, F.medium), nx, cy - 1, 10, F.medium, dangling ? destr : inherited ? muted : fg);
      const sub = inherited ? "inherited" : dangling ? "dangling" : tk.audio ? num(tk.gain) + " dB" : num(tk.opacity * 100) + "%";
      p.text(ellipsize(sub, right - nx, 8.5, F.sans), nx, cy + 10, 8.5, F.sans, fade(muted, 0.8));
      p.line(0, y + th - 0.5, hw, y + th - 0.5, border, 1);
      p.restore();
    }
    p.restore();
    p.line(hw - 0.5, 0, hw - 0.5, H, border, 1);
    // ruler
    const rr = rulerRect();
    p.rect(0, 0, W, rh, 0, { fill: col("ruler-bg") });
    p.rect(0, 0, hw, rh, 0, { fill: col("track-header-bg") });
    p.line(0, rh - 0.5, W, rh - 0.5, border, 1);
    p.clip(rr.x, rr.y, rr.w, rr.h);
    {
      let stp = STEPS[STEPS.length - 1];
      for (const s of STEPS) if (s * pp >= 76) { stp = s; break; }
      const minor = stp / 4, t0 = Math.max(0, Math.floor(toT(rr.x) / stp) * stp), t1 = toT(rr.x + rr.w) + stp;
      if (minor * pp >= 6) for (let tm = t0; tm <= t1; tm += minor) { const x = toX(tm); p.line(x, rh - 5, x, rh - 1, whiteA(0.12), 1); }
      for (let tm = t0; tm <= t1 + 1e-9; tm += stp) {
        const x = toX(tm);
        p.line(x, rh - 10, x, rh - 1, whiteA(0.28), 1);
        p.text(tickLabel(tm, stp, fps), x + 3, 10.5, 8.5, F.mono, muted);
      }
    }
    cacheSegs.forEach((cs, n) => {
      const x0 = toX(n * cacheSegSeconds), x1 = toX((n + 1) * cacheSegSeconds);
      if (x1 < rr.x || x0 > rr.x + rr.w) return;
      const bw = Math.max(0, x1 - x0 - 1);
      if (cs.stale.value > 0.01) p.rect(x0, 0, bw, 2, 0, { fill: whiteA(0.22 * cs.stale.value) });
      if (cs.cached.value > 0.01) p.rect(x0, 0, bw, 2, 0, { fill: [...col("success").slice(0, 3), 0.9 * cs.cached.value] });
      if (cs.building.value > 0.01) p.rect(x0, 0, bw, 2, 0, { fill: primaryA(0.9 * cs.building.value) });
    });
    const ba = bandAmt.value;
    if (ba > 0.001) {
      const x0 = Math.max(rr.x, toX(bandIn)), x1 = Math.min(rr.x + rr.w, toX(bandOut));
      if (x1 > x0) p.rect(x0, rh - 6, x1 - x0, 6, 0, { fill: whiteA(0.14 * ba) });
      const bc = fade(fg, ba);
      if (markIn >= 0) p.line(toX(markIn), 3, toX(markIn), rh, bc, 1.5);
      if (markOut >= 0) p.line(toX(markOut), 3, toX(markOut), rh, bc, 1.5);
    }
    for (const mk of markers) {
      const x = toX(mk.at);
      if (x < rr.x - 60 || x > rr.x + rr.w) continue;
      p.path(`M${x - 4} ${rh - 9}L${x + 4} ${rh - 9}L${x} ${rh - 3}Z`, { fill: col("marker") });
      if (mk.name) p.text(ellipsize(mk.name, 90, 8.5, F.sans), x + 6, rh - 4, 8.5, F.sans, fg);
    }
    p.restore();
    // zoom controls in the corner
    for (let k = 0; k < 2; ++k) {
      const b = k === 0 ? zoomOutRect() : zoomInRect(), hv = ui.amount(k);
      if (hv > 0.001) p.rect(b.x, b.y, b.w, b.h, rc, { fill: hoverWash(hv) });
      const c = css(lerp(muted, fg, 0.7 * hv)), gb = { x: b.x + 4, y: b.y + 4, w: 10, h: 10 };
      p.add(k === 0 ? glyph.minus(gb, c) : glyph.plus(gb, c));
    }
    p.text(ellipsize(Math.round(pp) + " px/s", hw - 9.75 - 58, 8.5, F.mono), 58, baseline(rh * 0.5, 8.5), 8.5, F.mono, fade(muted, 0.8));
    // the playhead: destructive, a position
    const px = toX(shownPlayhead.value);
    if (px >= hw - 0.5 && px <= W + 0.5) {
      p.clip(hw, 0, W - hw, H);
      const pc = col("playhead");
      p.line(px, rh - 2, px, H, pc, 1);
      p.path(`M${px - 5} 2L${px + 5} 2L${px + 5} ${rh - 9}L${px} ${rh - 3}L${px - 5} ${rh - 9}Z`, { fill: pc });
      p.restore();
    }
    const bar = vscroll.bar(W - 2);
    if (bar) p.rect(bar.x, bar.y, bar.w, bar.h, tok.px("radius-pill"), { fill: whiteA(0.14) });
    const maxS = maxScrollFor(pp);
    if (maxS > 0.5) {
      const vw = lr.w, cw = contentSeconds() * pp, thumbW = Math.max(24, (vw * vw) / cw), x = lr.x + (vw - thumbW) * (scroll.value / maxS);
      p.rect(x, lr.y + lr.h - 4, thumbW, 3, tok.px("radius-pill"), { fill: whiteA(0.14) });
    }
    p.end();
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    const more = advance(now);
    paint();
    return more;
  }

  // ---------------------------------------------------------------- input (Timeline::handleGesture)
  let press = null;                                  // {pt, id, moved}
  function down(pt, alt) {
    drag = { kind: "none" };
    if (inRect(pt, zoomOutRect()) || inRect(pt, zoomInRect())) return;
    if (keyLane.value > 0.5 && inRect(pt, keyLaneGrabRect())) { laneDragging = true; return; }
    if (inRect(pt, rulerRect()) && duration > 0) {
      const t = clamp(toT(pt.x), 0, duration);
      drag = { kind: "scrub", lastFrame: Math.round(t * fps) };
      shownPlayhead.set(t); playheadLast = t;
      vm.view.scrubbing.value = true;
      vm.notify("playhead " + seconds(t, fps));
      return;
    }
    const { id, edge } = clipAt(pt);
    const an = id ? liveAnim(id) : null;
    if (!an) return;
    const d = { kind: "none", clip: id, origAt: an.data.at, origDur: clipDur(an.data), origIn: an.data.in, origOut: an.data.out,
                speed: an.data.speed > 0 ? an.data.speed : 1, origLane: laneOfTrack(an.data.track), snapped: false, snapTime: 0 };
    d.lane = d.origLane;
    const pt0 = toT(pt.x);
    let left = id, right = "";
    if (alt && edge !== 0) {
      const cut = edge > 0 ? an.data.at + d.origDur : an.data.at;
      for (const o of anims.values()) {
        if (o.gone || o.data.id === id || o.data.track !== an.data.track) continue;
        if (edge > 0 && Math.abs(o.data.at - cut) < 0.5 / fps) right = o.data.id;
        if (edge < 0 && Math.abs(o.data.at + clipDur(o.data) - cut) < 0.5 / fps) { left = o.data.id; right = id; }
      }
    }
    if (right && liveAnim(left) && liveAnim(right)) {
      const l = liveAnim(left), r = liveAnim(right);
      Object.assign(d, { kind: "roll", clip: left, origAt: l.data.at, origDur: clipDur(l.data), other: right, otherAt: r.data.at, otherDur: clipDur(r.data),
                         grab: r.data.at - pt0, value: r.data.at });
    } else if (alt && edge === 0) Object.assign(d, { kind: "slip", pressT: pt0, value: 0 });
    else if (edge < 0) Object.assign(d, { kind: "trimIn", grab: an.data.at - pt0, value: an.data.at });
    else if (edge > 0) Object.assign(d, { kind: "trimOut", grab: an.data.at + d.origDur - pt0, value: an.data.at + d.origDur });
    else Object.assign(d, { kind: "move", grab: an.data.at - pt0, value: an.data.at });
    drag = d;
  }
  function move(pt) {
    if (laneDragging) {
      const hh = clamp(H - pt.y - KEY_HEAD_H, KEY_LANE_MIN_H, 600);
      laneH.set(hh); laneHTarget = laneHLast = hh;
      return;
    }
    if (drag.kind === "scrub") {
      const t = clamp(toT(pt.x), 0, duration);
      shownPlayhead.set(t); playheadLast = t;
      const fr = Math.round(t * fps);
      if (fr !== drag.lastFrame) { drag.lastFrame = fr; vm.notify("playhead " + seconds(t, fps)); }
      return;
    }
    const an = drag.kind !== "none" ? liveAnim(drag.clip) : null;
    if (!an) return;
    const pt0 = toT(pt.x), frame = 1 / fps;
    if (drag.kind === "move") {
      const s = snap(Math.max(0, pt0 + drag.grab), drag.origDur, drag.clip, true);
      const start = Math.max(0, s.v);
      drag.value = start; drag.snapped = s.snapped; drag.snapTime = s.snapT;
      const lane = laneAtY(pt.y);
      if (lane >= 0 && lane < tracks.length && tracks[lane].audio === an.data.audio) drag.lane = lane;
      an.at.set(start); an.atT = an.atL = start;
      an.lane.set(drag.lane); an.laneT = an.laneL = drag.lane;
    } else if (drag.kind === "trimIn") {
      const end = drag.origAt + drag.origDur, s = snap(pt0 + drag.grab, 0, drag.clip, false);
      const v = clamp(s.v, Math.max(0, drag.origAt - drag.origIn / drag.speed), end - frame);
      drag.value = v; drag.snapped = s.snapped; drag.snapTime = s.snapT;
      an.at.set(v); an.atT = an.atL = v;
      an.dur.set(end - v); an.durT = an.durL = end - v;
    } else if (drag.kind === "trimOut") {
      const s = snap(pt0 + drag.grab, 0, drag.clip, false), e = Math.max(s.v, drag.origAt + frame);
      drag.value = e; drag.snapped = s.snapped; drag.snapTime = s.snapT;
      an.dur.set(e - drag.origAt); an.durT = an.durL = e - drag.origAt;
    } else if (drag.kind === "roll") {
      const s = snap(pt0 + drag.grab, 0, drag.clip, false), c = clamp(s.v, drag.origAt + frame, drag.otherAt + drag.otherDur - frame);
      drag.value = c; drag.snapped = s.snapped; drag.snapTime = s.snapT;
      an.dur.set(c - drag.origAt); an.durT = an.durL = c - drag.origAt;
      const o = liveAnim(drag.other);
      if (o) { const end = drag.otherAt + drag.otherDur; o.at.set(c); o.atT = o.atL = c; o.dur.set(end - c); o.durT = o.durL = end - c; }
    } else if (drag.kind === "slip") {
      let by = -(pt0 - drag.pressT) * drag.speed;
      by = Math.max(by, -drag.origIn);
      drag.value = Math.round(by * fps) / fps;
    }
    guideWanted = !!drag.snapped;
    if (drag.snapped) guideTime = drag.snapTime;
  }
  async function up() {
    if (laneDragging) {
      laneDragging = false;
      vm.cmd("settings set keyLaneHeight=" + Math.round(clamp(laneH.value, 80, 600)));
      return;
    }
    const d = drag;
    drag = { kind: "none" };
    guideWanted = false;
    if (d.kind === "scrub") {
      vm.view.scrubbing.value = false;
      vm.cmd("playhead " + seconds(shownPlayhead.value, fps));
      return;
    }
    const tryLine = async (line, ...ids) => { const r = await vm.cmd(line); if (!r.ok) { for (const id of ids) resetToModel(id); loop.kick(); } };
    if (d.kind === "move") {
      if (Math.abs(d.value - d.origAt) > 1e-9 || d.lane !== d.origLane) {
        let line = "clip move " + q(d.clip) + " --at " + seconds(d.value, fps);
        if (d.lane !== d.origLane && d.lane >= 0 && d.lane < tracks.length) line += " --track " + q(tracks[d.lane].id);
        tryLine(line, d.clip);
      }
    } else if (d.kind === "trimIn" && Math.abs(d.value - d.origAt) > 1e-9) {
      tryLine("clip trim " + q(d.clip) + " --in " + seconds(d.origIn + (d.value - d.origAt) * d.speed, fps), d.clip);
    } else if (d.kind === "trimOut" && Math.abs(d.value - (d.origAt + d.origDur)) > 1e-9) {
      tryLine("clip trim " + q(d.clip) + " --out " + seconds(d.origIn + (d.value - d.origAt) * d.speed, fps), d.clip);
    } else if (d.kind === "roll" && Math.abs(d.value - d.otherAt) > 1e-9) {
      tryLine("clip roll " + q(d.clip) + " --at " + seconds(d.value, fps), d.clip, d.other);
    } else if (d.kind === "slip" && Math.abs(d.value) > 1e-9) vm.cmd("clip slip " + q(d.clip) + " --by " + num(d.value));
  }
  function click(pt) {
    if (inRect(pt, zoomOutRect())) { zoomBy(1 / ZOOM_STEP, toX(playhead)); return; }
    if (inRect(pt, zoomInRect())) { zoomBy(ZOOM_STEP, toX(playhead)); return; }
    if (inRect(pt, rulerRect())) return;
    const { id } = clipAt(pt);
    if (id) { if (id !== selected) vm.cmd("clip select " + q(id)); return; }
    if (inRect(pt, lanesRect()) && duration > 0) vm.cmd("playhead " + seconds(clamp(toT(pt.x), 0, duration), fps));
  }
  const inKeys = (e) => klHost.contains(e.target);
  host.addEventListener("pointerdown", (e) => {
    if (e.button !== 0 || inKeys(e) && !inRect(localPoint(host, e), keyLaneGrabRect())) return;
    const pt = localPoint(host, e);
    press = { pt, moved: false };
    down(pt, e.altKey);
    try { host.setPointerCapture(e.pointerId); } catch {}
    loop.kick();
  });
  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e);
    if (press) {
      if (!press.moved && Math.hypot(pt.x - press.pt.x, pt.y - press.pt.y) > 3) press.moved = true;
      if (press.moved || drag.kind === "scrub" || laneDragging) move(pt);
      loop.kick();
      return;
    }
    if (inKeys(e)) { hover.clear(); host.style.cursor = inRect(pt, keyLaneGrabRect()) && keyLane.value > 0.5 ? "ns-resize" : ""; loop.kick(); return; }
    const { id, edge } = clipAt(pt);
    hover.setHovered(id ? clipSlot(id) : -1);
    ui.setHovered(inRect(pt, zoomOutRect()) ? 0 : inRect(pt, zoomInRect()) ? 1 : -1);
    host.style.cursor = keyLane.value > 0.5 && inRect(pt, keyLaneGrabRect()) ? "ns-resize" : id && edge !== 0 ? "ew-resize" : "default";
    loop.kick();
  });
  const release = (e) => {
    if (!press) return;
    const pr = press;
    press = null;
    const wasDrag = pr.moved || drag.kind === "scrub" || laneDragging;
    up();
    if (!pr.moved && e.type === "pointerup") click(localPoint(host, e));
    void wasDrag;
    loop.kick();
  };
  host.addEventListener("pointerup", release);
  host.addEventListener("pointercancel", release);
  host.addEventListener("pointerleave", () => { if (!press) { hover.clear(); ui.clear(); loop.kick(); } });
  host.addEventListener("contextmenu", (e) => {
    if (inKeys(e)) return;
    e.preventDefault();
    const pt = localPoint(host, e), b = ctx.box.peek(), wx = b.x + pt.x, wy = b.y + pt.y;
    const { id } = clipAt(pt);
    if (id) { openClipContext(vm, ctx, id, wx, wy); return; }
    if (inRect(pt, lanesRect()) || (pt.x < HW() && pt.y > RH())) {
      const lane = laneAtY(pt.y), trk = lane >= 0 && lane < tracks.length ? tracks[lane].id : "";
      openLaneContext(vm, ctx, trk, clamp(toT(pt.x), 0, Math.max(0, duration + 60)), wx, wy);
    }
  });
  host.addEventListener("wheel", (e) => {
    if (inKeys(e)) return;
    const pt = localPoint(host, e), dy = wheelPx(e);
    if (e.ctrlKey) { e.preventDefault(); zoomBy(dy < 0 ? 1.25 : 1 / 1.25, pt.x); loop.kick(); return; }
    if (pt.x < HW()) { if (vscroll.scrollBy(dy)) { e.preventDefault(); loop.kick(); } return; }
    const maxS = maxScrollFor(pps.value);
    if (maxS <= 0.5) { if (vscroll.scrollBy(dy)) { e.preventDefault(); loop.kick(); } return; }
    e.preventDefault();
    anchored = false;
    scrollTarget = clamp(scrollTarget + dy, 0, maxS);
    loop.kick();
  }, { passive: false });
  loop.kick();
}
