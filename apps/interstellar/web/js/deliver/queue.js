/*
 * Interstellar — deliver/queue.js: the Deliver tab's deck, every render, each NAMING its timeline
 * (R-RENDER-1). Ports app/widgets/RenderQueue.{h,cpp}.
 *
 * Header "RENDER QUEUE  n ───". A 32.5 px row per job, alternate rows on lane-alt-bg: the state glyph
 * (film muted = queued/cancelled, film primary = running, checkCircle success = done, warn destructive =
 * failed), the version's name (Roboto Medium 11), the format as a mono chip; line two the spec in words
 * (R-RENDER-6) then the output path (mono, ellipsized). Right: a 140 px bar (while running or done) and
 * a 120 px status: "queued" / "cancelled", "42% · 24 fps" (or "42% · done/total" before a rate exists),
 * "done · n frames", "failed — why".
 *
 * Motion as the native's: the bar's fraction CATCHES UP to done/total (motion-catch-up 220 ms
 * EaseOutCubic; the first placement sets); the four states are each an eased 0..1 (motion-cross-fade
 * 200 ms) so running → done cross-fades the glyph, the bar's colour (primary → success) and the status;
 * a pill shorter than its height is never drawn; the frame rate is smoothed 0.7 / 0.3. Rows TRAVEL
 * (AnimatedRows); the list scrolls eased. Empty: "nothing queued" + the hint line.
 *
 * Intents (WEB ADDITION - the native row takes no clicks; the service has the line, R-RENDER-4):
 * a right-click opens the row's menu: "Cancel render" (queued / running) sends `render cancel <job>`;
 * "Reveal output" says the output path on the chip and copies it to the clipboard when the page may.
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
import { Painter, canvasSvg, col, css, fade, lerp, whiteA, Anim, frameLoop, localPoint, inRect, Ease, F } from "../kit/paint.js";

const ROW_H = 32.5, HEADER_H = 29.25, PAD_X = 9.75, BAR_W = 140, STATUS_W = 120;   // RenderQueue.h:32-33, .cpp:16-18
const stateIndex = (s) => (s === "running" ? 1 : s === "done" ? 2 : s === "failed" ? 3 : 0);   // queued, cancelled = 0

export function mount(host, vm, ctx) {
  useCss("css/deliver/queue.css");
  host.classList.add("rq");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  let W = 0, H = 0, jobs = [];
  const progress = new Map();                        // job id -> Progress (RenderQueue.h:56-66)
  const rows = new AnimatedRows(() => loop.kick());
  const scroll = new EasedScroll(() => loop.kick());

  own(host, effect(() => {                           // RenderQueue::bind
    const m = vm.model.value;
    if (!m) return;
    const now = performance.now();
    jobs = m.renders || [];
    const items = [];
    for (const j of jobs) {
      items.push([j.id, j]);
      let pr = progress.get(j.id);
      if (!pr) {
        pr = { frac: new Anim(0), target: 0, last: -1, lastDone: -1, lastMs: 0, rate: 0, state: [0, 1, 2, 3].map(() => new Anim(0)), want: 0, applied: -1 };
        progress.set(j.id, pr);
      }
      pr.target = j.total > 0 ? Math.min(1, Math.max(0, j.done / j.total)) : j.state === "done" ? 1 : 0;
      if (pr.lastDone >= 0 && j.done > pr.lastDone && now > pr.lastMs) {
        const inst = (j.done - pr.lastDone) / ((now - pr.lastMs) / 1000);
        pr.rate = pr.rate <= 0 ? inst : pr.rate * 0.7 + inst * 0.3;   // smoothed, so it does not flicker
      }
      if (j.done !== pr.lastDone) { pr.lastDone = j.done; pr.lastMs = now; }
      pr.want = stateIndex(j.state);
    }
    rows.sync(items, ROW_H);
    loop.kick();
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));

  function rowRect(i) { const r = rows.byIndex(i); const y = r ? r.y : i * ROW_H; return { x: 0, y: HEADER_H + y - scroll.value, w: W, h: ROW_H }; }
  function rowAt(pt) {
    if (!inRect(pt, { x: 0, y: HEADER_H, w: W, h: Math.max(0, H - HEADER_H) })) return -1;
    for (let i = 0; i < jobs.length; ++i) if (inRect(pt, rowRect(i))) return i;
    return -1;
  }

  function advance(now) {                            // RenderQueue::advance
    let moving = rows.moving();
    scroll.setExtent(HEADER_H, Math.max(0, H - HEADER_H), jobs.length * ROW_H);
    for (const pr of progress.values()) {
      if (pr.last < 0) { pr.frac.set(pr.target); pr.last = pr.target; }
      else if (pr.target !== pr.last) { pr.frac.animateTo(pr.target, tok.ms("motion-catch-up") || 220, Ease.EaseOutCubic, now); pr.last = pr.target; }
      moving = pr.frac.update(now) || moving;
      for (let k = 0; k < 4; ++k) {
        if (pr.applied < 0) pr.state[k].set(k === pr.want ? 1 : 0);
        else if (pr.want !== pr.applied) pr.state[k].animateTo(k === pr.want ? 1 : 0, tok.ms("motion-cross-fade") || 200, Ease.EaseOutCubic, now);
        moving = pr.state[k].update(now) || moving;
      }
      pr.applied = pr.want;
    }
    return moving || scroll.moving;
  }

  function paint() {                                 // RenderQueue::onPaint
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), destr = col("destructive");
    const primary = col("primary"), success = col("success"), secondary = col("secondary");
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("deck-bg") });
    p.line(0, 0.5, W, 0.5, border, 1);
    const hy = HEADER_H * 0.5, tr = 0.13 * 9;
    p.text("RENDER QUEUE", PAD_X, baseline(hy, 9), 9, F.semibold, muted, tr);
    const lw = measure("RENDER QUEUE", 9, F.semibold, tr), count = String(jobs.length);
    p.text(count, PAD_X + lw + 6, baseline(hy, 9), 9, F.mono, fade(muted, 0.7));
    const rx = PAD_X + lw + 6 + measure(count, 9, F.mono) + 6.5;
    if (W - PAD_X > rx) p.line(rx, hy, W - PAD_X, hy, border, 1);
    const vh = Math.max(0, H - HEADER_H);
    p.clip(0, HEADER_H, W, vh);
    if (!jobs.length && !rows.rows().length) {
      const s1 = "nothing queued", s2 = "Choose a version and press Render — the job names the version it renders.";
      const cy = HEADER_H + vh * 0.42;
      p.text(s1, (W - measure(s1, 12, F.sans)) * 0.5, cy, 12, F.sans, muted);
      const s2e = ellipsize(s2, W - 2 * PAD_X, 10, F.sans);
      p.text(s2e, (W - measure(s2e, 10, F.sans)) * 0.5, cy + 17, 10, F.sans, fade(muted, 0.75));
    }
    for (const row of rows.rows()) {
      const a = row.alpha;
      if (a <= 0.001 || !scroll.bandVisible(row.y, ROW_H)) continue;
      const j = row.data, top = HEADER_H + row.y - scroll.value, l1 = top + 11.5, l2 = top + 23;
      if (row.index >= 0 && row.index % 2) p.rect(0, top, W, ROW_H, 0, { fill: fade(col("lane-alt-bg"), a) });
      const pr = progress.get(j.id);
      const st = [0, 1, 2, 3].map((k) => (pr ? pr.state[k].value : k === stateIndex(j.state) ? 1 : 0));
      const barA = st[1] + st[2];                    // the bar shows while running or done
      const gb = { x: PAD_X, y: l1 - 5.5, w: 11, h: 11 };
      if (st[3] > 0.001) p.add(glyph.warn(gb, css(fade(destr, a * st[3]))));
      if (st[2] > 0.001) p.icon("checkCircle", gb.x, gb.y, 11, fade(success, a * st[2]));
      if (st[1] > 0.001) p.add(glyph.film(gb, css(fade(primary, a * st[1]))));
      if (st[0] > 0.001) p.add(glyph.film(gb, css(fade(muted, a * st[0]))));

      // right block first (measured), so the name and the path get what is left (R5)
      const right = W - PAD_X, statusX = right - STATUS_W, barX = statusX - 10 - BAR_W;
      const textRight = statusX - 12 - (BAR_W + 10) * Math.min(1, barA);
      const x = PAD_X + 18, fmt = String(j.format || "").toUpperCase();
      const chipW = measure(fmt, 8, F.mono) + 8;
      const name = ellipsize(j.timelineName || j.timeline || "", textRight - x - chipW - 8, 11, F.medium);
      p.text(name, x, baseline(l1, 11), 11, F.medium, fade(fg, a));
      const cx = x + measure(name, 11, F.medium) + 8;
      p.rect(cx, l1 - 6.5, chipW, 13, tok.px("radius-hairline"), { fill: fade(secondary, a), stroke: fade(border, a), sw: 1 });
      p.text(fmt, cx + 4, baseline(l1, 8), 8, F.mono, fade(muted, a));
      let px = x;
      if (j.spec) {
        const sp = ellipsize(j.spec, (textRight - x) * 0.62, 9, F.sans);
        p.text(sp, x, baseline(l2, 9), 9, F.sans, fade(fg, 0.75 * a));
        px = x + measure(sp, 9, F.sans) + 8;
      }
      p.text(ellipsize(j.outPath || "", Math.max(0, textRight - px), 9, F.mono), px, baseline(l2, 9), 9, F.mono, fade(muted, a));

      // the bar: its colour slides from the accent to success as the job finishes
      const frac = pr ? pr.frac.value : 0;
      if (barA > 0.001) {
        const by = top + ROW_H * 0.5 - 1.5, pill = tok.px("radius-pill");
        p.rect(barX, by, BAR_W, 3, pill, { fill: fade(secondary, a * barA) });
        if (frac * BAR_W >= 3) {
          const doneMix = barA > 0 ? st[2] / barA : 0;
          p.rect(barX, by, frac * BAR_W, 3, pill, { fill: fade(lerp(primary, success, doneMix), a * barA) });
        }
      }
      // the status line, one per state, cross-faded
      for (let k = 0; k < 4; ++k) {
        if (st[k] <= 0.001) continue;
        let status, sc = muted, fam = F.sans;
        if (k === 1) {
          const r = pr ? pr.rate : 0, pct = Math.round(frac * 100);
          status = r > 0.05 ? `${pct}% · ${Math.round(r)} fps` : `${pct}% · ${j.done}/${j.total}`;
          sc = fg; fam = F.mono;
        } else if (k === 2) { status = `done · ${j.total} frames`; sc = success; }
        else if (k === 3) { status = j.error ? "failed — " + j.error : "failed"; sc = destr; }
        else status = j.state === "cancelled" ? "cancelled" : "queued";
        const se = ellipsize(status, STATUS_W, 10, fam);
        p.text(se, right - measure(se, 10, fam), baseline(top + ROW_H * 0.5, 10), 10, fam, fade(sc, a * st[k]));
      }
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

  // ---------------------------------------------------------------- input
  function reveal(j) {
    const path = j.outPath || "";
    if (!path) { ctx.toast("This render has no output path", true); return; }
    try { if (navigator.clipboard && window.isSecureContext) navigator.clipboard.writeText(path).catch(() => {}); } catch { /* not allowed */ }
    ctx.toast(path);
  }
  host.addEventListener("contextmenu", (e) => {
    e.preventDefault();
    const i = rowAt(localPoint(host, e));
    if (i < 0 || !ctx.menu) return;
    const j = jobs[i], r = ctx.point ? ctx.point(e) : { x: e.clientX, y: e.clientY };
    const items = [];
    if (j.state === "queued" || j.state === "running") items.push({ label: "Cancel render", action: () => vm.cmd("render cancel " + q(j.id)) });
    items.push({ label: "Reveal output", action: () => reveal(j) });
    ctx.menu.open(items, r.x, r.y);
  });
  host.addEventListener("wheel", (e) => { if (scroll.scrollBy(wheelPx(e))) { e.preventDefault(); loop.kick(); } }, { passive: false });
  loop.kick();
}
