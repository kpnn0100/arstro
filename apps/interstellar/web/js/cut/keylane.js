/*
 * Interstellar — cut/keylane.js: the timeline's ANIMATION section (R-ANIM-9). Ports app/widgets/KeyLane.{h,cpp}.
 *
 * Mounted by cut/timeline.js with ctx.slot in the section's rect (ctx.box: the WHOLE section, its 22.75 px
 * header and the body, ROOT px). It reads the timeline's axis from vm "timeline" (toX / toT in the
 * timeline's local x, which is ours: the section starts at x 0) and provides
 *   vm "keyLane"  { contentHeight() }    KeyLane::contentHeight - the timeline sizes the section from it
 *   vm "keyGraph" { selectionList(), selectionCount(), now(), shown() }   the keys' Ctrl+C/V (shell.js)
 *
 * The header: the fold (▾ turns with the timeline's eased keyOpen, calls toggleKeys), ANIMATION and how many
 * are marked, "＋ Animate…" (cut/keymenus.js openAnimateMenu). Under it, while nothing is marked, the hint;
 * else ONE LANE PER MARKED PROPERTY, sorted by object, group, label: its name on two lines, its diamond
 * (keys at the playhead, only over a clip of its object: `key add|remove`), its mode (OFFSET | FIXED,
 * `key mode`), and on the timeline's axis every clip of its object with the keys under the frames they key.
 * The playhead runs through the lanes. Choosing a lane opens its curve (cut/keygraph.js) in a band under it,
 * eased; choosing it again closes it; Ctrl/Shift adds curves to the band (R-ANIM-7).
 *
 * Gestures (KeyLane.cpp:354-470): a key dragged in time on the clip it was grabbed in, between its
 * neighbours, on the millisecond grid - on release ONE line `key set <a> --at <from> --to <t>`; a double-click
 * in a clip of the lane `key add <a> --at <t>`; right-click a key: openKeyContext, a lane: openKeyRowContext,
 * the curve band's empty plot: openKeyPlotContext. Its top 3 px are the timeline's resize grip.
 *
 * Motion (KeyLane.cpp:300-350): a lane eases in when marked and out when unmarked (motion-slide), a curve
 * band opens / closes (motion-slide), a diamond's fill and outline ease 180 ms, the hint fades with the
 * lanes, the body scrolls eased (kit/easedscroll.js).
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { q, num } from "../kit/cmd.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { EasedScroll, wheelPx } from "../kit/easedscroll.js";
import { Painter, canvasSvg, col, css, fade, lerp, whiteA, primaryA, hoverWash, Anim, HoverFade, frameLoop, localPoint, inRect, Ease, F } from "../kit/paint.js";
import { describe, toggle } from "./key/keystate.js";
import { KeyGraph } from "./keygraph.js";
import { openKeyContext, openKeyPlotContext, openAnimateMenu, openKeyRowContext } from "./keymenus.js";

// KeyLane.h:43-48, KeyLane.cpp:15-21
const HEAD_H = 22.75, ROW_H = 29.25, HINT_H = 22.75, PAD = 4.875, GRAPH_H = 104, KEY_R = 3.5;
const PAD_X = 9.75, DIAMOND_W = 16, MODE_W = 34, ADD_W = 84, GAP_T = 1e-3;
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const spanDur = (c) => (c.duration > 0 ? c.duration : (c.out - c.in) / Math.max(1e-6, c.speed));
const HINT = "Nothing is animated — mark a property in Grade (its diamond) or Animate…";

export function mount(host, vm, ctx) {
  useCss("css/cut/keylane.css");
  host.classList.add("kl");
  const svg = canvasSvg();
  host.append(svg);
  const P = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());
  const ms = (n) => tok.ms(n);
  const TL = () => vm.provided("timeline");

  let W = 0, H = 0, colW = 117, playhead = 0, openAmt = 1, model = null, everBound = false;
  let rows = [], selected = "", shownRows = [], pendingSelect = "", revealFront = false, lastContentH = -1;
  const present = new Map(), band = new Map(), dia = new Map();
  const scroll = new EasedScroll(() => loop.kick());
  const hover = new HoverFade();
  const graph = new KeyGraph();
  let keyDrag = null, press = null;

  const xOf = (t) => { const tl = TL(); return tl ? tl.toX(t) : t; };
  const tOf = (x) => { const tl = TL(); return tl ? tl.toT(x) : x; };
  const keyX = (s, k) => xOf(s.at + (k - s.in) / Math.max(1e-6, s.speed));

  // ---------------------------------------------------------------- geometry (KeyLane.cpp:200-280)
  const rowAmount = (i) => { const r = rows[i]; if (!r) return 0; const a = present.get(r.prop.address); return a ? a.anim.value : r.gone ? 0 : 1; };
  const bandAmount = (i) => { const r = rows[i]; if (!r) return 0; const a = band.get(r.prop.address); return a ? a.anim.value : 0; };
  const hintAmount = () => { let none = 1; for (let i = 0; i < rows.length; i++) none = Math.min(none, 1 - rowAmount(i)); return none; };
  const rowH = (i) => ROW_H * rowAmount(i);
  const bandH = (i) => GRAPH_H * bandAmount(i) * rowAmount(i);
  function rowTop(i) { let y = HEAD_H + PAD + HINT_H * hintAmount(); for (let k = 0; k < i; k++) y += rowH(k) + bandH(k); return y; }
  function contentH() { let h = PAD + HINT_H * hintAmount(); for (let i = 0; i < rows.length; i++) h += rowH(i) + bandH(i); return h + PAD; }
  const foldRect = () => ({ x: 0, y: 0, w: colW, h: HEAD_H });
  const addRect = () => ({ x: colW + PAD_X - 4, y: 3, w: ADD_W, h: HEAD_H - 6 });
  const rowRect = (i) => ({ x: 0, y: rowTop(i) - scroll.value, w: colW, h: rowH(i) });
  const bandRect = (i) => { const r = rowRect(i); return { x: 0, y: r.y + r.h, w: W, h: bandH(i) }; };
  const diamondRect = (i) => { const r = rowRect(i); return { x: colW - PAD_X - DIAMOND_W + 4, y: r.y + r.h - ROW_H, w: DIAMOND_W, h: ROW_H * 0.5 }; };
  const modeRect = (i) => { const r = rowRect(i), cy = r.y + r.h - ROW_H * 0.3; return { x: colW - PAD_X - MODE_W, y: cy - 6, w: MODE_W, h: 12 }; };
  const rowOf = (a) => rows.findIndex((r) => r.prop.address === a);
  const markedCount = () => rows.reduce((n, r) => n + (r.gone ? 0 : 1), 0);
  function nowOf(i) {
    const r = rows[i];
    if (!r) return -1;
    for (const s of r.spans) if (playhead >= s.at - 1e-9 && playhead < s.at + s.dur - 1e-9) return clamp(s.in + (playhead - s.at) * s.speed, s.in, s.out);
    return -1;
  }
  function graphSpan(i) {
    const r = rows[i];
    if (!r || !r.spans.length) return -1;
    for (let s = 0; s < r.spans.length; s++) if (playhead >= r.spans[s].at - 1e-9 && playhead < r.spans[s].at + r.spans[s].dur - 1e-9) return s;
    if (model) for (let s = 0; s < r.spans.length; s++) if (r.spans[s].clip === model.selectedClip) return s;
    return 0;
  }
  function spanAt(i, x) { const sp = rows[i].spans; for (let s = 0; s < sp.length; s++) if (x >= xOf(sp[s].at) && x <= xOf(sp[s].at + sp[s].dur)) return s; return -1; }
  function rowAt(p) {
    if (p.y < HEAD_H) return -1;
    for (let i = 0; i < rows.length; i++) { const r = rowRect(i); if (!rows[i].gone && r.h > 1 && p.y >= r.y && p.y < r.y + r.h) return i; }
    return -1;
  }
  function keyAt(p) {
    for (let i = 0; i < rows.length; i++) {
      const r = rows[i], rr = rowRect(i);
      if (rowH(i) < 2 || r.gone || p.y < rr.y || p.y >= rr.y + rr.h) continue;
      for (let s = 0; s < r.spans.length; s++)
        for (let k = r.keys.length - 1; k >= 0; k--) {
          const sp = r.spans[s];
          if (r.keys[k] < sp.in - 1e-9 || r.keys[k] > sp.out + 1e-9) continue;
          if (Math.abs(keyX(sp, r.keys[k]) - p.x) <= KEY_R + 3) return { row: i, key: k, span: s };
        }
      return { row: i, key: -1, span: -1 };
    }
    return null;
  }
  const graphRect = () => { const f = rowOf(selected); if (f < 0) return null; const b = bandRect(f); return { x: colW, y: b.y, w: Math.max(0, W - colW), h: b.h }; };

  // ---------------------------------------------------------------- bind (KeyLane.cpp:35-150)
  function rebuild(m) {
    const fresh = [];
    for (const a of m.anims || []) {
      const r = { prop: describe(m, a), mode: a.mode || "fixed", keys: (a.keys || []).map((k) => +k.t).sort((x, y) => x - y), spans: [], gone: false, animId: a.id };
      let src = a.node;
      if (a.owner === "effect") for (const e of m.effects || []) if (e.id === a.node) src = e.node;
      for (const c of m.clips || []) {
        if (c.audio || c.nested) continue;
        if (a.owner === "clip" ? c.id === a.node : c.src === src) r.spans.push({ clip: c.id, at: c.at, dur: spanDur(c), in: c.in, out: c.out, speed: c.speed || 1 });
      }
      r.spans.sort((x, y) => x.at - y.at);
      const p5 = (n, w) => String(Math.max(0, n)).padStart(w, "0");
      r.sortKey = p5(r.prop.objectOrder, 5) + "|" + p5(r.prop.groupOrder, 4) + "|" + p5(r.prop.labelOrder, 4) + "|" + r.prop.address;
      fresh.push(r);
    }
    for (const old of rows) {
      const still = fresh.some((f) => f.prop.address === old.prop.address), pr = present.get(old.prop.address);
      if (!still && pr && pr.anim.value > 0.001) { old.gone = true; fresh.push(old); }
    }
    fresh.sort((x, y) => (x.sortKey < y.sortKey ? -1 : x.sortKey > y.sortKey ? 1 : 0));
    rows = fresh;
    for (const r of rows)
      if (!present.has(r.prop.address)) present.set(r.prop.address, { anim: new Anim(everBound ? 0 : 1), applied: !everBound });
    everBound = true;
    const live = (a) => { const i = rowOf(a); return i >= 0 && !rows[i].gone; };
    if (selected && !live(selected)) selected = "";
    shownRows = shownRows.filter(live);
    if (!selected) shownRows = [];
    else if (!shownRows.includes(selected)) shownRows.push(selected);
    if (pendingSelect) { const a = pendingSelect; pendingSelect = ""; if (live(a)) select(a); }
  }
  function bindGraph() {
    if (!model) return;
    const ids = [];
    for (const a of shownRows) { const i = rowOf(a); if (i >= 0) ids.push(rows[i].animId); }
    const front = rowOf(selected), s = graphSpan(front);
    if (s < 0) { graph.bind(model, [], 0, 1); return; }
    const sp = rows[front].spans[s], n = nowOf(front);
    graph.setNow(n >= 0 ? n : sp.in);
    graph.setFront(selected);
    graph.bind(model, ids, sp.in, sp.out);
  }
  function select(address, add = false) {
    const i = rowOf(address);
    if (i < 0) { pendingSelect = address; return; }
    if (rows[i].gone) return;
    const at = shownRows.indexOf(address);
    if (add && at >= 0 && shownRows.length > 1) {
      shownRows.splice(at, 1);
      if (selected === address) selected = shownRows[shownRows.length - 1];
      bindGraph(); loop.kick();
      return;
    }
    if (!add) shownRows = [];
    if (!shownRows.includes(address)) shownRows.push(address);
    selected = address;
    scroll.reveal(rowTop(i) - HEAD_H, ROW_H + GRAPH_H);
    revealFront = true;
    bindGraph(); loop.kick();
  }
  function closeCurve() { selected = ""; shownRows = []; bindGraph(); loop.kick(); }

  // the handle the key menus and the shell read
  const lane = {
    select, closeCurve, rowOf, row: (i) => rows[i], selected: () => selected, now: () => nowOf(rowOf(selected)),
    graph: { shown: () => !!selected && openAmt > 0.5, selectionList: () => graph.selectionList(), selectionCount: () => graph.selectionCount() },
  };
  own(host, vm.provide("keyLane", { contentHeight: () => contentH() }));
  own(host, vm.provide("keyGraph", { selectionList: () => graph.selectionList(), selectionCount: () => graph.selectionCount(), now: () => lane.now(), shown: () => lane.graph.shown() }));

  own(host, effect(() => { const m = vm.model.value; if (!m) return; model = m; rebuild(m); bindGraph(); loop.kick(); }));
  own(host, effect(() => {                          // the native shot's view (cut.keySelected): its curve open, settled
    const fx = vm.view && vm.view.fixture ? vm.view.fixture.value : null;
    const a = fx && fx.cut && fx.cut.keySelected;
    if (!a) return;
    select(a);
    band.set(a, { anim: new Anim(1), applied: true });
    loop.kick();
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));
  own(host, effect(() => {
    const tl = TL();
    if (!tl) return;
    tl.axis.value;
    colW = tl.colW.value; openAmt = tl.keyOpen.value;
    const ph = tl.playhead.value;
    if (ph !== playhead) { playhead = ph; bindGraph(); }
    loop.kick();
  }));

  // ---------------------------------------------------------------- advance (KeyLane.cpp:296-352)
  function advance(now) {
    let moving = false;
    for (let i = 0; i < rows.length; i++) {
      const r = rows[i], a = r.prop.address, shown = !r.gone;
      let pr = present.get(a);
      if (!pr) { pr = { anim: new Anim(shown ? 1 : 0), applied: shown }; present.set(a, pr); }
      else if (pr.applied !== shown) { pr.anim.animateTo(shown ? 1 : 0, ms("motion-slide"), Ease.EaseOutCubic, now); pr.applied = shown; }
      moving = pr.anim.update(now) || moving;
      let bd = band.get(a);
      const want = a === selected;
      if (!bd) { bd = { anim: new Anim(want ? 1 : 0), applied: want }; band.set(a, bd); }
      else if (bd.applied !== want) { bd.anim.animateTo(want ? 1 : 0, ms("motion-slide"), Ease.EaseOutCubic, now); bd.applied = want; }
      moving = bd.anim.update(now) || moving;
      const n = nowOf(i), here = n >= 0 && r.keys.some((k) => Math.abs(k - n) < 5e-4);
      const fill = here ? 1 : 0, line = n >= 0 ? 1 : 0;
      let d = dia.get(a);
      if (!d) { d = { fill: new Anim(fill), line: new Anim(line), fillL: fill, lineL: line }; dia.set(a, d); }
      if (fill !== d.fillL) { d.fill.animateTo(fill, 180, Ease.EaseOutCubic, now); d.fillL = fill; }
      if (line !== d.lineL) { d.line.animateTo(line, 180, Ease.EaseOutCubic, now); d.lineL = line; }
      moving = d.fill.update(now) || moving;
      moving = d.line.update(now) || moving;
    }
    const before = rows.length;
    rows = rows.filter((r, i) => !(r.gone && rowAmount(i) < 0.001));
    if (rows.length !== before) moving = true;
    if (revealFront) { const f = rowOf(selected); if (f >= 0) scroll.reveal(rowTop(f) - HEAD_H, ROW_H + GRAPH_H); revealFront = moving && f >= 0; }
    scroll.setExtent(HEAD_H, Math.max(0, H - HEAD_H), contentH());
    // the graph: the front property's curve over exactly the clip it is drawn over
    const f = rowOf(selected), s = graphSpan(f);
    if (s >= 0) { const sp = rows[f].spans[s]; graph.setPlotSpan(xOf(sp.at) - colW, xOf(sp.at + sp.dur) - colW); }
    moving = graph.advance(now) || moving;
    moving = hover.advance(now) || moving;
    return moving;
  }

  // the timeline sizes the section from contentHeight() in its own frame: while ours changes, wake it
  function nudgeTimeline() {
    const c = contentH();
    if (Math.abs(c - lastContentH) < 0.05) return;
    lastContentH = c;
    { const tl = vm.provided("timeline"); if (tl && tl.relayout) tl.relayout(); }   // the timeline re-reads contentHeight()
  }

  // ---------------------------------------------------------------- paint (KeyLane.cpp:472-650)
  function step(now) {
    const moving = advance(now);
    nudgeTimeline();
    paint();
    return moving || scroll.moving;
  }

  function paint() {
    const w = W, h = H;
    P.begin();
    if (!(w > 0) || !(h > 0)) { P.end(); return; }
    const fg = col("foreground"), mut = col("muted-foreground"), border = col("border"), rc = tok.px("radius-control");
    P.clip(0, 0, w, h);
    P.rect(0, 0, w, h, 0, { fill: col("lane-bg") });
    P.rect(0, 0, colW, h, 0, { fill: col("track-header-bg") });
    P.line(colW - 0.5, 0, colW - 0.5, h, border, 1);
    // ── the header: ▾ ANIMATION n, and ＋ Animate… ──
    P.rect(0, 0, w, HEAD_H, 0, { fill: col("card") });
    P.line(0, 0.5, w, 0.5, border, 1);
    P.line(0, HEAD_H - 0.5, w, HEAD_H - 0.5, border, 1);
    const cy = HEAD_H * 0.5, hv = hover.amount(1);
    if (hv > 0.001) P.rect(2, 2, colW - 4, HEAD_H - 4, rc, { fill: hoverWash(hv) });
    {
      const a = (1 - clamp(openAmt, 0, 1)) * -Math.PI / 2, cx = PAD_X + 3.5;
      const pt = (px, py) => [cx + px * Math.cos(a) - py * Math.sin(a), cy + px * Math.sin(a) + py * Math.cos(a)];
      const [p0, p1, p2] = [pt(-3.5, -1.75), pt(3.5, -1.75), pt(0, 2.75)];
      P.path(`M${p0[0]} ${p0[1]}L${p1[0]} ${p1[1]}L${p2[0]} ${p2[1]}Z`, { fill: lerp(mut, fg, 0.5 + 0.5 * hv) });
    }
    const lx = PAD_X + 12, track = 9 * 0.13, count = String(markedCount()), cw = measure(count, 9, F.mono);
    P.text(ellipsize("ANIMATION", colW - lx - cw - PAD_X - 6, 9, F.semibold, track), lx, baseline(cy, 9), 9, F.semibold, fg, track);
    P.text(count, colW - PAD_X - cw, baseline(cy, 9), 9, F.mono, mut);
    {
      const b = addRect(), av = hover.amount(0);
      P.rect(b.x, b.y, b.w, b.h, rc, { fill: lerp(col("secondary"), primaryA(0.22), av), stroke: border, sw: 1 });
      P.add(glyph.plus({ x: b.x + 6, y: b.y + (b.h - 8) * 0.5, w: 8, h: 8 }, css(lerp(mut, col("primary"), av))));
      P.text(ellipsize("Animate…", b.w - 22, 10, F.sans), b.x + 18, baseline(b.y + b.h * 0.5, 10), 10, F.sans, lerp(fg, col("primary"), av));
    }
    P.clip(0, HEAD_H, w, Math.max(0, h - HEAD_H));
    const none = hintAmount();
    if (none > 0.001) {
      const y = HEAD_H + PAD - scroll.value + HINT_H * 0.5;
      P.text(ellipsize(HINT, w - colW - 2 * PAD_X, 10, F.sans), colW + PAD_X, baseline(y, 10), 10, F.sans, fade(mut, none));
    }
    for (let i = 0; i < rows.length; i++) paintRow(i, w, h, fg, mut, border, rc);
    // the playhead runs through the lanes as it runs through the tracks
    const px = xOf(playhead);
    if (px >= colW && px <= w) { P.clip(colW, HEAD_H, w - colW, h - HEAD_H); P.line(px, HEAD_H, px, h, col("playhead"), 1); P.restore(); }
    // the front curve: KeyLane's child, painted over the lanes (and the playhead line) in its band
    const f = rowOf(selected), gb = f >= 0 ? bandRect(f) : null;
    if (gb && gb.h > 1 && graphSpan(f) >= 0 && gb.y + gb.h > HEAD_H && gb.y < h) {
      P.clip(colW, Math.max(HEAD_H, gb.y), Math.max(0, w - colW), gb.h);
      graph.paint(P, { x: colW, y: gb.y, w: Math.max(0, w - colW), h: Math.max(0, gb.h - 1) }, clamp(gb.h / GRAPH_H, 0, 1));
      P.restore();
    }
    P.restore();
    const bar = scroll.bar(w - 2);
    if (bar) P.rect(bar.x, bar.y, bar.w, bar.h, 1.5, { fill: whiteA(0.18) });
    P.restore();
    P.end();
  }

  function paintRow(i, w, h, fg, mut, border, rc) {
    const r = rowRect(i), bd = bandRect(i);
    if ((r.h < 0.5 && bd.h < 0.5) || Math.max(r.y + r.h, bd.y + bd.h) < HEAD_H || r.y > h) return;
    const row = rows[i], pres = rowAmount(i), sel = row.prop.address === selected;
    const shownToo = !sel && shownRows.includes(row.prop.address);
    P.clip(0, r.y, w, r.h + bd.h);
    P.group(pres * pres);
    const line = { x: 0, y: r.y + r.h - ROW_H, w, h: ROW_H };
    const cell = { x: 4, y: line.y + 1, w: colW - 8, h: line.h - 2 };
    if (sel) P.rect(cell.x, cell.y, cell.w, cell.h, rc, { fill: primaryA(0.16) });
    else if (shownToo) P.rect(cell.x, cell.y, cell.w, cell.h, rc, { fill: whiteA(0.06) });
    else { const hv = Math.max(hover.amount(2 + 2 * i), hover.amount(3 + 2 * i)); if (hv > 0.001) P.rect(cell.x, cell.y, cell.w, cell.h, rc, { fill: hoverWash(hv) }); }
    const y1 = line.y + ROW_H * 0.3, y2 = line.y + ROW_H * 0.7;
    P.text(ellipsize(row.prop.label, colW - 2 * PAD_X - DIAMOND_W - 2, 10, F.sans), PAD_X, baseline(y1, 10), 10, F.sans, fg);
    P.text(ellipsize(row.prop.object + " · " + row.prop.group, colW - 2 * PAD_X - MODE_W - 4, 9, F.sans), PAD_X, baseline(y2, 9), 9, F.sans, mut);
    {
      const d = diamondRect(i), cx = d.x + d.w * 0.5, cyy = d.y + d.h * 0.5 + 1, rr = 4, da = dia.get(row.prop.address);
      const fill = da ? da.fill.value : 0, ln = da ? da.line.value : 0;
      const path = `M${cx} ${cyy - rr}L${cx + rr} ${cyy}L${cx} ${cyy + rr}L${cx - rr} ${cyy}Z`;
      if (fill > 0.01) P.path(path, { fill: primaryA(fill) });
      P.path(path, { stroke: lerp(whiteA(0.22), fg, ln), sw: 1 });
    }
    {
      const m = modeRect(i), offset = row.mode === "offset", hv = row.prop.fixedOnly ? 0 : hover.amount(3 + 2 * i);
      P.rect(m.x, m.y, m.w, m.h, tok.px("radius-hairline"), { fill: lerp(offset ? primaryA(0.16) : col("secondary"), primaryA(0.3), hv), stroke: offset ? primaryA(0.45) : border, sw: 1 });
      const s = offset ? "OFFSET" : "FIXED", tr = 7 * 0.06, tw = measure(s, 7, F.semibold, tr);
      P.text(s, m.x + (m.w - tw) * 0.5, baseline(m.y + m.h * 0.5, 7), 7, F.semibold, offset ? col("primary") : fade(mut, row.prop.fixedOnly ? 0.7 : 1), tr);
    }
    // ── its lane: every clip of its object, the keys under the frames they key ──
    P.clip(colW, line.y, Math.max(0, w - colW), line.h);
    if (sel) P.rect(colW, line.y, w - colW, line.h, 0, { fill: primaryA(0.06) });
    const cy = line.y + line.h * 0.5;
    if (!row.spans.length) P.text(ellipsize("not in this timeline", w - colW - 2 * PAD_X, 9, F.sans), colW + PAD_X, baseline(cy, 9), 9, F.sans, fade(mut, 0.8));
    for (let s = 0; s < row.spans.length; s++) {
      const sp = row.spans[s], x0 = xOf(sp.at), x1 = xOf(sp.at + sp.dur);
      if (x1 < colW || x0 > w) continue;
      P.rect(x0, line.y + 3, Math.max(1, x1 - x0), line.h - 6, rc, { fill: whiteA(0.04) });
      let first = -1, last = -1;
      for (const k of row.keys) if (k >= sp.in - 1e-9 && k <= sp.out + 1e-9) { if (first < 0) first = k; last = k; }
      if (first >= 0 && last > first) P.line(keyX(sp, first), cy, keyX(sp, last), cy, whiteA(sel ? 0.32 : 0.18), 1);
      for (let k = 0; k < row.keys.length; k++) {
        const dragged = keyDrag && keyDrag.row === i && keyDrag.key === k && keyDrag.moved;
        if (dragged && s !== keyDrag.span) continue;
        const kt = dragged ? keyDrag.t : row.keys[k];
        if (kt < sp.in - 1e-9 || kt > sp.out + 1e-9) continue;
        const xx = keyX(sp, kt);
        P.path(`M${xx} ${cy - KEY_R}L${xx + KEY_R} ${cy}L${xx} ${cy + KEY_R}L${xx - KEY_R} ${cy}Z`, { fill: sel || shownToo ? col("primary") : fg });
      }
    }
    P.restore();
    P.line(colW, line.y + line.h - 0.5, w, line.y + line.h - 0.5, whiteA(0.04), 1);
    // ── its curve, when open: the column beside it says whose ──
    if (bd.h > 0.5) {
      P.rect(0, bd.y, colW, bd.h, 0, { fill: col("track-header-bg") });
      const a = clamp(bd.h / GRAPH_H, 0, 1);
      P.line(PAD_X + 2, bd.y, PAD_X + 2, bd.y + Math.max(0, bd.h - 8), fade(col("primary"), 0.6 * a), 1);
      P.text(ellipsize(row.mode === "offset" ? "curve (offset)" : "curve", colW - PAD_X * 2 - 8, 9, F.sans), PAD_X + 8, baseline(bd.y + 10, 9), 9, F.sans, fade(mut, a));
      if (!row.spans.length)
        P.text(ellipsize("no clip of it in this timeline to draw the curve over", w - colW - 2 * PAD_X, 9, F.sans), colW + PAD_X, baseline(bd.y + bd.h * 0.5, 9), 9, F.sans, fade(mut, 0.8 * a));
      P.line(0, bd.y + bd.h - 0.5, w, bd.y + bd.h - 0.5, border, 1);
    }
    P.restore();
    P.restore();
  }

  // ---------------------------------------------------------------- gestures (KeyLane.cpp:354-470)
  const rootXY = (pt) => { const b = ctx.box.peek(); return [b.x + pt.x, b.y + pt.y]; };
  const inGraph = (pt) => { const g = graphRect(); return g && g.h > 1 && inRect(pt, g) ? g : null; };
  host.addEventListener("pointerdown", (e) => {
    const pt = localPoint(host, e);
    if (e.button !== 0 || pt.y < 3) return;
    press = { pt, moved: false };
    keyDrag = null;
    const k = pt.x < colW ? null : keyAt(pt);
    if (k && k.key >= 0) keyDrag = { address: rows[k.row].prop.address, row: k.row, key: k.key, span: k.span, from: rows[k.row].keys[k.key], t: rows[k.row].keys[k.key], moved: false };
    try { host.setPointerCapture(e.pointerId); } catch {}
    e.stopPropagation();
  });
  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e);
    if (press) {
      if (!press.moved && Math.hypot(pt.x - press.pt.x, pt.y - press.pt.y) > 3) press.moved = true;
      if (press.moved && keyDrag) {
        const r = rows[keyDrag.row];
        if (r && keyDrag.key < r.keys.length && r.spans[keyDrag.span]) {
          const sp = r.spans[keyDrag.span];
          const lo = keyDrag.key > 0 ? r.keys[keyDrag.key - 1] + GAP_T : -1e9, hi = keyDrag.key + 1 < r.keys.length ? r.keys[keyDrag.key + 1] - GAP_T : 1e9;
          keyDrag.t = clamp(Math.round((sp.in + (tOf(pt.x) - sp.at) * sp.speed) * 1000) / 1000, lo, hi);
          keyDrag.moved = keyDrag.moved || Math.abs(keyDrag.t - keyDrag.from) >= 5e-4;
        }
      }
      loop.kick();
      return;
    }
    const i = rowAt(pt);
    hover.setHovered(inRect(pt, addRect()) ? 0 : inRect(pt, foldRect()) ? 1 : i >= 0 ? (inRect(pt, modeRect(i)) ? 3 + 2 * i : 2 + 2 * i) : -1);
    host.style.cursor = keyAt(pt)?.key >= 0 && pt.x >= colW ? "ew-resize" : "";
    loop.kick();
  });
  const release = (e) => {
    if (!press) return;
    const pr = press, d = keyDrag;
    press = null; keyDrag = null;
    if (d && d.moved && Math.abs(d.t - d.from) >= 5e-4) vm.cmd("key set " + q(d.address) + " --at " + num(d.from) + " --to " + num(d.t));
    else if (!pr.moved && e.type === "pointerup") click(localPoint(host, e), e);
    loop.kick();
  };
  host.addEventListener("pointerup", release);
  host.addEventListener("pointercancel", release);
  host.addEventListener("pointerleave", () => { if (!press) { hover.clear(); loop.kick(); } });
  host.addEventListener("dblclick", (e) => {
    const pt = localPoint(host, e), i = rowAt(pt);
    if (pt.x < colW || i < 0) return;
    const s = spanAt(i, pt.x);
    if (s < 0) return;
    const sp = rows[i].spans[s];
    const t = clamp(Math.round((sp.in + (tOf(pt.x) - sp.at) * sp.speed) * 1000) / 1000, sp.in, sp.out);
    vm.cmd("key add " + q(rows[i].prop.address) + " --at " + num(t));
  });
  host.addEventListener("contextmenu", (e) => {
    e.preventDefault();
    e.stopPropagation();
    const pt = localPoint(host, e), [wx, wy] = rootXY(pt);
    const g = inGraph(pt);
    if (g) {
      const hit = graph.keyAt(g, pt.x, pt.y);
      if (hit) { const cv = graph.curves[hit.curve]; openKeyContext(vm, ctx, lane, cv.address, cv.keys[hit.key].t, wx, wy); return; }
      const f = rowOf(selected), s = graphSpan(f);
      if (s >= 0) { const p = graph.plot(g); openKeyPlotContext(vm, ctx, lane, graph.tAt(p, pt.x), wx, wy); }
      return;
    }
    const k = pt.x < colW ? null : keyAt(pt);
    if (k && k.key >= 0) { openKeyContext(vm, ctx, lane, rows[k.row].prop.address, rows[k.row].keys[k.key], wx, wy); return; }
    const i = rowAt(pt);
    if (i >= 0) openKeyRowContext(vm, ctx, lane, rows[i].prop.address, wx, wy);
  });
  host.addEventListener("wheel", (e) => {
    if (!scroll.scrollable) return;
    e.preventDefault();
    e.stopPropagation();
    scroll.scrollBy(wheelPx(e));
    loop.kick();
  }, { passive: false });

  function click(pt, e) {
    const add = e.ctrlKey || e.metaKey || e.shiftKey;
    if (inRect(pt, addRect())) { const b = addRect(), [x, y] = rootXY(b); openAnimateMenu(vm, ctx, lane, { x, y, w: b.w, h: b.h }); return; }
    if (inRect(pt, foldRect())) { const tl = TL(); if (tl) tl.toggleKeys(); return; }
    const g = inGraph(pt);
    if (g) {
      const hit = graph.keyAt(g, pt.x, pt.y);
      if (hit) graph.selectKey(hit.curve, hit.key, add); else graph.clearSelection();
      loop.kick();
      return;
    }
    const i = rowAt(pt);
    if (i < 0) return;
    const r = rows[i], column = pt.x < colW;
    if (column && inRect(pt, diamondRect(i))) {
      const n = nowOf(i);
      if (n >= 0) vm.cmd(toggle(r.prop.address, r.keys.some((k) => Math.abs(k - n) < 5e-4) ? 2 : 1, n));
      return;
    }
    if (column && inRect(pt, modeRect(i))) {
      if (!r.prop.fixedOnly) vm.cmd("key mode " + q(r.prop.address) + (r.mode === "offset" ? " fixed" : " offset"));
      return;
    }
    const k = column ? null : keyAt(pt);
    if (k && k.key >= 0) {
      if (selected !== r.prop.address || add) select(r.prop.address, add);
      const curve = shownRows.indexOf(r.prop.address);
      if (curve >= 0 && curve < graph.curveCount()) graph.selectKey(curve, k.key, false);
      loop.kick();
      return;
    }
    if (!add && r.prop.address === selected) closeCurve();
    else select(r.prop.address, add);
  }

  loop.kick();
}
