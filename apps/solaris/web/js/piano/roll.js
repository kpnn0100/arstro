/*
 * Solaris — piano/roll.js: PianoRoll (widgets/PianoRoll.{h,cpp}, R-ROLL-1…8, R-ROLL-11/14, R-MIDI-3) — the content of a
 * `roll:<pt>` float window. The window layer mounts it with ctx.box (the content's rect) and ctx.window ({key, id, clip}).
 *
 * ONE canvas, painted as the native's onPaint: the toolbar (▶, SNAP 1/4 1/8 1/16 1/32 Off, Notes | Steps; Quantize…,
 * Length, Scale, Chord, Tools), the keys (C named with its octave, C4 = 60; a kit's pads by their names), the beat grid
 * (bars stronger, sixteenths only from 40 px/beat), the rows off the pattern's scale dimmed (eased per pitch class), the
 * notes in their strip's colour (brighter the louder, fainter the less likely), the pattern's end as a handle with the
 * rest scrimmed, the lane below (Vel | Bend | Mod | Chance) and the playhead where the roll's clip is heard.
 *
 * EDITS — the native's exact lines (PianoRoll.cpp handleGesture / track / contextClick / action):
 *   click on empty        note add <pt> --pitch <p> --at <b> --length <l>[ --vel <v>][ --chance <c>]
 *   a note dragged        note move <pt> --pitch <p> --at <b> --to-pitch <p2> --to-at <b2>
 *   its right edge        note move <pt> --pitch <p> --at <b> --length <l>
 *   a velocity stem       note move <pt> --pitch <p> --at <b> --vel <v>      (Chance lane: --chance <c>)
 *   double/right-click    note delete <pt> --pitch <p> --at <b>
 *   the end handle        set <pt>.length=<b>
 *   a note pressed / made note preview <track> <pitch> --vel <v> --length <l>   (vm.note: never awaited, never in undo)
 *   ▶ / ■                 transport play --clip <ac> · transport stop --return
 *   keys (vm.rolls)       roll.selectAll · roll.deselect · roll.delete (notes delete <pt> "<p@b …>") ·
 *                         roll.up/down/octaveUp/octaveDown/left/right (notes move <pt> "<…>" --by <b> --semi <s>)
 * Zoom (Ctrl+wheel, anchored at the pointer, 8…400 px/beat, eased kCatchUpMs) and scroll (wheel; Shift: across; eased
 * EasedScroll) as the native. Notes fade in and out (kCrossFadeMs), the selection lights (kHoverMs), the playhead fades
 * with the play and the grid follows it a page at a time.
 * NOT PORTED: Steps mode (its chip is drawn), the tools' menus (Quantize…, Length, Scale, Chord, Tools), box selection,
 * copy/paste/duplicate, Shift-drag copy, a selection dragged as one, Alt+wheel turns, the Bend/Mod lanes' editing.
 */
import { effect } from "../core/signal.js";
import { own } from "../core/dom.js";
import { Anim, Ease, frames, motion, EasedScroll, reduced } from "../kit/anim.js";
import { canvasIn, color, colorsVersion, whiteAlpha, primaryAlpha, scrim, track, lerpColor, withAlpha, fillRR, strokeRR, line, text } from "../kit/draw.js";
import { FONT, measure, ellipsize, baseline } from "../kit/text.js";
import { useCss } from "../kit/css.js";

// PianoRoll.h
const TOOL_H = 29.25, BAR_H = 58.5, KEYS_W = 58.5, NOTE_H = 13.0, VEL_H = 65.0;
const CHIP_W = 35.75, MODE_W = 52.0, QUANT_W = 71.5, PLAY_W = 29.25, LEN_W = 91.0, SCALE_W = 91.0, CHORD_W = 78.0, TOOLS_W = 61.75;
const PAD_X = 9.75, R_CTRL = 2, EDGE = 5.0, TOL = 0.5 / 960;
const SNAPS = [1.0, 0.5, 0.25, 0.125, 0.0];
const SNAP_LABELS = ["1/4", "1/8", "1/16", "1/32", "Off"];
const KEY_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const LANE_TABS = ["Vel", "Bend", "Mod", "Chance"];

const beats = (b) => String(+(Math.round(b * 960) / 960).toPrecision(6));
const key = (p, at) => p + "@" + Math.round(at * 960);
const black = (p) => [1, 3, 6, 8, 10].includes(((p % 12) + 12) % 12);
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const fade = (c, a) => withAlpha(c, c[3] * a);
const white = () => color("white");

function lengthName(b) {
  const names = { 0.0625: "1/64", 0.125: "1/32", 0.25: "1/16", 0.5: "1/8", 1: "1/4", 2: "1/2", 4: "1/1" };
  return names[b] || beats(b) + " beats";
}

export function mount(host, vm, ctx) {
  useCss("piano/roll.css");
  host.classList.add("sl-roll");
  const pt = (ctx.window && ctx.window.id) || String((ctx.window && ctx.window.key) || "").replace(/^roll:/, "");
  const clipWanted = (ctx.window && ctx.window.clip) || "";
  const cv = canvasIn(host, ctx);
  const el = cv.el;
  el.tabIndex = -1;

  // ------------------------------------------------------------------ state (the native's members)
  const S = {
    present: false, model: { notes: [], length: 4, expr: [] }, names: [], colour: 0, clip: "", clipTrack: "", clipAt: 0, clipLen: 0,
    playing: false, looping: false, headBeat: -1, headLast: false, mode: 0, snap: 0.25, lastLength: 0.25, lengthChoice: 0,
    modelKey: "", modelVel: 100, modelChance: 1, lane: 0, laneLast: 0, ever: false, scrolledIn: false, endLast: -1,
    ppbTarget: 48, nowMs: performance.now(), hover: -1,
  };
  const notes = [];             // NoteLive: {n, alpha, aLast, sel, selLast, vel, chance, velLast, chanceLast, placed, gone, instant}
  const sel = new Set();
  const ppb = new Anim(48), end = new Anim(4), headAlpha = new Anim(0);
  const laneAmt = [new Anim(1), new Anim(0), new Anim(0), new Anim(0)];
  const scaleOut = Array.from({ length: 12 }, () => new Anim(0)), scaleWant = new Array(12).fill(0);
  let scalePlaced = false;
  const sx = new EasedScroll(), sy = new EasedScroll();
  const hoverAmt = new Map();   // HoverFade: id -> Anim
  // a gesture in flight
  let drag = "none", pressNote = null, live = null, pressKey = "", grabBeat = 0, grabPitch = 0, liveEnd = 0, pressPt = null, heardPitch = -1;

  // ------------------------------------------------------------------ geometry (local, as DRAWN)
  const W = () => cv.w || ctx.box.peek().w, H = () => cv.h || ctx.box.peek().h;
  const gridRect = () => ({ x: KEYS_W, y: BAR_H, w: Math.max(0, W() - KEYS_W), h: Math.max(0, H() - BAR_H - VEL_H) });
  const velRect = () => ({ x: KEYS_W, y: H() - VEL_H, w: Math.max(0, W() - KEYS_W), h: VEL_H });
  const beatToX = (b) => KEYS_W + b * ppb.value - sx.value();
  const xToBeat = (x) => (x - KEYS_W + sx.value()) / Math.max(1e-6, ppb.value);
  const pitchToY = (p) => BAR_H + (127 - p) * NOTE_H - sy.value();
  const yToPitch = (y) => clamp(127 - Math.floor((y - BAR_H + sy.value()) / NOTE_H), 0, 127);
  const inR = (r, p) => p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
  const playRect = () => ({ x: PAD_X, y: 4.875, w: PLAY_W, h: TOOL_H - 9.75 });
  const snapRect = (i) => ({ x: PAD_X + PLAY_W + 13 + 34 + i * (CHIP_W + 3.25), y: 4.875, w: CHIP_W, h: TOOL_H - 9.75 });
  const modeRect = (m) => { const l = snapRect(4); return { x: l.x + l.w + 19.5 + m * MODE_W, y: 4.875, w: MODE_W, h: TOOL_H - 9.75 }; };
  const row2 = (x, w) => ({ x, y: TOOL_H + 4.875, w, h: TOOL_H - 9.75 });
  const quantizeRect = () => row2(PAD_X, QUANT_W);
  const lengthRect = () => row2(PAD_X + QUANT_W + 6.5, LEN_W);
  const scaleRect = () => row2(lengthRect().x + LEN_W + 6.5, SCALE_W);
  const chordRect = () => row2(scaleRect().x + SCALE_W + 6.5, CHORD_W);
  const toolsRect = () => row2(chordRect().x + CHORD_W + 6.5, TOOLS_W);
  const laneTabRect = (l) => ({ x: 4, y: H() - VEL_H + 4 + l * 15.5, w: KEYS_W - 8, h: 13 });
  const endRect = () => ({ x: beatToX(drag === "end" ? liveEnd : end.value) - 4.875, y: BAR_H, w: 9.75, h: 13 });
  const rectOf = (n) => ({ x: beatToX(n.at), y: pitchToY(n.pitch) + 1, w: Math.max(3, n.length * ppb.value - 1), h: NOTE_H - 2 });
  const snapped = (b) => (S.snap <= 0 ? Math.max(0, Math.round(b * 960) / 960) : Math.max(0, Math.round(b / S.snap) * S.snap));
  const clickLength = () => (S.lengthChoice > 0 ? S.lengthChoice : S.lastLength);

  function noteName(p) {
    for (const n of S.names) if (n.note === p) return n.name;
    if (S.names.length) return "";
    return p % 12 === 0 ? "C" + (Math.floor(p / 12) - 1) : "";
  }
  function noteAt(p) {
    for (let i = notes.length - 1; i >= 0; --i) {
      const l = notes[i];
      if (l.gone) continue;
      const r = rectOf(l.n);
      if (inR(r, p)) return { l, edge: p.x >= r.x + r.w - Math.min(EDGE, r.w / 3) };
    }
    return null;
  }

  // ------------------------------------------------------------------ intents
  const send = (line_) => vm.cmd(line_);
  function preview(pitch, vel, length) {
    if (!S.clipTrack || pitch < 0 || pitch > 127) return;
    vm.note("note preview " + S.clipTrack + " " + pitch + " --vel " + clamp(vel, 1, 127) + " --length " + beats(clamp(length, 0.125, 1.0)));
  }
  function takeModel(k) {
    S.modelKey = k;
    for (const n of S.model.notes || []) if (key(n.pitch, n.at) === k) { S.modelVel = n.vel; S.modelChance = n.chance ?? 1; S.lastLength = n.length; }
  }
  const clickAttrs = () => " --length " + beats(clickLength()) + (S.modelVel !== 100 ? " --vel " + S.modelVel : "") +
    (S.modelChance < 1 ? " --chance " + beats(S.modelChance) : "");
  const selectedNotes = () => (S.model.notes || []).filter((n) => sel.has(key(n.pitch, n.at)));
  const tokens = (ns, by = 0, semi = 0) => '"' + ns.map((n) => (n.pitch + semi) + "@" + beats(n.at + by)).join(" ") + '"';
  async function moveSelection(by, semi) {
    const ns = selectedNotes();
    if (!ns.length) return;
    let lo = Infinity, pmin = 127, pmax = 0;
    for (const n of ns) { lo = Math.min(lo, n.at); pmin = Math.min(pmin, n.pitch); pmax = Math.max(pmax, n.pitch); }
    by = Math.max(by, -lo); semi = clamp(semi, -pmin, 127 - pmax);
    if (by === 0 && semi === 0) return;
    const r = await send("notes move " + pt + " " + tokens(ns) + " --by " + beats(by) + " --semi " + semi);
    if (r) { sel.clear(); for (const n of ns) sel.add(key(n.pitch + semi, n.at + by)); kick(); }
  }
  function togglePlay() {
    if (!S.clip) return false;
    send(S.looping && S.playing ? "transport stop --return" : "transport play --clip " + S.clip);
    return true;
  }
  function action(id) {
    if (id === "roll.selectAll") { for (const n of S.model.notes || []) sel.add(key(n.pitch, n.at)); kick(); return true; }
    if (id === "roll.deselect") { sel.clear(); kick(); return true; }
    if (!sel.size) return false;
    if (id === "roll.delete") { send("notes delete " + pt + " " + tokens(selectedNotes())).then((r) => { if (r) { sel.clear(); kick(); } }); return true; }
    const semi = id === "roll.up" ? 1 : id === "roll.down" ? -1 : id === "roll.octaveUp" ? 12 : id === "roll.octaveDown" ? -12 : 0;
    if (semi) { moveSelection(0, semi); return true; }
    if (id === "roll.left" || id === "roll.right") { moveSelection((id === "roll.right" ? 1 : -1) * (S.snap > 0 ? S.snap : 0.25), 0); return true; }
    return false;
  }
  vm.rolls.set(pt, { action, togglePlay });
  own(host, () => { if (vm.rolls.get(pt) && vm.rolls.get(pt).action === action) vm.rolls.delete(pt); });

  // ------------------------------------------------------------------ bind (PianoRoll::bind)
  function bind(m) {
    const p = m && (m.patterns || []).find((x) => x.id === pt);
    S.present = !!p;
    if (!p) return;
    S.model = p;
    takeModel(S.modelKey);
    const strip = (m.strips || []).find((s) => s.id === p.strip);
    S.colour = strip ? strip.colour : 0;
    const dt = (m.deviceTypes || []).find((t) => t.name === p.instrument);
    S.names = (dt && dt.noteNames) || [];
    let cl = (m.clips || []).find((c) => c.pattern === pt && c.id === clipWanted) || null;
    if (!cl) for (const c of m.clips || []) if (c.pattern === pt && (!cl || c.at < cl.at)) cl = c;
    S.clip = cl ? cl.id : ""; S.clipTrack = cl ? cl.track : ""; S.clipAt = cl ? cl.at : 0; S.clipLen = cl ? cl.length : 0;
    const tr_ = m.transport || {};
    S.playing = !!tr_.playing;
    S.looping = !!S.clip && tr_.loopClip === S.clip;
    S.headBeat = -1;
    const pos = tr_.position || 0;
    if (S.playing && cl && pos >= S.clipAt && pos < S.clipAt + S.clipLen) {
      const inn = pos - S.clipAt;
      S.headBeat = p.length > 0 ? inn % p.length : inn;
    }
    for (const l of notes) l.gone = true;
    for (const n of p.notes || []) {
      const k = key(n.pitch, n.at);
      let l = null;
      for (const x of notes) if (key(x.n.pitch, x.n.at) === k) l = x;
      if (!l) { l = { n, alpha: new Anim(0), aLast: 0, sel: new Anim(0), selLast: false, vel: new Anim(0), chance: new Anim(1), velLast: -1, chanceLast: -1, placed: false, gone: false, instant: false }; notes.push(l); }
      l.n = n; l.gone = false;
    }
    const present = new Set((p.notes || []).map((n) => key(n.pitch, n.at)));
    for (const k of [...sel]) if (!present.has(k)) sel.delete(k);
  }

  function layout() {
    const g = gridRect();
    const content = Math.max(S.model.length || 0, end.value) + 8;
    sx.setExtent(KEYS_W, g.w, content * S.ppbTarget);
    sy.setExtent(BAR_H, g.h, 128 * NOTE_H);
    if (!S.scrolledIn && g.h > 0 && S.present) {
      let lo = 60, hi = 60;
      for (const n of S.model.notes || []) { lo = Math.min(lo, n.pitch); hi = Math.max(hi, n.pitch); }
      const centre = (127 - (lo + hi) * 0.5) * NOTE_H;
      sy.reveal(Math.max(0, centre - g.h * 0.5), g.h);
      sy._live.set(sy.target()); sy._last = sy.target(); // first placement: nowhere to travel from
      S.scrolledIn = true;
    }
  }

  // ------------------------------------------------------------------ advance (PianoRoll::advance)
  function hoverOf(id) { const a = hoverAmt.get(id); return a ? a.value : 0; }
  function advance(now) {
    S.nowMs = now;
    const M = motion();
    let busy = false;
    const fadeIn = S.ever && !reduced();
    for (const l of notes) {
      const want = l.gone ? 0 : 1;
      if (!l.placed) { l.alpha.set(fadeIn && !l.instant ? 0 : 1); l.aLast = l.alpha.value; l.placed = true; }
      if (want !== l.aLast) {
        if (l.instant && l.gone) l.alpha.set(0); else l.alpha.animateTo(want, M.crossFade, Ease.EaseOutCubic, now);
        l.aLast = want;
      }
      l.alpha.update(now);
      const s = !l.gone && sel.has(key(l.n.pitch, l.n.at));
      if (s !== l.selLast) { l.sel.animateTo(s ? 1 : 0, M.hover, Ease.EaseOutCubic, now); l.selLast = s; }
      l.sel.update(now);
      const ch = l.n.chance ?? 1;
      if (l.velLast < 0) { l.vel.set(l.n.vel); l.velLast = l.n.vel; l.chance.set(ch); l.chanceLast = ch; }
      if (l.n.vel !== l.velLast) { l.vel.animateTo(l.n.vel, M.catchUp, Ease.EaseOutCubic, now); l.velLast = l.n.vel; }
      if (ch !== l.chanceLast) { l.chance.animateTo(ch, M.catchUp, Ease.EaseOutCubic, now); l.chanceLast = ch; }
      l.vel.update(now); l.chance.update(now);
      busy = busy || l.alpha.animating || l.sel.animating || l.vel.animating || l.chance.animating;
    }
    for (let i = notes.length - 1; i >= 0; --i) if (notes[i].gone && !notes[i].alpha.animating && notes[i].alpha.value <= 0.001) notes.splice(i, 1);
    for (let i = 0; i < 4; ++i) {
      if (S.lane !== S.laneLast && (i === S.lane || i === S.laneLast)) laneAmt[i].animateTo(S.lane === i ? 1 : 0, M.crossFade, Ease.EaseOutCubic, now);
      laneAmt[i].update(now); busy = busy || laneAmt[i].animating;
    }
    S.laneLast = S.lane;
    const mask = S.model.scaleMask || 0;
    for (let pc = 0; pc < 12; ++pc) {
      const want = mask && !((mask >> pc) & 1) ? 1 : 0;
      if (!scalePlaced) { scaleOut[pc].set(want); scaleWant[pc] = want; }
      else if (want !== scaleWant[pc]) { scaleOut[pc].animateTo(want, M.crossFade, Ease.EaseOutCubic, now); scaleWant[pc] = want; }
      scaleOut[pc].update(now); busy = busy || scaleOut[pc].animating;
    }
    scalePlaced = S.present;
    if (S.endLast < 0 && S.present) { end.set(S.model.length); S.endLast = S.model.length; }
    else if (S.present && S.model.length !== S.endLast) { end.animateTo(S.model.length, M.catchUp, Ease.EaseOutCubic, now); S.endLast = S.model.length; }
    end.update(now); ppb.update(now);
    busy = busy || end.animating || ppb.animating;
    // the playhead: fades with the play; the grid follows it a page at a time
    const head = S.headBeat >= 0;
    if (head !== S.headLast) { headAlpha.animateTo(head ? 1 : 0, M.crossFade, Ease.EaseOutCubic, now); S.headLast = head; }
    headAlpha.update(now);
    busy = busy || headAlpha.animating;
    layout();
    if (head && drag === "none") {
      const g = gridRect(), x = beatToX(S.headBeat);
      if (g.w > 40 && (x > g.x + g.w - 13 || x < g.x)) sx.scrollBy(x - (g.x + 13));
    }
    for (const [, a] of hoverAmt) { a.update(now); busy = busy || a.animating; }
    if (sx.advance(now)) busy = true;
    if (sy.advance(now)) busy = true;
    if (S.present) S.ever = true;
    return busy || drag !== "none";
  }

  // ------------------------------------------------------------------ paint (PianoRoll::onPaint / paintNotes)
  function chip(g, r, words, on, hv, px = 10) {
    fillRR(g, r.x, r.y, r.w, r.h, R_CTRL, on ? color("primary") : lerpColor(color("secondary"), color("popover"), hv));
    const label = ellipsize(words, r.w - 4, px, FONT.sans);
    text(g, label, r.x + (r.w - measure(label, px, FONT.sans)) * 0.5, baseline(r.y + r.h * 0.5, px), px, FONT.sans,
      on ? color("primary-foreground") : color("secondary-foreground"));
  }
  function caret(g, r) {
    const x = r.x + r.w - 11, y = r.y + r.h * 0.5 - 1.5;
    g.fillStyle = `rgba(${color("muted-foreground").slice(0, 3).map((v) => v * 255).join(",")},1)`;
    g.beginPath(); g.moveTo(x, y); g.lineTo(x + 6, y); g.lineTo(x + 3, y + 3.5); g.closePath(); g.fill();
  }
  function menuChip(g, r, label, hv) {
    fillRR(g, r.x, r.y, r.w, r.h, R_CTRL, lerpColor(color("secondary"), color("popover"), hv));
    const s = ellipsize(label, r.w - 20, 10, FONT.sans);
    text(g, s, r.x + 7, baseline(r.y + r.h * 0.5, 10), 10, FONT.sans, color("secondary-foreground"));
    caret(g, r);
  }

  function paintNotes(g, a) {
    const grid = gridRect(), w = W();
    g.save();
    g.beginPath(); g.rect(0, grid.y, w, grid.h); g.clip();
    for (let p = 127; p >= 0; --p) {
      const y = pitchToY(p);
      if (y + NOTE_H < grid.y || y > grid.y + grid.h) continue;
      if (black(p)) fillRR(g, grid.x, y, grid.w, NOTE_H, 0, fade(whiteAlpha(0.025), a));
      const out = scaleOut[p % 12].value;
      if (out > 0.001) fillRR(g, grid.x, y, grid.w, NOTE_H, 0, fade(scrim(1), 0.6 * out * a));
      if (p % 12 === 0) line(g, grid.x, Math.round(y + NOTE_H) + 0.5, w, Math.round(y + NOTE_H) + 0.5, fade(whiteAlpha(0.07), a), 1);
      fillRR(g, 0, y + 0.5, KEYS_W - 1, NOTE_H - 1, 0,
        fade(lerpColor(black(p) ? color("canvas-bg") : color("secondary"), color("canvas-bg"), 0.7 * out), a));
      const nm = noteName(p);
      if (nm) text(g, ellipsize(nm, KEYS_W - 8, 9, FONT.sans), 4, baseline(y + NOTE_H * 0.5, 9), 9, FONT.sans,
        fade(S.names.length ? color("foreground") : color("muted-foreground"), a * (1 - 0.6 * out)));
    }
    g.beginPath(); g.rect(grid.x, grid.y, grid.w, grid.h); g.clip();
    const base = track(S.colour);
    for (const l of notes) {
      const na = (l.placed ? l.alpha.value : 0) * a;
      if (na <= 0.001) continue;
      const dragged = (drag === "move" || drag === "resize" || drag === "vel" || drag === "chance") && !l.gone && key(l.n.pitch, l.n.at) === pressKey;
      const n = dragged ? live : l.n;
      const r = rectOf(n);
      if (r.x + r.w < grid.x || r.x > grid.x + grid.w || r.y + r.h < grid.y || r.y > grid.y + grid.h) continue;
      const s = l.sel.value;
      const v = dragged || l.velLast < 0 ? n.vel : l.vel.value, ch = dragged || l.velLast < 0 ? (n.chance ?? 1) : l.chance.value;
      const c = lerpColor(base, white(), 0.12 + 0.28 * (v / 127) + 0.25 * s);
      fillRR(g, r.x, r.y, r.w, r.h, R_CTRL, fade(c, na * (dragged ? 1 : 0.9) * (0.35 + 0.65 * ch)));
      strokeRR(g, r.x, r.y, r.w, r.h, R_CTRL, fade(lerpColor(whiteAlpha(0.35), color("primary"), s), na), 1 + 0.5 * s);
    }
    g.restore();
  }

  function paint() {
    colorsVersion.value; // eslint-disable-line no-unused-expressions
    const g = cv.begin();
    const w = W(), h = H();
    if (!(w > 0 && h > 0)) return;
    const grid = gridRect(), vel = velRect();
    fillRR(g, 0, 0, w, h, 0, color("stage-bg"));
    // the beat grid
    g.save();
    g.beginPath(); g.rect(grid.x, grid.y, grid.w, h - grid.y); g.clip();
    const pb = ppb.value;
    for (let b = Math.max(0, Math.floor(xToBeat(grid.x))); b <= xToBeat(w) + 1; b += 0.25) {
      const isBeat = b % 1 === 0, bar = b % 4 === 0;
      if (!isBeat && pb < 40) continue;
      const x = Math.round(beatToX(b)) + 0.5;
      line(g, x, grid.y, x, h, whiteAlpha(bar ? 0.12 : isBeat ? 0.06 : 0.025), 1);
    }
    g.restore();
    if (S.mode === 0) paintNotes(g, 1);
    // the pattern's end
    {
      const ex = beatToX(drag === "end" ? liveEnd : end.value);
      if (ex < w) {
        g.save(); g.beginPath(); g.rect(grid.x, grid.y, grid.w, h - grid.y); g.clip();
        const x0 = Math.max(grid.x, ex);
        fillRR(g, x0, grid.y, w - x0, h - grid.y, 0, scrim(1));
        g.restore();
      }
      line(g, ex, grid.y, ex, h, color("primary"), 1.5);
      const er = endRect();
      fillRR(g, er.x, er.y, er.w, er.h, R_CTRL, color("primary"));
    }
    // the lane below
    fillRR(g, vel.x, vel.y, vel.w, vel.h, 0, color("muted"));
    fillRR(g, 0, vel.y, KEYS_W, vel.h, 0, color("muted"));
    line(g, 0, vel.y + 0.5, w, vel.y + 0.5, color("border"), 1);
    for (let l = 0; l < 4; ++l) {
      const r = laneTabRect(l), on = laneAmt[l].value;
      fillRR(g, r.x, r.y, r.w, r.h, R_CTRL, lerpColor(color("secondary"), color("primary"), on));
      const tab = ellipsize(vm.tr(LANE_TABS[l]), r.w - 4, 9, FONT.sans);
      text(g, tab, r.x + (r.w - measure(tab, 9, FONT.sans)) * 0.5, baseline(r.y + r.h * 0.5, 9), 9, FONT.sans,
        lerpColor(color("secondary-foreground"), color("primary-foreground"), on));
    }
    g.save();
    g.beginPath(); g.rect(vel.x, vel.y, vel.w, vel.h); g.clip();
    const base = track(S.colour);
    const tip = lerpColor(base, white(), 0.3);
    for (let ln = 1; ln < 3; ++ln) {
      const la = laneAmt[ln].value;
      if (la <= 0.001) continue;
      const lane = ln === 1 ? "bend" : "cc1";
      const top = vel.y + 6, hh = VEL_H - 12;
      const y = (v) => (ln === 1 ? top + (1 - (v + 1) * 0.5) * hh : top + (1 - v) * hh);
      if (ln === 1) line(g, vel.x, Math.round(y(0)) + 0.5, w, Math.round(y(0)) + 0.5, fade(whiteAlpha(0.15), la), 1);
      const pts = (S.model.expr || []).filter((e) => e.lane === lane).sort((p, q) => p.at - q.at);
      if (pts.length) {
        g.beginPath(); g.moveTo(beatToX(0), y(pts[0].value));
        for (const p of pts) g.lineTo(beatToX(p.at), y(p.value));
        g.lineTo(beatToX(Math.max(S.model.length, pts[pts.length - 1].at)), y(pts[pts.length - 1].value));
        g.strokeStyle = `rgba(${base.slice(0, 3).map((v) => v * 255).join(",")},${la})`; g.lineWidth = 1.5; g.stroke();
        for (const p of pts) { g.beginPath(); g.arc(beatToX(p.at), y(p.value), 3, 0, Math.PI * 2); g.fillStyle = `rgba(${tip.slice(0, 3).map((v) => v * 255).join(",")},${la})`; g.fill(); }
      }
    }
    const stems = (laneIdx, valueOf) => {
      for (const l of notes) {
        const na = (l.placed ? l.alpha.value : 0) * laneAmt[laneIdx].value;
        if (na <= 0.001) continue;
        const dragged = drag !== "none" && drag !== "end" && !l.gone && key(l.n.pitch, l.n.at) === pressKey;
        const n = dragged ? live : l.n;
        const v = valueOf(l, n, dragged);
        const x = beatToX(n.at) + 1.5, top = vel.y + vel.h - 6 - v * (VEL_H - 12);
        line(g, x, vel.y + vel.h - 6, x, top, fade(base, na), 2);
        g.beginPath(); g.arc(x, top, 3, 0, Math.PI * 2);
        g.fillStyle = `rgba(${tip.slice(0, 3).map((c) => c * 255).join(",")},${na})`; g.fill();
      }
    };
    stems(0, (l, n, d) => (d || l.velLast < 0 ? n.vel : l.vel.value) / 127);
    stems(3, (l, n, d) => (drag === "chance" && d ? live.chance : l.velLast < 0 ? (l.n.chance ?? 1) : l.chance.value));
    g.restore();
    // the playhead
    if (headAlpha.value > 0.001 && S.headBeat >= 0) {
      const x = Math.round(beatToX(S.headBeat)) + 0.5;
      if (x >= grid.x && x <= w) line(g, x, grid.y, x, h, fade(color("playhead"), headAlpha.value), 1);
    }
    // the toolbar
    fillRR(g, 0, 0, w, BAR_H, 0, color("header-bg"));
    line(g, 0, BAR_H - 0.5, w, BAR_H - 0.5, color("border"), 1);
    {
      const r = playRect(), on = S.looping && S.playing, can = !!S.clip;
      fillRR(g, r.x, r.y, r.w, r.h, R_CTRL, on ? color("primary") : lerpColor(color("secondary"), color("popover"), hoverOf(30)));
      const ink = fade(on ? color("primary-foreground") : color("foreground"), can ? 1 : 0.35);
      const cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
      g.beginPath();
      if (on) { g.rect(cx - 4, cy - 4, 8, 8); } else { g.moveTo(cx - 3.5, cy - 5); g.lineTo(cx + 5, cy); g.lineTo(cx - 3.5, cy + 5); g.closePath(); }
      g.fillStyle = `rgba(${ink.slice(0, 3).map((v) => v * 255).join(",")},${ink[3]})`; g.fill();
    }
    text(g, vm.upper(vm.tr("Snap")), PAD_X + PLAY_W + 13, baseline(TOOL_H * 0.5, 8), 8, FONT.semibold, color("muted-foreground"), 0.13 * 8);
    for (let i = 0; i < 5; ++i) chip(g, snapRect(i), i === 4 ? vm.tr(SNAP_LABELS[i]) : SNAP_LABELS[i], S.snap === SNAPS[i], hoverOf(i));
    chip(g, modeRect(0), vm.tr("Notes"), S.mode === 0, hoverOf(10));
    chip(g, modeRect(1), vm.tr("Steps"), S.mode === 1, hoverOf(11));
    chip(g, quantizeRect(), vm.tr("Quantize…"), false, hoverOf(20));
    menuChip(g, lengthRect(), vm.fillIn(vm.tr("Length {1}"), lengthName(clickLength())) + (S.lengthChoice > 0 ? "" : "*"), hoverOf(21));
    const scaleName = S.model.scaleMask && S.model.scale ? S.model.scale : vm.tr("Scale");
    menuChip(g, scaleRect(), scaleName, hoverOf(22));
    menuChip(g, chordRect(), vm.tr("Chord"), hoverOf(23));
    menuChip(g, toolsRect(), vm.tr("Tools"), hoverOf(24));
    {
      const len = vm.fillIn(vm.tr("{1} beats"), beats(S.model.length || 0));
      const tr_ = toolsRect();
      const lx = Math.max(tr_.x + tr_.w + 13, w - PAD_X - measure(len, 10, FONT.mono));
      text(g, len, lx, baseline(TOOL_H * 1.5, 10), 10, FONT.mono, color("muted-foreground"));
    }
    if (S.mode === 0) sy.drawBar(g, w - 5, 1);
    if (!S.present) text(g, vm.tr("This pattern is gone"), grid.x + 13, grid.y + 26, 11, FONT.sans, color("muted-foreground"));
  }

  // ------------------------------------------------------------------ the frame loop
  let running = false, dead = false;
  function kick() {
    if (running || dead) return;
    running = true;
    frames((now) => {
      if (dead) { running = false; return false; }
      const busy = advance(now);
      paint();
      if (!busy) running = false;
      return busy;
    });
  }
  own(host, () => { dead = true; });
  cv.onResize(kick);

  const stop = effect(() => {
    const m = vm.model.value;
    vm.lang && vm.lang.value;
    colorsVersion.value;
    ctx.box.value;
    bind(m);
    kick();
  });
  own(host, stop);

  // ------------------------------------------------------------------ input (PianoRoll::handleGesture)
  const local = (e) => { const r = el.getBoundingClientRect(); const k = r.width > 0 ? W() / r.width : 1; return { x: (e.clientX - r.left) * k, y: (e.clientY - r.top) * k }; };
  let downAt = null, swallow = false;

  function hoverAt(p) {
    let hv = -1;
    for (let i = 0; i < 5; ++i) if (inR(snapRect(i), p)) hv = i;
    for (let m = 0; m < 2; ++m) if (inR(modeRect(m), p)) hv = 10 + m;
    if (inR(quantizeRect(), p)) hv = 20;
    if (inR(lengthRect(), p)) hv = 21;
    if (inR(scaleRect(), p)) hv = 22;
    if (inR(chordRect(), p)) hv = 23;
    if (inR(toolsRect(), p)) hv = 24;
    if (inR(playRect(), p) && S.clip) hv = 30;
    if (hv !== S.hover) {
      const M = motion();
      for (const id of [S.hover, hv]) {
        if (id < 0) continue;
        if (!hoverAmt.has(id)) hoverAmt.set(id, new Anim(0));
        hoverAmt.get(id).animateTo(id === hv ? 1 : 0, M.hover, Ease.EaseOutCubic, performance.now());
      }
      S.hover = hv;
      kick();
    }
  }
  function cursorAt(p) {
    if (drag === "resize" || drag === "end") return "ew-resize";
    if (drag === "vel" || drag === "chance") return "ns-resize";
    if (inR(endRect(), p)) return "ew-resize";
    if (S.mode === 0 && inR(gridRect(), p)) { const hit = noteAt(p); if (hit && hit.edge) return "ew-resize"; }
    if (inR(velRect(), p) && (S.lane === 0 || S.lane === 3))
      for (const l of notes) if (!l.gone && Math.abs(beatToX(l.n.at) + 1.5 - p.x) <= 5) return "ns-resize";
    return "default";
  }

  function trackTo(p) {
    const vel = velRect();
    if (drag === "move") {
      live = { ...live, at: snapped(xToBeat(p.x) - grabBeat), pitch: clamp(yToPitch(p.y) - grabPitch, 0, 127) };
      if (live.pitch !== heardPitch) { heardPitch = live.pitch; preview(live.pitch, live.vel, live.length); }
    } else if (drag === "resize") {
      const step = S.snap > 0 ? S.snap : 1 / 16;
      live = { ...live, length: Math.max(step, snapped(xToBeat(p.x)) - live.at) };
    } else if (drag === "vel") {
      live = { ...live, vel: clamp(Math.round((vel.y + vel.h - 6 - p.y) / Math.max(1, VEL_H - 12) * 127), 1, 127) };
    } else if (drag === "chance") {
      live = { ...live, chance: clamp(Math.round((vel.y + vel.h - 6 - p.y) / Math.max(1, VEL_H - 12) * 100) / 100, 0, 1) };
    } else if (drag === "end") {
      liveEnd = Math.max(0.25, snapped(xToBeat(p.x)));
    }
    kick();
  }

  function onDown(e) {
    if (e.button !== 0) return;
    const p = local(e);
    drag = "none"; pressPt = p; swallow = false; downAt = p;
    el.setPointerCapture && el.setPointerCapture(e.pointerId);
    const grid = gridRect(), vel = velRect();
    if (inR(endRect(), p)) { drag = "end"; liveEnd = end.value; return; }
    if (S.mode === 0 && inR(grid, p)) {
      const hit = noteAt(p);
      if (hit) {
        const n = hit.l.n;
        drag = hit.edge ? "resize" : "move";
        pressNote = { ...n, chance: n.chance ?? 1 }; live = { ...pressNote };
        pressKey = key(n.pitch, n.at);
        grabBeat = xToBeat(p.x) - n.at; grabPitch = yToPitch(p.y) - n.pitch;
        heardPitch = n.pitch;
        takeModel(pressKey);
        preview(n.pitch, n.vel, n.length);
      }
      return;
    }
    if (S.mode === 0 && p.x < KEYS_W && p.y >= grid.y && p.y < grid.y + grid.h) { preview(yToPitch(p.y), 100, 0.5); return; }
    if (inR(vel, p) && (S.lane === 0 || S.lane === 3)) {
      for (let i = notes.length - 1; i >= 0; --i) {
        const l = notes[i];
        if (!l.gone && Math.abs(beatToX(l.n.at) + 1.5 - p.x) <= 5) {
          drag = S.lane === 3 ? "chance" : "vel";
          pressNote = { ...l.n, chance: l.n.chance ?? 1 }; live = { ...pressNote };
          pressKey = key(l.n.pitch, l.n.at);
          takeModel(pressKey);
          break;
        }
      }
    }
  }
  function onMove(e) {
    const p = local(e);
    if (downAt) { if (drag !== "none") trackTo(p); }
    else hoverAt(p);
    el.style.cursor = cursorAt(p);
  }
  function onUp(e) {
    if (!downAt) return;
    const p = local(e);
    const moved = Math.hypot(p.x - pressPt.x, p.y - pressPt.y);
    downAt = null;
    if (drag !== "none" && moved > 0.5) { trackTo(p); swallow = true; }
    const d = drag;
    drag = "none";
    const from = pressNote ? " --pitch " + pressNote.pitch + " --at " + beats(pressNote.at) : "";
    if (d === "move" && (live.at !== pressNote.at || live.pitch !== pressNote.pitch)) {
      for (const l of notes) if (key(l.n.pitch, l.n.at) === pressKey) l.instant = true;
      const nl = { n: { ...live }, alpha: new Anim(1), aLast: 1, sel: new Anim(0), selLast: false, vel: new Anim(live.vel), chance: new Anim(live.chance), velLast: live.vel, chanceLast: live.chance, placed: true, gone: false, instant: true };
      notes.push(nl);
      const k2 = key(live.pitch, live.at);
      send("note move " + pt + from + " --to-pitch " + live.pitch + " --to-at " + beats(live.at)).then((r) => {
        if (r) { sel.clear(); sel.add(k2); takeModel(k2); kick(); }
      });
    } else if (d === "resize" && live.length !== pressNote.length) {
      S.lastLength = live.length;
      for (const l of notes) if (key(l.n.pitch, l.n.at) === pressKey) l.n = { ...l.n, length: live.length };
      send("note move " + pt + from + " --length " + beats(live.length));
    } else if (d === "vel" && live.vel !== pressNote.vel) {
      for (const l of notes) if (key(l.n.pitch, l.n.at) === pressKey) { l.n = { ...l.n, vel: live.vel }; l.velLast = live.vel; l.vel.set(live.vel); }
      send("note move " + pt + from + " --vel " + live.vel);
    } else if (d === "chance" && live.chance !== pressNote.chance) {
      for (const l of notes) if (key(l.n.pitch, l.n.at) === pressKey) { l.n = { ...l.n, chance: live.chance }; l.chanceLast = live.chance; l.chance.set(live.chance); }
      send("note move " + pt + from + " --chance " + beats(live.chance));
    } else if (d === "end" && Math.abs(liveEnd - end.value) > TOL) {
      end.set(liveEnd); S.endLast = liveEnd;
      send("set " + pt + ".length=" + beats(liveEnd));
    }
    kick();
    if (swallow || moved > 4) return;
    click(p, e);
  }
  function click(p, e) {
    for (let i = 0; i < 5; ++i) if (inR(snapRect(i), p)) { S.snap = SNAPS[i]; kick(); return; }
    for (let m = 0; m < 2; ++m) if (inR(modeRect(m), p)) { S.mode = m; kick(); return; }
    if (inR(playRect(), p)) { togglePlay(); return; }
    for (let l = 0; l < 4; ++l) if (inR(laneTabRect(l), p)) { S.lane = l; kick(); return; }
    const grid = gridRect();
    if (S.mode !== 0 || !inR(grid, p)) return;
    if (e.ctrlKey || e.metaKey) return;
    const hit = noteAt(p);
    if (hit) {
      const k = key(hit.l.n.pitch, hit.l.n.at);
      if (e.shiftKey) { if (!sel.delete(k)) sel.add(k); } else { sel.clear(); sel.add(k); }
      kick();
      return;
    }
    sel.clear();
    const at = S.snap > 0 ? Math.max(0, Math.floor(xToBeat(p.x) / S.snap) * S.snap) : snapped(xToBeat(p.x));
    const pitch = yToPitch(p.y);
    send("note add " + pt + " --pitch " + pitch + " --at " + beats(at) + clickAttrs());
    preview(pitch, S.modelVel, clickLength());
    kick();
  }
  function onDelete(e) {
    e.preventDefault();
    const p = local(e);
    if (S.mode !== 0 || !inR(gridRect(), p)) return;
    const hit = noteAt(p);
    if (hit) send("note delete " + pt + " --pitch " + hit.l.n.pitch + " --at " + beats(hit.l.n.at));
  }
  function onWheel(e) {
    const p = local(e);
    e.preventDefault();
    const dy = e.deltaMode === 1 ? e.deltaY * 16 : e.deltaY;
    if (e.ctrlKey) {
      const anchor = xToBeat(p.x);
      const next = clamp(S.ppbTarget * (dy < 0 ? 1.25 : 0.8), 8, 400);
      if (next === S.ppbTarget) return;
      S.ppbTarget = next;
      ppb.animateTo(next, motion().catchUp, Ease.EaseOutCubic, performance.now());
      layout();
      sx.scrollBy(anchor * next - (p.x - KEYS_W) - sx.target());
    } else if (e.shiftKey || S.mode === 1) sx.scrollBy(dy || e.deltaX);
    else if (Math.abs(e.deltaX) > Math.abs(dy)) sx.scrollBy(e.deltaX);
    else sy.scrollBy(dy);
    kick();
  }
  function onLeave() { if (!downAt) hoverAt({ x: -1, y: -1 }); }
  el.addEventListener("pointerdown", onDown);
  el.addEventListener("pointermove", onMove);
  el.addEventListener("pointerup", onUp);
  el.addEventListener("pointercancel", () => { downAt = null; drag = "none"; kick(); });
  el.addEventListener("pointerleave", onLeave);
  el.addEventListener("dblclick", onDelete);
  el.addEventListener("contextmenu", onDelete);
  el.addEventListener("wheel", onWheel, { passive: false });
  own(host, () => {
    el.removeEventListener("pointerdown", onDown);
    el.removeEventListener("pointermove", onMove);
    el.removeEventListener("pointerup", onUp);
    el.removeEventListener("pointerleave", onLeave);
    el.removeEventListener("dblclick", onDelete);
    el.removeEventListener("contextmenu", onDelete);
    el.removeEventListener("wheel", onWheel);
  });
  kick();
}
