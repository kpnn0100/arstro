/*
 * Interstellar — cut/inspector.js: the Cut tab's right column, the selected clip in numbers. Ports
 * app/widgets/ClipInspector.{h,cpp}.
 *
 * Cuts only, no colour (R-TL-2), on cosmo's card with cosmo's section headers (SectionHeader.h:
 * 9 px SemiBold tracked 0.13, a hairline rule after the label): CLIP - name, source (destructive when
 * offline), reel and source timecode (R-XCH-5: the file's own clock at the clip's in), track,
 * provenance in the timeline's words; PLACEMENT in mono - at, in, out, duration, speed; MIX - opacity,
 * gain; ACTIONS - Split at playhead (`clip split <c> --at <t>`) and Delete clip (`clip delete <c>`),
 * cosmo PillButtons (outline; hover brightens and pulls the border toward the emphasis, 0.5).
 * Rows are 22.75 high, the value right-aligned past cosmo's 86 px label column. Nothing selected:
 * "No clip selected." / "Click a clip on the timeline to inspect it.", cross-faded (180 ms) with the
 * values; another clip dips the values (0.35) and settles (180 ms). The column scrolls (R6).
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { q, seconds, num, timecode } from "../kit/cmd.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { EasedScroll, wheelPx } from "../kit/easedscroll.js";
import { Painter, canvasSvg, col, fade, lerp, brighten, whiteA, Anim, HoverFade, frameLoop, localPoint, inRect, Ease, F } from "../kit/paint.js";

const PAD_X = 9.75, ROW_H = 22.75, BTN_H = 26, SECTION_H = 27.95, LABEL_W = 86;   // ClipInspector.cpp:14-20, SectionHeader.h
const ACTIONS_TOP = 3 * SECTION_H + (6 + 5 + 2) * ROW_H + SECTION_H, CONTENT_H = ACTIONS_TOP + BTN_H + PAD_X;
const HOVER_FILL_LIFT = 0.14, HOVER_STROKE_LIFT = 0.5;
const ENC = new TextEncoder();
const estW = (s, px) => ENC.encode(s).length * px * 0.6;       // TextMetrics.h estimateTextWidth
const PROV = { inherited: "inherited from base", overridden: "overridden in this version", dangling: "dangling", local: "local" };

// core/Timecode.h: TcRate::of, tcFromFrames, framesFromTc (drop-frame at 29.97 / 59.94)
function tcRate(fps) {
  fps = fps > 0 ? fps : 24;
  return { fps, nominal: Math.round(fps), drop: Math.abs(fps - 30000 / 1001) < 1e-3 || Math.abs(fps - 60000 / 1001) < 1e-3 };
}
function tcFromFrames(frames, r) {
  const neg = frames < 0;
  if (neg) frames = -frames;
  const n = Math.max(1, r.nominal);
  if (r.drop) {
    const d = n === 60 ? 4 : 2, per10 = n * 600 - d * 9, perMin = n * 60 - d, tens = Math.floor(frames / per10), rem = frames % per10;
    frames += d * 9 * tens + (rem > d ? d * Math.floor((rem - d) / perMin) : 0);
  }
  const ff = frames % n, s = Math.floor(frames / n), p2 = (v) => String(v).padStart(2, "0");
  return `${neg ? "-" : ""}${p2(Math.floor(s / 3600))}:${p2(Math.floor(s / 60) % 60)}:${p2(s % 60)}${r.drop ? ";" : ":"}${p2(ff)}`;
}
function framesFromTc(tc, r) {
  const m = /^\s*(-?\d+)(.)(\d+)(.)(\d+)(.)(\d+)/.exec(tc || "");
  if (!m) return null;
  const h = +m[1], mi = +m[3], s = +m[5], f = +m[7];
  if (h < 0 || mi > 59 || s > 59 || f < 0) return null;
  const n = Math.max(1, r.nominal);
  let frames = (h * 3600 + mi * 60 + s) * n + f;
  if ((r.drop || m[6] === ";" || m[6] === ",") && tcRate(r.fps).drop) { const d = n === 60 ? 4 : 2, minutes = h * 60 + mi; frames -= d * (minutes - Math.floor(minutes / 10)); }
  return frames;
}

export function mount(host, vm, ctx) {
  useCss("css/cut/inspector.css");
  host.classList.add("ci");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  let W = 0, H = 0, clip = null, has = false, hasApplied = false, init = false, lastId = "", swapPending = false, fps = 24;
  let reel = "", sourceTc = "", trackName = "";
  const content = new Anim(0), swap = new Anim(1), btnHover = new HoverFade();
  const scroll = new EasedScroll(() => loop.kick());

  own(host, effect(() => {                           // ClipInspector::bind
    const m = vm.model.value;
    if (!m) return;
    fps = m.fps > 0 ? m.fps : 24;
    clip = (m.clips || []).find((c) => c.id === m.selectedClip) || null;
    has = !!clip;
    trackName = ""; reel = ""; sourceTc = "";
    if (has && !clip.audio)
      for (const r of m.rack || [])
        if (r.rackObj === clip.src) {
          reel = r.reel || "";
          if (r.timecode) {
            const rate = tcRate(r.mediaFps > 0 ? r.mediaFps : fps), f = framesFromTc(r.timecode, rate);
            if (f !== null) sourceTc = tcFromFrames(f + Math.round(clip.in * rate.fps), rate);
          }
        }
    if (has) { const tk = (m.tracks || []).find((t) => t.id === clip.track); if (tk) trackName = tk.name || tk.id; }
    if (has && clip.id !== lastId) { if (lastId) swapPending = true; lastId = clip.id; }
    loop.kick();
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));

  function buttonRect(i) {
    const bw = (W - 2 * PAD_X - 6.5) * 0.5, y = ACTIONS_TOP - scroll.value;
    return { x: i === 0 ? PAD_X : PAD_X + bw + 6.5, y, w: Math.max(0, bw), h: BTN_H };
  }
  const buttonsInView = () => { const y = ACTIONS_TOP - scroll.value; return y + BTN_H > 0 && y < H; };

  function advance(now) {
    let moving = false;
    scroll.setExtent(0, H, CONTENT_H);
    if (!init) { content.set(has ? 1 : 0); hasApplied = has; init = true; }
    if (has !== hasApplied) { content.animateTo(has ? 1 : 0, tok.ms("motion-scroll"), Ease.EaseOutCubic, now); hasApplied = has; }
    if (swapPending) { swap.set(0); swap.animateTo(1, tok.ms("motion-scroll"), Ease.EaseOutCubic, now); swapPending = false; }
    moving = content.update(now) || moving;
    moving = swap.update(now) || moving;
    moving = btnHover.advance(now) || moving;
    return moving || scroll.moving;
  }

  function sectionHeader(x, y, w, label) {           // cosmo SectionHeader.h drawSectionHeader
    const base = y + 11.375 + 9 * 0.85;
    p.text(label, x, base, 9, F.semibold, col("muted-foreground"), 0.13 * 9);
    const lineY = base - 9 * 0.35, labelW = estW(label, 9) + 9 * label.length * 0.13, rs = x + labelW + 6.5, re = x + w;
    if (re > rs) p.line(rs, lineY, re, lineY, col("border"), 1);
    return SECTION_H;
  }

  function paint() {
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), destr = col("destructive"), primary = col("primary");
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("card") });
    p.line(0.5, 0, 0.5, H, border, 1);
    const ca = content.value;
    if (ca < 0.999) {
      const a = 1 - ca, s1 = "No clip selected.", s2 = ellipsize("Click a clip on the timeline to inspect it.", W - 2 * PAD_X, 11, F.sans);
      p.text(s1, (W - measure(s1, 12, F.medium)) * 0.5, H * 0.36, 12, F.medium, fade(fg, a));
      p.text(s2, (W - measure(s2, 11, F.sans)) * 0.5, H * 0.36 + 18, 11, F.sans, fade(muted, a));
    }
    if (ca > 0.001 && clip) {
      const a = ca * (0.35 + 0.65 * swap.value);
      p.clip(0, 0, W, H);
      p.group(a);
      let y = -scroll.value;
      const row = (label, value, fam, vc) => {
        const cy = y + ROW_H * 0.5;
        p.text(label, PAD_X, baseline(cy, 10), 10, F.sans, muted);
        const v = ellipsize(value, W - PAD_X - (PAD_X + LABEL_W), 10, fam);
        p.text(v, W - PAD_X - measure(v, 10, fam), baseline(cy, 10), 10, fam, vc);
        y += ROW_H;
      };
      y += sectionHeader(PAD_X, y, W - 2 * PAD_X, "CLIP");
      row("Name", clip.name || clip.id, F.sans, fg);
      row("Source", clip.srcName || clip.src, F.mono, clip.offline ? destr : fg);
      row("Reel", reel || "—", F.mono, fg);
      row("Source TC", sourceTc || "—", F.mono, fg);
      row("Track", trackName, F.sans, fg);
      const dang = clip.provenance === "dangling";
      row("Provenance", dang ? "dangling — base deleted its target" : PROV[clip.provenance] || "local", F.sans, dang ? destr : clip.provenance === "overridden" ? primary : fg);
      y += sectionHeader(PAD_X, y, W - 2 * PAD_X, "PLACEMENT");
      row("At", timecode(clip.at, fps), F.mono, fg);
      row("In", timecode(clip.in, fps), F.mono, fg);
      row("Out", timecode(clip.out, fps), F.mono, fg);
      row("Duration", timecode(clip.duration > 0 ? clip.duration : (clip.out - clip.in) / Math.max(1e-6, clip.speed), fps), F.mono, fg);
      row("Speed", num(clip.speed * 100) + "%", F.mono, fg);
      y += sectionHeader(PAD_X, y, W - 2 * PAD_X, "MIX");
      row("Opacity", num(clip.opacity * 100) + "%", F.mono, fg);
      row("Gain", clip.audio ? num(clip.gain) + " dB" : "—", F.mono, fg);
      sectionHeader(PAD_X, y, W - 2 * PAD_X, "ACTIONS");
      p.restore();
      p.restore();
    }
    // the two PillButtons (cosmo PillButton, styled by ClipInspector.cpp:42-49), faded with the content
    if (buttonsInView() && ca > 0.001) {
      p.group(ca);
      ["Split at playhead", "Delete clip"].forEach((label, i) => {
        const r = buttonRect(i), hv = btnHover.amount(i), destructive = i === 1;
        const stroke = lerp(destructive ? destr : border, destructive ? destr : primary, HOVER_STROKE_LIFT * hv);
        p.rect(r.x, r.y, r.w, r.h, tok.px("radius-control"), { fill: brighten([0, 0, 0, 0], HOVER_FILL_LIFT * hv), stroke, sw: 1 });
        const tc = lerp(destructive ? destr : fg, col("white"), 0.55 * hv), tw = estW(label, 10);
        p.text(label, r.x + (r.w - tw) * 0.5, r.y + r.h * 0.5 + 10 * 0.35, 10, F.sans, tc);
      });
      p.restore();
    }
    const bar = scroll.bar(W);
    if (bar) p.rect(bar.x, bar.y, bar.w, bar.h, tok.px("radius-pill"), { fill: whiteA(0.14) });
    p.end();
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    const more = advance(now);
    paint();
    return more;
  }

  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e);
    btnHover.setHovered(has && buttonsInView() ? (inRect(pt, buttonRect(0)) ? 0 : inRect(pt, buttonRect(1)) ? 1 : -1) : -1);
    loop.kick();
  });
  host.addEventListener("pointerleave", () => { btnHover.clear(); loop.kick(); });
  host.addEventListener("click", (e) => {
    if (!has || !buttonsInView()) return;
    const pt = localPoint(host, e);
    if (inRect(pt, buttonRect(0))) vm.cmd("clip split " + q(clip.id) + " --at " + seconds(vm.playhead.peek(), fps));
    else if (inRect(pt, buttonRect(1))) vm.cmd("clip delete " + q(clip.id));
  });
  host.addEventListener("wheel", (e) => { if (scroll.scrollBy(wheelPx(e))) { e.preventDefault(); loop.kick(); } }, { passive: false });
  loop.kick();
}
