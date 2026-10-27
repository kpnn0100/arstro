/*
 * Solaris — home/home.js: HomeScreen (widgets/HomeScreen.{h,cpp}, R-HOME-1, R-UI-1) — the launcher, on one canvas.
 *
 * A 300 px sidebar on `leftRailBg`: the `solaris.` wordmark fitted to 236 px (46 … 24 px, tracked −0.03 × size, the dot
 * in the accent at the MEASURED end), the two-line tagline, a rule at 159, the actions from 183 (New song primary,
 * Open song… and From template… outlined, 34 high, 6 apart), Settings and the version at the foot (a 92 px block).
 * A 60 px header band ("Recent Songs" + the count) and a grid of cards (min 220 px, 16 apart, 9:16 plate + 46 meta).
 *
 * Motion (as the native's): the column count snaps from the width and every card EASES to its slot (motion.reflow,
 * EaseOutCubic; the first placement sets); a card that arrives fades in (crossFade); the grid scrolls eased
 * (EasedScroll); hovers fade (HoverFade, motion.hover).
 *
 * Intent: New song / the empty state's button → A.newSong(); Open song… → A.openSong(); From template… →
 * A.pickTemplate(x, y); Settings → A.openSettings(); a card → A.openRecent(path); a card's right-click →
 * A.forgetRecent(path) (project/menus.js: the same lines App sends).
 */
import { effect } from "../core/signal.js";
import { own } from "../core/dom.js";
import { Anim, Ease, EasedScroll, frames, motion, reduced } from "../kit/anim.js";
import { canvasIn, color, colorsVersion, fillRR, strokeRR, line, text, withAlpha, primaryAlpha, whiteAlpha, hoverWash, lerpColor, brighten } from "../kit/draw.js";
import { FONT, measure, ellipsize, fitSize, baseline } from "../kit/text.js";
import { glyph } from "../kit/glyphs.js";
import { g as printfG } from "../kit/format.js";
import { useCss } from "../kit/css.js";
import { a11yNode, A11Y_ORDER } from "../kit/a11y.js";
import { appActions } from "../project/menus.js";

// HomeScreen.h
const kSidebarW = 300, kPad = 32, kHeaderH = 60, kGap = 16, kMinCard = 220, kMetaH = 46, kActionH = 34;
// HomeScreen.cpp's anonymous namespace
const kWordBaseline = 96, kLogoDividerY = 159, kActionsTop = 183, kActionGap = 6, kBottomBlockH = 92;
const kWordMaxPx = 46, kWordMinPx = 24, kWord = "solaris";
const RADIUS = 2; // radius::control
const HOVER_LIFT = 0.14; // interaction::kHoverFillLift

const fade = (c, a) => withAlpha(c, c[3] * a);
const hex111 = [0x11 / 255, 0x11 / 255, 0x11 / 255, 1];

function lengthLabel(beats, bpm) {
  const s = bpm > 0 ? (beats * 60) / bpm : 0;
  return `${Math.trunc(s / 60)}:${String(Math.trunc(s % 60)).padStart(2, "0")}`;
}
const enc = new TextEncoder();
/** FNV-1a over the name's bytes (HomeScreen.cpp hashOf), unsigned 32-bit. */
function hashOf(s) {
  let h = 2166136261;
  for (const c of enc.encode(s)) h = Math.imul(h ^ c, 16777619) >>> 0;
  return h >>> 0;
}

/** cosmo_v2::HoverFade — one hovered id; amounts ease in and out over motion.hover. */
class HoverFade {
  constructor() { this.cur = -1; this.map = new Map(); }
  setHovered(id, now) {
    if (id === this.cur) return;
    const M = motion();
    if (this.cur !== -1 && this.map.has(this.cur)) this.map.get(this.cur).animateTo(0, M.hover, Ease.EaseOutCubic, now);
    this.cur = id;
    if (id !== -1) {
      if (!this.map.has(id)) this.map.set(id, new Anim(0));
      this.map.get(id).animateTo(1, M.hover, Ease.EaseOutCubic, now);
    }
  }
  clear(now) { this.setHovered(-1, now); }
  advance(now) { let m = false; for (const a of this.map.values()) m = a.update(now) || m; return m; }
  amount(id) { const a = this.map.get(id); return a ? a.value : 0; }
}

// lucide's folder (cosmo_v2::icon::folder), its 24-unit path
const FOLDER = typeof Path2D === "function"
  ? new Path2D("M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z")
  : null;
function folderIcon(g, b, c, sw) {
  if (!FOLDER) return;
  g.save();
  g.translate(b.x, b.y);
  g.scale(b.w / 24, b.h / 24);
  g.strokeStyle = `rgba(${Math.round(c[0] * 255)}, ${Math.round(c[1] * 255)}, ${Math.round(c[2] * 255)}, ${c[3]})`;
  g.lineWidth = (sw * 24) / b.w;
  g.lineJoin = "round"; g.lineCap = "round";
  g.stroke(FOLDER);
  g.restore();
}
function circle(g, cx, cy, r, c, sw) {
  g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2);
  g.strokeStyle = `rgba(${Math.round(c[0] * 255)}, ${Math.round(c[1] * 255)}, ${Math.round(c[2] * 255)}, ${c[3]})`;
  g.lineWidth = sw; g.stroke();
}
const contains = (r, p) => p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h;

export function mount(host, vm, ctx) {
  useCss("home/home.css");
  host.classList.add("sl-home");
  const cv = canvasIn(host, ctx);
  const A = appActions(vm, ctx);
  const tr = vm.tr;

  let cards = [];          // {info, target, lastTarget, ax, ay, aw, ah, alpha, placed, fadeIn}
  let cols = 1, hasModel = false, nowMs = performance.now(), wordPx = 46, hovered = false;
  const scroll = new EasedScroll();
  const hover = new HoverFade();
  const W = () => ctx.box.peek().w, H = () => ctx.box.peek().h;

  // ------------------------------------------------------------------ geometry (HomeScreen.cpp)
  const gridTop = () => kHeaderH + 24;
  const gridLeft = () => kSidebarW + kPad;
  function grid() {
    const availW = Math.max(kMinCard, W() - kPad - gridLeft());
    const n = Math.max(1, Math.floor((availW + kGap) / (kMinCard + kGap)));
    const cardW = (availW - (n - 1) * kGap) / n;
    return { cols: n, cardW, cardH: (cardW * 9) / 16 + kMetaH };
  }
  const toScreen = (c) => ({ x: gridLeft() + c.x, y: gridTop() + c.y - scroll.value(), w: c.w, h: c.h });
  const live = (c) => ({ x: c.ax.value, y: c.ay.value, w: c.aw.value, h: c.ah.value });
  const gridViewport = () => ({ x: gridLeft(), y: gridTop(), w: Math.max(0, W() - kPad - gridLeft()), h: Math.max(0, H() - gridTop()) });
  const actionRect = (i) => ({ x: kPad, y: kActionsTop + i * (kActionH + kActionGap), w: kSidebarW - 2 * kPad, h: kActionH });
  const settingsRect = () => ({ x: kPad, y: H() - kBottomBlockH + 16, w: kSidebarW - 2 * kPad, h: 18 });
  const emptyButtonRect = () => { const vp = gridViewport(); return { x: vp.x + (vp.w - 140) * 0.5, y: vp.y + 96, w: 140, h: 30 }; };
  const cardLive = (i) => toScreen(live(cards[i]));

  // ------------------------------------------------------------------ bind (HomeScreen::bind)
  let recentsKey = null;
  function bind(m) {
    if (!m) return false; // no model yet: nothing bound (the empty state is for a model with no recents)
    const recents = m.recents || [];
    const key = JSON.stringify(recents);
    if (key === recentsKey && hasModel) return false;
    recentsKey = key;
    const next = [];
    for (const r of recents) {
      let c = cards.find((o) => o.info.path === r.path);
      if (!c) {
        c = { info: r, target: { x: 0, y: 0, w: 0, h: 0 }, lastTarget: { x: -1, y: -1, w: -1, h: -1 },
              ax: new Anim(0), ay: new Anim(0), aw: new Anim(0), ah: new Anim(0), alpha: new Anim(1), placed: false, fadeIn: false };
        if (hasModel) { c.alpha.set(0); c.fadeIn = true; } // a card that arrives fades in at its slot
      }
      c.info = r;
      next.push(c);
    }
    cards = next;
    hasModel = true;
    return true;
  }

  // ------------------------------------------------------------------ HomeScreen::layout
  function layout() {
    const gr = grid();
    cols = gr.cols;
    let slot = 0;
    for (const c of cards) {
      const r = Math.floor(slot / gr.cols), k = slot % gr.cols;
      ++slot;
      c.target = { x: k * (gr.cardW + kGap), y: r * (gr.cardH + kGap), w: gr.cardW, h: gr.cardH };
      const t = c.target, l = c.lastTarget;
      if (t.x === l.x && t.y === l.y && t.w === l.w && t.h === l.h) continue;
      if (!c.placed || reduced()) {
        c.ax.set(t.x); c.ay.set(t.y); c.aw.set(t.w); c.ah.set(t.h); // the first placement SETS
        c.placed = true;
      } else {
        const ms = motion().reflow;
        c.ax.animateTo(t.x, ms, Ease.EaseOutCubic, nowMs); c.ay.animateTo(t.y, ms, Ease.EaseOutCubic, nowMs);
        c.aw.animateTo(t.w, ms, Ease.EaseOutCubic, nowMs); c.ah.animateTo(t.h, ms, Ease.EaseOutCubic, nowMs);
      }
      c.lastTarget = { ...t };
    }
    const rows = Math.ceil(slot / gr.cols);
    const contentH = rows * gr.cardH + Math.max(0, rows - 1) * kGap + kPad;
    scroll.setExtent(gridTop(), Math.max(0, H() - gridTop()), contentH);
  }

  // ------------------------------------------------------------------ HomeScreen::advance
  function advance(now) {
    nowMs = now;
    let moving = false;
    for (const c of cards) {
      if (c.fadeIn) { c.alpha.animateTo(1, motion().crossFade, Ease.EaseOutCubic, now); c.fadeIn = false; }
      for (const a of [c.ax, c.ay, c.aw, c.ah, c.alpha]) moving = a.update(now) || moving;
    }
    moving = scroll.advance(now) || moving;
    if (!hovered) hover.clear(now);
    moving = hover.advance(now) || moving;
    paint();
    return moving;
  }
  const kick = () => frames(advance);

  // ------------------------------------------------------------------ hit testing (HomeScreen::hoverAt)
  function hoverAt(p) {
    for (let i = 0; i < 2; ++i) if (contains(actionRect(i), p)) return i;
    if (contains(actionRect(2), p)) return 3; // R-PRESET-2: From template… (2 is Settings')
    if (contains(settingsRect(), p)) return 2;
    if (!contains(gridViewport(), p)) return -1;
    if (!cards.length) return contains(emptyButtonRect(), p) ? 9 : -1;
    for (let i = 0; i < cards.length; ++i) if (contains(cardLive(i), p)) return 10 + i;
    return -1;
  }
  const local = (e) => { const r = cv.el.getBoundingClientRect(), k = r.width / Math.max(1, W()); return { x: (e.clientX - r.left) / k, y: (e.clientY - r.top) / k }; };
  const rootPoint = (e) => (ctx.screenPoint ? ctx.screenPoint(e) : local(e));

  const onMove = (e) => {
    hovered = true;
    const h = hoverAt(local(e));
    hover.setHovered(h, performance.now());
    cv.el.style.cursor = h >= 0 ? "pointer" : "";
    kick();
  };
  const onLeave = () => { hovered = false; cv.el.style.cursor = ""; kick(); };
  const onClick = (e) => {
    if (e.button !== 0) return;
    const h = hoverAt(local(e));
    if (h === 0 || h === 9) A.newSong();
    else if (h === 1) A.openSong();
    else if (h === 2) A.openSettings();
    else if (h === 3) { const p = rootPoint(e); A.pickTemplate(p.x, p.y); } // R-PRESET-2
    else if (h >= 10) A.openRecent(cards[h - 10].info.path);
  };
  const onContext = (e) => {
    e.preventDefault();
    const h = hoverAt(local(e));
    if (h >= 10) A.forgetRecent(cards[h - 10].info.path);
  };
  const onWheel = (e) => {
    const p = local(e);
    if (!contains(gridViewport(), p)) return;
    const dy = e.deltaMode === 1 ? e.deltaY * 16 : e.deltaMode === 2 ? e.deltaY * H() : e.deltaY;
    if (scroll.scrollBy(dy)) { e.preventDefault(); kick(); }
  };
  cv.el.addEventListener("pointermove", onMove);
  cv.el.addEventListener("pointerleave", onLeave);
  cv.el.addEventListener("click", onClick);
  cv.el.addEventListener("contextmenu", onContext);
  cv.el.addEventListener("wheel", onWheel, { passive: false });

  // ------------------------------------------------------------------ paint (HomeScreen::paintCard / onPaint)
  function paintCard(g, c, s, hv) {
    const info = c.info;
    const a = c.alpha.value * (info.missing ? 0.55 : 1);
    const th = (s.w * 9) / 16;
    const bg = brighten(color("folder-chip-bg"), 0.06 * hv);
    const bd = lerpColor(color("border"), primaryAlpha(0.7), hv);
    fillRR(g, s.x, s.y, s.w, s.h, RADIUS, fade(bg, a));
    strokeRR(g, s.x, s.y, s.w, s.h, RADIUS, fade(bd, a), 1 + 0.5 * hv);
    // the plate: a song's bars, from its name — deterministic, muted, never the accent
    const plate = { x: s.x + 1, y: s.y + 1, w: s.w - 2, h: th - 1 };
    fillRR(g, plate.x, plate.y, plate.w, plate.h, 0, fade(hex111, a));
    let h = hashOf(info.name ? info.name : info.path);
    const bars = 28, bw = (plate.w - 32) / bars;
    const barC = fade(whiteAlpha(0.10 + 0.05 * hv), a);
    for (let i = 0; i < bars; ++i) {
      h = (Math.imul(h, 1103515245) + 12345) >>> 0;
      const v = 0.18 + ((0.82 * ((h >>> 16) & 0xff)) / 255) * (0.55 + 0.45 * Math.sin(i * 0.37));
      const bh = Math.max(2, (plate.h - 36) * Math.abs(v));
      fillRR(g, plate.x + 16 + i * bw + bw * 0.2, plate.y + (plate.h - bh) * 0.5, bw * 0.6, bh, RADIUS, barC);
    }
    const mx = s.x + 12;
    // a missing song has no header to read: it is named by its file, which is what the user moved
    let name = info.name || "";
    if (!name) {
      const p = info.path || "";
      const sl = p.lastIndexOf("/");
      name = sl < 0 ? p : p.slice(sl + 1);
      if (name.length > 4 && name.endsWith(".slp")) name = name.slice(0, -4);
      if (!name) name = tr("Untitled");
    }
    text(g, ellipsize(name, s.w - 24, 11, FONT.medium), mx, s.y + th + 18, 11, FONT.medium, fade(color("foreground"), a));
    let meta;
    if (info.missing) meta = tr("missing — moved or deleted");
    else if (info.recovery) meta = tr("unsaved changes kept — open it to recover them"); // R-SAFE-2
    else {
      const n = info.strips | 0;
      meta = `${printfG(info.bpm)} bpm  ·  ${lengthLabel(info.lengthBeats || 0, info.bpm || 0)}  ·  ` +
             vm.fillIn(vm.trn(n, "{1} strip", "{1} strips"), String(n));
    }
    const mc = info.missing ? color("destructive") : info.recovery ? color("primary") : color("muted-foreground");
    text(g, ellipsize(meta, s.w - 24, 10, FONT.sans), mx, s.y + th + 33, 10, FONT.sans, fade(mc, a));
  }

  function paint() {
    const g = cv.begin();
    const Wd = W(), Ht = H();
    const fg = color("foreground"), muted = color("muted-foreground"), border = color("border"), primary = color("primary");
    fillRR(g, 0, 0, Wd, Ht, 0, color("background"));
    fillRR(g, 0, 0, kSidebarW, Ht, 0, color("left-rail-bg"));
    line(g, kSidebarW, 0, kSidebarW, Ht, border, 1);
    line(g, kSidebarW, kHeaderH, Wd, kHeaderH, border, 1);

    // ── wordmark, sized to fit; the dot at the MEASURED end of the word ──
    const px = fitSize(kWord + ".", kSidebarW - 2 * kPad, kWordMaxPx, kWordMinPx, FONT.semibold, -0.03);
    wordPx = px;
    const sp = -0.03 * px;
    text(g, kWord, kPad, kWordBaseline, px, FONT.semibold, fg, sp);
    text(g, ".", kPad + measure(kWord, px, FONT.semibold, sp), kWordBaseline, px, FONT.semibold, primary, sp);
    const tagW = kSidebarW - 2 * kPad;
    text(g, ellipsize(tr("Arrange, mix and play a song"), tagW, 11, FONT.sans), kPad, 118, 11, FONT.sans, muted);
    text(g, ellipsize(tr("on every audio device you have."), tagW, 11, FONT.sans), kPad, 133, 11, FONT.sans, muted);
    line(g, kPad, kLogoDividerY, kSidebarW - kPad, kLogoDividerY, border, 1);

    for (let i = 0; i < 3; ++i) {
      const r = actionRect(i);
      const hv = hover.amount(i === 2 ? 3 : i);
      const prim = i === 0;
      if (prim) fillRR(g, r.x, r.y, r.w, r.h, RADIUS, brighten(primary, HOVER_LIFT * hv));
      else {
        fillRR(g, r.x, r.y, r.w, r.h, RADIUS, primaryAlpha(0.10 * hv));
        strokeRR(g, r.x, r.y, r.w, r.h, RADIUS, lerpColor(border, primary, 0.5 * hv), 1);
      }
      const ink = prim ? color("primary-foreground") : lerpColor(muted, fg, 0.6 + 0.4 * hv);
      const ib = { x: r.x + 14, y: r.y + (r.h - 13) * 0.5, w: 13, h: 13 };
      if (prim) glyph.plus(g, ib, ink, 1.6);
      else if (i === 1) folderIcon(g, ib, ink, 1.2);
      else {
        // R-PRESET-2: a template — two sheets, one over the other
        strokeRR(g, ib.x + 3, ib.y, ib.w - 3, ib.h - 3, 1, ink, 1.1);
        fillRR(g, ib.x, ib.y + 3, ib.w - 3, ib.h - 3, 1, color("background"));
        strokeRR(g, ib.x, ib.y + 3, ib.w - 3, ib.h - 3, 1, ink, 1.1);
      }
      const fam = prim ? FONT.semibold : FONT.sans;
      const label = prim ? tr("New song") : i === 1 ? tr("Open song…") : tr("From template…");
      text(g, ellipsize(label, r.w - 44, 12, fam), r.x + 36, baseline(r.y + r.h * 0.5, 12), 12, fam, ink);
    }

    const blockTop = Ht - kBottomBlockH;
    line(g, kPad, blockTop, kSidebarW - kPad, blockTop, border, 1);
    {
      const r = settingsRect();
      const hv = hover.amount(2);
      if (hv > 0.001) fillRR(g, r.x - 6, r.y - 1, r.w + 12, r.h + 2, RADIUS, hoverWash(hv));
      const lc = lerpColor(muted, fg, 0.7 * hv);
      circle(g, r.x + 5.5, r.y + 9, 2.4, lc, 1.1);
      circle(g, r.x + 5.5, r.y + 9, 4.6, lc, 1.1);
      text(g, ellipsize(tr("Settings"), r.w - 18, 11, FONT.sans), r.x + 18, baseline(r.y + r.h * 0.5, 11), 11, FONT.sans, lc);
    }
    text(g, "solaris v0.1.0", kPad, blockTop + 60, 10, FONT.mono, fade(muted, 0.4));

    // ── header ──
    const hx = gridLeft();
    const title = tr("Recent Songs");
    glyph.speaker(g, { x: hx, y: kHeaderH * 0.5 - 6.5, w: 13, h: 13 }, muted, 1.1);
    text(g, title, hx + 20, baseline(kHeaderH * 0.5, 13), 13, FONT.semibold, fg);
    text(g, String(cards.length), hx + 20 + measure(title, 13, FONT.semibold) + 8, baseline(kHeaderH * 0.5, 10), 10, FONT.sans, fade(muted, 0.6));

    // ── the grid, clipped to its viewport ──
    const vp = gridViewport();
    g.save();
    g.beginPath(); g.rect(vp.x, vp.y, vp.w, vp.h); g.clip();
    if (!cards.length && hasModel) {
      const s = ellipsize(tr("No songs yet — start one."), vp.w - 16, 13, FONT.sans);
      text(g, s, vp.x + (vp.w - measure(s, 13, FONT.sans)) * 0.5, vp.y + 72, 13, FONT.sans, muted);
      const b = emptyButtonRect();
      const hv = hover.amount(9);
      fillRR(g, b.x, b.y, b.w, b.h, RADIUS, primaryAlpha(0.10 + 0.08 * hv));
      strokeRR(g, b.x, b.y, b.w, b.h, RADIUS, lerpColor(border, primary, 0.6 + 0.4 * hv), 1);
      const ns = ellipsize(tr("New song"), b.w - 12, 12, FONT.sans);
      text(g, ns, b.x + (b.w - measure(ns, 12, FONT.sans)) * 0.5, baseline(b.y + b.h * 0.5, 12), 12, FONT.sans, fg);
    }
    for (let i = 0; i < cards.length; ++i) {
      const s = cardLive(i);
      if (s.y + s.h < vp.y || s.y > vp.y + vp.h || cards[i].alpha.value <= 0.001) continue; // culled
      paintCard(g, cards[i], s, hover.amount(10 + i));
    }
    g.restore();
    scroll.drawBar(g, Wd - 6);
  }

  // ------------------------------------------------------------------ the model, the size, the words, the colours
  own(host, effect(() => {
    const m = vm.model.value;
    ctx.box.value; vm.lang && vm.lang.value; colorsVersion.value; // re-lay and re-paint on any of them
    nowMs = performance.now();
    bind(m);
    layout();
    kick();
  }));
  cv.onResize(() => { layout(); kick(); });

  // R-ACCESS-1: New song, Open song…, From template…, Settings, and each recent song in view
  if (vm.a11y) {
    own(host, vm.a11y.register(A11Y_ORDER.home, () => {
      const o = ctx.box.peek();
      const at = (r) => ({ x: r.x + o.x, y: r.y + o.y, w: r.w, h: r.h });
      const out = [];
      const names = ["New song", "Open song…", "From template…"];
      for (let i = 0; i < 3; ++i) out.push(a11yNode("home.action." + i, "button", tr(names[i]), at(actionRect(i))));
      out.push(a11yNode("home.settings", "button", tr("Settings"), at(settingsRect())));
      const vp = gridViewport();
      for (let i = 0; i < cards.length; ++i) {
        const r = cardLive(i);
        if (r.w <= 0 || r.y + r.h < vp.y || r.y > vp.y + vp.h) continue;
        const c = cards[i].info;
        out.push(a11yNode("home.recent." + c.path, "listitem", c.name || c.path, at(r),
          c.missing ? tr("missing") : c.recovery ? tr("unsaved edits to recover") : ""));
      }
      return out;
    }, { screen: "home" }));
  }

  own(host, () => {
    cv.el.removeEventListener("pointermove", onMove);
    cv.el.removeEventListener("pointerleave", onLeave);
    cv.el.removeEventListener("click", onClick);
    cv.el.removeEventListener("contextmenu", onContext);
    cv.el.removeEventListener("wheel", onWheel);
  });
  // a test's handles (HomeScreen.h): columns(), cardCount(), wordmarkSize(), cardLive(i)
  host.__home = { columns: () => cols, cardCount: () => cards.length, wordmarkSize: () => wordPx, cardLive, actionRect, settingsRect };
}
