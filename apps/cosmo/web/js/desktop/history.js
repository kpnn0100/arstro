/*
 * Cosmo by arstro — desktop/history.js: the history-tree modal (History ▸ Show History Tree…).
 *
 * Ports widgets/HistoryView.cpp, drawn into one canvas exactly as the native draws its overlay:
 * a scrim black @ 0.5, a card clamp(W-100, 360, 640) x clamp(H-140, 300, 560) centred (12 px
 * lower while it fades), popover with a 1 px border and r6, "HISTORY" 12 px SemiBold spaced
 * 1.44 and the hint line, a 22 px close button that lifts 1.5 px on hover, and the tree: a
 * pre-order DFS where the first child keeps its parent's lane and every further child (and
 * every further root) takes a new one; nodes of radius 7 at (24 + 30 lane, 24 + 46 row), the
 * current one filled with the accent and ringed at r10, the others secondary with a border;
 * edges straight down in a lane, else across at parent.y + 23 and down; labels in one column
 * after the widest lane; per-node hover wash; scroll bars when the tree overflows. Every colour
 * is multiplied by the appear value (150 ms in / 110 ms out EaseOutCubic).
 *
 * Pan glides EaseOutCubic toward its target: drag 70 ms, wheel 170 ms (46 px a notch), a jump's
 * re-centre 260 ms; on open it is centred on the current node at once. Click a node = `history
 * jump <i>` (the view stays open and re-centres when the model's current step moves); click the
 * close button or outside the card = close. No keys - Ctrl+Z / Ctrl+Y still work behind it.
 *
 * Data: vm.history.entries = [{parent, label}] (index = step), vm.history.current. Without them
 * (an older core) the card opens with the tree area empty and says so.
 */
import { untracked } from "../core/signal.js";
import { h, bindEffect } from "../core/dom.js";
import { Tween, Ease, reducedMotion } from "../core/motion.js";
import { gestures, local, scaleOf, wheelDelta, canvasFont, hintSpacing } from "./editor.js";

const NODE_R = 7, ROW_H = 46, LANE_W = 30, PAD = 24, LABEL_SPACE = 190, HEADER_H = 50, MARGIN = 14, BTN = 22;
const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const smooth = (t) => t * t * (3 - 2 * t);

/** A token colour as [r, g, b, a] (0..255, alpha 0..1). */
function tokenColor(name) {
  const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  if (v.startsWith("#")) {
    const n = parseInt(v.slice(1), 16);
    return [(n >> 16) & 255, (n >> 8) & 255, n & 255, 1];
  }
  const m = v.match(/[\d.]+/g) || [0, 0, 0, 1];
  return [+m[0], +m[1], +m[2], m[3] !== undefined ? +m[3] : 1];
}
const lerpC = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
const brighten = (c, t) => [c[0] + (255 - c[0]) * t, c[1] + (255 - c[1]) * t, c[2] + (255 - c[2]) * t, c[3]];
const css = (c, a = 1) => `rgba(${Math.round(c[0])},${Math.round(c[1])},${Math.round(c[2])},${c[3] * a})`;

export function mountHistory(layer, vm, ed) {
  const cv = h("canvas.hv");
  layer.append(cv);
  const g = cv.getContext("2d");
  const C = {
    popover: tokenColor("--popover"), border: tokenColor("--border"), fg: tokenColor("--foreground"),
    muted: tokenColor("--muted-foreground"), secondary: tokenColor("--secondary"), primary: tokenColor("--primary"),
    white: tokenColor("--white"),
  };

  let open = false, nodes = [], current = -1, rows = [], lanes = [], maxRow = 0, maxLane = 0, available = true;
  let panX = 0, panY = 0;                       // targets
  const px = new Tween(0), py = new Tween(0);
  const appear = new Tween(0);
  const closeAmt = new Tween(0);
  let closeHover = false, hoverNode = -1;
  const hv = new Map();                         // node -> raw HoverFade amount
  let raf = 0, last = 0;

  const size = () => ed.size.peek();
  function cardRect() {
    const { w: W, h: H } = size();
    const w = clamp(W - 100, 360, 640), hh = clamp(H - 140, 300, 560);
    return { x: (W - w) / 2, y: (H - hh) / 2 + (1 - appear.value) * 12, w, h: hh };
  }
  const treeRect = () => { const c = cardRect(); return { x: c.x + MARGIN, y: c.y + HEADER_H, w: c.w - 2 * MARGIN, h: c.h - HEADER_H - MARGIN }; };
  const closeRect = () => { const c = cardRect(); return { x: c.x + c.w - MARGIN - BTN, y: c.y + (HEADER_H - BTN) / 2 - 6, w: BTN, h: BTN }; };
  const inside = (r, p) => p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;

  function relayout() {                         // HistoryView::relayout
    const n = nodes.length;
    rows = new Array(n).fill(0); lanes = new Array(n).fill(0); maxRow = maxLane = 0;
    if (!n) return;
    const kids = Array.from({ length: n }, () => []), roots = [];
    nodes.forEach((nd, i) => { const p = nd.parent; if (p >= 0 && p < n) kids[p].push(i); else roots.push(i); });
    let nextRow = 0, nextLane = 0;
    const dfs = (node, lane) => {
      lanes[node] = lane; rows[node] = nextRow++;
      maxLane = Math.max(maxLane, lane); maxRow = Math.max(maxRow, rows[node]);
      kids[node].forEach((k, i) => dfs(k, i === 0 ? lane : ++nextLane));
    };
    roots.forEach((r, i) => dfs(r, i === 0 ? 0 : ++nextLane));
  }
  function clampPan() {
    const tr = treeRect();
    const cw = PAD * 2 + maxLane * LANE_W + LABEL_SPACE, ch = PAD * 2 + maxRow * ROW_H;
    panX = clamp(panX, 0, Math.max(0, cw - tr.w));
    panY = clamp(panY, 0, Math.max(0, ch - tr.h));
  }
  function toCurrent() {
    if (current < 0 || current >= rows.length) { clampPan(); return; }
    const tr = treeRect();
    panY = PAD + rows[current] * ROW_H - tr.h / 2;
    panX = PAD + lanes[current] * LANE_W - tr.w * 0.35;
    clampPan();
  }
  function glide(ms) { px.to(panX, ms, Ease.EaseOutCubic); py.to(panY, ms, Ease.EaseOutCubic); kick(); }
  const center = (i) => { const tr = treeRect(); return { x: tr.x + PAD + lanes[i] * LANE_W - px.value, y: tr.y + PAD + rows[i] * ROW_H - py.value }; };
  function nodeAt(p) {
    if (!inside(treeRect(), p)) return -1;
    for (let i = 0; i < nodes.length; i++) { const c = center(i); if (Math.hypot(p.x - c.x, p.y - c.y) <= NODE_R + 5) return i; }
    return -1;
  }

  function show() {
    const hs = vm.history.peek();
    available = Array.isArray(hs.entries);
    if (available && !hs.entries.length) return;              // App::openHistoryView's guard
    nodes = available ? hs.entries : [];
    current = hs.current;
    relayout();
    appear.set(appear.value);                                  // keep an in-flight fade
    open = true;
    ed.historyOpen.value = true;
    layer.classList.add("open");
    toCurrent();
    px.set(panX); py.set(panY);
    appear.to(1, 150, Ease.EaseOutCubic);
    kick();
  }
  function hide() {
    if (!open) return;
    open = false;
    ed.historyOpen.value = false;
    layer.classList.remove("open");                            // hit-testing stops at once
    closeHover = false; hoverNode = -1;
    appear.to(0, 110, Ease.EaseOutCubic);
    kick();
  }
  ed.openHistory = show;

  // The model's history moves under an open view (a jump, an edit, another client).
  bindEffect(layer, () => {
    const hs = vm.history.value;
    untracked(() => {
      if (!open || !Array.isArray(hs.entries)) return;
      const changed = hs.entries.length !== nodes.length || hs.entries.some((e, i) => e.parent !== nodes[i].parent || e.label !== nodes[i].label);
      nodes = hs.entries;
      if (changed) relayout();
      if (hs.current !== current || changed) { current = hs.current; toCurrent(); glide(260); }
      kick();
    });
  });

  // ---- drawing
  function kick() { if (!raf) { last = 0; raf = requestAnimationFrame(frame); } }
  function frame(now) {
    raf = 0;
    const dt = last ? now - last : 16;
    last = now;
    let moving = appear.animating || px.animating || py.animating || closeAmt.animating;
    const step = reducedMotion() ? 1 : dt / 120;
    for (let i = 0; i < Math.max(nodes.length, hv.size); i++) {
      const cur = hv.get(i) || 0, want = open && i === hoverNode ? 1 : 0;
      if (cur !== want) { hv.set(i, want > cur ? Math.min(1, cur + step) : Math.max(0, cur - step)); moving = true; }
    }
    draw();
    if (moving || open) raf = requestAnimationFrame(frame);
  }
  function draw() {
    const { w: W, h: H } = size();
    const dpr = (window.devicePixelRatio || 1) * scaleOf(layer);
    const bw = Math.round(W * dpr), bh = Math.round(H * dpr);
    if (cv.width !== bw || cv.height !== bh) { cv.width = bw; cv.height = bh; cv.style.width = W + "px"; cv.style.height = H + "px"; }
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, W, H);
    const a = appear.value;
    if (!open && a <= 0.001) return;
    g.fillStyle = `rgba(0,0,0,${0.5 * a})`;                   // Color{0,0,0,0.5·appear} (HistoryView.cpp:218)
    g.fillRect(0, 0, W, H);

    const c = cardRect();
    rr(c.x, c.y, c.w, c.h, 6);
    g.fillStyle = css(C.popover, a); g.fill();
    g.lineWidth = 1; g.strokeStyle = css(C.border, a); g.stroke();

    g.fillStyle = css(C.fg, a);
    label("HISTORY", c.x + MARGIN, c.y + 24, 12, "semibold", 0.12 * 12);
    g.fillStyle = css(C.muted, a);
    label("click a node to jump  .  Ctrl+Z undo  .  Ctrl+Y redo", c.x + MARGIN, c.y + 40, 10);

    const ch = closeAmt.value;
    const cr = closeRect(); cr.y -= 1.5 * ch;
    rr(cr.x, cr.y, cr.w, cr.h, 2);
    g.fillStyle = css(brighten(C.secondary, 0.18 * ch), a); g.fill();
    g.strokeStyle = css(lerpC(C.border, C.primary, 0.6 * ch), a); g.lineWidth = 1; g.stroke();
    g.beginPath();
    g.moveTo(cr.x + 6, cr.y + 6); g.lineTo(cr.x + BTN - 6, cr.y + BTN - 6);
    g.moveTo(cr.x + BTN - 6, cr.y + 6); g.lineTo(cr.x + 6, cr.y + BTN - 6);
    g.strokeStyle = css(lerpC(C.muted, C.fg, ch), a); g.lineWidth = 1.5; g.lineCap = "butt"; g.stroke();

    const tr = treeRect();
    g.save();
    g.beginPath(); g.rect(tr.x, tr.y, tr.w, tr.h); g.clip();
    if (!available) {
      g.fillStyle = css(C.muted, a);
      label("History is not available yet", tr.x + PAD, tr.y + PAD + 4, 12);
    }
    for (let i = 0; i < nodes.length; i++) {
      const hvv = smooth(hv.get(i) || 0) * a;
      if (hvv <= 0.001) continue;
      const hc = center(i);
      if (hc.y < tr.y - ROW_H || hc.y > tr.y + tr.h + ROW_H) continue;
      rr(tr.x + 2, hc.y - ROW_H / 2 + 3, tr.w - 4, ROW_H - 6, 2);
      g.fillStyle = css(C.white, 0.07 * hvv); g.fill();        // hoverWash
    }
    g.strokeStyle = css(C.border, a); g.lineWidth = 1.5;
    for (let i = 0; i < nodes.length; i++) {
      const p = nodes[i].parent;
      if (p < 0 || p >= nodes.length) continue;
      const A = center(p), B = center(i);
      g.beginPath(); g.moveTo(A.x, A.y);
      if (Math.abs(A.x - B.x) < 0.5) g.lineTo(B.x, B.y); else { g.lineTo(B.x, A.y + ROW_H / 2); g.lineTo(B.x, B.y); }
      g.stroke();
    }
    for (let i = 0; i < nodes.length; i++) {
      const cn = center(i);
      if (cn.y < tr.y - ROW_H || cn.y > tr.y + tr.h + ROW_H) continue;
      const cur = i === current;
      g.beginPath(); g.arc(cn.x, cn.y, NODE_R, 0, Math.PI * 2);
      if (cur) { g.fillStyle = css(C.primary, a); g.fill(); }
      else { g.fillStyle = css(C.secondary, a); g.fill(); g.strokeStyle = css(C.border, a); g.lineWidth = 1.5; g.stroke(); }
      if (cur) { g.beginPath(); g.arc(cn.x, cn.y, NODE_R + 3, 0, Math.PI * 2); g.strokeStyle = css(C.primary, a); g.lineWidth = 1.5; g.stroke(); }
      g.fillStyle = css(cur ? C.fg : C.muted, a);
      label(nodes[i].label, tr.x + PAD + maxLane * LANE_W + 18 - px.value, cn.y + 4, 12);
    }
    g.restore();

    const contentH = PAD * 2 + maxRow * ROW_H, contentW = PAD * 2 + maxLane * LANE_W + LABEL_SPACE;
    const maxY = Math.max(0, contentH - tr.h), maxX = Math.max(0, contentW - tr.w);
    if (nodes.length && maxY > 0.5) {
      const th = Math.max(28, tr.h * tr.h / contentH), ty = tr.y + (py.value / maxY) * (tr.h - th);
      rr(tr.x + tr.w - 5, tr.y, 4, tr.h, 2); g.fillStyle = css(C.white, 0.06 * a); g.fill();
      rr(tr.x + tr.w - 5, ty, 4, th, 2); g.fillStyle = css(C.white, 0.22 * a); g.fill();
    }
    if (nodes.length && maxX > 0.5) {
      const tw = Math.max(28, tr.w * tr.w / contentW), tx = tr.x + (px.value / maxX) * (tr.w - tw);
      rr(tr.x, tr.y + tr.h - 5, tr.w, 4, 2); g.fillStyle = css(C.white, 0.06 * a); g.fill();
      rr(tx, tr.y + tr.h - 5, tw, 4, 2); g.fillStyle = css(C.white, 0.22 * a); g.fill();
    }
  }
  function rr(x, y, w, hh, r) {
    r = Math.min(r, w / 2, hh / 2);
    g.beginPath();
    g.roundRect ? g.roundRect(x, y, w, hh, r) : g.rect(x, y, w, hh);
  }
  /** drawText: baseline-placed, no kerning, the native's rounded advances (editor.js). */
  function label(str, x, y, size, font = "sans", ls = 0) {
    g.font = canvasFont(size, font);
    g.textBaseline = "alphabetic";
    if ("fontKerning" in g) g.fontKerning = "none";
    if ("letterSpacing" in g) g.letterSpacing = ls + hintSpacing(str, size, font) + "px";
    g.fillText(str, x, y);
  }

  // ---- input (modal while open)
  let dragLast = null;
  gestures(cv, {
    dragStart: (e, p) => { dragLast = p; },
    drag: (e, p) => {
      if (!open || !dragLast) return;
      panX -= p.x - dragLast.x; panY -= p.y - dragLast.y;
      dragLast = p;
      clampPan(); glide(70);
    },
    drop: () => { dragLast = null; },
    click: (e, p) => {
      if (!open) return;
      if (inside(closeRect(), p) || !inside(cardRect(), p)) { hide(); return; }
      const i = nodeAt(p);
      if (i >= 0 && available) vm.historyJump(i).catch(() => {});
    },
  });
  cv.addEventListener("pointermove", (e) => {
    if (!open) return;
    const p = local(cv, e);
    const onClose = inside(closeRect(), p);
    if (onClose !== closeHover) { closeHover = onClose; closeAmt.to(onClose ? 1 : 0, 120, Ease.EaseOutCubic); }
    hoverNode = onClose ? -1 : nodeAt(p);
    kick();
  });
  cv.addEventListener("pointerleave", () => {
    if (closeHover) { closeHover = false; closeAmt.to(0, 120, Ease.EaseOutCubic); }
    hoverNode = -1; kick();
  });
  cv.addEventListener("wheel", (e) => {
    if (!open) return;
    e.preventDefault();
    panY -= wheelDelta(e) * ROW_H;
    clampPan(); glide(170);
  }, { passive: false });
}
