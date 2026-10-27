/*
 * Interstellar — deliver/checks.js: the Deliver tab's left column, what would stop a delivery. Ports
 * app/widgets/ChecksPanel.{h,cpp}.
 *
 * Read straight off the model, nothing inferred: a version with DANGLING deltas (R-VER-4), an OFFLINE
 * source (R-RACK-7) and how many clips use it, a source still decoding, a render that failed and why;
 * then, as information, which versions are pinned or frozen. Each 39 px line carries a severity glyph
 * (warn destructive / spinner / checkCircle success / lock muted), a sentence (Roboto 11) and its
 * detail (Roboto 9.5, muted), both ellipsized. Header "CHECKS  n ───" (n = problems, destructive, only
 * when > 0). With nothing wrong: "No problems found". With no project: "Open a project to check it."
 * Lines TRAVEL (AnimatedRows); the column scrolls eased. It sends no command lines (the native has none).
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { EasedScroll, wheelPx } from "../kit/easedscroll.js";
import { AnimatedRows } from "../kit/animatedrows.js";
import { Painter, canvasSvg, col, css, fade, whiteA, frameLoop, F } from "../kit/paint.js";

const ROW_H = 39, HEADER_H = 29.25, PAD_X = 9.75;                   // ChecksPanel.h:30-31, .cpp:14
const plural = (n, one, many) => n + " " + (n === 1 ? one : many);

/** ChecksPanel::bind - the lines, in the native's order, keyed for AnimatedRows. */
function checksOf(m) {
  const items = [];
  let problems = 0;
  const add = (key, severity, text, detail) => items.push([key, { severity, text, detail }]);
  const tls = m.timelines || [];
  for (const tl of tls)
    if (tl.danglingDeltas > 0) {
      ++problems;
      add("dangling:" + tl.id, 2, (tl.name || tl.id) + ": " + plural(tl.danglingDeltas, "dangling delta", "dangling deltas"),
          "The base deleted what they point at. Rebase to reconcile.");
    }
  for (const n of m.rack || []) {
    if (n.failed) {
      ++problems;
      add("offline:" + n.rackObj, 2, (n.cosmoName || n.bindName) + " is offline",
          "Media missing" + (n.usedBy > 0 ? " — " + plural(n.usedBy, "clip uses", "clips use") + " it" : ""));
    } else if (n.pending) add("pending:" + n.rackObj, 1, (n.cosmoName || n.bindName) + " is still decoding", "Renders wait for it.");
  }
  for (const j of m.renders || [])
    if (j.state === "failed") {
      ++problems;
      add("render:" + j.id, 2, "Render of " + (j.timelineName || j.timeline) + " failed", j.error || "No reason was given.");
    }
  if (problems === 0 && tls.length) add("clear", 3, "No problems found", "Every version can render.");
  for (const tl of tls) {
    if (tl.colourPinned) {
      const pc = String(tl.pinCommit || "");
      add("pin:" + tl.id, 0, (tl.name || tl.id) + " is pinned", "Colour stopped at @" + (pc.length > 7 ? pc.slice(0, 7) : pc) + "; base regrades do not reach it.");
    }
    if (tl.cutFrozen) add("frozen:" + tl.id, 0, (tl.name || tl.id) + " is frozen", "Its arrangement no longer follows the base.");
  }
  return { items, problems };
}

export function mount(host, vm, ctx) {
  useCss("css/deliver/checks.css");
  host.classList.add("dk");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  let W = 0, H = 0, count = 0, problems = 0;
  const rows = new AnimatedRows(() => loop.kick());
  const scroll = new EasedScroll(() => loop.kick());

  own(host, effect(() => {                           // ChecksPanel::bind
    const m = vm.model.value;
    if (!m) return;
    const r = checksOf(m);
    problems = r.problems;
    count = r.items.length;
    rows.sync(r.items, ROW_H);
    loop.kick();
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));

  function advance() {                               // ChecksPanel::advance
    scroll.setExtent(HEADER_H, Math.max(0, H - HEADER_H), count * ROW_H);
    return rows.moving() || scroll.moving;
  }

  function paint() {                                 // ChecksPanel::onPaint
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), destr = col("destructive"), ok = col("success");
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("left-rail-bg") });
    p.line(W - 0.5, 0, W - 0.5, H, border, 1);
    const hy = HEADER_H * 0.5, tr = 0.13 * 9;
    p.text("CHECKS", PAD_X, baseline(hy, 9), 9, F.semibold, muted, tr);
    const lw = measure("CHECKS", 9, F.semibold, tr);
    if (problems > 0) p.text(String(problems), PAD_X + lw + 6, baseline(hy, 9), 9, F.mono, destr);
    if (W - PAD_X > PAD_X + lw + 22) p.line(PAD_X + lw + 22, hy, W - PAD_X, hy, border, 1);
    const vh = Math.max(0, H - HEADER_H);
    p.clip(0, HEADER_H, W, vh);
    if (!count && !rows.rows().length) {
      const s = "Open a project to check it.";
      p.text(s, (W - measure(s, 11, F.sans)) * 0.5, HEADER_H + vh * 0.4, 11, F.sans, muted);
    }
    for (const row of rows.rows()) {
      const a = row.alpha;
      if (a <= 0.001 || !scroll.bandVisible(row.y, ROW_H)) continue;
      const top = HEADER_H + row.y - scroll.value, c = row.data;
      const gb = { x: PAD_X, y: top + 8, w: 11, h: 11 };
      const gc = c.severity === 2 ? destr : c.severity === 3 ? ok : muted;
      if (c.severity === 2) p.add(glyph.warn(gb, css(fade(gc, a))));
      else if (c.severity === 3) p.icon("checkCircle", gb.x, gb.y, 11, fade(gc, a));
      else if (c.severity === 1) p.add(glyph.spinner(gb.x + 5.5, gb.y + 5.5, 4, 0, a));
      else p.add(glyph.lock({ x: gb.x + 1.5, y: gb.y, w: 8, h: 11 }, css(fade(gc, a))));
      const x = PAD_X + 18;
      p.text(ellipsize(c.text, W - PAD_X - x, 11, F.sans), x, top + 16.5, 11, F.sans, fade(fg, a));
      p.text(ellipsize(c.detail, W - PAD_X - x, 9.5, F.sans), x, top + 30, 9.5, F.sans, fade(muted, a));
    }
    p.restore();
    const bar = scroll.bar(W - 2);
    if (bar) p.rect(bar.x, bar.y, bar.w, bar.h, tok.px("radius-pill"), { fill: whiteA(0.14) });
    p.end();
  }

  function step() {
    if (!(W > 0) || !(H > 0)) return false;
    const more = advance();
    paint();
    return more;
  }

  host.addEventListener("wheel", (e) => { if (scroll.scrollBy(wheelPx(e))) { e.preventDefault(); loop.kick(); } }, { passive: false });
  loop.kick();
}
