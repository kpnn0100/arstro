/*
 * Interstellar — cut/bin.js: the Cut tab's left column, the footage you can cut with. Ports
 * app/widgets/SourceBin.{h,cpp}.
 *
 * The rack's SOURCES (groups are colour structure, not material) as 32.5 px two-line rows under a
 * "SOURCE BIN  n ───" header: the name, then the bind name in mono and how many clips of the current
 * version use it ("unused" / "1 clip" / "n clips", "shared ·" when another source uses the file, the
 * proxy's progress, R-MEDIA-2); offline reads "offline — <why>" in destructive with the warn glyph, a
 * decoding source carries the spinner. Rows TRAVEL (AnimatedRows: a move eases 200 ms, a new row fades
 * in, a removed one fades out as a ghost); hover and the selection wash cross-fade; the list scrolls
 * eased. While the timeline has no picture clip the bin is lit - an eased accent wash and outline -
 * because "drag a source here" points at it. Empty: "no footage yet — add some" with Add footage….
 *
 * Intents: a click `rack select <bind>`; a double-click `source view <bind>` (R-EDT-1); a right-click
 * the rack's menu (edit/menus.js openRackContext); Add footage… the board's picker (`rack add`); a DRAG
 * carries the source to the timeline: vm.view.binDrag = {bind, label, dur, x, y (root px), phase:
 * "move" | "drop" | "cancel"} - the timeline shows the ghost and places it (`clip add`). An offline
 * source has no frames to place, so it does not drag.
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { q } from "../kit/cmd.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { EasedScroll, wheelPx } from "../kit/easedscroll.js";
import { AnimatedRows } from "../kit/animatedrows.js";
import { Painter, canvasSvg, col, css, fade, lerp, whiteA, primaryA, hoverWash, Anim, HoverFade, frameLoop,
         localPoint, inRect, Ease, F } from "../kit/paint.js";
import { openRackContext, pickFootage } from "../edit/menus.js";

const ROW_H = 32.5, HEADER_H = 29.25, PAD_X = 9.75;              // SourceBin.h:30-31, .cpp:14
const emptyChip = (vp) => ({ x: vp.x + (vp.w - 116) * 0.5, y: vp.y + vp.h * 0.42 + 14, w: 116, h: 24 });

export function mount(host, vm, ctx) {
  useCss("css/cut/bin.css");
  host.classList.add("sb");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  let W = 0, H = 0, sources = [], rackIndex = [], proxyNote = new Map(), selectedBind = "", nowMs = 0;
  let highlightWanted = false, highlightApplied = false, init = false;
  const highlight = new Anim(0), hover = new HoverFade(), sel = new HoverFade();
  const rows = new AnimatedRows(() => loop.kick());
  const scroll = new EasedScroll(() => loop.kick());

  own(host, effect(() => {                           // SourceBin::bind
    const m = vm.model.value;
    if (!m) return;
    sources = []; rackIndex = [];
    const items = [];
    (m.rack || []).forEach((n, k) => { if (!n.group) { sources.push(n); rackIndex.push(k); items.push([n.rackObj || n.bindName, n]); } });
    rows.sync(items, ROW_H);
    proxyNote = new Map();
    for (const j of m.proxyJobs || []) {
      if ((j.state === "queued" || j.state === "running") && j.total > 0) proxyNote.set(j.rackObj, "proxy " + Math.floor((100 * j.done) / j.total) + "% · ");
      else if (j.state === "queued" || j.state === "running") proxyNote.set(j.rackObj, "proxy… · ");
    }
    for (const n of sources) if (n.proxy && !proxyNote.has(n.rackObj)) proxyNote.set(n.rackObj, "proxy · ");
    highlightWanted = !(m.clips || []).some((c) => !c.audio) && sources.length > 0 && (m.tracks || []).length > 0;
    selectedBind = m.selectedRack >= 0 && m.selectedRack < (m.rack || []).length ? m.rack[m.selectedRack].bindName : "";
    sel.setHovered(sources.findIndex((n) => n.bindName === selectedBind));
    loop.kick();
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));

  const viewport = () => ({ x: 0, y: HEADER_H, w: W, h: Math.max(0, H - HEADER_H) });
  function rowRect(i) { const r = rows.byIndex(i); const y = r ? r.y : i * ROW_H; return { x: 0, y: HEADER_H + y - scroll.value, w: W, h: ROW_H }; }
  function rowAt(pt) { if (!inRect(pt, viewport())) return -1; for (let i = 0; i < sources.length; ++i) if (inRect(pt, rowRect(i))) return i; return -1; }

  function advance(now) {
    nowMs = now;
    let moving = rows.moving();
    scroll.setExtent(HEADER_H, Math.max(0, H - HEADER_H), sources.length * ROW_H);
    if (!init) { highlight.set(highlightWanted ? 1 : 0); highlightApplied = highlightWanted; init = true; }
    if (highlightWanted !== highlightApplied) { highlight.animateTo(highlightWanted ? 1 : 0, tok.ms("motion-cross-fade"), Ease.EaseOutCubic, now); highlightApplied = highlightWanted; }
    moving = highlight.update(now) || moving;
    moving = hover.advance(now) || moving;
    moving = sel.advance(now, tok.ms("motion-select")) || moving;
    return moving || scroll.moving || sources.some((n) => n.pending);   // the spinner turns
  }

  function paint() {                                 // SourceBin::onPaint + onOverlay
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), destr = col("destructive"), primary = col("primary"), rc = tok.px("radius-control");
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("left-rail-bg") });
    const hl = highlight.value;
    if (hl > 0.001) p.rect(0, 0, W, H, 0, { fill: primaryA(0.05 * hl) });
    p.line(W - 0.5, 0, W - 0.5, H, border, 1);
    const hy = HEADER_H * 0.5, tr = 0.13 * 9;
    p.text("SOURCE BIN", PAD_X, baseline(hy, 9), 9, F.semibold, muted, tr);
    const lw = measure("SOURCE BIN", 9, F.semibold, tr), count = String(sources.length);
    p.text(count, PAD_X + lw + 6, baseline(hy, 9), 9, F.mono, fade(muted, 0.7));
    const rx = PAD_X + lw + 6 + measure(count, 9, F.mono) + 6.5;
    if (W - PAD_X > rx) p.line(rx, hy, W - PAD_X, hy, border, 1);
    const vp = viewport();
    p.clip(vp.x, vp.y, vp.w, vp.h);
    if (!sources.length && !rows.rows().length) {
      const s = "no footage yet — add some";
      p.text(s, vp.x + (vp.w - measure(s, 11, F.sans)) * 0.5, vp.y + vp.h * 0.42, 11, F.sans, muted);
      const chip = emptyChip(vp), hv = hover.amount(0);
      p.rect(chip.x, chip.y, chip.w, chip.h, rc, { fill: primaryA(0.10 * hv), stroke: lerp(border, primary, 0.5 + 0.5 * hv), sw: 1 });
      const cl = "Add footage…";
      p.text(cl, chip.x + (chip.w - measure(cl, 11, F.sans)) * 0.5, baseline(chip.y + chip.h * 0.5, 11), 11, F.sans, fg);
    }
    for (const row of rows.rows()) {
      const n = row.data, a = row.alpha;
      if (a <= 0.001 || !scroll.bandVisible(row.y, ROW_H)) continue;
      const top = HEADER_H + row.y - scroll.value;
      const hv = row.index >= 0 ? hover.amount(10 + row.index) : 0, sa = row.index >= 0 ? sel.amount(row.index) : 0;
      const dragged = dragRow === row.index && dragMoved;
      if (sa > 0.001 || dragged) p.rect(0, top, W - 1, ROW_H, 0, { fill: primaryA(0.10 * a * Math.max(sa, dragged ? 1 : 0)) });
      if (hv > 0.001) p.rect(0, top, W - 1, ROW_H, 0, { fill: hoverWash(hv * a) });
      const l1 = top + 11.5, l2 = top + 23, gb = { x: PAD_X, y: l1 - 6, w: 12, h: 12 }, gc = fade(n.failed ? destr : muted, a);
      if (n.pending) p.add(glyph.spinner(gb.x + 6, gb.y + 6, 4.5, nowMs, a));
      else if (n.failed) p.add(glyph.warn(gb, css(gc)));
      else if (n.video) p.add(glyph.film(gb, css(gc)));
      else p.icon("image", gb.x, gb.y, 12, gc);
      const x = PAD_X + 18, pn = proxyNote.get(n.rackObj) || "";
      const uses = pn + (n.sharesMedia > 0 ? "shared · " : "") + (n.usedBy === 0 ? "unused" : n.usedBy === 1 ? "1 clip" : n.usedBy + " clips");
      const uw = measure(uses, 9, F.sans);
      p.text(ellipsize(n.cosmoName || n.bindName, W - PAD_X - x, 11, F.sans), x, baseline(l1, 11), 11, F.sans, fade(n.failed ? destr : fg, a));
      if (n.failed) {
        const why = n.offlineWhy ? "offline — " + n.offlineWhy : "offline — media missing";
        p.text(ellipsize(why, W - PAD_X - x, 9, F.sans), x, baseline(l2, 9), 9, F.sans, fade(destr, a));
      } else {
        p.text(uses, W - PAD_X - uw, baseline(l2, 9), 9, F.sans, fade(muted, a));
        const meta = n.pending ? "decoding…" : n.bindName, fam = n.pending ? F.sans : F.mono;
        p.text(ellipsize(meta, W - PAD_X - uw - 8 - x, 9, fam), x, baseline(l2, 9), 9, fam, fade(muted, a));
      }
    }
    p.restore();
    const bar = scroll.bar(W - 2);
    if (bar) p.rect(bar.x, bar.y, bar.w, bar.h, tok.px("radius-pill"), { fill: whiteA(0.14) });
    if (hl > 0.001) p.rect(1.5, 1.5, W - 3, H - 3, rc, { stroke: primaryA(0.55 * hl), sw: 1 });
    p.end();
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    const more = advance(now);
    paint();
    return more;
  }

  // ---------------------------------------------------------------- input (SourceBin::handleGesture)
  let dragRow = -1, dragMoved = false, downPt = null;
  const rootPt = (pt) => { const b = ctx.box.peek(); return { x: b.x + pt.x, y: b.y + pt.y }; };
  function dragMsg(i, pt, phase) {
    const n = sources[i];
    const r = rootPt(pt);
    vm.view.binDrag.value = { bind: n.bindName, label: n.cosmoName || n.bindName, dur: n.mediaDuration > 0 ? n.mediaDuration : n.video ? 0 : 5,
                              x: r.x, y: r.y, phase };
  }
  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e);
    if (dragRow >= 0) {
      if (!dragMoved && downPt && Math.hypot(pt.x - downPt.x, pt.y - downPt.y) > 4) dragMoved = true;
      if (dragMoved) { dragMsg(dragRow, pt, "move"); host.style.cursor = "grabbing"; }
      loop.kick();
      return;
    }
    if (!sources.length && inRect(pt, emptyChip(viewport()))) hover.setHovered(0);
    else { const i = rowAt(pt); hover.setHovered(i >= 0 ? 10 + i : -1); }
    loop.kick();
  });
  host.addEventListener("pointerleave", () => { if (dragRow < 0) { hover.clear(); loop.kick(); } });
  host.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    const pt = localPoint(host, e), i = rowAt(pt);
    dragRow = i >= 0 && !sources[i].failed ? i : -1;
    dragMoved = false;
    downPt = pt;
    if (dragRow >= 0) try { host.setPointerCapture(e.pointerId); } catch {}
  });
  const end = (e, phase) => {
    const i = dragRow, moved = dragMoved;
    dragRow = -1; dragMoved = false;
    host.style.cursor = "";
    if (moved && i >= 0 && i < sources.length) dragMsg(i, localPoint(host, e), phase);
    loop.kick();
    return moved;
  };
  let suppressClick = false;
  host.addEventListener("pointerup", (e) => { suppressClick = end(e, "drop"); });
  host.addEventListener("pointercancel", (e) => { end(e, "cancel"); });
  host.addEventListener("click", (e) => {
    if (suppressClick) { suppressClick = false; return; }
    const pt = localPoint(host, e);
    if (!sources.length && inRect(pt, emptyChip(viewport()))) { pickFootage(vm, ctx); return; }
    const i = rowAt(pt);
    if (i >= 0 && sources[i].bindName !== selectedBind) vm.cmd("rack select " + q(sources[i].bindName));
  });
  host.addEventListener("dblclick", (e) => {
    const i = rowAt(localPoint(host, e));
    if (i >= 0 && !sources[i].failed) vm.cmd("source view " + q(sources[i].bindName));
  });
  host.addEventListener("contextmenu", (e) => {
    e.preventDefault();
    const pt = localPoint(host, e), i = rowAt(pt), r = rootPt(pt);
    if (i >= 0) openRackContext(vm, ctx, rackIndex[i], r.x, r.y);
  });
  host.addEventListener("wheel", (e) => { if (scroll.scrollBy(wheelPx(e))) { e.preventDefault(); loop.kick(); } }, { passive: false });
  loop.kick();
}
