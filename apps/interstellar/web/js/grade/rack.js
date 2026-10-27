/*
 * Interstellar — grade/rack.js: the Grade tab's left column, the rack (Cosmo's group tree, hosted).
 * Ports app/widgets/RackTree.{h,cpp}.
 *
 * Groups and sources in tree order, indented by depth, each a 32.5 px two-line row under a
 * "RACK  n ───  +" header. Line one: the open/shut chevron (groups), the glyph (folder / film / image,
 * cross-fading to cosmo's spinner while decoding and to the warn glyph when OFFLINE), the name, an OVR
 * badge (a click: `revert <bind>`) and a SHARED badge; line two: the bind name in mono (or
 * "decoding…", or "offline — <why>" in destructive) and the USED-BY count. A bypass toggle sits at the
 * row's end (`set <bind>.bypass=0|1`); a bypassed row dims. Every row state (bypass, pending, failed,
 * override, in-selection, open, shared) is eased (motion-select, EaseOutCubic), never flipped.
 *
 * Groups COLLAPSE: the chevron opens / shuts (presentation, never a command: vm.view.rackOpen, owned
 * here; vm.setRackOpen), the chevron TURNS with the eased amount and the rows below travel
 * (AnimatedRows). When the Grade target moves into a shut group its ancestors open, once per move.
 * Double-clicking a group row opens it AND drills the deck's SOURCES strip into it (vm.openDeckGroup).
 *
 * Selection, Cosmo's: Shift-click `rack select <bind> --range`, Ctrl-click `rack select <bind> --add`,
 * a plain click `rack select <bind>` (unless it is already the one and only selection). Right-click the
 * rack's menu (edit/menus.js openRackContext, which carries Input Colour… -> openInputColourMenu); the
 * + button and the empty state's chip: pickFootage (`rack add`). The list scrolls eased (EasedScroll).
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { q, setLine } from "../kit/cmd.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { EasedScroll, wheelPx } from "../kit/easedscroll.js";
import { AnimatedRows } from "../kit/animatedrows.js";
import { Painter, canvasSvg, col, css, fade, lerp, whiteA, primaryA, hoverWash, Anim, HoverFade, frameLoop,
         localPoint, inRect, Ease, F } from "../kit/paint.js";
import { openRackContext, pickFootage } from "../edit/menus.js";

const ROW_H = 32.5, HEADER_H = 29.25;                            // RackTree.h:50-51
const PAD_X = 9.75, GLYPH = 12, CHEVRON = 12, BYPASS = 20, NAME_PX = 11, META_PX = 9;   // RackTree.cpp:16-21
const usesLabel = (n) => (n === 0 ? "unused" : n === 1 ? "1 clip" : n + " clips");
const emptyChip = (vp) => ({ x: vp.x + (vp.w - 116) * 0.5, y: vp.y + vp.h * 0.42 + 14, w: 116, h: 24 });
const bypassAt = (w, top) => ({ x: w - PAD_X - BYPASS, y: top + (ROW_H - BYPASS) * 0.5, w: BYPASS, h: BYPASS });
const keyOf = (n) => n.rackObj || n.bindName;
const STATES = ["bypass", "pending", "failed", "ovr", "selected", "open", "shared"];

export function mount(host, vm, ctx) {
  useCss("css/grade/rack.css");
  host.classList.add("rk");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  let W = 0, H = 0, bound = false, rack = [], selected = -1, open = new Set(), revealedFor = null, nowMs = 0;
  const hover = new HoverFade(), sel = new HoverFade();
  const rows = new AnimatedRows(() => loop.kick());
  const scroll = new EasedScroll(() => loop.kick());
  const states = new Map();                          // RowState, keyed by rackObj
  let ovrRects = new Map();                          // rack index -> the OVR badge's rect (paint writes it)
  const indent = () => tok.px("space-indent") || 13;

  function stateOf(key) {
    let st = states.get(key);
    if (!st) { st = { init: false, want: {}, applied: {} }; for (const k of STATES) st[k] = new Anim(0); states.set(key, st); }
    return st;
  }
  const parentOf = (n) => (Number.isInteger(n.parent) ? n.parent : -1);
  function rowVisible(i) {
    if (i < 0 || i >= rack.length) return false;
    for (let pi = parentOf(rack[i]); pi >= 0 && pi < rack.length; pi = parentOf(rack[pi]))
      if (!open.has(rack[pi].rackObj)) return false;
    return true;
  }
  function syncRows() {                              // RackTree::syncRows
    if (!bound) return;                              // first placement sets: wait for the model
    const items = [];
    rack.forEach((n, i) => {
      if (n.group) stateOf(keyOf(n)).want.open = open.has(n.rackObj);
      if (rowVisible(i)) items.push([keyOf(n), { rack: i, n }]);
    });
    rows.sync(items, ROW_H);
    loop.kick();
  }

  own(host, effect(() => {                           // RackTree::bind
    const m = vm.model.value;
    if (!m) return;
    rack = m.rack || [];
    selected = Number.isInteger(m.selectedRack) ? m.selectedRack : -1;
    const selKey = selected >= 0 && selected < rack.length ? rack[selected].rackObj || "" : "";
    if (selKey !== revealedFor) {                    // open the new Grade target's ancestors, once per move
      revealedFor = selKey;
      const cur = vm.view.rackOpen.peek(), s = new Set(cur);
      for (let pi = selected >= 0 && selected < rack.length ? parentOf(rack[selected]) : -1; pi >= 0 && pi < rack.length; pi = parentOf(rack[pi]))
        if (rack[pi].rackObj) s.add(rack[pi].rackObj);
      if (s.size !== cur.size) { open = s; vm.view.rackOpen.value = s; }
    }
    const selection = vm.selection ? vm.selection.peek() || [] : [];
    for (const n of rack) {
      const w = stateOf(keyOf(n)).want;
      w.bypass = !!n.bypass; w.pending = !!n.pending; w.failed = !!n.failed; w.ovr = !!n.overridden;
      w.selected = n.selected !== undefined ? !!n.selected : selection.includes(n.bindName);
      w.shared = !n.group && n.sharesMedia > 0;
    }
    sel.setHovered(selected);
    bound = true;
    syncRows();
  }));
  own(host, effect(() => { open = vm.view.rackOpen.value || new Set(); syncRows(); }));
  own(host, effect(() => {                           // TESTS ONLY: the native shot opened the deck's group here too
    const fx = vm.view.fixture && vm.view.fixture.value, lv = fx && fx.grade && fx.grade.deckLevel;
    if (typeof lv === "string" && lv && !vm.view.rackOpen.peek().has(lv)) vm.setRackOpen(lv, true);
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));

  const viewport = () => ({ x: 0, y: HEADER_H, w: W, h: Math.max(0, H - HEADER_H) });
  const addRect = () => ({ x: W - PAD_X - 20, y: (HEADER_H - 20) * 0.5, w: 20, h: 20 });
  function rowTop(i) {
    for (const r of rows.rows()) if (r.index >= 0 && r.data.rack === i) return HEADER_H + r.y - scroll.value;
    return -1e6;
  }
  const rowRect = (i) => ({ x: 0, y: rowTop(i), w: W, h: ROW_H });
  function chevronRect(i) {
    if (i < 0 || i >= rack.length || !rack[i].group) return { x: 0, y: 0, w: 0, h: 0 };
    return { x: PAD_X + (rack[i].depth || 0) * indent() - 2, y: rowTop(i) + 11.5 - 8, w: CHEVRON + 4, h: 16 };
  }
  function rowAt(pt) {
    if (!inRect(pt, viewport())) return -1;
    for (const r of rows.rows()) if (r.index >= 0 && inRect(pt, rowRect(r.data.rack))) return r.data.rack;
    return -1;
  }
  function setOpen(ro, o) { if (ro) vm.setRackOpen(ro, o); }

  function advance(now) {                            // RackTree::advance
    nowMs = now;
    let moving = rows.moving();
    scroll.setExtent(HEADER_H, Math.max(0, H - HEADER_H), rows.count() * ROW_H);
    const ms = tok.ms("motion-select");
    for (const st of states.values()) {
      for (const k of STATES) {
        const want = !!st.want[k];
        if (!st.init) st[k].set(want ? 1 : 0);
        else if (want !== st.applied[k]) st[k].animateTo(want ? 1 : 0, ms, Ease.EaseOutCubic, now);
        st.applied[k] = want;
        moving = st[k].update(now) || moving;
      }
      st.init = true;
    }
    moving = hover.advance(now) || moving;
    moving = sel.advance(now, ms) || moving;
    return moving || scroll.moving || rack.some((n) => n.pending);   // the spinner turns
  }

  function paint() {                                 // RackTree::onPaint
    ovrRects = new Map();
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), destr = col("destructive"), primary = col("primary");
    const rc = tok.px("radius-control"), rh = tok.px("radius-hairline");
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("left-rail-bg") });
    p.line(W - 0.5, 0, W - 0.5, H, border, 1);

    // header: RACK · n, rule, add button
    const hy = HEADER_H * 0.5, tr = 0.13 * 9;
    p.text("RACK", PAD_X, baseline(hy, 9), 9, F.semibold, muted, tr);
    const lw = measure("RACK", 9, F.semibold, tr), count = String(rack.length);
    p.text(count, PAD_X + lw + 6, baseline(hy, 9), 9, F.mono, fade(muted, 0.7));
    const rx = PAD_X + lw + 6 + measure(count, 9, F.mono) + 6.5, ar = addRect();
    if (ar.x - 6.5 > rx) p.line(rx, hy, ar.x - 6.5, hy, border, 1);
    {
      const hv = hover.amount(0);
      if (hv > 0.001) p.rect(ar.x, ar.y, ar.w, ar.h, rc, { fill: hoverWash(hv) });
      p.add(glyph.plus({ x: ar.x + 5, y: ar.y + 5, w: 10, h: 10 }, css(lerp(muted, fg, 0.7 * hv))));
    }

    const vp = viewport();
    p.clip(vp.x, vp.y, vp.w, vp.h);
    if (!rack.length && !rows.rows().length) {
      const s = "no footage yet — add some";
      p.text(s, vp.x + (vp.w - measure(s, 11, F.sans)) * 0.5, vp.y + vp.h * 0.42, 11, F.sans, muted);
      const chip = emptyChip(vp), hv = hover.amount(1);
      p.rect(chip.x, chip.y, chip.w, chip.h, rc, { fill: primaryA(0.10 * hv), stroke: lerp(border, primary, 0.5 + 0.5 * hv), sw: 1 });
      const cl = "Add footage…";
      p.text(cl, chip.x + (chip.w - measure(cl, 11, F.sans)) * 0.5, baseline(chip.y + chip.h * 0.5, 11), 11, F.sans, lerp(fg, whiteA(1), hv));
    }

    for (const row of rows.rows()) {
      const n = row.data.n, a = row.alpha;
      if (a <= 0.001 || !scroll.bandVisible(row.y, ROW_H)) continue;
      const top = HEADER_H + row.y - scroll.value;
      const i = row.index >= 0 ? row.data.rack : -1;   // a ghost has no index any more
      const sa = i >= 0 ? sel.amount(i) : 0, hv = i >= 0 ? hover.amount(10 + i) : 0;
      if (sa > 0.001) {
        p.rect(0, top, W - 1, ROW_H, 0, { fill: primaryA(0.14 * sa * a) });
        p.rect(0, top + 4, 2, ROW_H - 8, 0, { fill: primaryA(sa * a) });
      }
      if (hv > 0.001) p.rect(0, top, W - 1, ROW_H, 0, { fill: hoverWash(hv * a) });
      const st = states.get(keyOf(n));
      const v = (k, fb) => (st ? st[k].value : fb ? 1 : 0);
      const ms = st ? st.selected.value * (1 - sa) : 0;   // in the selection, not the target: a lighter wash
      if (ms > 0.001) p.rect(0, top, W - 1, ROW_H, 0, { fill: primaryA(0.08 * ms * a) });
      const by = v("bypass", n.bypass), pe = v("pending", n.pending), fa = v("failed", n.failed), ov = v("ovr", n.overridden);
      const content = a * (1 - 0.55 * by);
      const depth = n.depth || 0, cx0 = PAD_X + depth * indent(), x0 = cx0 + CHEVRON;
      const l1 = top + 11.5, l2 = top + 23;
      for (let d = 0; d < depth; ++d) {               // depth guides, under each ancestor's folder
        const gx = PAD_X + d * indent() + CHEVRON + 5.5;
        p.line(gx, top, gx, top + ROW_H, fade(border, a), 1);
      }
      if (n.group) {                                  // the chevron TURNS: › shut, ⌄ open
        const op = v("open", open.has(n.rackObj)), hvc = i >= 0 ? hover.amount(1000 + i) : 0;
        const ang = op * Math.PI / 2, c = Math.cos(ang), s = Math.sin(ang);
        const ccx = cx0 + CHEVRON * 0.5 - 1, ccy = l1;
        const pt = (px, py) => `${(ccx + px * c - py * s).toFixed(3)} ${(ccy + px * s + py * c).toFixed(3)}`;
        p.path(`M${pt(-1.75, -3.5)}L${pt(1.75, 0)}L${pt(-1.75, 3.5)}`, { stroke: fade(lerp(muted, fg, hvc), content), sw: 1.2 });
      }
      // the glyph cross-fades between its states: decoding (spinner), offline (warning), the node
      const gb = { x: x0, y: l1 - GLYPH * 0.5, w: GLYPH, h: GLYPH }, normal = (1 - pe) * (1 - fa);
      const gc = sa > 0.5 ? primary : muted;
      if (pe > 0.001) p.add(glyph.spinner(gb.x + gb.w * 0.5, gb.y + gb.h * 0.5, 4.5, nowMs, content * pe));
      if (fa > 0.001) p.add(glyph.warn(gb, css(fade(destr, content * fa))));
      if (normal > 0.001) {
        const nc = fade(gc, content * normal);
        if (n.group) p.icon("folder", gb.x, gb.y, GLYPH, nc);
        else if (n.video) p.add(glyph.film(gb, css(nc)));
        else p.icon("image", gb.x, gb.y, GLYPH, nc);
      }

      // line one: name … [OVR] [SHARED] — each badge's room opens with its eased amount
      const br = bypassAt(W, top);
      let right = W - PAD_X - BYPASS - 6;
      if (ov > 0.001) {
        const bw = measure("OVR", 7.5, F.semibold) + 8, badge = { x: right - bw, y: l1 - 6.5, w: bw, h: 13 };
        if (n.overridden && i >= 0) ovrRects.set(i, badge);
        p.rect(badge.x, badge.y, badge.w, badge.h, rh, { fill: primaryA(0.12 * content * ov), stroke: primaryA(0.55 * content * ov), sw: 1 });
        p.text("OVR", badge.x + 4, baseline(l1, 7.5), 7.5, F.semibold, fade(primary, content * ov));
        right -= (bw + 5) * ov;
      }
      const shd = v("shared", !n.group && n.sharesMedia > 0);
      if (shd > 0.001) {
        const bw = measure("SHARED", 7.5, F.semibold) + 8, badge = { x: right - bw, y: l1 - 6.5, w: bw, h: 13 };
        p.rect(badge.x, badge.y, badge.w, badge.h, rh, { stroke: fade(border, 2 * content * shd), sw: 1 });
        p.text("SHARED", badge.x + 4, baseline(l1, 7.5), 7.5, F.semibold, fade(muted, content * shd));
        right -= (bw + 5) * shd;
      }
      const fam = n.group ? F.medium : F.sans;
      p.text(ellipsize(n.cosmoName || n.bindName, right - (x0 + 18), NAME_PX, fam), x0 + 18, baseline(l1, NAME_PX), NAME_PX, fam,
             fade(lerp(fg, destr, fa), content));

      // line two: what it spells and its uses — or the reason it has neither
      const usesRight = W - PAD_X - BYPASS - 8;
      if (fa > 0.001) {
        const why = n.offlineWhy ? "offline — " + n.offlineWhy : "offline — media missing";
        p.text(ellipsize(why, usesRight - (x0 + 18), META_PX, F.sans), x0 + 18, baseline(l2, META_PX), META_PX, F.sans, fade(destr, a * fa));
      }
      if (fa < 0.999) {
        const la = content * (1 - fa), uses = n.group ? "" : usesLabel(n.usedBy || 0);
        let usesW = 0;
        if (uses) {
          usesW = measure(uses, META_PX, F.sans);
          p.text(uses, usesRight - usesW, baseline(l2, META_PX), META_PX, F.sans, fade(muted, la));
        }
        const room = usesRight - usesW - 8 - (x0 + 18);
        if (pe > 0.001) p.text(ellipsize("decoding…", room, META_PX, F.sans), x0 + 18, baseline(l2, META_PX), META_PX, F.sans, fade(muted, la * pe));
        if (pe < 0.999) p.text(ellipsize(n.bindName, room, META_PX, F.mono), x0 + 18, baseline(l2, META_PX), META_PX, F.mono, fade(muted, la * (1 - pe)));
      }

      // bypass toggle (full strength even when the row is dimmed: it is how you undo it)
      if (hv > 0.001) p.rect(br.x, br.y, br.w, br.h, rc, { fill: hoverWash(hv * a * (1 - by)) });
      if (by > 0.001) p.rect(br.x, br.y, br.w, br.h, rc, { fill: primaryA(0.14 * a * by) });
      const idle = lerp(fade(muted, 0.55), fg, 0.6 * hv);
      p.icon("ban", br.x + 5, br.y + 5, 10, fade(lerp(idle, primary, by), a), 1.1);
    }
    p.restore();
    const bar = scroll.bar(W - 2);
    if (bar) p.rect(bar.x, bar.y, bar.w, bar.h, tok.px("radius-pill"), { fill: whiteA(0.14) });
    p.end();
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    const more = advance(now);
    paint();
    return more;
  }

  // ---------------------------------------------------------------- input (RackTree::handleGesture)
  const rootPt = (pt) => { const b = ctx.box.peek(); return { x: b.x + pt.x, y: b.y + pt.y }; };
  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e);
    if (inRect(pt, addRect())) hover.setHovered(0);
    else if (!rack.length && inRect(pt, emptyChip(viewport()))) hover.setHovered(1);
    else {
      const i = rowAt(pt);
      hover.setHovered(i >= 0 ? (rack[i].group && inRect(pt, chevronRect(i)) ? 1000 + i : 10 + i) : -1);
    }
    loop.kick();
  });
  host.addEventListener("pointerleave", () => { hover.clear(); loop.kick(); });
  host.addEventListener("click", (e) => {
    const pt = localPoint(host, e);
    if (inRect(pt, addRect()) || (!rack.length && inRect(pt, emptyChip(viewport())))) { pickFootage(vm, ctx); return; }
    const i = rowAt(pt);
    if (i < 0) return;
    const n = rack[i];
    if (n.group && inRect(pt, chevronRect(i))) { setOpen(n.rackObj, !open.has(n.rackObj)); return; }   // presentation: no command
    if (inRect(pt, bypassAt(W, rowTop(i)))) { vm.cmd(setLine(n.bindName + ".bypass", n.bypass ? "0" : "1")); return; }
    const ovr = ovrRects.get(i);
    if (n.overridden && ovr && inRect(pt, ovr)) { vm.cmd("revert " + q(n.bindName)); return; }
    // Cosmo's selection: Shift-click a range, Ctrl-click to toggle, a plain click selects one.
    if (e.shiftKey) vm.cmd("rack select " + q(n.bindName) + " --range");
    else if (e.ctrlKey || e.metaKey) vm.cmd("rack select " + q(n.bindName) + " --add");
    else {
      let selectedCount = 0;
      for (const r of rack) selectedCount += r.selected ? 1 : 0;
      if (i !== selected || selectedCount > 1) vm.cmd("rack select " + q(n.bindName));
    }
  });
  host.addEventListener("dblclick", (e) => {         // a group: open it here and in the SOURCES strip
    const pt = localPoint(host, e), i = rowAt(pt);
    if (i >= 0 && rack[i].group && !inRect(pt, chevronRect(i)) && !inRect(pt, bypassAt(W, rowTop(i)))) {
      setOpen(rack[i].rackObj, true);
      vm.openDeckGroup(rack[i].rackObj);
    }
  });
  host.addEventListener("contextmenu", (e) => {
    e.preventDefault();
    const pt = localPoint(host, e), i = rowAt(pt), r = rootPt(pt);
    if (i >= 0) openRackContext(vm, ctx, i, r.x, r.y);
  });
  host.addEventListener("wheel", (e) => { if (scroll.scrollBy(wheelPx(e))) { e.preventDefault(); loop.kick(); } }, { passive: false });
  loop.kick();
}
