/*
 * Interstellar — edit/transport.js: ◀◀ ▶ ▶▶ ◉, the timecode, the scrubber and the meter, under the
 * monitor. Ports app/widgets/Transport.{h,cpp}.
 *
 * The scrubber is DIRECT MANIPULATION: while dragged the thumb is exactly under the pointer and every
 * frame-quantised move sends `playhead <t>` (as a notify - a scrub is a stream; the last one goes as
 * a command, so a refusal is said) or `source playhead <t>` in the source viewer (R-EDT-1). Released,
 * the thumb follows the model: a jump EASES there (220 ms), a playing step under half a second is
 * followed directly (it IS the motion). The play/pause glyph cross-fades (160 ms); prev/next send
 * `playhead prev-cut|next-cut`; the camera opens Copy / Save under it (R-UI-11). The shuttle rate's
 * badge (R-EDT-2) eases in beside the timecode; In/Out show as brackets with the span washed; marker
 * ticks; the stereo meter (R-AUD-8) eases in when the timeline has sound - RMS quiet, peak bright, the
 * top 3 dB destructive, a held peak tick (1.5 s, then 20 dB/s), a clip lamp; ballistics are a meter's
 * (~30 ms attack, 24 dB/s release). The playhead and meters come from the `transport` state (25/s).
 *
 * Native defect kept (D-16): the scrubber's hover thickening reads the CAPTURE button's hover amount
 * (Transport.cpp: paint reads id 3, the scrubber's hover id became 4 when the capture button came).
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { reducedMotion } from "../core/motion.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { seconds, timecode } from "../kit/cmd.js";
import { width as measure, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { Painter, canvasSvg, col, css, fade, lerp, whiteA, hoverWash, Anim, HoverFade, frameLoop, localPoint, inRect, Ease, F } from "../kit/paint.js";
import { openCaptureMenu } from "./menus.js";

// Transport.cpp:14-25
const BTN = 26, BUTTONS = 4, PAD = 9.75, TC_W = 88, DUR_W = 76, TC_PX = 11, PLAY_FADE_MS = 160, FOLLOW_STEP_S = 0.5,
      METER_W = 64, BADGE_W = 28, FLOOR_DB = -48;
const toDb = (v) => (v > 1e-6 ? 20 * Math.log10(v) : -120);
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

export function mount(host, vm, ctx) {
  useCss("css/edit/transport.css");
  host.classList.add("tp");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  let W = 0, H = 39;
  let duration = 0, fps = 24, modelTime = 0, playing = false, playApplied = false, init = false, markers = [];
  let targetTime = 0, lastTarget = -1, scrubbing = false, lastSentFrame = -1, sourceMode = false, markA = -1, markB = -1;
  let badgeText = "", badgeWanted = "", badgeOn = false, badgeApplied = false;
  const shown = new Anim(0), playAmt = new Anim(0), badgeAmt = new Anim(0), clipAmt = new Anim(0), meterAmt = new Anim(0);
  const peakIn = [0, 0], rmsIn = [0, 0], peakDb = [-120, -120], rmsDb = [-120, -120], holdDb = [-120, -120], holdAt = [0, 0];
  let clipIn = false, clipApplied = false, soundIn = false, soundApplied = false, meterInit = false, lastMs = 0;
  const hover = new HoverFade();

  // ---------------------------------------------------------------- bind (Transport::bind)
  own(host, effect(() => {
    const m = vm.model.value, tr = vm.transport.value || {};
    if (!m) return;
    sourceMode = !!vm.monitor.transportSourceMode.value;
    fps = m.fps > 0 ? m.fps : 24;
    markers = [];
    if (sourceMode) {
      duration = Math.max(0, m.sourceDuration || 0);
      modelTime = tr.sourceView === m.sourceView && tr.sourcePlayhead !== undefined ? tr.sourcePlayhead : m.sourcePlayhead;
      playing = false;
      markA = m.sourceIn; markB = m.sourceOut;
    } else {
      duration = Math.max(0, tr.duration ?? m.duration ?? 0);
      modelTime = tr.playhead ?? m.playhead;
      playing = !!(tr.playing ?? m.playing);
      markA = m.markIn; markB = m.markOut;
      for (const mk of m.markers || []) markers.push(mk.at);
    }
    if (!scrubbing) targetTime = modelTime;
    const shuttle = tr.shuttle ?? m.shuttle ?? 0;
    badgeOn = !sourceMode && playing && Math.abs(shuttle) > 0 && Math.abs(shuttle - 1) > 1e-9;
    if (badgeOn) badgeWanted = (shuttle < 0 ? "−" : "") + String(Math.abs(shuttle)) + "×";
    const pk = tr.meterPeak || [m.meterPeakL, m.meterPeakR], rm = tr.meterRms || [m.meterRmsL, m.meterRmsR];
    peakIn[0] = pk[0] || 0; peakIn[1] = pk[1] || 0; rmsIn[0] = rm[0] || 0; rmsIn[1] = rm[1] || 0;
    clipIn = !!(tr.meterClip ?? m.meterClip);
    soundIn = false;
    for (const tl of m.timelines || []) if (tl.id === m.currentTimeline) soundIn = !!tl.hasSound;
    loop.kick();
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h || 39; fontEpoch.value; loop.kick(); }));

  // ---------------------------------------------------------------- geometry
  const meterRect = () => { const a = meterAmt.value, x1 = W - PAD - DUR_W - 6; return { x: x1 - METER_W * a, y: H * 0.5 - 6, w: METER_W * a, h: 12 }; };
  const buttonRect = (i) => ({ x: PAD + i * (BTN + 2), y: (H - BTN) * 0.5, w: BTN, h: BTN });
  function scrubRect() {
    const x0 = PAD + BUTTONS * (BTN + 2) + 6 + TC_W + 6 + (BADGE_W + 4) * badgeAmt.value;
    const x1 = W - PAD - DUR_W - 6 - (METER_W + 10) * meterAmt.value;
    return { x: x0, y: 0, w: Math.max(0, x1 - x0), h: H };
  }
  const timeToX = (t) => { const r = scrubRect(); return duration <= 0 ? r.x : r.x + clamp(t / duration, 0, 1) * r.w; };
  const xToTime = (x) => { const r = scrubRect(); return r.w <= 0 || duration <= 0 ? 0 : clamp((x - r.x) / r.w, 0, 1) * duration; };
  function scrubTo(x, final = false) {
    const t = xToTime(x);
    targetTime = t; shown.set(t); lastTarget = t;
    const frame = Math.round(t * fps);
    if (frame !== lastSentFrame || final) {
      lastSentFrame = frame;
      const line = (sourceMode ? "source playhead " : "playhead ") + seconds(t, fps);
      if (final) vm.cmd(line); else vm.notify(line);
    }
  }

  // ---------------------------------------------------------------- advance (Transport::advance)
  function advance(now) {
    let moving = false;
    if (!init) { shown.set(targetTime); lastTarget = targetTime; playAmt.set(playing ? 1 : 0); playApplied = playing; init = true; }
    if (!scrubbing && targetTime !== lastTarget) {
      if (playing && Math.abs(targetTime - shown.value) < FOLLOW_STEP_S) shown.set(targetTime);
      else shown.animateTo(targetTime, tok.ms("motion-catch-up"), Ease.EaseOutCubic, now);
      lastTarget = targetTime;
    }
    moving = shown.update(now) || moving;
    if (playing !== playApplied) { playAmt.animateTo(playing ? 1 : 0, PLAY_FADE_MS, Ease.EaseOutCubic, now); playApplied = playing; }
    moving = playAmt.update(now) || moving;
    const dt = meterInit ? clamp(now - lastMs, 0, 200) : 0;
    lastMs = now;
    if (!meterInit) { meterAmt.set(soundIn ? 1 : 0); soundApplied = soundIn; meterInit = true; }
    const rm = reducedMotion();
    for (let c = 0; c < 2; ++c) {
      const ball = (live, target) => (rm ? target : target > live ? live + (target - live) * Math.min(1, dt / 30) : Math.max(target, live - (24 * dt) / 1000));
      peakDb[c] = ball(peakDb[c], Math.max(-120, toDb(peakIn[c])));
      rmsDb[c] = ball(rmsDb[c], Math.max(-120, toDb(rmsIn[c])));
      if (peakDb[c] >= holdDb[c]) { holdDb[c] = peakDb[c]; holdAt[c] = now; }
      else if (now - holdAt[c] > 1500) holdDb[c] = rm ? peakDb[c] : Math.max(peakDb[c], holdDb[c] - (20 * dt) / 1000);
      if (peakDb[c] > -120 || holdDb[c] > -120) moving = true;   // the meter falls on its own
    }
    if (clipIn !== clipApplied) { clipAmt.animateTo(clipIn ? 1 : 0, clipIn ? tok.ms("motion-hover") : 300, Ease.EaseOutCubic, now); clipApplied = clipIn; }
    moving = clipAmt.update(now) || moving;
    if (soundIn !== soundApplied) { meterAmt.animateTo(soundIn ? 1 : 0, tok.ms("motion-scroll"), Ease.EaseOutCubic, now); soundApplied = soundIn; }
    moving = meterAmt.update(now) || moving;
    if (badgeOn !== badgeApplied) { badgeAmt.animateTo(badgeOn ? 1 : 0, tok.ms("motion-hover"), Ease.EaseOutCubic, now); badgeApplied = badgeOn; }
    if (badgeOn) badgeText = badgeWanted;
    moving = badgeAmt.update(now) || moving;
    moving = hover.advance(now) || moving;
    return moving;
  }

  // ---------------------------------------------------------------- paint (Transport::onPaint)
  function paint() {
    const fg = col("foreground"), muted = col("muted-foreground"), white = col("white"), rc = tok.px("radius-control"),
          hair = tok.px("radius-hairline"), pill = tok.px("radius-pill");
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("card") });
    p.line(0, 0, W, 0, col("border"), 1);
    for (let i = 0; i < BUTTONS; ++i) {
      const r = buttonRect(i), hv = hover.amount(i);
      if (hv > 0.001) p.rect(r.x, r.y, r.w, r.h, rc, { fill: hoverWash(hv) });
      const c = lerp(i === 1 ? fg : muted, white, 0.5 * hv), gb = { x: r.x + 7, y: r.y + 7, w: r.w - 14, h: r.h - 14 };
      if (i === 0) p.add(glyph.skip(gb, css(c), -1));
      else if (i === 2) p.add(glyph.skip(gb, css(c), 1));
      else if (i === 3) p.add(glyph.camera(gb, css(c)));
      else {
        const pa = playAmt.value;
        if (pa < 0.999) p.add(glyph.play(gb, css(fade(c, 1 - pa))));
        if (pa > 0.001) p.add(glyph.pause(gb, css(fade(c, pa))));
      }
    }
    const cy = H * 0.5, tc = timecode(shown.value, fps), tcX = PAD + BUTTONS * (BTN + 2) + 6;
    p.text(tc, tcX, baseline(cy, TC_PX), TC_PX, F.mono, sourceMode ? col("primary") : fg);
    const ba = badgeAmt.value;
    if (ba > 0.001 && badgeText) {
      const bw = Math.min(BADGE_W, measure(badgeText, 9, F.monoMedium) + 8), br = { x: tcX + TC_W + 4, y: cy - 7, w: bw, h: 14 };
      p.rect(br.x, br.y, br.w, br.h, rc, { fill: fade(col("primary"), ba) });
      p.text(badgeText, br.x + 4, baseline(cy, 9), 9, F.monoMedium, fade(col("primary-foreground"), ba));
    }
    const sr = scrubRect();
    if (sr.w > 4) {
      const hv = hover.amount(3);                 // D-16: the native reads the capture button's id here
      const th = 3 + hv, x = timeToX(shown.value);
      p.rect(sr.x, cy - th * 0.5, sr.w, th, pill, { fill: col("secondary") });
      if (x - sr.x >= th) p.rect(sr.x, cy - th * 0.5, x - sr.x, th, pill, { fill: col("primary") });
      for (const mt of markers) { const mx = timeToX(mt); p.line(mx, cy - 6, mx, cy - 3, col("marker"), 1); }
      if (markA >= 0 && markB > markA) p.rect(timeToX(markA), cy - 5, Math.max(0, timeToX(markB) - timeToX(markA)), 10, hair, { fill: whiteA(0.06) });
      const bracket = (at, dir) => {
        const bx = timeToX(at);
        p.line(bx, cy - 6, bx, cy + 6, fg, 1);
        p.line(bx, cy - 6, bx + dir * 3, cy - 6, fg, 1);
        p.line(bx, cy + 6, bx + dir * 3, cy + 6, fg, 1);
      };
      if (markA >= 0) bracket(markA, 1);
      if (markB >= 0) bracket(markB, -1);
      p.circle(x, cy, 4.5 + hv, { fill: white });
    }
    const ma = meterAmt.value;
    if (ma > 0.001) {
      const mr = meterRect(), xOf = (db) => mr.x + clamp((db - FLOOR_DB) / -FLOOR_DB, 0, 1) * mr.w, hotX = xOf(-3);
      for (let c = 0; c < 2; ++c) {
        const y = mr.y + 2 + c * 5, bh = 3, rx = xOf(rmsDb[c]), px = xOf(peakDb[c]);
        p.rect(mr.x, y, mr.w, bh, hair, { fill: fade(col("secondary"), ma) });
        if (rx > mr.x) p.rect(mr.x, y, Math.min(rx, hotX) - mr.x, bh, hair, { fill: fade(col("success"), 0.45 * ma) });
        if (px > mr.x) p.rect(mr.x, y, Math.min(px, hotX) - mr.x, bh, hair, { fill: fade(col("success"), 0.9 * ma) });
        if (px > hotX) p.rect(hotX, y, px - hotX, bh, hair, { fill: fade(col("destructive"), ma) });
        const hx = xOf(holdDb[c]);
        if (holdDb[c] > FLOOR_DB) p.line(hx, y - 0.5, hx, y + bh + 0.5, fade(white, 0.8 * ma), 1);
      }
      p.circle(mr.x + mr.w + 5, mr.y + 6, 2.5, { fill: fade(col("secondary"), ma) });
      const ca = clipAmt.value;
      if (ca > 0.001) p.circle(mr.x + mr.w + 5, mr.y + 6, 2.5, { fill: fade(col("destructive"), ca * ma) });
    }
    const dur = timecode(duration, fps);
    p.text(dur, W - PAD - measure(dur, 10, F.mono), baseline(cy, 10), 10, F.mono, muted);
    p.end();
  }

  function step(now) {
    if (!(W > 0)) return false;
    const more = advance(now);
    paint();
    return more;
  }

  // ---------------------------------------------------------------- input (Transport::handleGesture)
  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e);
    if (scrubbing) { scrubTo(pt.x); loop.kick(); return; }
    let id = -1;
    for (let i = 0; i < BUTTONS; ++i) if (inRect(pt, buttonRect(i))) id = i;
    if (id < 0 && inRect(pt, scrubRect())) id = BUTTONS;
    hover.setHovered(id);
    loop.kick();
  });
  host.addEventListener("pointerleave", () => { hover.clear(); loop.kick(); });
  host.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    const pt = localPoint(host, e);
    if (inRect(pt, scrubRect()) && duration > 0) {
      scrubbing = true;
      vm.view.scrubbing.value = true;
      lastSentFrame = -1;
      scrubTo(pt.x);
      try { host.setPointerCapture(e.pointerId); } catch {}
      loop.kick();
    }
  });
  const end = (e) => {
    if (!scrubbing) return;
    scrubTo(localPoint(host, e).x, true);
    scrubbing = false;
    vm.view.scrubbing.value = false;
    loop.kick();
  };
  host.addEventListener("pointerup", end);
  host.addEventListener("pointercancel", end);
  host.addEventListener("click", (e) => {
    const pt = localPoint(host, e);
    if (inRect(pt, buttonRect(0))) vm.cmd("playhead prev-cut");
    else if (inRect(pt, buttonRect(1))) vm.cmd(playing ? "pause" : "play");
    else if (inRect(pt, buttonRect(2))) vm.cmd("playhead next-cut");
    else if (inRect(pt, buttonRect(3))) {
      const r = buttonRect(3), b = ctx.box.peek();
      openCaptureMenu(vm, ctx, { x: b.x + r.x, y: b.y + r.y, w: r.w, h: r.h });
    }
  });
  loop.kick();
}
