/*
 * Interstellar — grade/deck.js: the Grade tab's deck. Ports app/widgets/GradeDeck.{h,cpp} (and the
 * cosmo Filmstrip / Breadcrumb it holds).
 *
 * Header: SOURCES n · the breadcrumb (two instances cross-fading) · the views SOURCES / STILLS / NODES
 * right-aligned with the travelling underline. Under it ONE strip (86 px, cosmo's Filmstrip): the shown
 * level's rack nodes (folder chips with their count, thumbnails through session.thumbNow, the spinner
 * while decoding, OFFLINE chips), or the stills (session.stillNow), or the node graph (grade/deck/
 * nodegraph.js). The level change fades out, swaps, fades in sliding 18 px from the side it came from.
 * Below: the REFERENCE-FRAME band - a strip of frames across the whole source, the accent marker at the
 * reference frame; dragging previews (vm.view.preview = {bind, t}); release sends
 * `rack frame <bind> --at <t>`; ‹ › step one source frame and commit at once.
 *
 * Intents (exact native lines): click `rack select <bind>` (Shift: --range, Ctrl: --add); double-click a
 * group drills in (vm.openDeckGroup + vm.setRackOpen(ro, true)); right-click edit/menus.js
 * openRackContext; stills: right-click grade/menus.js openStillContext, double-click `still apply`.
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { q, seconds, timecode } from "../kit/cmd.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { Painter, canvasSvg, col, css, fade, lerp, whiteA, primaryA, hoverWash, Anim, HoverFade, frameLoop,
         localPoint, inRect, Ease, F } from "../kit/paint.js";
import { openRackContext } from "../edit/menus.js";
import { openStillContext, activateStill, openNodeContext } from "./menus.js";
import { paintNodeGraph, nodeAt, GraphMotion } from "./deck/nodegraph.js";

const HEADER_H = 29.25, BAND_LINE_H = 22.75, MAX_STRIP_H = 64, MIN_STRIP_H = 26, PAD_X = 9.75;   // GradeDeck.h
const FS_H = 86, CELL_H = 62, PHOTO_W = 86, FOLDER_W = 78, GAP = 4.875, FS_PAD = 9.75;          // cosmo Filmstrip.h
const CRUMB_H = 22.75, CRUMB_PX = 10, CRUMB_GAP = 3.25;                                            // cosmo Breadcrumb.cpp:12
const THUMB_EDGE = 172, FRAME_EDGE = 160, STEP = 16;
const VIEWS = ["sources", "stills", "nodes"], LABELS = ["SOURCES", "STILLS", "NODES"];
const bandTop = () => HEADER_H + FS_H + 4;

export function mount(host, vm, ctx) {
  useCss("css/grade/deck.css");
  host.classList.add("gd");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());
  const S = vm.session;

  let W = 0, H = 0, nowMs = 0, rack = [], stills = [], fps = 24, selected = -1, m = null;
  // level
  let level = "", levelWanted = "", levelIdx = -1, cellRack = [], levelInit = false, levelSwapping = false, levelDir = 1, selKeyLast = null;
  const levelFade = new Anim(1);
  // views
  let viewWanted = 0, viewApplied = 0, tabPlaced = false, tabTX = -1, tabTW = -1;
  const viewAmt = [new Anim(1), new Anim(0), new Anim(0)], tabX = new Anim(0), tabW = new Anim(0), tabHover = new HoverFade();
  let viewTab = [null, null, null];
  // strip
  const ringPos = new Anim(0), cellHover = new HoverFade(), stillHover = new HoverFade(), graphHover = new HoverFade();
  let graph = { nodes: [], rects: new Map(), moving: false };
  const graphMotion = new GraphMotion();
  let ringInit = false, ringTarget = -1, scrollX = new Anim(0), scrollT = 0, stillScrollX = new Anim(0), stillScrollT = 0;
  let primaryCell = -1, selCells = [];
  // crumbs
  const crumb = [{ path: [], chain: [], a: new Anim(1) }, { path: [], chain: [], a: new Anim(0) }];
  let crumbFront = 0, crumbPath = [], crumbChainNext = [], crumbShownKey = null, crumbHover = new HoverFade();
  let headerRight = 84;
  // reference frame
  let selVideo = false, reason = "", selMedia = "", selName = "", srcFps = 24, sourceDur = 1, frameTarget = 0, frameLast = -1;
  let selectorWanted = false, selectorApplied = false, selectorInit = false, dragging = false, trackHovered = false, trackHoverApplied = false;
  const shownFrame = new Anim(0), selectorAmt = new Anim(0), trackHover = new Anim(0), stepHover = new HoverFade(), stripFade = new Anim(1);
  let framesMedia = "", stepRect = [null, null];
  const frameMissing = new Set(), frameAlpha = new Map();   // a strip frame the host had to decode fades in

  // ---------------------------------------------------------------- bind (GradeDeck::bind)
  own(host, effect(() => {
    m = vm.model.value;
    if (!m) return;
    rack = m.rack || [];
    stills = m.stills || [];
    fps = m.fps > 0 ? m.fps : 24;
    selected = m.selectedRack ?? -1;
    const selKey = selected >= 0 && selected < rack.length ? rack[selected].rackObj : "";
    // (the first bind follows too, as the native's does - unless a test fixture set the level itself)
    const first = selKeyLast === null;
    if ((!first || !vm.view.fixture.peek()) && selKey !== (selKeyLast || "") && selected >= 0 && selected < rack.length) {
      const par = rack[selected].parent;
      const want = par >= 0 && par < rack.length ? rack[par].rackObj : "";
      if (want !== vm.view.deckLevel.peek()) vm.view.deckLevel.value = want;   // the strip follows the Grade target
    }
    selKeyLast = selKey;
    rebuildCells();
    bindFrame();
    loop.kick();
  }));
  own(host, effect(() => {                           // vm.view.deckLevel: RackTree.onOpenGroup -> GradeDeck::openGroup
    const want = vm.view.deckLevel.value || "";
    const r = vm.model.peek()?.rack || [];
    levelWanted = !want || r.some((n) => n.group && n.rackObj === want) ? want : "";
    if (!levelInit && vm.model.peek()) { level = levelWanted; levelInit = true; rebuildCells(); }
    loop.kick();
  }));
  own(host, effect(() => { const v = VIEWS.indexOf(vm.view.deckView.value); viewWanted = v < 0 ? 0 : v; loop.kick(); }));
  own(host, effect(() => { S.thumbsVersion.value; S.stillsVersion.value; loop.kick(); }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));

  function rebuildCells() {
    levelIdx = rack.findIndex((n) => n.group && n.rackObj === level);
    if (levelIdx < 0) level = "";
    cellRack = [];
    rack.forEach((n, i) => { if (n.parent === levelIdx) cellRack.push(i); });
    selCells = [];
    cellRack.forEach((ri, c) => { if (rack[ri].selected || ri === selected) selCells.push(c); });
    primaryCell = cellRack.indexOf(selected);
    if (primaryCell >= 0) { if (!ringInit) { ringPos.set(primaryCell); ringInit = true; } ringTarget = primaryCell; revealCell(primaryCell); }
    // the path: the top, each group down to the shown one, and the selected source when in this level
    const path = ["All sources"], chain = [""], up = [];
    for (let g = levelIdx; g >= 0 && g < rack.length; g = rack[g].parent) up.push(g);
    for (const g of up.reverse()) { path.push(rack[g].cosmoName || rack[g].bindName); chain.push(rack[g].rackObj); }
    if (primaryCell >= 0 && !rack[selected].group) path.push(rack[selected].cosmoName || rack[selected].bindName);
    crumbPath = path; crumbChainNext = chain;
  }

  function bindFrame() {
    selVideo = false; reason = ""; selMedia = "";
    if (selected >= 0 && selected < rack.length) {
      const n = rack[selected];
      selName = n.cosmoName || n.bindName;
      if (n.group) reason = "A group grades every source under it — no reference frame to pick.";
      else if (!n.video) reason = "A still is graded as it is — no reference frame to pick.";
      else if (n.failed) reason = "Offline — the reference frame cannot be read.";
      else {
        selVideo = true;
        selMedia = n.media;
        srcFps = n.mediaFps > 0 ? n.mediaFps : fps;
        let dur = n.mediaDuration || 0;
        if (dur <= 0) for (const c of m.clips || []) if (c.src === n.rackObj) dur = Math.max(dur, c.out);
        if (dur <= 0) dur = Math.max(m.duration || 0, (n.frame || 0) + 1);
        sourceDur = Math.max(1 / srcFps, dur);
        if (!dragging) frameTarget = n.frame || 0;
      }
    } else reason = rack.length ? "Select a source to choose the frame it is graded on." : "";
    selectorWanted = selVideo;
  }

  // ---------------------------------------------------------------- geometry
  const cellW = (c) => (rack[cellRack[c]]?.group ? FOLDER_W : PHOTO_W);
  function cellX(c, sx = scrollX.value) { let x = FS_PAD; for (let k = 0; k < c; ++k) x += cellW(k) + GAP; return x - sx; }
  const stillX = (c) => FS_PAD + c * (PHOTO_W + GAP) - stillScrollX.value;
  const contentW = () => { let x = FS_PAD; for (let k = 0; k < cellRack.length; ++k) x += cellW(k) + GAP; return x + FS_PAD; };
  const maxScroll = () => Math.max(0, contentW() - W);
  const stillMax = () => Math.max(0, FS_PAD * 2 + stills.length * (PHOTO_W + GAP) - W);
  function scrollTo(t) { scrollT = Math.min(Math.max(t, 0), maxScroll()); scrollX.animateTo(scrollT, tok.ms("motion-scroll"), Ease.EaseOutCubic, nowMs || performance.now()); }
  function revealCell(c) {
    if (!(W > 0)) return;
    const x = cellX(c, scrollT), w = cellW(c);
    if (x < FS_PAD) scrollTo(scrollT + x - FS_PAD);
    else if (x + w > W - FS_PAD) scrollTo(scrollT + x + w - (W - FS_PAD));
  }
  function cellAt(pt) {
    if (pt.y < HEADER_H + (FS_H - CELL_H) / 2 || pt.y > HEADER_H + (FS_H + CELL_H) / 2) return -1;
    for (let c = 0; c < cellRack.length; ++c) { const x = cellX(c) + stripDx(); if (pt.x >= x && pt.x <= x + cellW(c)) return c; }
    return -1;
  }
  function stillAt(pt) {
    if (pt.y < HEADER_H + (FS_H - CELL_H) / 2 || pt.y > HEADER_H + (FS_H + CELL_H) / 2) return -1;
    for (let c = 0; c < stills.length; ++c) { const x = stillX(c); if (pt.x >= x && pt.x <= x + PHOTO_W) return c; }
    return -1;
  }
  const stripDx = () => 18 * (1 - levelFade.value) * (levelSwapping ? -levelDir : levelDir);
  const hasFrameStrip = () => H - bandTop() - BAND_LINE_H - 8 >= MIN_STRIP_H;
  function frameTrackRect() {
    const top = bandTop();
    if (hasFrameStrip()) return { x: PAD_X, y: top + BAND_LINE_H, w: Math.max(0, W - 2 * PAD_X), h: Math.min(MAX_STRIP_H, H - top - BAND_LINE_H - 8) };
    const x0 = PAD_X + 205;
    return { x: x0, y: top, w: Math.max(0, W - PAD_X - x0), h: Math.max(0, Math.min(BAND_LINE_H, H - top)) };
  }
  const frameToX = (t) => { const r = frameTrackRect(); return r.x + Math.min(Math.max(t / sourceDur, 0), 1) * r.w; };
  function snapToFrame(t) {
    const last = Math.max(0, Math.floor(sourceDur * srcFps - 1e-6) / srcFps);
    return Math.min(Math.max(Math.round(t * srcFps) / srcFps, 0), last);
  }
  function crumbSpans(path) {
    const spans = []; let x = PAD_X;
    path.forEach((s, i) => { const w = measure(s, CRUMB_PX, F.sans); spans.push({ x, w }); x += w + CRUMB_GAP; if (i + 1 < path.length) x += 9 + CRUMB_GAP; });
    return spans;
  }
  const crumbY = () => (HEADER_H - CRUMB_H) * 0.5;

  // ---------------------------------------------------------------- advance (GradeDeck::advance)
  function advance(now) {
    nowMs = now;
    let moving = false;
    const xf = tok.ms("motion-cross-fade");
    if (viewWanted !== viewApplied) { viewAmt.forEach((a, v) => a.animateTo(v === viewWanted ? 1 : 0, xf, Ease.EaseOutCubic, now)); viewApplied = viewWanted; }
    for (const a of viewAmt) moving = a.update(now) || moving;
    const tab = viewTab[viewWanted];
    if (tab) {
      if (!tabPlaced) { tabX.set(tab.x); tabW.set(tab.w); tabTX = tab.x; tabTW = tab.w; tabPlaced = true; }
      else if (Math.abs(tabTX - tab.x) > 0.5 || Math.abs(tabTW - tab.w) > 0.5) {
        tabX.animateTo(tab.x, 200, Ease.EaseOutCubic, now); tabW.animateTo(tab.w, 200, Ease.EaseOutCubic, now); tabTX = tab.x; tabTW = tab.w;
      }
    }
    moving = tabX.update(now) || moving; moving = tabW.update(now) || moving;
    moving = tabHover.advance(now) || moving;
    // the level: fade out, swap, fade in
    if (!levelSwapping && levelWanted !== level && !levelFade.animating) {
      let dw = -1, dl = -1;
      for (const n of rack) { if (n.group && n.rackObj === levelWanted) dw = n.depth; if (n.group && n.rackObj === level) dl = n.depth; }
      levelDir = dw > dl ? 1 : -1;
      levelSwapping = true;
      levelFade.animateTo(0, tok.ms("motion-hover"), Ease.EaseOutCubic, now);
    }
    if (levelSwapping && !levelFade.animating && levelFade.value <= 0.001) {
      level = levelWanted; scrollX.set(0); scrollT = 0; ringInit = false;
      rebuildCells();
      levelSwapping = false;
      levelFade.animateTo(1, xf, Ease.EaseOutCubic, now);
    }
    moving = levelFade.update(now) || moving;
    // the breadcrumb cross-fades
    const key = crumbPath.join("\u0001");
    if (crumbShownKey === null) { crumb[crumbFront].path = crumbPath; crumb[crumbFront].chain = crumbChainNext; crumbShownKey = key; }
    else if (key !== crumbShownKey) {
      const back = 1 - crumbFront;
      crumb[back].path = crumbPath; crumb[back].chain = crumbChainNext;
      crumb[back].a.set(0); crumb[back].a.animateTo(1, xf, Ease.EaseOutCubic, now);
      crumb[crumbFront].a.animateTo(0, xf, Ease.EaseOutCubic, now);
      crumbFront = back; crumbShownKey = key;
    }
    for (const c of crumb) moving = c.a.update(now) || moving;
    moving = crumbHover.advance(now) || moving;
    // the ring slides
    if (ringTarget >= 0 && Math.abs(ringPos.target - ringTarget) > 1e-6) ringPos.animateTo(ringTarget, tok.ms("motion-select"), Ease.EaseOutCubic, now);
    moving = ringPos.update(now) || moving;
    moving = scrollX.update(now) || moving;
    moving = stillScrollX.update(now) || moving;
    moving = cellHover.advance(now) || moving;
    moving = stillHover.advance(now) || moving;
    moving = graphHover.advance(now) || moving;
    // the reference frame
    if (!selectorInit) { selectorAmt.set(selectorWanted ? 1 : 0); selectorApplied = selectorWanted; shownFrame.set(frameTarget); frameLast = frameTarget; selectorInit = true; }
    if (selectorWanted !== selectorApplied) { selectorAmt.animateTo(selectorWanted ? 1 : 0, tok.ms("motion-scroll"), Ease.EaseOutCubic, now); selectorApplied = selectorWanted; }
    moving = selectorAmt.update(now) || moving;
    if (!dragging && frameTarget !== frameLast) { shownFrame.animateTo(frameTarget, tok.ms("motion-catch-up"), Ease.EaseOutCubic, now); frameLast = frameTarget; }
    moving = shownFrame.update(now) || moving;
    if (trackHovered !== trackHoverApplied) { trackHover.animateTo(trackHovered ? 1 : 0, tok.ms("motion-hover"), Ease.EaseOutCubic, now); trackHoverApplied = trackHovered; }
    moving = trackHover.update(now) || moving;
    moving = stepHover.advance(now) || moving;
    if (selVideo && selMedia !== framesMedia) {
      if (framesMedia) { stripFade.set(0); stripFade.animateTo(1, xf, Ease.EaseOutCubic, now); }
      framesMedia = selMedia;
    }
    moving = stripFade.update(now) || moving;
    for (const [k, a] of frameAlpha) { if (a.update(now)) moving = true; else if (a.value >= 1) frameAlpha.delete(k); }
    return moving || rack.some((n) => n.pending);
  }

  // ---------------------------------------------------------------- paint (GradeDeck::onPaint + onOverlay)
  function paint() {
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), primary = col("primary"), destr = col("destructive");
    const rc = tok.px("radius-control"), pill = tok.px("radius-pill"), hair = tok.px("radius-hairline") || 1;
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("deck-bg") });
    p.line(0, 0.5, W, 0.5, border, 1);
    const hy = HEADER_H * 0.5, srcA = viewAmt[0].value, stA = viewAmt[1].value, grA = viewAmt[2].value;
    const sources = rack.filter((n) => !n.group).length;
    const title = (word, count, a) => {
      if (a <= 0.001) return 0;
      p.text(word, PAD_X, baseline(hy, 9), 9, F.semibold, fade(muted, a), 0.13 * 9);
      const lw = measure(word, 9, F.semibold, 0.13 * 9);
      if (count) p.text(count, PAD_X + lw + 6, baseline(hy, 9), 9, F.mono, fade(muted, 0.7 * a));
      return lw + 6 + (count ? measure(count, 9, F.mono) : 0);
    };
    const srcW = title("SOURCES", String(sources), srcA);
    title("STILLS", String(stills.length), stA);
    title("NODES", "", grA);
    headerRight = PAD_X + (srcW > 0 ? srcW : measure("SOURCES", 9, F.semibold, 0.13 * 9) + 20) + 6.5;
    // the views, right-aligned, the shown one underlined
    let right = W - PAD_X;
    for (let v = 2; v >= 0; --v) {
      const tw = measure(LABELS[v], 9, F.semibold, 0.06 * 9) + 12;
      viewTab[v] = { x: right - tw, y: (HEADER_H - 17) * 0.5, w: tw, h: 17 };
      right -= tw + 2;
    }
    for (let v = 0; v < 3; ++v) {
      const r = viewTab[v], hv = tabHover.amount(v), on = viewAmt[v].value;
      if (hv > 0.001) p.rect(r.x, r.y, r.w, r.h, rc, { fill: hoverWash(hv) });
      p.text(LABELS[v], r.x + 6, baseline(hy, 9), 9, F.semibold, lerp(lerp(muted, fg, hv * 0.6), fg, on), 0.06 * 9);
    }
    if (tabW.value > 0) p.rect(tabX.value + 4, viewTab[0].y + viewTab[0].h - 1, Math.max(0, tabW.value - 8), 1.5, pill, { fill: primary });
    // the breadcrumb (cosmo's), between the title and the tabs; gone with the sources view
    const bx = Math.min(headerRight, W), bw = Math.max(0, W - PAD_X - bx - (W - viewTab[0].x + 8));
    if (srcA > 0.001 && bw > 0) {
      const by = crumbY();
      p.clip(bx, by - 1, bw, CRUMB_H + 2);
      p.rect(bx, by, bw, CRUMB_H, 0, { fill: fade(col("left-rail-bg"), srcA) });
      p.line(bx, by, bx + bw, by, fade(border, srcA), 1);
      p.line(bx, by + CRUMB_H, bx + bw, by + CRUMB_H, fade(border, srcA), 1);
      for (let k = 0; k < 2; ++k) {
        const c = crumb[k], a = c.a.value * srcA;
        if (a <= 0.001) continue;
        const spans = crumbSpans(c.path);
        c.path.forEach((s, i) => {
          const last = i + 1 === c.path.length, hv = last || k !== crumbFront ? 0 : crumbHover.amount(i);
          const color = last ? fade(fg, 0.8) : lerp(muted, fg, hv);
          p.text(s, bx + spans[i].x, by + CRUMB_H * 0.5 + CRUMB_PX * 0.35, CRUMB_PX, F.sans, fade(color, a));
          if (!last) p.icon("chevronRight", bx + spans[i].x + spans[i].w + CRUMB_GAP, by + CRUMB_H * 0.5 - 4.5, 9, fade(muted, 0.4 * a), 1);
        });
      }
      p.restore();
    }
    // the strip (cosmo's Filmstrip)
    const sy = HEADER_H, ry = sy + (FS_H - CELL_H) * 0.5;
    // the deck's own sentences sit UNDER the strips (children paint over onPaint): they show only
    // where the strips have faded
    if (!rack.length && stA < 0.999) {
      const s = "Sources appear here as footage joins the rack.";
      p.text(s, (W - measure(s, 11, F.sans)) * 0.5, HEADER_H + FS_H * 0.5 + 4, 11, F.sans, fade(muted, 1 - stA));
    }
    if (!stills.length && stA > 0.001) {
      const s = ellipsize("Grab a still (Ctrl+Alt+G) to keep a frame and the grade it was made with.", W - 2 * PAD_X, 11, F.sans);
      p.text(s, (W - measure(s, 11, F.sans)) * 0.5, HEADER_H + FS_H * 0.5 + 4, 11, F.sans, fade(muted, stA));
    }
    const srcLayer = levelFade.value * srcA;
    for (const a of [srcLayer, stA]) if (a > 0.001) {   // each cosmo strip's background, at its opacity
      p.rect(0, sy, W, FS_H, 0, { fill: fade(col("filmstrip-bg"), a) });
      p.line(0, sy, W, sy, fade(border, a), 1);
    }
    p.clip(0, sy, W, FS_H);
    const la = levelFade.value * srcA;
    if (la > 0.001) {
      p.group(la);
      const dx = stripDx();
      cellRack.forEach((ri, c) => {
        const n = rack[ri], x = cellX(c) + dx, cw = cellW(c);
        if (x + cw < 0 || x > W) return;
        if (n.group) {
          p.rect(x, ry, cw, CELL_H, 2, { fill: col("folder-chip-bg") });
          p.path(dashRect(x, ry, cw, CELL_H), { stroke: [border[0], border[1], border[2], 0.6], sw: 1 });
          const name = n.cosmoName || n.bindName, count = rack.filter((k) => k.parent === ri).length + " items";
          p.text(name, x + cw * 0.5 - name.length * 9 * 0.3, ry + CELL_H * 0.5 - 1, 9, F.sans, muted);
          p.text(count, x + cw * 0.5 - count.length * 8 * 0.3, ry + CELL_H * 0.5 + 11, 8, F.sans, [muted[0], muted[1], muted[2], 0.4]);
        } else if (n.pending) {
          p.rect(x, ry, cw, CELL_H, rc, { fill: col("folder-chip-bg") });
          p.add(glyph.spinner(x + cw * 0.5, ry + CELL_H * 0.5, 9, nowMs));
        } else {
          const th = !n.failed && n.media ? S.thumbNow(n.media, n.frame || 0, THUMB_EDGE) : null;
          p.rect(x, ry, cw, CELL_H, 0, { fill: col("thumb-placeholder") });
          if (th) p.cover(th.url, th.w, th.h, x, ry, cw, CELL_H);
        }
        const hv = c === primaryCell ? 0 : cellHover.amount(c);
        if (hv > 0.001) p.rect(x, ry, cw, CELL_H, rc, { fill: hoverWash(hv) });
        if (n.bypass) {
          p.rect(x, ry, cw, CELL_H, rc, { fill: [0.02, 0.02, 0.02, 0.52] });
          p.rect(x + 4, ry + 4, 12, 12, rc, { fill: [0.02, 0.02, 0.02, 0.72] });
          p.icon("ban", x + 6, ry + 6, 8, whiteA(0.72), 1.1);
        }
        if (!n.group && c !== primaryCell) {
          const isSel = selCells.includes(c), o = isSel ? 2 : 1;
          p.rect(x - o, ry - o, cw + 2 * o, CELL_H + 2 * o, 0, { stroke: isSel ? primaryA(0.65) : whiteA(0.08), sw: isSel ? 2 : 1 });
        }
        if (n.failed) {
          const s = "OFFLINE", tw = measure(s, 7.5, F.semibold);
          const chip = { x: x + (PHOTO_W - tw - 8) * 0.5, y: ry + 20, w: tw + 8, h: 13 };
          p.rect(chip.x, chip.y, chip.w, chip.h, hair, { fill: [0, 0, 0, 0.8], stroke: destr, sw: 1 });
          p.text(s, chip.x + 4, baseline(chip.y + chip.h * 0.5, 7.5), 7.5, F.semibold, destr);
        }
      });
      if (primaryCell >= 0 && cellRack.length) {
        const pos = Math.min(Math.max(ringPos.value, 0), cellRack.length - 1), a = Math.floor(pos), b = Math.min(cellRack.length - 1, a + 1), f = pos - a;
        const rx = cellX(a) + dx + (cellX(b) - cellX(a)) * f, rw = cellW(a) + (cellW(b) - cellW(a)) * f, o = 3;
        const n = rack[cellRack[primaryCell]];
        p.rect(rx - o, ry - o, rw + 2 * o, CELL_H + 2 * o, 0, { stroke: n.bypass ? whiteA(0.42) : primary, sw: 2 });
        if (!n.group) {
          p.rect(rx, ry + CELL_H - 12, rw, 12, 0, { fill: n.bypass ? [0.16, 0.16, 0.16, 0.85] : primaryA(0.8) });
          const label = n.cosmoName || n.bindName;   // cosmo centres on an estimate (Filmstrip.cpp:371)
          p.text(label, rx + rw * 0.5 - label.length * 7 * 0.3, ry + CELL_H - 3, 7, F.medium, [1, 1, 1, 1]);
        }
      }
      p.restore();
    }
    if (stA > 0.001) {                              // R-CLR-4: the stills
      p.group(stA);
      stills.forEach((st, c) => {
        const x = stillX(c);
        if (x + PHOTO_W < 0 || x > W) return;
        p.rect(x, ry, PHOTO_W, CELL_H, 0, { fill: col("thumb-placeholder") });
        const th = S.stillNow(st.id, THUMB_EDGE);
        if (th) p.cover(th.url, th.w, th.h, x, ry, PHOTO_W, CELL_H);
        const hv = stillHover.amount(c);
        if (hv > 0.001) p.rect(x, ry, PHOTO_W, CELL_H, rc, { fill: hoverWash(hv) });
        p.rect(x - 1, ry - 1, PHOTO_W + 2, CELL_H + 2, 0, { stroke: whiteA(0.08), sw: 1 });
      });
      p.restore();
    }
    if (grA > 0.001) {                              // R-CLR-3: the node graph
      p.group(grA);
      graph = paintNodeGraph(p, m, { x: 0, y: sy, w: W, h: FS_H }, graphHover, graphMotion, nowMs);
      p.restore();
    }
    p.restore();
    paintBand(fg, muted, border, primary, rc, pill, hair);
    p.end();
  }

  function paintBand(fg, muted, border, primary, rc, pill, hair) {
    const top = bandTop();
    if (H - top < 12) return;
    const lineCy = top + Math.min(BAND_LINE_H, H - top) * 0.5, sa = selectorAmt.value, tr = frameTrackRect(), strip = hasFrameStrip();
    if (sa > 0.001) {
      p.text("REF FRAME", PAD_X, baseline(lineCy, 9), 9, F.semibold, fade(muted, sa), 0.13 * 9);
      const lx = PAD_X + measure("REF FRAME", 9, F.semibold, 0.13 * 9) + 6.5;
      const tc = timecode(shownFrame.value, srcFps), tcW = measure(tc, 10, F.mono);
      stepRect = [{ x: lx, y: lineCy - STEP * 0.5, w: STEP, h: STEP }, { x: lx + STEP + 3 + tcW + 3, y: lineCy - STEP * 0.5, w: STEP, h: STEP }];
      for (let k = 0; k < 2; ++k) {
        const hv = stepHover.amount(k), r = stepRect[k];
        if (hv > 0.001) p.rect(r.x, r.y, r.w, r.h, rc, { fill: fade(hoverWash(hv), sa) });
        const gc = css(fade(lerp(muted, fg, hv), sa)), gr = { x: r.x + 3, y: r.y + 3, w: STEP - 6, h: STEP - 6 };
        p.add(k === 0 ? glyph.chevronLeft(gr, gc) : glyph.chevronRightSmall(gr, gc));
      }
      p.text(tc, lx + STEP + 3, baseline(lineCy, 10), 10, F.mono, fade(fg, sa));
      const nx = stepRect[1].x + STEP + 8, len = "of " + timecode(sourceDur, srcFps), lenW = measure(len, 9, F.mono);
      if (strip) {
        p.text(len, W - PAD_X - lenW, baseline(lineCy, 9), 9, F.mono, fade(muted, 0.8 * sa));
        const s = selName + (dragging ? " · previewing in the monitor — release to grade on this frame" : " · drag to seek, ‹ › for the exact frame");
        p.text(ellipsize(s, W - PAD_X - lenW - 12 - nx, 10, F.sans), nx, baseline(lineCy, 10), 10, F.sans, fade(muted, sa));
      }
      const hv = trackHover.value, x = frameToX(shownFrame.value);
      if (strip) {
        p.rect(tr.x, tr.y, tr.w, tr.h, rc, { fill: fade(col("thumb-placeholder"), sa) });
        const n = Math.max(1, Math.floor(tr.w / Math.max(8, tr.h * 16 / 9)));
        p.clip(tr.x, tr.y, tr.w, tr.h);
        for (let i = 0; i < n; ++i) {
          const cell = { x: tr.x + tr.w * i / n, y: tr.y, w: tr.w / n - 1, h: tr.h };
          const ft = (i + 0.5) / n * sourceDur, fk = selMedia + "|" + ft;
          const th = selMedia ? S.thumbNow(selMedia, ft, FRAME_EDGE) : null;
          if (!th) frameMissing.add(fk);
          else if (frameMissing.delete(fk)) { const a = new Anim(0); a.animateTo(1, tok.ms("motion-scroll"), Ease.EaseOutCubic, nowMs); frameAlpha.set(fk, a); loop.kick(); }
          const fa = frameAlpha.get(fk)?.value ?? 1;
          if (th) p.cover(th.url, th.w, th.h, cell.x, cell.y, cell.w, cell.h, sa * stripFade.value * (0.55 + 0.25 * hv) * fa);
          else p.add(glyph.film({ x: cell.x + cell.w * 0.5 - 8, y: cell.y + cell.h * 0.5 - 6, w: 16, h: 12 }, css(fade(muted, 0.4 * sa))));
        }
        p.restore();
        p.rect(tr.x, tr.y, tr.w, tr.h, rc, { stroke: fade(lerp(border, whiteA(0.2), hv), sa), sw: 1 });
        p.rect(x - 1, tr.y - 3, 2, tr.h + 6, hair, { fill: fade(primary, sa) });
        p.rect(x - 5, tr.y - 5, 10, 6, hair, { fill: fade(primary, sa) });
        const knob = 5 + Math.max(hv, dragging ? 1 : 0);
        p.circle(x, tr.y + tr.h * 0.5, knob + 1.5, { fill: fade(primary, sa) });
        p.circle(x, tr.y + tr.h * 0.5, knob, { fill: fade([1, 1, 1, 1], sa) });
      } else {
        const cy = tr.y + tr.h * 0.5, th = 3 + hv;
        p.rect(tr.x, cy - th * 0.5, tr.w, th, pill, { fill: fade(col("secondary"), sa) });
        if (x - tr.x >= th) p.rect(tr.x, cy - th * 0.5, x - tr.x, th, pill, { fill: fade(primary, sa) });
        p.circle(x, cy, 4.5 + hv, { fill: fade([1, 1, 1, 1], sa) });
      }
    }
    if (sa < 0.999 && reason) {
      const cy = strip ? tr.y + tr.h * 0.5 : lineCy, s = ellipsize(reason, W - 2 * PAD_X, 10, F.sans);
      p.text(s, (W - measure(s, 10, F.sans)) * 0.5, baseline(cy, 10), 10, F.sans, fade(muted, 1 - sa));
    }
  }

  function dashRect(x, y, w, h) {
    let d = "";
    for (const s of [[x, y, x + w, y], [x + w, y, x + w, y + h], [x + w, y + h, x, y + h], [x, y + h, x, y]]) {
      const dx = s[2] - s[0], dy = s[3] - s[1], len = Math.hypot(dx, dy), n = Math.max(1, Math.floor(len / 6));
      for (let k = 0; k < n; k += 2) {
        const t0 = k / n, t1 = Math.min(1, (k + 1) / n);
        d += `M${(s[0] + dx * t0).toFixed(2)} ${(s[1] + dy * t0).toFixed(2)} L${(s[0] + dx * t1).toFixed(2)} ${(s[1] + dy * t1).toFixed(2)} `;
      }
    }
    return d;
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    const more = advance(now);
    paint();
    return more || graph.moving;
  }

  // ---------------------------------------------------------------- input (GradeDeck::handleGesture)
  const rootPt = (pt) => { const b = ctx.box.peek(); return { x: b.x + pt.x, y: b.y + pt.y }; };
  const timeAt = (x) => { const tr = frameTrackRect(); return tr.w > 0 ? Math.min(Math.max((x - tr.x) / tr.w, 0), 1) * sourceDur : 0; };
  const stepAt = (pt) => (!selVideo || selectorAmt.value < 0.5 ? 0 : inRect(pt, stepRect[0]) ? -1 : inRect(pt, stepRect[1]) ? 1 : 0);
  const shown = () => viewWanted;
  function crumbAt(pt) {
    if (shown() !== 0) return -1;
    const bx = Math.min(headerRight, W), by = crumbY();
    if (pt.y < by || pt.y > by + CRUMB_H) return -1;
    const c = crumb[crumbFront], spans = crumbSpans(c.path);
    for (let i = 0; i + 1 < spans.length; ++i) if (pt.x >= bx + spans[i].x && pt.x <= bx + spans[i].x + spans[i].w) return i;
    return -1;
  }
  function preview(t) {
    if (selected < 0 || selected >= rack.length) return;
    if (Math.abs(t - frameLast) > 1e-9) vm.view.preview.value = { bind: rack[selected].bindName, t };
  }
  function stepFrame(dir) {                          // GradeDeck::step
    if (!selVideo || selected < 0 || selected >= rack.length) return;
    const from = rack[selected].frame || 0, to = snapToFrame(from + dir / srcFps);
    if (Math.abs(to - snapToFrame(from)) < 1e-9) return;
    vm.cmd("rack frame " + q(rack[selected].bindName) + " --at " + seconds(to, srcFps));
  }

  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e);
    if (dragging) {
      const t = snapToFrame(timeAt(pt.x));
      preview(t); frameTarget = t; shownFrame.set(t); frameLast = t;
      loop.kick();
      return;
    }
    let hv = -1;
    for (let v = 0; v < 3; ++v) if (inRect(pt, viewTab[v])) hv = v;
    tabHover.setHovered(hv);
    trackHovered = selVideo && inRect(pt, frameTrackRect());
    const sd = stepAt(pt);
    stepHover.setHovered(sd < 0 ? 0 : sd > 0 ? 1 : -1);
    cellHover.setHovered(shown() === 0 ? cellAt(pt) : -1);
    stillHover.setHovered(shown() === 1 ? stillAt(pt) : -1);
    graphHover.setHovered(shown() === 2 ? nodeAt(graph.nodes, graph.rects, pt) : -1);
    crumbHover.setHovered(crumbAt(pt));
    loop.kick();
  });
  host.addEventListener("pointerleave", () => {
    if (dragging) return;
    tabHover.clear(); stepHover.clear(); cellHover.clear(); stillHover.clear(); graphHover.clear(); crumbHover.clear(); trackHovered = false; loop.kick();
  });
  host.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    const pt = localPoint(host, e);
    if (selVideo && inRect(pt, frameTrackRect())) {
      dragging = true;
      try { host.setPointerCapture(e.pointerId); } catch {}
      const t = snapToFrame(timeAt(pt.x));
      preview(t); frameTarget = t; shownFrame.set(t); frameLast = t;
      loop.kick();
    }
  });
  const endDrag = (commit) => {
    if (!dragging) return;
    dragging = false;
    if (commit && selected >= 0 && selected < rack.length)
      vm.cmd("rack frame " + q(rack[selected].bindName) + " --at " + seconds(frameTarget, srcFps));
    vm.view.preview.value = null;                    // the commit is what shows now
    loop.kick();
  };
  host.addEventListener("pointerup", () => endDrag(true));
  host.addEventListener("pointercancel", () => endDrag(false));
  host.addEventListener("click", (e) => {
    const pt = localPoint(host, e);
    for (let v = 0; v < 3; ++v) if (inRect(pt, viewTab[v])) { vm.view.deckView.value = VIEWS[v]; return; }
    const sd = stepAt(pt);
    if (sd) { stepFrame(sd); return; }
    const ci = crumbAt(pt);
    if (ci >= 0) { const ro = crumb[crumbFront].chain[ci]; if (ro !== undefined) vm.openDeckGroup(ro); return; }
    if (shown() === 0) {                             // cosmo's filmstrip selection
      const c = cellAt(pt);
      if (c < 0) return;
      const i = cellRack[c], b = q(rack[i].bindName);
      if (e.shiftKey) vm.cmd("rack select " + b + " --range");
      else if (e.ctrlKey || e.metaKey) vm.cmd("rack select " + b + " --add");
      else {
        const selectedCount = rack.filter((r) => r.selected).length;
        if (i !== selected || selectedCount > 1) vm.cmd("rack select " + b);
      }
    } else if (shown() === 2) {                      // NodeGraph::onSelect (App.cpp:114)
      const i = nodeAt(graph.nodes, graph.rects, pt);
      if (i >= 0 && graph.nodes[i].bind) vm.cmd("rack select " + q(graph.nodes[i].bind));
    }
  });
  host.addEventListener("dblclick", (e) => {
    const pt = localPoint(host, e);
    if (shown() === 0) {                             // cosmo's drill-in
      const c = cellAt(pt);
      if (c < 0) return;
      const n = rack[cellRack[c]];
      if (!n.group) return;
      vm.openDeckGroup(n.rackObj);
      vm.setRackOpen(n.rackObj, true);
    } else if (shown() === 1) {
      const c = stillAt(pt);
      if (c >= 0) activateStill(vm, stills[c].id);
    }
  });
  host.addEventListener("contextmenu", (e) => {
    e.preventDefault();
    const pt = localPoint(host, e), r = rootPt(pt);
    if (shown() === 0) { const c = cellAt(pt); if (c >= 0) openRackContext(vm, ctx, cellRack[c], r.x, r.y); }
    else if (shown() === 1) { const c = stillAt(pt); if (c >= 0) openStillContext(vm, ctx, stills[c].id, r.x, r.y); }
    else { const i = nodeAt(graph.nodes, graph.rects, pt); if (i >= 0) openNodeContext(vm, ctx, graph.nodes[i], r.x, r.y); }
  });
  host.addEventListener("wheel", (e) => {
    const pt = localPoint(host, e);
    if (pt.y < HEADER_H || pt.y > HEADER_H + FS_H) return;
    const d = (Math.abs(e.deltaY) > Math.abs(e.deltaX) ? e.deltaY : e.deltaX) / 100 * (FS_H + GAP);
    if (shown() === 0) scrollTo(scrollT + d);
    else if (shown() === 1) { stillScrollT = Math.min(Math.max(stillScrollT + d, 0), stillMax()); stillScrollX.animateTo(stillScrollT, tok.ms("motion-scroll"), Ease.EaseOutCubic, performance.now()); }
    e.preventDefault();
    loop.kick();
  }, { passive: false });
  loop.kick();
}
