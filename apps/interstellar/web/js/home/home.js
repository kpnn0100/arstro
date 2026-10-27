/*
 * Interstellar — home/home.js: Screen::Home, the launcher. Ports app/widgets/HomeScreen.{h,cpp}.
 *
 * A 300 px sidebar on leftRailBg (the wordmark sized to fit with the accent dot at the MEASURED end
 * of the word, the tagline, New project / Open project…, Settings and the version at the foot); a
 * 60 px header with "Recent Projects" and the count; a grid of cards at a 220 px minimum width, each
 * a 16:9 cover (the first frame of the project's first source, `thumb` of the recent's coverPath)
 * over the name, "N sources · size" and when it was last opened (HomeScreen.cpp:1-413).
 *
 * Motion as the native: every card's rect EASES to its slot when the column count changes (260 ms,
 * EaseOutCubic; the first placement snaps); a card that joins after the first bind fades in (200 ms);
 * a cover that lands late fades in (180 ms); hover cross-fades per region (HoverFade, 120 ms); the
 * grid scrolls eased (EasedScroll, 180 ms). Loading (no model yet, revision 0) draws breathing
 * skeleton cards at the REAL card geometry, cross-faded with the cards (180 ms); an empty list says
 * so with a "New project" chip.
 *
 * Intents: New project / the empty chip / the dashed new card → the board's file picker, then
 * `project new <path>` (App::onPickProjectToCreate); Open project… → the picker, `project open`;
 * a card → `project open <its path>` (App::onOpenRecent); Settings → Engine Settings (vm.openSettings).
 * "Now" for the relative dates is the page's clock, or the fixture's `homeClock` (the native shots'
 * App::setHomeClock).
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { width as measure, ellipsize, fitSize, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { EasedScroll, wheelPx } from "../kit/easedscroll.js";
import { Painter, canvasSvg, col, css, fade, lerp, brighten, primaryA, whiteA, hoverWash, Anim, HoverFade,
         frameLoop, localPoint, inRect, Ease, F } from "../kit/paint.js";
import { pickProjectToCreate, pickProjectToOpen } from "../edit/menus.js";

// HomeScreen.h:43-50
const SIDEBAR_W = 300, PAD = 32, HEADER_H = 60, GAP = 16, MIN_CARD = 220, META_H = 46, ACTION_H = 34, SKELETON_COUNT = 6;
// HomeScreen.cpp:19-28 - the sidebar's anchored blocks (cosmo's numbers)
const WORD_BASELINE = 96, LOGO_DIVIDER_Y = 159, ACTIONS_TOP = 183, ACTION_GAP = 6, BOTTOM_BLOCK_H = 92;
const WORD_MAX_PX = 46, WORD_MIN_PX = 24, COVER_EDGE = 480, WORD = "interstellar";
const HOVER_FILL_LIFT = 0.14;                       // artboard interaction::kHoverFillLift (Interaction.h:18)

function sizeLabel(b) {                             // HomeScreen.cpp:30-37
  if (b >= 2 ** 30) return (b / 2 ** 30).toFixed(1) + " GB";
  if (b >= 2 ** 20) return (b / 2 ** 20).toFixed(0) + " MB";
  return (b / 1024).toFixed(0) + " KB";
}
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
function relativeDate(when, now) {                  // HomeScreen.cpp:229-249 (gmtime, "%b %e")
  if (!(when > 0)) return "never";
  const d = now - when;
  if (d < 60) return "just now";
  if (d < 3600) return Math.floor(d / 60) + "m ago";
  if (d < 86400) return Math.floor(d / 3600) + "h ago";
  if (d < 7 * 86400) return Math.floor(d / 86400) + "d ago";
  const t = new Date(when * 1000);
  return MONTHS[t.getUTCMonth()] + " " + String(t.getUTCDate()).padStart(2, " ");
}

function mkCard() {
  return { info: null, target: { x: 0, y: 0, w: 0, h: 0 }, last: null, ax: new Anim(0), ay: new Anim(0), aw: new Anim(0), ah: new Anim(0),
           placed: false, alpha: new Anim(1), fadeIn: false, cover: null, coverKey: null, coverMissed: false, coverAlpha: new Anim(1), coverFade: false };
}
const live = (c) => ({ x: c.ax.value, y: c.ay.value, w: c.aw.value, h: c.ah.value });

export function mount(host, vm, ctx) {
  useCss("css/home/home.css");
  host.classList.add("hm");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);

  let W = 0, H = 0, nowMs = 0, cols = 1;
  let cards = [];
  const newCard = mkCard();
  let loading = false, loadingApplied = false, init = false, hasModel = false;
  const skeleton = new Anim(0);
  const hover = new HoverFade();
  const scroll = new EasedScroll(() => loop.kick());
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  const nowUnix = () => {
    const fx = vm.view.fixture && vm.view.fixture.peek();
    return fx && fx.homeClock > 0 ? fx.homeClock : Math.floor(Date.now() / 1000);
  };

  // ---------------------------------------------------------------- bind (HomeScreen::bind)
  function bind() {
    const rec = vm.recents.value;                   // newest first - App's stable sort on lastOpened
    vm.session.thumbsVersion.value;                 // a cover landing re-binds
    const next = [];
    for (const r of rec) {
      let c = cards.find((o) => o.info && o.info.path === r.path);
      if (!c) {
        c = mkCard();
        if (hasModel && cards.length) { c.alpha.set(0); c.fadeIn = true; }
      }
      c.info = r;
      if (c.coverKey !== r.coverPath || (!c.cover && c.coverMissed)) {
        if (c.coverKey !== r.coverPath) { c.cover = null; c.coverMissed = false; }
        c.coverKey = r.coverPath;
        const got = r.coverPath ? vm.session.thumbNow(r.coverPath, 0, COVER_EDGE) : null;
        if (got) {
          if (c.coverMissed) { c.coverAlpha.set(0); c.coverFade = true; }    // late: fade in (R1)
          c.cover = got;
          c.coverMissed = false;
        } else if (r.coverPath) c.coverMissed = true;
      }
      next.push(c);
    }
    cards = next;
    hasModel = vm.ready.peek();
    loading = !vm.ready.value || vm.revision.value === 0;   // App.cpp:1298 setLoading(revision == 0)
    loop.kick();
  }
  own(host, effect(bind));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));

  // ---------------------------------------------------------------- geometry
  const gridTop = () => HEADER_H + 24;
  const gridLeft = () => SIDEBAR_W + PAD;
  function grid() {                                 // HomeScreen.cpp:89-97
    const availW = Math.max(MIN_CARD, W - PAD - gridLeft());
    const c = Math.max(1, Math.floor((availW + GAP) / (MIN_CARD + GAP)));
    const cardW = (availW - (c - 1) * GAP) / c;
    return { cols: c, cardW, cardH: (cardW * 9) / 16 + META_H };
  }
  const toScreen = (r) => ({ x: gridLeft() + r.x, y: gridTop() + r.y - scroll.value, w: r.w, h: r.h });
  const viewport = () => ({ x: gridLeft(), y: gridTop(), w: Math.max(0, W - PAD - gridLeft()), h: Math.max(0, H - gridTop()) });
  const actionRect = (i) => ({ x: PAD, y: ACTIONS_TOP + i * (ACTION_H + ACTION_GAP), w: SIDEBAR_W - 2 * PAD, h: ACTION_H });
  const settingsRect = () => ({ x: PAD, y: H - BOTTOM_BLOCK_H + 16, w: SIDEBAR_W - 2 * PAD, h: 18 });
  function emptyChipRect() {
    const vp = viewport();
    return { x: vp.x + (vp.w - 140) * 0.5, y: vp.y + 96, w: 140, h: 30 };
  }

  function layout() {                               // HomeScreen.cpp:120-152
    const g = grid();
    cols = g.cols;
    let slot = 0;
    const place = (c) => {
      const r = Math.floor(slot / g.cols), k = slot % g.cols;
      c.target = { x: k * (g.cardW + GAP), y: r * (g.cardH + GAP), w: g.cardW, h: g.cardH };
      ++slot;
      const t = c.target, l = c.last;
      if (l && t.x === l.x && t.y === l.y && t.w === l.w && t.h === l.h) return;
      if (!c.placed) {
        c.ax.set(t.x); c.ay.set(t.y); c.aw.set(t.w); c.ah.set(t.h);
        c.placed = true;
      } else {
        const ms = tok.ms("motion-reflow");
        c.ax.animateTo(t.x, ms, Ease.EaseOutCubic, nowMs); c.ay.animateTo(t.y, ms, Ease.EaseOutCubic, nowMs);
        c.aw.animateTo(t.w, ms, Ease.EaseOutCubic, nowMs); c.ah.animateTo(t.h, ms, Ease.EaseOutCubic, nowMs);
      }
      c.last = { ...t };
    };
    for (const c of cards) place(c);
    place(newCard);
    const rows = Math.ceil(slot / g.cols);
    scroll.setExtent(gridTop(), Math.max(0, H - gridTop()), rows * g.cardH + Math.max(0, rows - 1) * GAP + PAD);
  }

  function advance(now) {                           // HomeScreen.cpp:154-181
    nowMs = now;
    let moving = false;
    if (!init) { skeleton.set(loading ? 1 : 0); loadingApplied = loading; init = true; }
    if (loading !== loadingApplied) {
      skeleton.animateTo(loading ? 1 : 0, tok.ms("motion-scroll"), Ease.EaseOutCubic, now);
      loadingApplied = loading;
    }
    moving = skeleton.update(now) || moving;
    for (const c of cards) {
      if (c.coverFade) { c.coverAlpha.animateTo(1, tok.ms("motion-scroll"), Ease.EaseOutCubic, now); c.coverFade = false; }
      moving = c.coverAlpha.update(now) || moving;
      if (c.fadeIn) { c.alpha.animateTo(1, tok.ms("motion-cross-fade"), Ease.EaseOutCubic, now); c.fadeIn = false; }
      for (const a of [c.ax, c.ay, c.aw, c.ah, c.alpha]) moving = a.update(now) || moving;
    }
    for (const a of [newCard.ax, newCard.ay, newCard.aw, newCard.ah]) moving = a.update(now) || moving;
    moving = hover.advance(now) || moving;
    return moving || skeleton.value > 0.001 || scroll.moving;   // the skeleton breathes while shown
  }

  function hoverAt(pt) {                            // HomeScreen.cpp:183-193
    for (let i = 0; i < 2; ++i) if (inRect(pt, actionRect(i))) return i;
    if (inRect(pt, settingsRect())) return 2;
    if (!inRect(pt, viewport()) || loading) return -1;
    if (!cards.length) return inRect(pt, emptyChipRect()) ? 9 : -1;
    for (let i = 0; i < cards.length; ++i) if (inRect(pt, toScreen(live(cards[i])))) return 10 + i;
    if (inRect(pt, toScreen(live(newCard)))) return 3;
    return -1;
  }

  // ---------------------------------------------------------------- paint (HomeScreen::onPaint)
  function paint() {
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), primary = col("primary");
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("background") });
    p.rect(0, 0, SIDEBAR_W, H, 0, { fill: col("left-rail-bg") });
    p.line(SIDEBAR_W, 0, SIDEBAR_W, H, border, 1);
    p.line(SIDEBAR_W, HEADER_H, W, HEADER_H, border, 1);

    // wordmark: sized to fit, the dot at the MEASURED end of the word
    const px = fitSize(WORD + ".", SIDEBAR_W - 2 * PAD, WORD_MAX_PX, WORD_MIN_PX, F.semibold, -0.03);
    const sp = -0.03 * px;
    p.text(WORD, PAD, WORD_BASELINE, px, F.semibold, fg, sp);
    p.text(".", PAD + measure(WORD, px, F.semibold, sp), WORD_BASELINE, px, F.semibold, primary, sp);
    p.text("Group, grade, cut and deliver", PAD, 118, 11, F.sans, muted);
    p.text("the colour of a production.", PAD, 133, 11, F.sans, muted);
    p.line(PAD, LOGO_DIVIDER_Y, SIDEBAR_W - PAD, LOGO_DIVIDER_Y, border, 1);

    // actions
    for (let i = 0; i < 2; ++i) {
      const r = actionRect(i), hv = hover.amount(i), prim = i === 0;
      if (prim) p.rect(r.x, r.y, r.w, r.h, tok.px("radius-control"), { fill: brighten(primary, HOVER_FILL_LIFT * hv) });
      else p.rect(r.x, r.y, r.w, r.h, tok.px("radius-control"), { fill: primaryA(0.10 * hv), stroke: lerp(border, primary, 0.5 * hv), sw: 1 });
      const c = prim ? col("primary-foreground") : lerp(muted, fg, 0.6 + 0.4 * hv);
      const ib = { x: r.x + 14, y: r.y + (r.h - 13) * 0.5, w: 13, h: 13 };
      if (prim) p.add(glyph.plus(ib, css(c), 1.6));
      else p.icon("folder", ib.x, ib.y, 13, c, 1.2);
      p.text(prim ? "New project" : "Open project…", r.x + 36, baseline(r.y + r.h * 0.5, 12), 12, prim ? F.semibold : F.sans, c);
    }

    // foot: settings + version
    const blockTop = H - BOTTOM_BLOCK_H;
    p.line(PAD, blockTop, SIDEBAR_W - PAD, blockTop, border, 1);
    {
      const r = settingsRect(), hv = hover.amount(2);
      if (hv > 0.001) p.rect(r.x - 6, r.y - 1, r.w + 12, r.h + 2, tok.px("radius-control"), { fill: hoverWash(hv) });
      const lc = lerp(muted, fg, 0.7 * hv);
      p.circle(r.x + 5.5, r.y + (r.h - 11) * 0.5 + 5.5, 2.4, { stroke: lc, sw: 1.1 });
      p.circle(r.x + 5.5, r.y + (r.h - 11) * 0.5 + 5.5, 4.6, { stroke: lc, sw: 1.1 });
      p.text("Settings", r.x + 18, baseline(r.y + r.h * 0.5, 11), 11, F.sans, lc);
    }
    p.text("interstellar v0.1.0", PAD, blockTop + 60, 10, F.mono, fade(muted, 0.4));

    // header
    const hx = gridLeft(), ck = { x: hx, y: HEADER_H * 0.5 - 6.5 };
    p.circle(ck.x + 6.5, ck.y + 6.5, 5.5, { stroke: muted, sw: 1.2 });
    p.line(ck.x + 6.5, ck.y + 6.5, ck.x + 6.5, ck.y + 3.2, muted, 1.2);
    p.line(ck.x + 6.5, ck.y + 6.5, ck.x + 9.2, ck.y + 6.5, muted, 1.2);
    const title = "Recent Projects";
    p.text(title, hx + 20, baseline(HEADER_H * 0.5, 13), 13, F.semibold, fg);
    if (!loading) p.text(String(cards.length), hx + 20 + measure(title, 13, F.semibold) + 8, baseline(HEADER_H * 0.5, 10), 10, F.sans, fade(muted, 0.6));

    // grid, clipped to its viewport
    const vp = viewport();
    p.clip(vp.x, vp.y, vp.w, vp.h);
    const sk = skeleton.value, realAll = 1 - sk, g = grid(), rc = tok.px("radius-control");
    if (sk > 0.001) {
      const breathe = 0.75 + 0.25 * Math.sin((nowMs / tok.ms("motion-pulse")) * 6.28318530718);
      for (let i = 0; i < SKELETON_COUNT; ++i) {
        const r = Math.floor(i / g.cols), k = i % g.cols;
        const c = toScreen({ x: k * (g.cardW + GAP), y: r * (g.cardH + GAP), w: g.cardW, h: g.cardH });
        if (c.y > H) break;
        const th = (c.w * 9) / 16, a = sk * breathe, skc = col("skeleton");
        p.rect(c.x, c.y, c.w, c.h, rc, { fill: fade(col("folder-chip-bg"), sk), stroke: fade(border, sk), sw: 1 });
        p.rect(c.x, c.y, c.w, th, 0, { fill: fade(skc, a) });
        p.rect(c.x + 12, c.y + th + 12, c.w * 0.55, 8, rc, { fill: fade(skc, a) });
        p.rect(c.x + 12, c.y + th + 27, c.w * 0.35, 7, rc, { fill: fade(skc, a * 0.8) });
      }
    }
    if (realAll > 0.001) {
      if (!cards.length && hasModel) {
        const s = "No projects yet — open some footage to start one.";
        const se = ellipsize(s, vp.w - 16, 13, F.sans);
        p.text(se, vp.x + (vp.w - measure(se, 13, F.sans)) * 0.5, vp.y + 72, 13, F.sans, fade(muted, realAll));
        const chip = emptyChipRect(), hv = hover.amount(9);
        p.rect(chip.x, chip.y, chip.w, chip.h, rc, { fill: fade(primaryA(0.10 + 0.08 * hv), realAll), stroke: fade(lerp(border, primary, 0.6 + 0.4 * hv), realAll), sw: 1 });
        const cl = "New project";
        p.text(cl, chip.x + (chip.w - measure(cl, 12, F.sans)) * 0.5, baseline(chip.y + chip.h * 0.5, 12), 12, F.sans, fade(fg, realAll));
      }
      const now = nowUnix();
      for (let i = 0; i < cards.length; ++i) {
        const c = cards[i], s = toScreen(live(c));
        if (s.y + s.h < vp.y || s.y > vp.y + vp.h) continue;          // culled, same test as hover
        if (c.alpha.value <= 0.001) continue;
        const real = realAll * c.alpha.value, hv = hover.amount(10 + i), th = (s.w * 9) / 16;
        const bg = brighten(col("folder-chip-bg"), 0.06 * hv), bd = lerp(border, primaryA(0.7), hv);
        p.rect(s.x, s.y, s.w, s.h, rc, { fill: fade(bg, real), stroke: fade(bd, real), sw: 1 + 0.5 * hv });
        const cv = { x: s.x + 1, y: s.y + 1, w: s.w - 2, h: th - 1 };
        p.rect(cv.x, cv.y, cv.w, cv.h, 0, { fill: fade(col("thumb-placeholder"), real) });
        if (c.cover) p.cover(c.cover.url, c.cover.w, c.cover.h, cv.x, cv.y, cv.w, cv.h, real * c.coverAlpha.value);
        else p.add(glyph.film({ x: cv.x + cv.w * 0.5 - 13, y: cv.y + cv.h * 0.5 - 10, w: 26, h: 20 }, css(fade(muted, 0.5 * real)), 1.2));
        // meta band: name (ellipsized against the card), sources · size, last opened
        const mx = s.x + 12, date = relativeDate(c.info.lastOpened, now), dw = measure(date, 9, F.sans);
        p.text(ellipsize(c.info.name, s.w - 24, 11, F.medium), mx, s.y + th + 18, 11, F.medium, fade(fg, real));
        let meta = c.info.sourceCount + (c.info.sourceCount === 1 ? " source" : " sources");
        if (c.info.sizeBytes > 0) meta += "  ·  " + sizeLabel(c.info.sizeBytes);
        p.text(ellipsize(meta, s.w - 24 - dw - 8, 10, F.sans), mx, s.y + th + 33, 10, F.sans, fade(muted, real));
        p.text(date, s.x + s.w - 12 - dw, s.y + th + 33, 9, F.sans, fade(muted, 0.6 * real));
      }
      if (cards.length) {
        const s = toScreen(live(newCard)), th = (s.w * 9) / 16, hv = hover.amount(3);
        dashedRect({ x: s.x, y: s.y, w: s.w, h: th }, fade(lerp(whiteA(0.22), primary, 0.5 * hv), realAll), 1 + 0.4 * hv);
        const nc = fade(lerp(muted, fg, 0.6 * hv), realAll);
        p.add(glyph.plus({ x: s.x + s.w * 0.5 - 10, y: s.y + th * 0.5 - 16, w: 20, h: 20 }, css(nc), 1.4));
        p.text("New project", s.x + (s.w - measure("New project", 11, F.sans)) * 0.5, s.y + th * 0.5 + 18, 11, F.sans, nc);
      }
    }
    p.restore();
    const bar = scroll.bar(W - 6);
    if (bar) p.rect(bar.x, bar.y, bar.w, bar.h, tok.px("radius-pill"), { fill: whiteA(0.14) });
    p.end();
  }
  function dashedRect(r, c, sw) {                   // HomeScreen.cpp:38-55: 5 on, 5 off, per edge from its start
    const edge = (x0, y0, x1, y1) => {
      const len = Math.hypot(x1 - x0, y1 - y0);
      if (len <= 0) return;
      const ux = (x1 - x0) / len, uy = (y1 - y0) / len;
      for (let d = 0; d < len; d += 10) {
        const e = Math.min(d + 5, len);
        p.line(x0 + ux * d, y0 + uy * d, x0 + ux * e, y0 + uy * e, c, sw);
      }
    };
    edge(r.x, r.y, r.x + r.w, r.y);
    edge(r.x + r.w, r.y, r.x + r.w, r.y + r.h);
    edge(r.x + r.w, r.y + r.h, r.x, r.y + r.h);
    edge(r.x, r.y + r.h, r.x, r.y);
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    layout();
    const more = advance(now);
    paint();
    return more;
  }

  // ---------------------------------------------------------------- input (HomeScreen::handleGesture)
  host.addEventListener("pointermove", (e) => { hover.setHovered(hoverAt(localPoint(host, e))); loop.kick(); });
  host.addEventListener("pointerleave", () => { hover.clear(); loop.kick(); });
  host.addEventListener("wheel", (e) => {
    if (!inRect(localPoint(host, e), viewport())) return;
    if (scroll.scrollBy(wheelPx(e))) { e.preventDefault(); loop.kick(); }
  }, { passive: false });
  host.addEventListener("click", (e) => {
    const h = hoverAt(localPoint(host, e));
    if (h === 0 || h === 3 || h === 9) pickProjectToCreate(vm, ctx);
    else if (h === 1) pickProjectToOpen(vm, ctx);
    else if (h === 2) vm.openSettings();
    else if (h >= 10) vm.openRecent(h - 10);
  });
  host.style.cursor = "default";
  loop.kick();
}
