/*
 * Solaris — timeline/timeline.js: Timeline.cpp in the page — the ruler (markers' band, bars / beats, the loop), the lane
 * headers, the lanes' grid, the clips, the playhead; Ctrl+wheel zoom (×1.25 a step, anchored at the pointer) and the
 * scroll, both EASED (motion.scroll EaseOutCubic); click selects, drag moves, the right edge trims, a click on the ruler
 * seeks, a double-click on a note clip opens its piano roll (vm.openRoll) — each gesture sends EXACTLY the native's
 * line (Timeline::handleGesture):
 *   clip move <id> [--at <beats>] [--lane <ln>]   ·   clip duplicate <id> --at <beats> [--lane <ln>]  (Shift held)
 *   set <id>.length=<beats>   ·   transport seek <beats>   ·   transport loop <a> <b> (ruler dragged with Shift)
 * Provides vm.dnd.lanes (vm/dnd.js) and draws vm.dnd.hint.
 * NOT PORTED (yet): automation rows, chords, tempo flags, takes, the video row, groups' folding, box select, fades.
 */
import { effect, untracked } from "../core/signal.js";
import { own } from "../core/dom.js";
import { canvasIn, color, rgba, withAlpha, whiteAlpha, track, fillRR, strokeRR, colorsVersion } from "../kit/draw.js";
import { FONT, ellipsize, measure, baseline } from "../kit/text.js";
import { Anim, Ease, motion } from "../kit/anim.js";
import { useCss } from "../kit/css.js";

const kHeaderW = 117.0, kMarkH = 13.0, kRulerH = 35.75, kRowH = 48.75;
const kEdgeGrip = 6.5, kSnapPx = 11.0, kZoomPpb = 28.0, kPpq = 960;
const ZMIN = kZoomPpb / Math.pow(1.25, 8), ZMAX = kZoomPpb * Math.pow(1.25, 14);
const SPANS = [64, 32, 16, 8, 4, 1, 0.5, 0.25, 0.125, 0.0625, 0.03125]; // beats; 4 = a 4/4 bar (scaled by beatsPerBar)
const smooth01 = (x) => { const t = Math.max(0, Math.min(1, x)); return t * t * (3 - 2 * t); };
const fade = (c, a) => withAlpha(c, c[3] * a);

export function mount(host, vm, ctx) {
  useCss("timeline/timeline.css");
  host.classList.add("sl-timeline");
  const cv = canvasIn(host, ctx);
  const M = motion();
  const ppb = new Anim(kZoomPpb), sx = new Anim(0), sy = new Anim(0), head = new Anim(0);
  let model = null, rows = [], clips = [], bpb = 4, dirty = true, raf = 0, headInit = false;
  const st = { press: "", edge: "", grab: 0, dragging: false, resizing: false, dragBeat: 0, dragRow: -1, resizeLen: 0,
               down: null, ruler: false, moved: false, hover: "" };

  // ------------------------------------------------------------------ geometry
  const top = () => kRulerH;
  const beatToX = (b) => kHeaderW + (b - sx.value) * ppb.value;
  const xToBeat = (x) => (x - kHeaderW) / ppb.value + sx.value;
  const rowY = (i) => top() + i * kRowH - sy.value;
  const rowAt = (y) => (y < top() ? -1 : Math.max(0, Math.min(rows.length, Math.floor((y - top() + sy.value) / kRowH))));
  function snapStep() {
    const p = ppb.value;
    let s = 0;
    for (const sp of SPANS) { const span = sp >= 4 ? (sp / 4) * bpb : sp; if (span * p >= kSnapPx) s = span; else break; }
    return p >= ZMAX - 1e-6 ? 0 : s;
  }
  const snap = (b) => { const s = snapStep(); return s > 0 ? Math.round(b / s) * s : Math.round(b * kPpq) / kPpq; };
  const snapLabel = (s) => (s <= 0 ? "off" : s >= bpb ? (s / bpb) + " bar" : "1/" + Math.round(4 / s));
  const clipRect = (c) => {
    const r = rows.findIndex((x) => x.lane === c.lane);
    const len = c.id === st.edge && st.resizing ? st.resizeLen : c.length;
    const at = c.id === st.press && st.dragging ? st.dragBeat : c.at;
    const ri = c.id === st.press && st.dragging && st.dragRow >= 0 ? st.dragRow : r;
    return { x: beatToX(at), y: rowY(ri) + 3, w: Math.max(2, len * ppb.value), h: kRowH - 6, row: ri };
  };
  function clipAt(x, y) {
    if (x < kHeaderW || y < top()) return "";
    for (let i = clips.length - 1; i >= 0; --i) {
      const r = clipRect(clips[i]);
      if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) return clips[i].id;
    }
    return "";
  }

  // ------------------------------------------------------------------ the model
  own(host, effect(() => {
    const m = vm.model.value;
    model = m;
    if (!m) { rows = []; clips = []; invalidate(); return; }
    const sig = String(m.sig || "4/4").split("/");
    bpb = Math.max(1, parseInt(sig[0], 10) || 4);
    const strips = new Map((m.strips || []).map((s) => [s.id, s]));
    const lanes = [...(m.lanes || [])].filter((l) => !l.automation).sort((a, b) => a.order - b.order);
    clips = (m.clips || []).filter((c) => c.kind !== "automation");
    rows = lanes.map((l) => {
      const s = strips.get(l.strip) || strips.get((clips.find((c) => c.lane === l.id) || {}).track);
      const hue = l.colour >= 0 ? l.colour : s && s.colour >= 0 ? s.colour : l.order;
      const dev = s && (s.devices || []).find((d) => d.instrument);
      return { lane: l.id, strip: l.strip || "", name: l.name, sub: l.strip && dev ? dev.label : "", hue, kind: s ? s.kind : "" };
    });
    const pos = (m.transport && m.transport.position) || 0;
    if (!headInit) { head.set(pos); headInit = true; }
    else if (m.transport && m.transport.playing) head.set(pos);
    else if (Math.abs(head.target - pos) > 1e-9) head.animateTo(pos, M.slide, Ease.EaseOutCubic, performance.now());
    invalidate();
  }));
  own(host, effect(() => { colorsVersion.value; vm.lang && vm.lang.value; invalidate(); }));
  own(host, effect(() => { ctx.box.value; invalidate(); }));
  cv.onResize(() => invalidate());

  // ------------------------------------------------------------------ paint
  function invalidate() {
    dirty = true;
    if (!raf) raf = requestAnimationFrame(frame);
  }
  function frame(now) {
    raf = 0;
    let moving = false;
    for (const a of [ppb, sx, sy, head]) moving = a.update(now) || moving;
    if (dirty || moving) { dirty = false; paint(); }
    if (moving) invalidate();
  }

  function paint() {
    const g = cv.begin(), W = cv.w, H = cv.h, p = ppb.value;
    untracked(() => { if (vm.view.snapStep) vm.view.snapStep.value = snapStep(); });
    g.fillStyle = rgba(color("stage-bg")); g.fillRect(0, 0, W, H);
    const b0 = Math.max(0, Math.floor(xToBeat(kHeaderW))), b1 = xToBeat(W) + 1;
    // lanes
    g.save(); g.beginPath(); g.rect(0, top(), W, H - top()); g.clip();
    rows.forEach((r, i) => {
      const y = rowY(i);
      if (y + kRowH < top() || y > H) return;
      g.fillStyle = rgba(color(i % 2 ? "lane-alt-bg" : "lane-bg")); g.fillRect(kHeaderW, y, W - kHeaderW, kRowH);
    });
    // grid
    g.save(); g.beginPath(); g.rect(kHeaderW, top(), W - kHeaderW, H - top()); g.clip();
    const step = snapStep() || 1 / 32, lines = [[bpb, 0.10], [1, 0.05], [step, 0.03]];
    for (const [span, a] of lines) {
      if (span * p < 6) continue;
      const k = smooth01((span * p - 6) / 10);
      for (let n = Math.ceil(b0 / span); n * span <= b1; ++n) {
        const x = Math.round(beatToX(n * span)) + 0.5;
        g.strokeStyle = rgba(whiteAlpha(a * k)); g.lineWidth = 1;
        g.beginPath(); g.moveTo(x, top()); g.lineTo(x, H); g.stroke();
      }
    }
    // the loop region over the lanes
    const t = model && model.transport;
    if (t && t.loopTo > t.loopFrom) {
      g.fillStyle = rgba(fade(color("primary"), 0.06));
      g.fillRect(beatToX(t.loopFrom), top(), (t.loopTo - t.loopFrom) * p, H - top());
    }
    // clips
    const sel = new Set(vm.view.selection ? vm.view.selection.peek() || [] : []);
    for (const c of clips) {
      const r = clipRect(c);
      if (r.row < 0 || r.x > W || r.x + r.w < kHeaderW || r.y > H || r.y + r.h < top()) continue;
      const row = rows[r.row] || {};
      const strip = model && (model.strips || []).find((s) => s.id === c.track);
      const hue = track(strip && strip.colour >= 0 ? strip.colour : row.hue || 0);
      const on = sel.has(c.id), hov = st.hover === c.id;
      fillRR(g, r.x, r.y, r.w, r.h, 4, fade(hue, 0.32 + (hov ? 0.06 : 0)));
      strokeRR(g, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 4, on ? color("foreground") : fade(hue, 0.85), on ? 1.5 : 1);
      g.save(); g.beginPath(); g.rect(r.x, r.y, r.w, r.h); g.clip();
      paintPreview(g, c, r, hue);
      const name = ellipsize(c.name || c.id, Math.max(0, r.w - 10), 10, FONT.semibold);
      g.font = `10px "${FONT.semibold}"`; g.fillStyle = rgba(color("foreground"));
      g.fillText(name, r.x + 5, r.y + 12);
      if (c.offline) { g.font = `9px "${FONT.sans}"`; g.fillStyle = rgba(color("muted-foreground")); g.fillText(vm.tr("offline"), r.x + 5, r.y + r.h - 5); }
      g.restore();
    }
    // the drop hint (vm.dnd.hint)
    const hint = vm.dnd && vm.dnd.hint ? vm.dnd.hint.peek() : null;
    if (hint && hint.on && hint.row >= 0) {
      const x = beatToX(hint.beat), y = rowY(hint.row);
      fillRR(g, x, y + 3, bpb * p, kRowH - 6, 4, fade(color("primary"), 0.18));
      strokeRR(g, x + 0.5, y + 3.5, bpb * p - 1, kRowH - 7, 4, fade(color("primary"), 0.8), 1);
      if (hint.label) { g.font = `10px "${FONT.sans}"`; g.fillStyle = rgba(color("foreground")); g.fillText(vm.shown ? vm.shown(hint.label) : hint.label, x + 5, y + 16); }
    }
    g.restore();
    // headers
    g.fillStyle = rgba(color("header-bg")); g.fillRect(0, top(), kHeaderW, H - top());
    rows.forEach((r, i) => {
      const y = rowY(i);
      if (y + kRowH < top() || y > H) return;
      g.fillStyle = rgba(track(r.hue)); g.fillRect(0, y, 3, kRowH);
      g.font = `11px "${FONT.sans}"`; g.fillStyle = rgba(color("foreground"));
      g.fillText(ellipsize(vm.shown ? vm.shown(r.name) : r.name, kHeaderW - 24, 11, FONT.sans), 12, baseline(y + 17, 11) - 1);
      if (r.sub) { g.font = `9px "${FONT.sans}"`; g.fillStyle = rgba(color("muted-foreground")); g.fillText(ellipsize(r.sub, kHeaderW - 20, 9, FONT.sans), 12, baseline(y + 31, 9)); }
      if (r.kind === "instrument") {
        g.beginPath(); g.arc(kHeaderW - 17, y + 17, 6.5, 0, Math.PI * 2);
        g.strokeStyle = rgba(color("muted-foreground")); g.lineWidth = 1; g.stroke();
      }
      g.fillStyle = rgba(color("border")); g.fillRect(0, Math.round(y + kRowH) - 0.5, W, 0.5);
    });
    g.fillStyle = rgba(color("border")); g.fillRect(kHeaderW - 1, top(), 1, H - top());
    g.restore();
    paintRuler(g, W, b0, b1);
    // the playhead
    const hx = Math.round(beatToX(head.value)) + 0.5;
    if (hx >= kHeaderW - 1 && hx <= W) {
      g.strokeStyle = rgba(color("destructive")); g.lineWidth = 1;
      g.beginPath(); g.moveTo(hx, 0); g.lineTo(hx, H); g.stroke();
      g.beginPath(); g.moveTo(hx - 4.5, 0); g.lineTo(hx + 4.5, 0); g.lineTo(hx, 6); g.closePath();
      g.fillStyle = rgba(color("destructive")); g.fill();
    }
  }

  function paintPreview(g, c, r, hue) {
    if (c.kind === "note" && model) {
      const pt = (model.patterns || []).find((x) => x.id === c.pattern);
      const notes = pt && (pt.notes || []);
      if (!notes || !notes.length) return;
      let lo = 127, hi = 0;
      for (const n of notes) { lo = Math.min(lo, n.pitch ?? n.key ?? 60); hi = Math.max(hi, n.pitch ?? n.key ?? 60); }
      const span = Math.max(1, hi - lo), y0 = r.y + 18, hh = r.h - 22, plen = pt.length || c.length;
      g.fillStyle = rgba(fade(brightish(hue), 0.9));
      for (let rep = 0; rep * plen < c.length; ++rep)
        for (const n of notes) {
          const at = rep * plen + (n.at ?? n.start ?? 0) - (c.offset || 0);
          if (at < 0 || at >= c.length) continue;
          const y = y0 + (1 - ((n.pitch ?? n.key ?? 60) - lo) / span) * (hh - 2);
          g.fillRect(r.x + at * ppb.value, y, Math.max(2, (n.length ?? n.len ?? 0.25) * ppb.value - 1), 2);
        }
    } else if (c.kind === "audio") {
      g.strokeStyle = rgba(fade(brightish(hue), 0.6)); g.lineWidth = 1;
      const y = Math.round(r.y + r.h * 0.65) + 0.5;
      g.beginPath(); g.moveTo(r.x + 4, y); g.lineTo(r.x + r.w - 4, y); g.stroke();
    }
  }
  const brightish = (c) => [c[0] + (1 - c[0]) * 0.35, c[1] + (1 - c[1]) * 0.35, c[2] + (1 - c[2]) * 0.35, c[3]];

  function paintRuler(g, W, b0, b1) {
    const p = ppb.value, barPx = p * bpb;
    g.fillStyle = rgba(color("ruler-bg")); g.fillRect(0, 0, W, kRulerH);
    g.fillStyle = rgba(color("border")); g.fillRect(0, kRulerH - 0.5, W, 0.5);
    g.font = `9px "${FONT.sans}"`; g.fillStyle = rgba(color("muted-foreground"));
    const snapW = vm.tr("Snap");
    g.fillText(snapW, 12, kMarkH + 13);
    g.font = `9px "${FONT.mono}"`; g.fillStyle = rgba(color("foreground"));
    g.fillText(snapLabel(snapStep()), 12 + measure(snapW, 9, FONT.sans) + 6, kMarkH + 13);
    g.font = `8px "${FONT.semibold}"`; g.fillStyle = rgba(color("muted-foreground"));
    g.fillText(vm.tr("Markers").toUpperCase(), 12, 9);
    g.save(); g.beginPath(); g.rect(kHeaderW, 0, W - kHeaderW, kRulerH); g.clip();
    // loop brace
    const t = model && model.transport;
    if (t && t.loopTo > t.loopFrom) {
      const x0 = beatToX(t.loopFrom), x1 = beatToX(t.loopTo);
      fillRR(g, x0, kRulerH - 6, x1 - x0, 5, 2, fade(color("primary"), 0.7));
    }
    // markers' band
    for (const mk of (model && model.markers) || []) {
      const x = beatToX(mk.at), w = Math.max(4, (mk.length || 0) * p);
      g.fillStyle = rgba(whiteAlpha(0.08)); g.fillRect(x, 2, w, kMarkH - 3);
      g.fillStyle = rgba(color("muted-foreground")); g.fillRect(x, 1, 1, kMarkH - 1);
      g.font = `9px "${FONT.sans}"`; g.fillStyle = rgba(color("foreground"));
      g.fillText(ellipsize(mk.name || "", Math.max(0, w - 8), 9, FONT.sans), x + 4, 10);
    }
    // ticks
    for (let n = Math.max(0, Math.floor(b0)); n <= b1; ++n) {
      const bar = n % bpb === 0, x = Math.round(beatToX(n)) + 0.5;
      const a = bar ? 0.35 : 0.18 * smooth01((p - 6) / 10);
      if (a <= 0.001) continue;
      g.strokeStyle = rgba(whiteAlpha(a)); g.lineWidth = 1;
      g.beginPath(); g.moveTo(x, kRulerH - (bar ? 10 : 5)); g.lineTo(x, kRulerH); g.stroke();
    }
    // numbers
    const lastBar = Math.floor(b1 / bpb) + 1;
    const barNeed = measure(String(lastBar), 9, FONT.mono) + 8, beatNeed = measure(lastBar + ".8", 9, FONT.mono) + 8;
    const beatA = bpb > 1 ? smooth01((p - beatNeed) / 10) : 0;
    g.font = `9px "${FONT.mono}"`;
    for (let n = Math.max(0, Math.floor(b0)); n <= b1; ++n) {
      const bar = Math.floor(n / bpb), beat = n % bpb;
      let a = beatA, s = String(bar + 1);
      if (beat === 0) { let every = 64; while (every > 1 && bar % every !== 0) every /= 2; a = smooth01((every * barPx - barNeed) / 10); }
      else s += "." + (beat + 1);
      if (a <= 0.001) continue;
      g.fillStyle = rgba(fade(color("muted-foreground"), (beat === 0 ? 1 : 0.7) * a));
      g.fillText(s, Math.round(beatToX(n)) + 0.5 + 4, kMarkH + 13);
    }
    g.restore();
  }

  // ------------------------------------------------------------------ gestures
  const local = (e) => { const pt = ctx.screenPoint(e), b = ctx.box.peek(); return { x: pt.x - b.x, y: pt.y - b.y }; };
  const beats = (b) => vm.beatText(b);
  const clipById = (id) => clips.find((c) => c.id === id);
  function select(id) {
    if (vm.view.selection) vm.view.selection.value = id ? [id] : [];
    if (vm.view.selectedClip) vm.view.selectedClip.value = id || "";
    invalidate();
  }
  const el = cv.el;
  el.style.touchAction = "none";
  el.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    const L = local(e);
    el.setPointerCapture(e.pointerId);
    st.down = L; st.moved = false; st.ruler = L.y < kRulerH && L.x >= kHeaderW;
    st.press = clipAt(L.x, L.y); st.edge = "";
    if (st.press) {
      const r = clipRect(clipById(st.press));
      if (L.x >= r.x + r.w - kEdgeGrip && r.w > 3 * kEdgeGrip) { st.edge = st.press; st.press = ""; }
      else st.grab = xToBeat(L.x) - clipById(st.press).at;
    }
    if (vm.view.interacting) vm.view.interacting.value = true;
  });
  el.addEventListener("pointermove", (e) => {
    const L = local(e);
    if (!st.down) {
      const id = clipAt(L.x, L.y);
      const edge = id && (() => { const r = clipRect(clipById(id)); return L.x >= r.x + r.w - kEdgeGrip && r.w > 3 * kEdgeGrip; })();
      el.style.cursor = edge ? "ew-resize" : id ? "grab" : "";
      if (id !== st.hover) { st.hover = id; invalidate(); }
      return;
    }
    if (!st.moved && Math.hypot(L.x - st.down.x, L.y - st.down.y) < 3) return;
    if (!st.moved) {
      st.moved = true;
      if (st.edge) { st.resizing = true; st.resizeLen = clipById(st.edge).length; select(st.edge); }
      if (st.press) { st.dragging = true; select(st.press); }
    }
    if (st.resizing) {
      const c = clipById(st.edge);
      if (c) st.resizeLen = Math.max(Math.max(snapStep(), 1 / kPpq), snap(xToBeat(L.x)) - c.at);
    } else if (st.dragging) {
      const c = clipById(st.press);
      st.dragBeat = Math.max(0, snap(xToBeat(L.x) - st.grab));
      let row = rowAt(L.y);
      const own = rows.findIndex((r) => r.lane === c.lane);
      if (row < 0 || row >= rows.length || !rows[row].lane) row = own;
      st.dragRow = row;
    } else if (st.ruler) {
      head.set(Math.max(0, xToBeat(L.x)));
    }
    invalidate();
  });
  const up = (e, cancel) => {
    if (!st.down) return;
    const L = local(e);
    if (vm.view.interacting) vm.view.interacting.value = false;
    if (st.resizing) {
      const c = clipById(st.edge);
      st.resizing = false;
      if (!cancel && c && Math.abs(st.resizeLen - c.length) > 1e-9) { c.length = st.resizeLen; vm.cmd("set " + c.id + ".length=" + beats(st.resizeLen)); }
    } else if (st.dragging) {
      const c = clipById(st.press);
      st.dragging = false;
      if (!cancel && c) {
        const ownRow = rows.findIndex((r) => r.lane === c.lane);
        if (e.shiftKey) {
          let line = "clip duplicate " + c.id + " --at " + beats(st.dragBeat);
          if (st.dragRow !== ownRow) line += " --lane " + vm.q(rows[st.dragRow].lane);
          if (vm.view.expectCopy) vm.view.expectCopy.value = { instant: true };
          vm.cmd(line);
        } else {
          let line = "clip move " + c.id, any = false;
          if (Math.abs(st.dragBeat - c.at) > 1e-9) { line += " --at " + beats(st.dragBeat); any = true; }
          if (st.dragRow !== ownRow) { line += " --lane " + vm.q(rows[st.dragRow].lane); any = true; }
          if (any) { c.at = st.dragBeat; c.lane = rows[st.dragRow].lane; vm.cmd(line); }
        }
      }
    } else if (!cancel && st.ruler) {
      vm.cmd("transport seek " + beats(snap(xToBeat(L.x))));
    } else if (!cancel && !st.moved) {
      select(st.press || st.edge);
    }
    st.down = null; st.press = ""; st.edge = "";
    invalidate();
  };
  el.addEventListener("pointerup", (e) => up(e, false));
  el.addEventListener("pointercancel", (e) => up(e, true));
  el.addEventListener("dblclick", (e) => {
    const L = local(e), ri = rowAt(L.y), id = clipAt(L.x, L.y);
    if (id) {
      // a note clip: its piano roll, for that clip (Timeline::onOpenPattern, R-ROLL-1, R-ROLL-6)
      const c = clipById(id);
      if (c && c.kind === "note" && c.pattern && vm.openRoll) vm.openRoll(c.pattern, c.id);
      return;
    }
    if (L.x < kHeaderW || ri < 0 || ri >= rows.length || !rows[ri].lane) return;
    vm.cmd("clip add --lane " + vm.q(rows[ri].lane) + " --at " + beats(snap(xToBeat(L.x))) + " --length " + beats(bpb));
  });
  el.addEventListener("wheel", (e) => {
    e.preventDefault();
    const L = local(e), now = performance.now();
    if (e.ctrlKey || e.metaKey) {
      const anchor = xToBeat(Math.max(kHeaderW, L.x));
      const want = Math.max(ZMIN, Math.min(ZMAX, ppb.target * (e.deltaY < 0 ? 1.25 : 0.8)));
      // keep the beat under the pointer where it is: scroll target follows the zoom target
      const sxT = Math.max(0, anchor - (Math.max(kHeaderW, L.x) - kHeaderW) / want);
      ppb.animateTo(want, M.scroll, Ease.EaseOutCubic, now);
      sx.animateTo(sxT, M.scroll, Ease.EaseOutCubic, now);
    } else if (e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
      const d = (e.shiftKey ? e.deltaY : e.deltaX) / ppb.target;
      sx.animateTo(Math.max(0, sx.target + d), M.scroll, Ease.EaseOutCubic, now);
    } else {
      const maxY = Math.max(0, (rows.length + 1) * kRowH - (cv.h - top()));
      sy.animateTo(Math.max(0, Math.min(maxY, sy.target + e.deltaY)), M.scroll, Ease.EaseOutCubic, now);
    }
    invalidate();
  }, { passive: false });

  // ------------------------------------------------------------------ the browser's drop target (vm/dnd.js)
  if (vm.dnd) {
    vm.dnd.lanes = {
      probe(pt) {
        const b = ctx.box.peek(), x = pt.x - b.x, y = pt.y - b.y;
        if (x < kHeaderW || x > b.w || y < top() || y > b.h) return null;
        return { beat: Math.max(0, snap(xToBeat(x))), row: rowAt(y) };
      },
      rowCount: () => rows.length,
      rowLane: (row) => (rows[row] ? rows[row].lane : ""),
      rowTrack: (row) => (rows[row] && rows[row].kind === "instrument" ? rows[row].strip : ""),
    };
    if (vm.dnd.hint) own(host, effect(() => { vm.dnd.hint.value; invalidate(); }));
  }
  own(host, effect(() => { if (vm.view.selection) vm.view.selection.value; invalidate(); }));
  own(host, () => { if (raf) cancelAnimationFrame(raf); if (vm.dnd && vm.dnd.lanes) vm.dnd.lanes = null; });
  invalidate();
}
