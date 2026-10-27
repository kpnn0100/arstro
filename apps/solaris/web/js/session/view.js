/*
 * Solaris — session/view.js: SessionView (app/widgets/SessionView.{h,cpp}, R-LAUNCH-1) — the session grid, in the
 * lanes' place (View › Session), drawn into one canvas line for line as the native paints it.
 *
 * A column per strip that can hold a slot (instrument and audio strips, in processing order: the mixers' order, then
 * each strip's), a row per scene (its name and ▶ at the left), a slot's name in its cell. Every gesture is ONE line,
 * the native's exact text: a slot clicked `launch <sl>`; a scene's ▶ `launch <sc>`; a column's ■ `launch stop <ch>`;
 * Back to Arrangement `launch back`; "+ Scene" `scene add`; an empty cell offers the song's patterns (an instrument's
 * column) or the strip's audio clips → `slot add <sc> <ch> --pattern <pt>` / `--clip <cl>`; a slot's right-click
 * Launch · Rename… (`set <sl>.name=<quoted>`) · Delete Slot (`slot delete <sl>`); a scene's name right-clicked Launch
 * Scene · Rename… · Delete Scene (`scene delete <sc>`). It draws only the model: `transport.launched` from vm.model
 * (the live values merged — a loop's progress moves frame by frame, like the playhead).
 *
 * Motion (§1, nothing snaps): per slot id, play eased kCrossFadeMs and queued eased kSelectMs (EaseOutCubic); a queued
 * slot's outline pulses on a 500 ms sine (a timing, not a tween); hover washes cross-fade (HoverFade, kHoverMs); the
 * grid scrolls both ways (wheel; Shift sideways), eased (EasedScroll, kScrollMs). The first placement does not travel.
 *
 * Constants (SessionView.h): kHeadH 32, kSceneW 136, kColW 132, kRowH 40, kFootH 36.
 */
import { effect, untracked } from "../core/signal.js";
import { own } from "../core/dom.js";
import { Anim, Ease, frames, motion, EasedScroll } from "../kit/anim.js";
import { canvasIn, color, colorsVersion, fillRR, strokeRR, line, text, track, lerpColor, whiteAlpha, hoverWash, withAlpha } from "../kit/draw.js";
import { FONT, measure, ellipsize, baseline } from "../kit/text.js";
import { glyph } from "../kit/glyphs.js";
import { useCss } from "../kit/css.js";
import { tr, say } from "../vm/words.js";

export const HEAD_H = 32, SCENE_W = 136, COL_W = 132, ROW_H = 40, FOOT_H = 36;
const PULSE_MS = 500;
const K_BACK = 1, K_ADD_SCENE = 2, K_CELL = 1000, K_SCENE = 100000, K_STOP = 200000;
const RADIUS_CONTROL = 2, RADIUS_PILL = 9999;

const fade = (c, a) => [c[0], c[1], c[2], c[3] * a];
const inside = (r, p) => r && p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;
/** SessionView's quoted(): a `"` becomes `'`; wrapped in quotes when empty or holding a space, tab, ; = or #. */
function quoted(v) {
  v = String(v).replace(/"/g, "'");
  return v && !/[ \t;=#]/.test(v) ? v : '"' + v + '"';
}

export function mount(host, vm, ctx) {
  useCss("session/view.css");
  host.classList.add("sl-session");
  const cv = canvasIn(host, ctx);

  // ---- the model, as bound (SessionView::bind)
  let cols = [], scenes = [], slots = [], patterns = [], clips = [], launched = [];
  function bind(m) {
    if (!m) { cols = scenes = slots = patterns = clips = launched = []; return; }
    const mixers = m.mixers || [];
    const mixOrder = (id) => { const x = mixers.find((y) => y.id === id); return x ? x.order || 0 : 0; };
    cols = (m.strips || []).filter((x) => x.kind === "instrument" || x.kind === "audio")
      .map((x, i) => ({ x, i }))
      .sort((a, b) => {
        const ma = mixOrder(a.x.mixer), mb = mixOrder(b.x.mixer);
        if (ma !== mb) return ma - mb;
        if ((a.x.order || 0) !== (b.x.order || 0)) return (a.x.order || 0) - (b.x.order || 0);
        return a.i - b.i; // stable
      }).map((y) => y.x);
    scenes = (m.scenes || []).slice().sort((a, b) => (a.order || 0) - (b.order || 0));
    slots = m.slots || [];
    patterns = m.patterns || [];
    clips = m.clips || [];
    launched = (m.transport && m.transport.launched) || [];
  }

  const colOf = (strip) => cols.findIndex((c) => c.id === strip);
  const rowOf = (scene) => scenes.findIndex((s) => s.id === scene);
  const slotAt = (scene, strip) => slots.find((x) => x.scene === scene && x.strip === strip) || null;

  // ---- live state
  const live = new Map();          // slot id -> {play, queued, playWas, queuedWas}
  const sx = new EasedScroll(), sy = new EasedScroll();
  const hover = new Map();         // hover id -> Anim
  let hovered = -1, nowMs = 0;

  const size = () => { const b = ctx.box.peek(); return { W: b.w, H: b.h }; };
  function cellRect(scene, strip) {
    const c = colOf(strip), r = rowOf(scene);
    if (c < 0 || r < 0) return null;
    return { x: SCENE_W + c * COL_W - sx.value() + 3, y: HEAD_H + r * ROW_H - sy.value() + 3, w: COL_W - 6, h: ROW_H - 6 };
  }
  function sceneLaunchRect(scene) {
    const r = rowOf(scene);
    if (r < 0) return null;
    return { x: SCENE_W - 32, y: HEAD_H + r * ROW_H - sy.value() + (ROW_H - 24) * 0.5, w: 24, h: 24 };
  }
  function stopRect(strip) {
    const c = colOf(strip);
    if (c < 0) return null;
    return { x: SCENE_W + c * COL_W - sx.value() + (COL_W - 28) * 0.5, y: HEAD_H + scenes.length * ROW_H - sy.value() + 6, w: 28, h: 24 };
  }
  const backRect = () => ({ x: 8, y: 4, w: SCENE_W - 16, h: HEAD_H - 8 });
  const addSceneRect = () => ({ x: 8, y: HEAD_H + scenes.length * ROW_H - sy.value() + 6, w: SCENE_W - 48, h: 24 });

  function hoverId(p) {
    if (inside(backRect(), p)) return K_BACK;
    if (inside(addSceneRect(), p) && p.y > HEAD_H) return K_ADD_SCENE;
    for (let r = 0; r < scenes.length; r++) if (p.y > HEAD_H && inside(sceneLaunchRect(scenes[r].id), p)) return K_SCENE + r;
    for (let c = 0; c < cols.length; c++) {
      if (p.x < SCENE_W) break;
      if (p.y > HEAD_H && inside(stopRect(cols[c].id), p)) return K_STOP + c;
      for (let r = 0; r < scenes.length; r++) if (p.y > HEAD_H && inside(cellRect(scenes[r].id, cols[c].id), p)) return K_CELL + r * cols.length + c;
    }
    return -1;
  }
  const hv = (id) => { const a = hover.get(id); return a ? a.value : 0; };

  // ---- advance (SessionView::advance): returns true while anything moves
  function advance(now) {
    nowMs = now;
    let moving = false;
    const M = motion();
    for (const sl of slots) {
      let playing = false, queued = false;
      for (const l of launched)
        if (l.strip === sl.strip) { playing = !!l.session && l.slot === sl.id; queued = l.next === sl.id; }
      let v = live.get(sl.id);
      if (!v) {
        v = { play: new Anim(playing ? 1 : 0), queued: new Anim(queued ? 1 : 0), playWas: playing, queuedWas: queued };
        live.set(sl.id, v);
      }
      if (playing !== v.playWas) { v.play.animateTo(playing ? 1 : 0, M.crossFade, Ease.EaseOutCubic, now); v.playWas = playing; }
      if (queued !== v.queuedWas) { v.queued.animateTo(queued ? 1 : 0, M.select, Ease.EaseOutCubic, now); v.queuedWas = queued; }
      if (v.play.update(now)) moving = true;
      if (v.queued.update(now)) moving = true;
      if (playing || queued) moving = true; // a loop's progress, a pulse: frame by frame
    }
    const { W, H } = size();
    sx.setExtent(SCENE_W, Math.max(0, W - SCENE_W), cols.length * COL_W);
    sy.setExtent(HEAD_H, Math.max(0, H - HEAD_H), scenes.length * ROW_H + FOOT_H);
    if (sx.advance(now)) moving = true;
    if (sy.advance(now)) moving = true;
    for (const [id, a] of hover) {
      const want = id === hovered ? 1 : 0;
      if (a.target !== want) a.animateTo(want, M.hover, Ease.EaseOutCubic, now);
      if (a.update(now)) moving = true;
      else if (a.value === 0 && id !== hovered) hover.delete(id);
    }
    return moving;
  }

  // ---- paint (SessionView::onPaint)
  function paint() {
    const g = cv.begin();
    const { W, H } = size();
    const C = {
      canvas: color("canvas-bg"), rail: color("left-rail-bg"), border: color("border"), muted: color("muted-foreground"),
      fg: color("foreground"), secondary: color("secondary"), card: color("card"), secFg: color("secondary-foreground"),
    };
    const colourOf = (s) => (s.colour >= 0 ? track(s.colour) : C.secFg);
    const scx = sx.value(), scy = sy.value();
    fillRR(g, 0, 0, W, H, 0, C.canvas);

    // the cells — clipped to the grid, scrolled both ways
    g.save();
    g.beginPath(); g.rect(SCENE_W, HEAD_H, Math.max(0, W - SCENE_W), Math.max(0, H - HEAD_H)); g.clip();
    for (let c = 0; c < cols.length; c++) {
      const st = cols[c];
      const x = SCENE_W + c * COL_W - scx;
      if (x + COL_W < SCENE_W || x > W) continue;
      const col = colourOf(st);
      for (let r = 0; r < scenes.length; r++) {
        const cr = cellRect(scenes[r].id, st.id);
        if (cr.y + cr.h < HEAD_H || cr.y > H) continue;
        const h = hv(K_CELL + r * cols.length + c);
        const sl = slotAt(scenes[r].id, st.id);
        if (!sl) {
          fillRR(g, cr.x, cr.y, cr.w, cr.h, RADIUS_CONTROL, whiteAlpha(0.02 + 0.04 * h));
          strokeRR(g, cr.x, cr.y, cr.w, cr.h, RADIUS_CONTROL, C.border, 1);
          if (h > 0.001) text(g, "+", cr.x + (cr.w - measure("+", 12, FONT.sans)) * 0.5, baseline(cr.y + cr.h * 0.5, 12), 12, FONT.sans, fade(C.muted, h));
          continue;
        }
        const lv = live.get(sl.id);
        const play = lv ? lv.play.value : 0, queued = lv ? lv.queued.value : 0;
        fillRR(g, cr.x, cr.y, cr.w, cr.h, RADIUS_CONTROL, lerpColor(lerpColor(C.secondary, C.card, h), fade(col, 0.45), play));
        // queued: an outline in its colour, pulsing until it lands
        if (queued > 0.001) {
          const pulse = 0.5 + 0.5 * Math.sin((nowMs / PULSE_MS) * 2 * Math.PI);
          g.beginPath();
          g.moveTo(cr.x + 0.75, cr.y + 0.75); g.lineTo(cr.x + cr.w - 0.75, cr.y + 0.75); g.lineTo(cr.x + cr.w - 0.75, cr.y + cr.h - 0.75);
          g.lineTo(cr.x + 0.75, cr.y + cr.h - 0.75); g.lineTo(cr.x + 0.75, cr.y + 0.75);
          g.strokeStyle = rgbaOf(fade(col, queued * (0.35 + 0.65 * pulse))); g.lineWidth = 1.5; g.stroke();
        }
        // ▶, then its name
        glyph.play(g, { x: cr.x + 8, y: cr.y + (cr.h - 9) * 0.5, w: 9, h: 9 }, lerpColor(C.muted, C.fg, Math.max(play, h)));
        let name = sl.name || "";
        if (!name) for (const pt of patterns) if (pt.id === sl.pattern) name = pt.name;
        if (!name) name = sl.pattern ? sl.pattern : String(sl.src || "").slice(String(sl.src || "").lastIndexOf("/") + 1);
        text(g, ellipsize(name, Math.max(0, cr.w - 30), 11, FONT.sans), cr.x + 24, baseline(cr.y + cr.h * 0.5, 11), 11, FONT.sans, C.fg);
        // playing: its loop's progress along its foot
        if (play > 0.001)
          for (const l of launched)
            if (l.strip === st.id && l.slot === sl.id && l.session)
              fillRR(g, cr.x + 3, cr.y + cr.h - 4, Math.max(0, (cr.w - 6) * Math.min(1, Math.max(0, +l.progress || 0))), 2, RADIUS_PILL, fade(col, play));
      }
      // its ■, under the last scene
      const sr = stopRect(st.id);
      const h = hv(K_STOP + c);
      fillRR(g, sr.x, sr.y, sr.w, sr.h, RADIUS_CONTROL, hoverWash(0.4 + 0.6 * h));
      fillRR(g, sr.x + (sr.w - 8) * 0.5, sr.y + (sr.h - 8) * 0.5, 8, 8, RADIUS_CONTROL, lerpColor(C.muted, C.fg, h));
    }
    g.restore();

    // the strips' heads: colour and name, scrolled sideways only
    fillRR(g, 0, 0, W, HEAD_H, 0, C.rail);
    g.save();
    g.beginPath(); g.rect(SCENE_W, 0, Math.max(0, W - SCENE_W), HEAD_H); g.clip();
    for (let c = 0; c < cols.length; c++) {
      const x = SCENE_W + c * COL_W - scx;
      fillRR(g, x + 3, 4, COL_W - 6, 3, RADIUS_PILL, colourOf(cols[c]));
      text(g, ellipsize(cols[c].name || "", COL_W - 12, 11, FONT.medium), x + 6, 23, 11, FONT.medium, C.fg);
    }
    g.restore();
    line(g, 0, HEAD_H - 0.5, W, HEAD_H - 0.5, C.border, 1);

    // the scenes' column: names and ▶, scrolled up and down only; Back to Arrangement above; "+ Scene" under
    fillRR(g, 0, HEAD_H, SCENE_W, Math.max(0, H - HEAD_H), 0, C.rail);
    g.save();
    g.beginPath(); g.rect(0, HEAD_H, SCENE_W, Math.max(0, H - HEAD_H)); g.clip();
    for (let r = 0; r < scenes.length; r++) {
      const y = HEAD_H + r * ROW_H - scy;
      text(g, ellipsize(scenes[r].name || "", SCENE_W - 52, 11, FONT.sans), 12, baseline(y + ROW_H * 0.5, 11), 11, FONT.sans, C.fg);
      const lr = sceneLaunchRect(scenes[r].id);
      const h = hv(K_SCENE + r);
      fillRR(g, lr.x, lr.y, lr.w, lr.h, RADIUS_CONTROL, hoverWash(0.3 + 0.7 * h));
      glyph.play(g, { x: lr.x + 8, y: lr.y + 7.5, w: 9, h: 9 }, lerpColor(C.muted, C.fg, h));
    }
    const ar = addSceneRect();
    const ah = hv(K_ADD_SCENE);
    if (ah > 0.001) fillRR(g, ar.x, ar.y, ar.w, ar.h, RADIUS_CONTROL, hoverWash(ah));
    text(g, ellipsize(tr("+ Scene"), ar.w - 8, 11, FONT.sans), ar.x + 6, baseline(ar.y + ar.h * 0.5, 11), 11, FONT.sans, lerpColor(C.muted, C.fg, ah));
    g.restore();
    const br = backRect();
    const bh = hv(K_BACK);
    fillRR(g, br.x, br.y, br.w, br.h, RADIUS_CONTROL, lerpColor(C.secondary, C.card, bh));
    strokeRR(g, br.x, br.y, br.w, br.h, RADIUS_CONTROL, C.border, 1);
    text(g, ellipsize(tr("Back to Arrangement"), br.w - 12, 10, FONT.sans), br.x + 6, baseline(br.y + br.h * 0.5, 10), 10, FONT.sans, lerpColor(C.secFg, C.fg, bh));
    if (!scenes.length) {
      const hint = tr("No scenes yet — \"+ Scene\" adds one; a cell takes a pattern or a clip.");
      text(g, ellipsize(hint, Math.max(0, W - SCENE_W - 24), 11, FONT.sans), SCENE_W + 12, HEAD_H + 24, 11, FONT.sans, C.muted);
    }
    // the bars, only when there is more to see (R6)
    if (sy.scrollable()) {
      const vh = Math.max(0, H - HEAD_H), th = Math.max(18, (vh * vh) / Math.max(vh, sy.contentH));
      fillRR(g, W - 5, HEAD_H + (vh - th) * (scy / Math.max(1, sy.maxScroll())), 3, th, RADIUS_PILL, whiteAlpha(0.18));
    }
    if (sx.scrollable()) {
      const vw = Math.max(0, W - SCENE_W), tw = Math.max(18, (vw * vw) / Math.max(vw, sx.contentH));
      fillRR(g, SCENE_W + (vw - tw) * (scx / Math.max(1, sx.maxScroll())), H - 5, tw, 3, RADIUS_PILL, whiteAlpha(0.18));
    }
  }
  const rgbaOf = (c) => `rgba(${Math.round(c[0] * 255)}, ${Math.round(c[1] * 255)}, ${Math.round(c[2] * 255)}, ${Math.max(0, c[3])})`;

  // ---- the frame loop: advance + paint while anything moves; a model change or a hover kicks it
  let running = false, dead = false;
  function tick(now) {
    if (dead) { running = false; return false; }
    let moving = false;
    try { moving = advance(now); paint(); } catch (e) { console.error(e); }
    running = moving;
    return moving;
  }
  function kick() { if (!running && !dead) { running = true; frames(tick); } }
  cv.onResize(kick);

  own(host, effect(() => {
    const m = vm.model.value;
    colorsVersion.value;
    ctx.box.value;
    untracked(() => { bind(m); kick(); });
  }));
  own(host, effect(() => { tr("Back to Arrangement"); untracked(kick); }));  // the language changed: redraw

  // ---- input (SessionView::handleGesture)
  const localOf = (e) => { const p = ctx.screenPoint(e), b = ctx.box.peek(); return { x: p.x - b.x, y: p.y - b.y }; };
  const send = (l) => vm.cmd(l);
  function setHovered(id) {
    if (id === hovered) return;
    hovered = id;
    if (id >= 0 && !hover.has(id)) hover.set(id, new Anim(0));
    kick();
  }
  function rename(current, p, id) {
    ctx.menu.openRename(current || "", p.x, p.y, (n) => { if (n) send("set " + id + ".name=" + quoted(n)); }, { title: say("Rename") });
  }
  function emptyCellMenu(scene, strip, p) {
    const items = [];
    if (strip.kind === "instrument")
      for (const pt of patterns) items.push({ label: pt.name || pt.id, action: () => send("slot add " + scene + " " + strip.id + " --pattern " + pt.id) });
    else
      for (const c of clips)
        if (c.track === strip.id && c.kind === "audio") items.push({ label: c.name || c.id, action: () => send("slot add " + scene + " " + strip.id + " --clip " + c.id) });
    if (!items.length) items.push({ label: say(strip.kind === "instrument" ? "(the song has no patterns yet)" : "(no clips on this strip yet)"), action: () => {} });
    ctx.menu.open(items, p.x, p.y);
  }
  function gesture(e, right) {
    const p = localOf(e), at = ctx.screenPoint(e);
    const id = hoverId(p);
    if (id === K_BACK) return send("launch back");
    if (id === K_ADD_SCENE) return send("scene add");
    if (id >= K_STOP) return send("launch stop " + cols[id - K_STOP].id);
    if (id >= K_SCENE && !right) return send("launch " + scenes[id - K_SCENE].id);
    // a scene's name, right-clicked: its menu
    if (p.x < SCENE_W && p.y > HEAD_H) {
      const r = Math.floor((p.y + sy.value() - HEAD_H) / ROW_H);
      if (right && r >= 0 && r < scenes.length) {
        const sc = scenes[r];
        ctx.menu.open([
          { label: say("Launch Scene"), action: () => send("launch " + sc.id) },
          { label: say("Rename…"), action: () => rename(sc.name, at, sc.id) },
          { label: say("Delete Scene"), action: () => send("scene delete " + sc.id) },
        ], at.x, at.y);
      }
      return;
    }
    if (id >= K_CELL) {
      const k = id - K_CELL, r = Math.floor(k / cols.length), c = k % cols.length;
      const sc = scenes[r], st = cols[c];
      const sl = slotAt(sc.id, st.id);
      if (!sl) return emptyCellMenu(sc.id, st, at);
      if (!right) return send("launch " + sl.id);
      ctx.menu.open([
        { label: say("Launch"), action: () => send("launch " + sl.id) },
        { label: say("Rename…"), action: () => rename(sl.name, at, sl.id) },
        { label: say("Delete Slot"), action: () => send("slot delete " + sl.id) },
      ], at.x, at.y);
    }
  }
  const el = cv.el;
  let downAt = null;
  el.addEventListener("pointermove", (e) => setHovered(hoverId(localOf(e))));
  el.addEventListener("pointerleave", () => setHovered(-1));
  el.addEventListener("pointerdown", (e) => { if (e.button === 0) downAt = { x: e.clientX, y: e.clientY }; });
  el.addEventListener("pointerup", (e) => {
    if (e.button !== 0 || !downAt) return;
    const moved = Math.hypot(e.clientX - downAt.x, e.clientY - downAt.y) > 4;
    downAt = null;
    if (!moved) gesture(e, false);
  });
  el.addEventListener("contextmenu", (e) => { e.preventDefault(); gesture(e, true); });
  el.addEventListener("wheel", (e) => {
    // the wheel: up and down; Shift: sideways — a grid that cannot move lets it go on
    const k = e.deltaMode === 1 ? 40 : e.deltaMode === 2 ? 400 : 1;
    const took = e.shiftKey ? sx.scrollBy((e.deltaY || e.deltaX) * k) : sy.scrollBy(e.deltaY * k);
    if (took) { e.preventDefault(); kick(); }
  }, { passive: false });

  own(host, () => { dead = true; });
  kick();
}
