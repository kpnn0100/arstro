/*
 * Solaris — history/view.js: the history tree (R-HIST-3) — cosmo's HistoryView (cosmo/widgets/HistoryView.{h,cpp})
 * as Solaris's App uses it (App.cpp openHistory / syncHistory), ported from cosmo's web port (desktop/history.js).
 *
 * The shell mounts this on vm.view.history with ctx.open = Signal<true>; ctx.open turning false closes it — the card
 * fades out (110 ms EaseOutCubic) before the shell removes it. Drawn into one canvas as the native's overlay: a scrim
 * black @ .5, a card clamp(W−100, 360, 640) × clamp(H−140, 300, 560) centred (12 px lower while it fades), popover
 * fill + 1 px border r6, the title upper(tr("History")) 12 px SemiBold tracked 1.44, the hint (App's: "click a node to
 * jump · <undo key> undo · <redo key> redo" — this machine's keys), a 22 px close button that lifts 1.5 px on hover,
 * and the tree: pre-order DFS, the first child keeps its parent's lane, every further child (and root) takes a new one;
 * nodes r7 at (24 + 30·lane, 24 + 46·row); edges down a lane, else across at parent.y + 23 then down; labels in one
 * column after the widest lane; per-node hover wash. easeCurrent (App.cpp:49): the highlight TRAVELS — each node's
 * current amount eased 200 ms EaseOutCubic — the accent faded in over the plain node, its ring with it.
 *
 * The model: m.history [{id, parent (id), label}] oldest first, m.historyCurrent (an id). A node clicked is ONE line,
 * `history jump <id>`; the view stays open and follows the model (syncHistory): a changed tree is a new snapshot
 * (placed, re-centred at once); the current one moving eases there and re-centres (260 ms). The pan glides
 * EaseOutCubic: drag 70 ms, wheel 170 ms (a notch is a row), jump 260 ms. The close button or a click outside the
 * card closes it (vm.closeHistory()); Escape and the undo / redo keys are the shell's.
 */
import { effect, untracked } from "../core/signal.js";
import { own } from "../core/dom.js";
import { Anim, Ease, frames, motion } from "../kit/anim.js";
import { color, colorsVersion, rgba, lerpColor, brighten, whiteAlpha, hoverWash, text } from "../kit/draw.js";
import { FONT } from "../kit/text.js";
import { keyHint } from "../kit/keys.js";
import { useCss } from "../kit/css.js";
import { tr, say, shown, upper } from "../vm/words.js";

const NODE_R = 7, ROW_H = 46, LANE_W = 30, PAD = 24, LABEL_SPACE = 190, HEADER_H = 50, MARGIN = 14, BTN = 22, CARD_R = 6;
const DRAG_MS = 70, SCROLL_MS = 170, JUMP_MS = 260, CURRENT_MS = 200;
const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const inside = (r, p) => p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;
const fade = (c, a) => [c[0], c[1], c[2], c[3] * a];

export function mount(host, vm, ctx) {
  useCss("history/view.css");
  host.classList.add("sl-historyview");
  const cv = document.createElement("canvas");
  host.append(cv);
  const g = cv.getContext("2d");

  let open = true, nodes = [], ids = [], labels = [], current = -1;
  let rows = [], lanes = [], maxRow = 0, maxLane = 0;
  let panX = 0, panY = 0, panMs = JUMP_MS, issuedX = 0, issuedY = 0;
  const px = new Anim(0), py = new Anim(0), appear = new Anim(0), closeAmt = new Anim(0);
  let curAmt = [], curLast = [];
  const hv = new Map();                 // node -> Anim (HoverFade)
  let hoverNode = -1, closeHover = false, hint = "";

  const size = () => { const b = ctx.box.peek(); return { W: b.w, H: b.h }; };
  function cardRect() {
    const { W, H } = size();
    const w = clamp(W - 100, 360, 640), hh = clamp(H - 140, 300, 560);
    return { x: (W - w) / 2, y: (H - hh) / 2 + (1 - appear.value) * 12, w, h: hh };
  }
  const treeRect = () => { const c = cardRect(); return { x: c.x + MARGIN, y: c.y + HEADER_H, w: c.w - 2 * MARGIN, h: c.h - HEADER_H - MARGIN }; };
  const closeRect = () => { const c = cardRect(); return { x: c.x + c.w - MARGIN - BTN, y: c.y + (HEADER_H - BTN) / 2 - 6, w: BTN, h: BTN }; };

  function relayout() {                 // HistoryView::relayout
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
    const t = treeRect();
    const cw = PAD * 2 + maxLane * LANE_W + LABEL_SPACE, ch = PAD * 2 + maxRow * ROW_H;
    panX = clamp(panX, 0, Math.max(0, cw - t.w));
    panY = clamp(panY, 0, Math.max(0, ch - t.h));
  }
  function toCurrent() {
    if (current < 0 || current >= rows.length) { clampPan(); return; }
    const t = treeRect();
    panY = PAD + rows[current] * ROW_H - t.h / 2;
    panX = PAD + lanes[current] * LANE_W - t.w * 0.35;
    clampPan();
  }
  const center = (i) => { const t = treeRect(); return { x: t.x + PAD + lanes[i] * LANE_W - px.value, y: t.y + PAD + rows[i] * ROW_H - py.value }; };
  function nodeAt(p) {
    if (!inside(treeRect(), p)) return -1;
    for (let i = 0; i < nodes.length; i++) { const c = center(i); if (Math.hypot(p.x - c.x, p.y - c.y) <= NODE_R + 5) return i; }
    return -1;
  }

  // App::openHistory: the model's history as nodes (parent by index), the current one, the hint with this machine's keys
  function snapshot(m) {
    const hs = (m && m.history) || [];
    ids = []; labels = []; nodes = []; let cur = -1;
    for (const n of hs) {
      let parent = -1;
      for (let j = 0; j < ids.length; j++) if (ids[j] === n.parent) parent = j;
      if (n.id === m.historyCurrent) cur = nodes.length;
      nodes.push({ parent, label: n.label });
      ids.push(n.id); labels.push(n.label);
    }
    const first = (id) => { const k = keyHint(m, id) || ""; return k ? k.slice(4, k.length - 1) : ""; };
    let hs2 = say("click a node to jump");
    if (first("song.undo")) hs2 += "  ·  " + say("{1} undo", first("song.undo"));
    if (first("song.redo")) hs2 += "  ·  " + say("{1} redo", first("song.redo"));
    hint = hs2;
    current = cur;
    relayout();
    toCurrent();
    px.set(panX); py.set(panY); issuedX = panX; issuedY = panY;
    curAmt = nodes.map((_, i) => new Anim(i === current ? 1 : 0));
    curLast = nodes.map((_, i) => (i === current ? 1 : 0));
  }
  // App::syncHistory: a changed tree → a new snapshot; the current one moved → the highlight eases there, re-centred
  function sync(m) {
    const hs = (m && m.history) || [];
    if (!hs.length) { vm.closeHistory(); return; }   // the song closed
    const sameTree = hs.length === ids.length && hs.every((n, i) => n.id === ids[i] && n.label === labels[i]);
    if (!sameTree) { snapshot(m); return; }
    for (let i = 0; i < ids.length; i++)
      if (ids[i] === m.historyCurrent && i !== current) { current = i; panMs = JUMP_MS; toCurrent(); }
  }

  // ---- advance + paint
  function advance(now) {
    let moving = false;
    const M = motion();
    if (panX !== issuedX) { px.animateTo(panX, panMs, Ease.EaseOutCubic, now); issuedX = panX; }
    if (panY !== issuedY) { py.animateTo(panY, panMs, Ease.EaseOutCubic, now); issuedY = panY; }
    for (let i = 0; i < curAmt.length; i++) {
      const want = i === current ? 1 : 0;
      if (want !== curLast[i]) { curAmt[i].animateTo(want, CURRENT_MS, Ease.EaseOutCubic, now); curLast[i] = want; }
      if (curAmt[i].update(now)) moving = true;
    }
    for (const [i, a] of hv) {
      const want = open && i === hoverNode ? 1 : 0;
      if (a.target !== want) a.animateTo(want, M.hover, Ease.EaseOutCubic, now);
      if (a.update(now)) moving = true;
      else if (!want && a.value === 0) hv.delete(i);
    }
    const ch = open && closeHover ? 1 : 0;
    if (closeAmt.target !== ch) closeAmt.animateTo(ch, M.hover, Ease.EaseOutCubic, now);
    for (const a of [appear, closeAmt, px, py]) if (a.update(now)) moving = true;
    return moving;
  }
  function paint() {
    const { W, H } = size();
    const k = (window.devicePixelRatio || 1) * (ctx.scale ? ctx.scale() : 1);
    const bw = Math.max(1, Math.round(W * k)), bh = Math.max(1, Math.round(H * k));
    if (cv.width !== bw || cv.height !== bh) { cv.width = bw; cv.height = bh; }
    cv.style.width = W + "px"; cv.style.height = H + "px";
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.clearRect(0, 0, bw, bh);
    g.setTransform(bw / Math.max(1, W), 0, 0, bh / Math.max(1, H), 0, 0);
    const a = appear.value;
    if (!open && a <= 0.001) return;
    const C = {
      popover: color("popover"), border: color("border"), fg: color("foreground"), muted: color("muted-foreground"),
      secondary: color("secondary"), primary: color("primary"),
    };
    const fa = (c) => fade(c, a);
    g.fillStyle = `rgba(0,0,0,${0.5 * a})`;
    g.fillRect(0, 0, W, H);

    const c = cardRect();
    rr(c.x, c.y, c.w, c.h, CARD_R);
    g.fillStyle = rgba(fa(C.popover)); g.fill();
    g.lineWidth = 1; g.strokeStyle = rgba(fa(C.border)); g.stroke();

    text(g, upper(tr("History")), c.x + MARGIN, c.y + 24, 12, FONT.semibold, fa(C.fg), 0.12 * 12);
    text(g, shown(hint), c.x + MARGIN, c.y + 40, 10, FONT.sans, fa(C.muted));

    const ch = closeAmt.value;
    const cr = closeRect(); cr.y -= 1.5 * ch;
    rr(cr.x, cr.y, cr.w, cr.h, 2);
    g.fillStyle = rgba(fa(brighten(C.secondary, 0.18 * ch))); g.fill();
    g.strokeStyle = rgba(fa(lerpColor(C.border, C.primary, 0.6 * ch))); g.lineWidth = 1; g.stroke();
    g.beginPath();
    g.moveTo(cr.x + 6, cr.y + 6); g.lineTo(cr.x + BTN - 6, cr.y + BTN - 6);
    g.moveTo(cr.x + BTN - 6, cr.y + 6); g.lineTo(cr.x + 6, cr.y + BTN - 6);
    g.strokeStyle = rgba(fa(lerpColor(C.muted, C.fg, ch))); g.lineWidth = 1.5; g.lineCap = "butt"; g.stroke();

    const t = treeRect();
    g.save();
    g.beginPath(); g.rect(t.x, t.y, t.w, t.h); g.clip();
    for (const [i, an] of hv) {          // each node's hover wash (HoverFade)
      if (i >= nodes.length) continue;
      const v = an.value * a;
      if (v <= 0.001) continue;
      const hc = center(i);
      rr(t.x + 2, hc.y - ROW_H / 2 + 3, t.w - 4, ROW_H - 6, 2);
      g.fillStyle = rgba(hoverWash(v)); g.fill();
    }
    g.strokeStyle = rgba(fa(C.border)); g.lineWidth = 1.5;
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
      if (cn.y < t.y - ROW_H || cn.y > t.y + t.h + ROW_H) continue;
      // easeCurrent: the plain node, the accent faded in over it, the ring with it
      const kk = curAmt[i] ? curAmt[i].value : (i === current ? 1 : 0);
      g.beginPath(); g.arc(cn.x, cn.y, NODE_R, 0, Math.PI * 2);
      g.fillStyle = rgba(fa(C.secondary)); g.fill();
      g.strokeStyle = rgba(fa(C.border)); g.lineWidth = 1.5; g.stroke();
      if (kk > 0.001) {
        const a1 = fa(fade(C.primary, kk));
        g.beginPath(); g.arc(cn.x, cn.y, NODE_R, 0, Math.PI * 2); g.fillStyle = rgba(a1); g.fill();
        g.beginPath(); g.arc(cn.x, cn.y, NODE_R + 3, 0, Math.PI * 2); g.strokeStyle = rgba(a1); g.lineWidth = 1.5; g.stroke();
      }
      text(g, nodes[i].label, t.x + PAD + maxLane * LANE_W + 18 - px.value, cn.y + 4, 12, FONT.sans, fa(lerpColor(C.muted, C.fg, kk)));
    }
    g.restore();

    const contentH = PAD * 2 + maxRow * ROW_H, contentW = PAD * 2 + maxLane * LANE_W + LABEL_SPACE;
    const maxY = Math.max(0, contentH - t.h), maxX = Math.max(0, contentW - t.w);
    if (nodes.length && maxY > 0.5) {
      const th = Math.max(28, (t.h * t.h) / contentH), ty = t.y + (py.value / maxY) * (t.h - th);
      rr(t.x + t.w - 5, t.y, 4, t.h, 2); g.fillStyle = rgba(whiteAlpha(0.06 * a)); g.fill();
      rr(t.x + t.w - 5, ty, 4, th, 2); g.fillStyle = rgba(whiteAlpha(0.22 * a)); g.fill();
    }
    if (nodes.length && maxX > 0.5) {
      const tw = Math.max(28, (t.w * t.w) / contentW), tx = t.x + (px.value / maxX) * (t.w - tw);
      rr(t.x, t.y + t.h - 5, t.w, 4, 2); g.fillStyle = rgba(whiteAlpha(0.06 * a)); g.fill();
      rr(tx, t.y + t.h - 5, tw, 4, 2); g.fillStyle = rgba(whiteAlpha(0.22 * a)); g.fill();
    }
  }
  function rr(x, y, w, hh, r) {
    r = Math.max(0, Math.min(r, w / 2, hh / 2));
    g.beginPath();
    g.roundRect(x, y, Math.max(0, w), Math.max(0, hh), r);
  }

  let running = false, dead = false;
  function tick(now) {
    if (dead) { running = false; return false; }
    let moving = false;
    try { moving = advance(now); paint(); } catch (e) { console.error(e); }
    running = moving;
    return moving;
  }
  function kick() { if (!running && !dead) { running = true; frames(tick); } }

  // ---- open, follow the model, close
  const m0 = vm.model.peek();
  snapshot(m0);
  appear.set(0);
  appear.animateTo(1, 150, Ease.EaseOutCubic, performance.now());
  own(host, effect(() => {
    const m = vm.model.value;
    colorsVersion.value;
    ctx.box.value;
    tr("History");                     // the language drawn
    untracked(() => { if (open && m) sync(m); else if (open) clampPan(); kick(); });
  }));
  own(host, effect(() => {
    const on = ctx.open ? ctx.open.value : true;
    untracked(() => {
      if (!on && open) {
        open = false; host.classList.remove("open");
        hoverNode = -1; closeHover = false;
        appear.animateTo(0, 110, Ease.EaseOutCubic, performance.now());
        kick();
      }
    });
  }));
  host.classList.add("open");

  // ---- input (modal while open)
  const localOf = (e) => { const p = ctx.screenPoint(e), b = ctx.box.peek(); return { x: p.x - b.x, y: p.y - b.y }; };
  let down = null, dragging = false, last = null;
  cv.addEventListener("pointerdown", (e) => {
    if (!open || e.button !== 0) return;
    down = localOf(e); last = down; dragging = false;
    try { cv.setPointerCapture(e.pointerId); } catch { /* ok */ }
  });
  cv.addEventListener("pointermove", (e) => {
    if (!open) return;
    const p = localOf(e);
    if (down) {
      if (!dragging && Math.hypot(p.x - down.x, p.y - down.y) > 4) dragging = true;
      if (dragging) {
        panMs = DRAG_MS;
        panX -= p.x - last.x; panY -= p.y - last.y; last = p;
        clampPan(); kick();
        return;
      }
    }
    closeHover = inside(closeRect(), p);
    const n = closeHover ? -1 : nodeAt(p);
    if (n !== hoverNode) { hoverNode = n; if (n >= 0 && !hv.has(n)) hv.set(n, new Anim(0)); }
    kick();
  });
  cv.addEventListener("pointerup", (e) => {
    if (!open || !down) return;
    const p = localOf(e), wasDrag = dragging;
    down = null; dragging = false;
    if (wasDrag) return;
    if (inside(closeRect(), p) || !inside(cardRect(), p)) { vm.closeHistory(); return; }
    const i = nodeAt(p);
    if (i >= 0 && i < ids.length) vm.cmd("history jump " + ids[i]);
  });
  cv.addEventListener("pointerleave", () => { closeHover = false; hoverNode = -1; kick(); });
  cv.addEventListener("wheel", (e) => {
    if (!open) return;
    e.preventDefault();
    const notch = e.deltaMode === 1 ? e.deltaY / 3 : e.deltaMode === 2 ? e.deltaY * 3 : e.deltaY / 100;
    panMs = SCROLL_MS;
    panY += notch * ROW_H;           // App: scrollBy(-delta.y / 40) — a notch is a row
    clampPan(); kick();
  }, { passive: false });
  cv.addEventListener("contextmenu", (e) => e.preventDefault());

  own(host, () => { dead = true; });
  kick();
}
