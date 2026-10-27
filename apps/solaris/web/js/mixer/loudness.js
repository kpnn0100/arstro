/*
 * Solaris — mixer/loudness.js: LoudnessPanel (widgets/LoudnessPanel.{h,cpp}, R-METER-1/2) — the Loudness window's content.
 * Every number is the model's `loudness` (live): momentary / short-term / integrated LUFS, true peak dBTP, correlation
 * (number + needle), 61 bands of 1/6 octave. Reset = ONE `meters clear`. A reading catches up over motion::catchUp; one
 * with nothing measured cross-fades to "—" (crossFade); a band rises in 40 ms and falls in motion::meterFall.
 */
import { effect } from "../core/signal.js";
import { own } from "../core/dom.js";
import { Anim, Ease, frames, motion } from "../kit/anim.js";
import { canvasIn, color, colorsVersion, fillRR, line, text, lerpColor, whiteAlpha, primaryAlpha, rgba } from "../kit/draw.js";
import { FONT, measure, ellipsize, baseline } from "../kit/text.js";
import { useCss } from "../kit/css.js";

const HEADER_H = 35.75, CELL_H = 42.25, BANDS = 61, PAD = 9.75, GAP = 6.5, DB_W = 22.75;
const SILENCE = -143, FLOOR_DB = -70, SPEC_FLOOR = -72, SPEC_TOP = 0, ATTACK_MS = 40;
const LABELS = ["Momentary", "Short-term", "Integrated", "True peak", "Correlation"];
const UNITS = ["LUFS", "LUFS", "LUFS", "dBTP", ""];
const CORR = 4;
const fade = (c, a) => [c[0], c[1], c[2], c[3] * a];
function number(v, d) { let s = v.toFixed(d); if (s === "-0.0" || s === "-0.00") s = s.slice(1); return s; }
const bandOf = (hz) => 34 + 6 * Math.log2(hz / 1000);

export function mount(host, vm, ctx) {
  useCss("mixer/loudness.css");
  host.classList.add("sl-loud");
  const M = motion();
  const cv = canvasIn(host, ctx);
  const live = LABELS.map((_, r) => new Anim(r === CORR ? 0 : FLOOR_DB)), on = LABELS.map(() => new Anim(0));
  const target = LABELS.map(() => 0), has = LABELS.map(() => false), last = LABELS.map(() => NaN), hasLast = LABELS.map(() => false);
  const band = Array.from({ length: BANDS }, () => new Anim(SPEC_FLOOR)), bandT = new Array(BANDS).fill(SPEC_FLOOR), bandLast = new Array(BANDS).fill(SPEC_FLOOR);
  const hover = new Anim(0);
  let placed = false, hovered = false;

  const size = () => ctx.box.peek();
  const resetRect = () => { const w = 58.5; return { x: size().w - PAD - w, y: (HEADER_H - 19.5) * 0.5, w, h: 19.5 }; };
  const specRect = () => { const W = size().w, H = size().h, top = HEADER_H + GAP + 2 * CELL_H + GAP;
    return { x: PAD + DB_W, y: top, w: Math.max(0, W - 2 * PAD - DB_W), h: Math.max(0, H - top - PAD - 13) }; };

  function bind(m) {
    const l = (m && m.loudness) || {};
    const v = [l.momentary, l.shortTerm, l.integrated, l.truePeak, l.correlation].map((x) => (typeof x === "number" ? x : -144));
    if (typeof l.correlation !== "number") v[CORR] = 0;
    for (let r = 0; r < LABELS.length; r++) {
      has[r] = r === CORR ? v[0] > SILENCE : v[r] > SILENCE;
      target[r] = r === CORR ? v[r] : Math.max(FLOOR_DB, v[r]);
    }
    const sp = l.spectrum || [];
    for (let b = 0; b < BANDS; b++) bandT[b] = b < sp.length ? Math.max(SPEC_FLOOR, Math.min(SPEC_TOP, sp[b])) : SPEC_FLOOR;
  }

  function advance(now) {
    let moving = false;
    for (let r = 0; r < LABELS.length; r++) {
      if (!placed) { if (has[r]) live[r].set(target[r]); on[r].set(has[r] ? 1 : 0); }
      else {
        if (has[r] && target[r] !== last[r]) live[r].animateTo(target[r], M.catchUp, Ease.EaseOutCubic, now);
        if (has[r] !== hasLast[r]) on[r].animateTo(has[r] ? 1 : 0, M.crossFade, Ease.EaseOutCubic, now);
      }
      hasLast[r] = has[r];
      if (has[r]) last[r] = target[r];
      moving = live[r].update(now) || moving;
      moving = on[r].update(now) || moving;
    }
    for (let b = 0; b < BANDS; b++) {
      const tg = bandT[b], bp = band[b];
      if (!placed) bp.set(tg);
      else if (tg !== bandLast[b]) bp.animateTo(tg, tg > bp.value ? ATTACK_MS : M.meterFall, Ease.EaseOutCubic, now);
      bandLast[b] = tg;
      moving = bp.update(now) || moving;
    }
    placed = true;
    const hw = hovered ? 1 : 0;
    if (hover.target !== hw) hover.animateTo(hw, M.hover, Ease.EaseOutCubic, now);
    moving = hover.update(now) || moving;
    paint();
    return moving;
  }
  const kick = () => frames(advance);

  function paint() {
    const g = cv.begin();
    const W = size().w;
    const muted = color("muted-foreground"), fg = color("foreground");
    const rs = resetRect();
    text(g, ellipsize("ITU-R BS.1770-4 · " + vm.tr("the master"), Math.max(0, rs.x - 2 * PAD), 10, FONT.mono), PAD, baseline(rs.y + rs.h * 0.5, 10), 10, FONT.mono, muted);
    fillRR(g, rs.x, rs.y, rs.w, rs.h, 2, lerpColor(color("secondary"), color("popover"), hover.value));
    const reset = ellipsize(vm.tr("Reset"), rs.w - 4, 10, FONT.sans);
    text(g, reset, rs.x + (rs.w - measure(reset, 10, FONT.sans)) * 0.5, baseline(rs.y + rs.h * 0.5, 10), 10, FONT.sans, color("secondary-foreground"));
    line(g, 0, HEADER_H - 0.5, W, HEADER_H - 0.5, color("border"), 1);
    const cw = Math.max(0, (W - 2 * PAD) / 3), y0 = HEADER_H + GAP;
    for (let r = 0; r < LABELS.length; r++) {
      const x = PAD + (r % 3) * cw, y = y0 + Math.floor(r / 3) * CELL_H;
      text(g, ellipsize(vm.upper(vm.tr(LABELS[r])), cw - 6.5, 9, FONT.semibold, 0.13 * 9), x, baseline(y + 9.75, 9), 9, FONT.semibold, muted, 0.13 * 9);
      const vy = baseline(y + 27, 13), o = on[r].value;
      const num = number(live[r].value, r === CORR ? 2 : 1);
      if (o > 0.001) {
        text(g, num, x, vy, 13, FONT.monoMedium, fade(fg, o));
        if (UNITS[r]) text(g, UNITS[r], x + measure(num, 13, FONT.monoMedium) + 4, vy, 10, FONT.sans, fade(muted, o));
      }
      if (o < 0.999) text(g, "—", x, vy, 13, FONT.monoMedium, fade(muted, 1 - o));
    }
    { // the correlation's needle
      const x = PAD + 2 * cw, y = y0 + CELL_H + 21, w = Math.max(0, cw - 6.5);
      fillRR(g, x, y - 2, w, 4, 9999, color("input"));
      const cx = Math.round(x + w * 0.5) + 0.5;
      line(g, cx, y - 5, cx, y + 5, whiteAlpha(0.3), 1);
      const c = Math.max(-1, Math.min(1, live[CORR].value)), o = on[CORR].value;
      const col = c >= 0 ? lerpColor(color("secondary-foreground"), color("success"), c) : lerpColor(color("secondary-foreground"), color("destructive"), -c);
      if (o > 0.001) fillRR(g, x + (c + 1) * 0.5 * w - 1.5, y - 5, 3, 10, 9999, fade(col, o));
      text(g, "-1", x, y + 15, 8, FONT.mono, muted);
      text(g, "+1", x + w - measure("+1", 8, FONT.mono), y + 15, 8, FONT.mono, muted);
    }
    const sp = specRect();
    if (sp.w <= 1 || sp.h <= 1) return;
    fillRR(g, sp.x, sp.y, sp.w, sp.h, 2, color("curve-plot-bg"));
    const yOf = (db) => sp.y + sp.h - (db - SPEC_FLOOR) / (SPEC_TOP - SPEC_FLOOR) * sp.h;
    for (let db = -12; db >= -60; db -= 12) {
      const y = Math.round(yOf(db)) + 0.5;
      line(g, sp.x, y, sp.x + sp.w, y, whiteAlpha(0.06), 1);
      const s = number(db, 0);
      text(g, s, sp.x - 4 - measure(s, 8, FONT.mono), baseline(y, 8), 8, FONT.mono, muted);
    }
    const bw = sp.w / BANDS;
    g.save();
    g.beginPath(); g.rect(sp.x, sp.y, sp.w, sp.h); g.clip();
    for (let b = 0; b < BANDS; b++) {
      const db = band[b].value;
      if (db <= SPEC_FLOOR + 0.01) continue;
      const y = yOf(db);
      fillRR(g, sp.x + b * bw + 0.5, y, Math.max(1, bw - 1), sp.y + sp.h - y, 1, primaryAlpha(0.75));
    }
    g.restore();
    for (const [hz, name] of [[50, "50"], [100, "100"], [500, "500"], [1000, "1k"], [5000, "5k"], [10000, "10k"]]) {
      const x = sp.x + (bandOf(hz) + 0.5) * bw;
      text(g, name, x - measure(name, 8, FONT.mono) * 0.5, sp.y + sp.h + 10, 8, FONT.mono, muted);
    }
    void rgba;
  }

  const local = (e) => { const p = ctx.screenPoint(e), b = ctx.box.peek(); return { x: p.x - b.x, y: p.y - b.y }; };
  const inReset = (p) => { const r = resetRect(); return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; };
  host.addEventListener("pointermove", (e) => { const hv = inReset(local(e)); if (hv !== hovered) { hovered = hv; host.style.cursor = "default"; kick(); } });
  host.addEventListener("pointerleave", () => { if (hovered) { hovered = false; kick(); } });
  host.addEventListener("click", (e) => { if (inReset(local(e))) vm.cmd("meters clear"); }); // R-METER-2: one line

  own(host, effect(() => { bind(vm.model.value); colorsVersion.value; vm.lang && vm.lang.value; kick(); }));
  own(host, effect(() => { ctx.box.value; kick(); }));
  cv.onResize(kick);
}
