/*
 * Interstellar — edit/monitor.js: the one picture, outside the tab host. Ports app/widgets/Monitor.{h,cpp}.
 *
 * Draws what vm.monitor (App's fetchFrame / fetchSource / fetchViewer, ported in vm/app.js) says:
 * the frame letterboxed on monitorBg with 9.75 px of air and a #050505 plate; the mono timecode chip,
 * the caption chip (ellipsized to half the width); the three states (frame / "decoding · <proxy>"
 * with a spinner / "no clip at the playhead") cross-fading (180 ms). A new picture at the same place
 * (a grade, a version) cross-DISSOLVES over 160 ms LINEAR; a playback or scrub step replaces it
 * directly (gotcha 10). A new picture is decoded before it is shown, so the old one never blinks.
 *
 * Zoom (R-UI-13): Ctrl+wheel about the pointer, 1×–8× in 1.15× notches, eased 220 ms with the anchor
 * re-derived every frame; a drag pans while zoomed (clamped so the picture covers the frame); a
 * double-click eases back to fit; the magnification chip fades with the live zoom. The wipe divider
 * (R-CLR-5) eases to the model's split and follows the pointer exactly while dragged (`view wipe --at
 * <a>`); the multicam angle bar (R-EDT-5) with its travelling highlight (`multicam angle <k>`); the
 * caption under the playhead (R-DLV-1) cross-fading; the capture button on the caption in Grade
 * (R-UI-11, → edit/menus.js openCaptureMenu); the clip warning (R-UI-15: clipMaskOf of the frame
 * shown, computed here from its pixels, faded with view.clipWarning).
 *
 * Writes view.monitorEdge (Monitor::wantedProxyEdge, from the drawn size and the TARGET zoom).
 * Provides "monitor" ({resetZoom}) for Workspace › Reset Workspace.
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok, SHELL } from "../kit/tokens.js";
import { num } from "../kit/cmd.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { wheelPx } from "../kit/easedscroll.js";
import { Painter, canvasSvg, col, css, fade, lerp, whiteA, hoverWash, primaryA, Anim, HoverFade, frameLoop,
         localPoint, inRect, Ease, F } from "../kit/paint.js";
import { openCaptureMenu } from "./menus.js";

const INSET = 9.75, CHIP_PX = 10, MAX_ZOOM = 8, ZOOM_NOTCH = 1.15;   // Monitor.cpp:16-17, Monitor.h:111-112
const STATES = ["frame", "loading", "empty"];
function proxyLabel(edge) {                                          // Monitor.cpp:19-26
  if (edge >= 3840) return "full-res";
  if (edge >= 1920) return "1080p proxy";
  if (edge >= 1280) return "720p proxy";
  if (edge >= 960) return "540p proxy";
  return "360p proxy";
}
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

export function mount(host, vm, ctx) {
  useCss("css/edit/monitor.css");
  host.classList.add("mo");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  let W = 0, H = 0, nowMs = 0;
  // the picture: cur / prev {url, w, h}; a dissolve between them
  let cur = null, prev = null, dissolveWanted = false, pendingSeq = 0;
  const dissolve = new Anim(1);
  let state = "empty", stateInit = false, stateApplied = "empty";
  const stateAmt = { frame: new Anim(0), loading: new Anim(0), empty: new Anim(1) };
  let timecode = "", caption = "";
  // capture
  let captureWanted = false, captureApplied = false, captureInit = false, captureRect = null;
  const captureAmt = new Anim(0), captureHover = new HoverFade();
  // wipe
  let wipeWanted = false, wipeApplied = false, wipeInit = false, wipeVertical = true, wipeDrag = false, wipeTarget = 0.5, wipeLast = -1, wipeLabel = "";
  const wipeAmt = new Anim(0), wipeLive = new Anim(0.5);
  // angles
  let angleNames = [], angleActive = 0, anglesWanted = false, anglesApplied = false, anglesInit = false, angleSelApplied = -1, angleSnap = true;
  const anglesAmt = new Anim(0), angleSel = new Anim(0), angleHover = new HoverFade();
  let angleRects = [];
  // subtitle
  let subWanted = "", subShown = "", subLeaving = "", subInit = false;
  const subAmt = new Anim(0), subLeaveAmt = new Anim(0);
  // clip warning
  let clipWanted = false, clipApplied = false, clipMask = null, clipFor = null;
  const clipAmt = new Anim(0);
  // zoom / pan
  let zoomTarget = 1, zoomLast = 1, anchored = false, anchorAt = { x: 0, y: 0 }, anchorU = { x: 0.5, y: 0.5 }, centre = { x: 0.5, y: 0.5 };
  let panning = false, panLast = { x: 0, y: 0 };
  const zoom = new Anim(1);

  // ---------------------------------------------------------------- bind from the vm
  own(host, effect(() => {
    const f = vm.monitor.frame.value;
    if (!f || !f.url || (cur && cur.url === f.url)) return;
    const seq = ++pendingSeq, img = new Image();
    img.src = f.url;
    const show = () => {
      if (seq !== pendingSeq) return;              // a newer picture overtook this one
      const next = { url: f.url, w: f.w || img.naturalWidth, h: f.h || img.naturalHeight, png: !!f.png };
      if (f.dissolve && cur) { prev = cur; dissolveWanted = true; } else prev = null;
      cur = next;
      clipFor = null;
      loop.kick();
    };
    (img.decode ? img.decode() : Promise.resolve()).then(show, show);
  }));
  own(host, effect(() => { state = vm.monitor.state.value; loop.kick(); }));
  own(host, effect(() => { timecode = vm.monitor.timecode.value; caption = vm.monitor.caption.value; loop.kick(); }));
  own(host, effect(() => { captureWanted = !!vm.monitor.captureShown.value; loop.kick(); }));
  own(host, effect(() => {
    const w = vm.monitor.wipe.value;
    wipeWanted = !!w.on;
    if (w.on) { wipeVertical = w.vertical; wipeLabel = w.label; if (!wipeDrag) wipeTarget = clamp(w.at, 0, 1); }
    loop.kick();
  }));
  own(host, effect(() => {
    const a = vm.monitor.angles.value, shown = a.shown && a.names.length > 0;
    if (shown) {
      if (JSON.stringify(a.names) !== JSON.stringify(angleNames)) angleSnap = true;
      angleNames = a.names; angleActive = a.active;
    }
    anglesWanted = shown;
    loop.kick();
  }));
  own(host, effect(() => { subWanted = vm.monitor.subtitle.value || ""; loop.kick(); }));
  own(host, effect(() => { clipWanted = !!vm.view.clipWarning.value; loop.kick(); }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));
  own(host, effect(() => {                            // the native shot's zoom
    const fx = vm.view.fixture.value;
    if (fx && fx.monitor && fx.monitor.zoom > 1) { zoomTarget = fx.monitor.zoom; zoom.set(zoomTarget); zoomLast = zoomTarget; loop.kick(); }
  }));
  vm.provide("monitor", { resetZoom: () => { resetZoom(); loop.kick(); } });

  // ---------------------------------------------------------------- geometry (Monitor.cpp:40-105)
  function wantedProxyEdge() {
    const edge = Math.max(W, H) * Math.max(1, zoomTarget);
    return edge <= 640 ? 640 : edge <= 960 ? 960 : edge <= 1280 ? 1280 : edge <= 1920 ? 1920 : edge <= 3840 ? 3840 : 7680;
  }
  function frameRect() {
    const fw0 = W - 2 * INSET, fh0 = H - 2 * INSET;
    if (fw0 <= 0 || fh0 <= 0) return { x: 0, y: 0, w: 0, h: 0 };
    const aspect = cur && cur.w > 0 && cur.h > 0 ? cur.w / cur.h : 16 / 9;
    let fw = fw0, fh = fw0 / aspect;
    if (fh > fh0) { fh = fh0; fw = fh0 * aspect; }
    return { x: INSET + (fw0 - fw) * 0.5, y: INSET + (fh0 - fh) * 0.5, w: fw, h: fh };
  }
  function centreFor(z) {
    const fr = frameRect();
    const c = { ...centre };
    if (anchored && fr.w > 0 && fr.h > 0) {
      c.x = (fr.x + fr.w * 0.5 - anchorAt.x) / (fr.w * z) + anchorU.x;
      c.y = (fr.y + fr.h * 0.5 - anchorAt.y) / (fr.h * z) + anchorU.y;
    }
    const half = 0.5 / Math.max(1, z);
    c.x = clamp(c.x, half, 1 - half);
    c.y = clamp(c.y, half, 1 - half);
    return c;
  }
  function imageRect() {
    const fr = frameRect(), z = Math.max(1, zoom.value), c = centreFor(z);
    return { x: fr.x + fr.w * 0.5 - c.x * fr.w * z, y: fr.y + fr.h * 0.5 - c.y * fr.h * z, w: fr.w * z, h: fr.h * z };
  }
  function zoomAbout(factor, at) {
    const ir = imageRect();
    if (ir.w <= 0 || ir.h <= 0) return;
    anchorU = { x: clamp((at.x - ir.x) / ir.w, 0, 1), y: clamp((at.y - ir.y) / ir.h, 0, 1) };
    anchorAt = at;
    anchored = true;
    zoomTarget = clamp(zoomTarget * factor, 1, MAX_ZOOM);
  }
  function resetZoom() {
    const fr = frameRect(), c = centreFor(Math.max(1, zoom.value));
    anchorAt = { x: fr.x + fr.w * 0.5, y: fr.y + fr.h * 0.5 };
    anchorU = c;
    anchored = true;
    zoomTarget = 1;
  }
  function wipeAtPoint(pt) {
    const fr = frameRect();
    return wipeVertical ? (fr.w > 0 ? clamp((pt.x - fr.x) / fr.w, 0, 1) : 0.5) : (fr.h > 0 ? clamp((pt.y - fr.y) / fr.h, 0, 1) : 0.5);
  }
  function nearWipe(pt) {
    if (wipeAmt.value < 0.5) return false;
    const fr = frameRect();
    if (!inRect(pt, fr)) return false;
    const at = wipeLive.value;
    return wipeVertical ? Math.abs(pt.x - (fr.x + at * fr.w)) <= 8 : Math.abs(pt.y - (fr.y + at * fr.h)) <= 8;
  }
  function wipeGripRect() {
    if (wipeAmt.value < 0.5) return null;
    const fr = frameRect(), at = wipeLive.value;
    return wipeVertical ? { x: fr.x + at * fr.w - 5, y: fr.y + fr.h * 0.5 - 14, w: 10, h: 28 } : { x: fr.x + fr.w * 0.5 - 14, y: fr.y + at * fr.h - 5, w: 28, h: 10 };
  }
  const angleRect = (k) => (k >= 1 && k <= angleRects.length && anglesAmt.value > 0.5 ? angleRects[k - 1] : null);
  function angleAt(pt) { for (let k = 1; k <= angleRects.length; ++k) if (inRect(pt, angleRect(k))) return k; return 0; }

  // ---------------------------------------------------------------- the clip warning's mask (Scopes.cpp clipMaskOf)
  function buildClipMask() {
    if (!cur || clipFor === cur.url) return;
    clipFor = cur.url;
    const img = new Image();
    img.src = cur.url;
    const forUrl = cur.url;
    img.decode().then(() => {
      if (!cur || cur.url !== forUrl) return;
      const c = document.createElement("canvas");
      c.width = img.naturalWidth; c.height = img.naturalHeight;
      const g = c.getContext("2d", { willReadFrequently: true });
      g.drawImage(img, 0, 0);
      const d = g.getImageData(0, 0, c.width, c.height), px = d.data;
      for (let i = 0; i < px.length; i += 4) {
        const r = px[i], gg = px[i + 1], b = px[i + 2];
        if (r === 255 || gg === 255 || b === 255) { px[i] = 255; px[i + 1] = 40; px[i + 2] = 40; px[i + 3] = 220; }
        else if (r === 0 || gg === 0 || b === 0) { px[i] = 40; px[i + 1] = 90; px[i + 2] = 255; px[i + 3] = 220; }
        else px[i + 3] = 0;
      }
      g.putImageData(d, 0, 0);
      c.toBlob((blob) => {
        if (!blob) return;
        if (clipMask) URL.revokeObjectURL(clipMask);
        clipMask = URL.createObjectURL(blob);
        loop.kick();
      });
    }, () => {});
  }
  own(host, () => { if (clipMask) URL.revokeObjectURL(clipMask); });

  // ---------------------------------------------------------------- advance (Monitor::advance)
  function advance(now) {
    nowMs = now;
    let moving = false;
    const ms = (n) => tok.ms(n);
    if (!captureInit) { captureAmt.set(captureWanted ? 1 : 0); captureApplied = captureWanted; captureInit = true; }
    if (captureWanted !== captureApplied) { captureAmt.animateTo(captureWanted ? 1 : 0, ms("motion-cross-fade"), Ease.EaseOutCubic, now); captureApplied = captureWanted; }
    moving = captureAmt.update(now) || moving;
    moving = captureHover.advance(now) || moving;
    if (!wipeInit) { wipeAmt.set(wipeWanted ? 1 : 0); wipeApplied = wipeWanted; wipeLive.set(wipeTarget); wipeLast = wipeTarget; wipeInit = true; }
    if (wipeWanted !== wipeApplied) {
      wipeAmt.animateTo(wipeWanted ? 1 : 0, ms("motion-cross-fade"), Ease.EaseOutCubic, now);
      if (wipeWanted && wipeAmt.value < 0.01) { wipeLive.set(wipeTarget); wipeLast = wipeTarget; }
      wipeApplied = wipeWanted;
    }
    moving = wipeAmt.update(now) || moving;
    if (!wipeDrag && Math.abs(wipeTarget - wipeLast) > 1e-9) { wipeLive.animateTo(wipeTarget, ms("motion-scroll"), Ease.EaseOutCubic, now); wipeLast = wipeTarget; }
    moving = wipeLive.update(now) || moving;
    if (!subInit) { subShown = subWanted; subAmt.set(subWanted ? 1 : 0); subInit = true; }
    else if (subWanted !== subShown) {
      subLeaving = subShown;
      subLeaveAmt.set(subAmt.value);
      subLeaveAmt.animateTo(0, ms("motion-hover"), Ease.EaseOutCubic, now);
      subShown = subWanted;
      subAmt.set(0);
      if (subShown) subAmt.animateTo(1, ms("motion-hover"), Ease.EaseOutCubic, now);
    }
    moving = subAmt.update(now) || moving;
    moving = subLeaveAmt.update(now) || moving;
    if (!anglesInit) { anglesAmt.set(anglesWanted ? 1 : 0); anglesApplied = anglesWanted; anglesInit = true; }
    if (anglesWanted !== anglesApplied) {
      anglesAmt.animateTo(anglesWanted ? 1 : 0, ms("motion-cross-fade"), Ease.EaseOutCubic, now);
      if (anglesWanted && anglesAmt.value < 0.01) angleSnap = true;
      anglesApplied = anglesWanted;
    }
    moving = anglesAmt.update(now) || moving;
    const sel = Math.max(0, angleActive - 1);
    if (angleSnap) { angleSel.set(sel); angleSelApplied = sel; angleSnap = false; }
    else if (sel !== angleSelApplied) { angleSel.animateTo(sel, 220, Ease.EaseOutCubic, now); angleSelApplied = sel; }
    moving = angleSel.update(now) || moving;
    moving = angleHover.advance(now) || moving;
    if (!stateInit) { for (const s of STATES) stateAmt[s].set(s === state ? 1 : 0); stateApplied = state; stateInit = true; }
    else if (state !== stateApplied) { for (const s of STATES) stateAmt[s].animateTo(s === state ? 1 : 0, ms("motion-scroll"), Ease.EaseOutCubic, now); stateApplied = state; }
    for (const s of STATES) moving = stateAmt[s].update(now) || moving;
    if (dissolveWanted) { dissolve.set(0); dissolve.animateTo(1, ms("motion-dissolve"), Ease.Linear, now); dissolveWanted = false; }
    moving = dissolve.update(now) || moving;
    if (prev && !dissolve.animating) prev = null;
    if (zoomTarget !== zoomLast) { zoom.animateTo(zoomTarget, ms("motion-catch-up"), Ease.EaseOutCubic, now); zoomLast = zoomTarget; }
    moving = zoom.update(now) || moving;
    if (clipWanted !== clipApplied) { clipAmt.animateTo(clipWanted ? 1 : 0, ms("motion-cross-fade"), Ease.EaseOutCubic, now); clipApplied = clipWanted; }
    moving = clipAmt.update(now) || moving;
    if (clipWanted) buildClipMask();
    const edge = wantedProxyEdge();
    if (vm.view.monitorEdge.peek() !== edge) vm.view.monitorEdge.value = edge;
    return moving || stateAmt.loading.value > 0.001;   // the spinner turns while decoding shows
  }

  // ---------------------------------------------------------------- paint (Monitor::onPaint)
  function paint() {
    const fg = col("foreground"), muted = col("muted-foreground"), rc = tok.px("radius-control");
    const sr = tok.raw("scrim-rgb").split(",").map((v) => +v / 255);
    const scrim = (a) => [sr[0], sr[1], sr[2], a];                   // surface::scrim(a)
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: col("monitor-bg") });
    const fr = frameRect(), frameA = stateAmt.frame.value;
    if (fr.w > 0 && fr.h > 0) {
      p.rect(fr.x, fr.y, fr.w, fr.h, 0, { fill: "#050505" });
      if (frameA > 0.001 && cur) {
        const ir = imageRect();
        p.clip(fr.x, fr.y, fr.w, fr.h);
        p.group(frameA);
        if (prev && dissolve.value < 1) {
          p.image(prev.url, ir.x, ir.y, ir.w, ir.h);
          p.image(cur.url, ir.x, ir.y, ir.w, ir.h, dissolve.value);
        } else p.image(cur.url, ir.x, ir.y, ir.w, ir.h);
        const ca = clipAmt.value;
        if (ca > 0.001 && clipMask) p.image(clipMask, ir.x, ir.y, ir.w, ir.h, ca);
        p.restore();
        p.restore();
      }
    }
    const cy = fr.h > 0 ? fr.y + fr.h * 0.5 : H * 0.5;
    const la = stateAmt.loading.value;
    if (la > 0.001) {
      p.add(glyph.spinner(W * 0.5, cy - 14, 9, nowMs, la));
      const s = "decoding · " + proxyLabel(wantedProxyEdge());
      p.text(s, (W - measure(s, 11, F.sans)) * 0.5, cy + 16, 11, F.sans, fade(muted, la));
    }
    const ea = stateAmt.empty.value;
    if (ea > 0.001) {
      p.add(glyph.film({ x: W * 0.5 - 11, y: cy - 26, w: 22, h: 18 }, css(fade(muted, 0.7 * ea)), 1.2));
      const s = "no clip at the playhead";
      p.text(s, (W - measure(s, 12, F.sans)) * 0.5, cy + 10, 12, F.sans, fade(muted, ea));
    }
    if (timecode) {
      const tw = measure(timecode, CHIP_PX, F.mono), chip = { x: INSET + 6, y: INSET + 6, w: tw + 12, h: 18 };
      p.rect(chip.x, chip.y, chip.w, chip.h, rc, { fill: scrim(0.72) });
      p.text(timecode, chip.x + 6, baseline(chip.y + chip.h * 0.5, CHIP_PX), CHIP_PX, F.mono, fg);
    }
    let capLeft = W - INSET - 6;
    if (caption) {
      const cap = ellipsize(caption, Math.max(0, W * 0.5 - 48), CHIP_PX, F.sans), tw = measure(cap, CHIP_PX, F.sans);
      if (cap) {
        const chip = { x: W - INSET - 6 - tw - 12, y: INSET + 6, w: tw + 12, h: 18 };
        p.rect(chip.x, chip.y, chip.w, chip.h, rc, { fill: scrim(0.72) });
        p.text(cap, chip.x + 6, baseline(chip.y + chip.h * 0.5, CHIP_PX), CHIP_PX, F.sans, muted);
        capLeft = chip.x - 4;
      }
    }
    const zl = zoom.value, za = clamp((zl - 1) * 5, 0, 1);
    if (za > 0.001 && fr.w > 0) {
      const z = Math.round(zl * 100) + "%", tw = measure(z, CHIP_PX, F.mono);
      const chip = { x: fr.x + fr.w - 8 - tw - 12, y: fr.y + fr.h - 8 - 18, w: tw + 12, h: 18 };
      p.rect(chip.x, chip.y, chip.w, chip.h, rc, { fill: fade(scrim(0.72), za) });
      p.text(z, chip.x + 6, baseline(chip.y + chip.h * 0.5, CHIP_PX), CHIP_PX, F.mono, fade(fg, za));
    }
    // R-CLR-5: the wipe
    const wa = wipeAmt.value;
    if (wa > 0.001 && fr.w > 0 && fr.h > 0) {
      const at = wipeLive.value, line = fade(col("white"), 0.85 * wa);
      p.clip(fr.x, fr.y, fr.w, fr.h);
      if (wipeVertical) p.line(fr.x + at * fr.w, fr.y, fr.x + at * fr.w, fr.y + fr.h, line, 1.5);
      else p.line(fr.x, fr.y + at * fr.h, fr.x + fr.w, fr.y + at * fr.h, line, 1.5);
      const g = wipeGripRect();
      if (g) p.rect(g.x, g.y, g.w, g.h, tok.px("radius-pill"), { fill: fade(scrim(0.72), wa), stroke: line, sw: 1 });
      const b = "B · " + wipeLabel, bw = measure(b, CHIP_PX, F.sans) + 12;
      const ca = { x: fr.x + 8, y: fr.y + fr.h - 26, w: measure("A", CHIP_PX, F.sans) + 12, h: 18 };
      const cb = { x: fr.x + fr.w - 8 - bw, y: fr.y + fr.h - 26, w: bw, h: 18 };
      const cA = wipeVertical ? ca : { x: fr.x + 8, y: fr.y + 8, w: ca.w, h: 18 };
      for (const [r, txt] of [[cA, "A"], [cb, b]]) {
        p.rect(r.x, r.y, r.w, r.h, rc, { fill: fade(scrim(0.72), wa) });
        p.text(txt, r.x + 6, baseline(r.y + r.h * 0.5, CHIP_PX), CHIP_PX, F.sans, fade(fg, wa));
      }
      p.restore();
    }
    // R-EDT-5: the angle bar
    const aa = anglesAmt.value;
    angleRects = [];
    if (aa > 0.001 && fr.w > 40 && angleNames.length) {
      const n = angleNames.length;
      let cw = 0;
      for (let k = 0; k < n; ++k) cw = Math.max(cw, measure(String(k + 1), CHIP_PX, F.mono) + 6 + measure(angleNames[k], CHIP_PX, F.sans) + 16);
      const gap = 4, room = fr.w - 16;
      cw = Math.max(28, Math.min(cw, (room - gap * (n - 1)) / n));
      const total = cw * n + gap * (n - 1), hh = 22, x0 = fr.x + (fr.w - total) * 0.5, y = fr.y + fr.h - 8 - hh;
      p.rect(x0 - 4, y - 4, total + 8, hh + 8, rc, { fill: fade(scrim(0.72), aa) });
      const hs = clamp(angleSel.value, 0, n - 1);
      p.rect(x0 + hs * (cw + gap), y, cw, hh, rc, { fill: primaryA(0.85 * aa) });
      for (let k = 0; k < n; ++k) {
        const r = { x: x0 + k * (cw + gap), y, w: cw, h: hh };
        angleRects.push(r);
        const hv = angleHover.amount(k);
        if (hv > 0.001) p.rect(r.x, r.y, r.w, r.h, rc, { fill: hoverWash(hv * aa) });
        const on = clamp(1 - Math.abs(angleSel.value - k), 0, 1), nm = String(k + 1), nw = measure(nm, CHIP_PX, F.mono), by = baseline(r.y + r.h * 0.5, CHIP_PX);
        p.text(nm, r.x + 8, by, CHIP_PX, F.mono, fade(lerp(muted, col("primary-foreground"), on), aa));
        p.text(ellipsize(angleNames[k], Math.max(0, r.w - nw - 22), CHIP_PX, F.sans), r.x + 8 + nw + 6, by, CHIP_PX, F.sans, fade(lerp(fg, col("primary-foreground"), on), aa));
      }
    }
    // R-DLV-1: the caption under the playhead, as a render burns it
    const drawSubtitle = (text, a) => {
      if (a <= 0.001 || !text || fr.w < 40 || fr.h < 24) return;
      const px = Math.max(10, fr.h * 0.045), pad = Math.round(px * 0.45), bh = Math.ceil(px * 1.5);
      const lines = text.split("\n").filter((l) => l);
      let y = Math.min(fr.y + fr.h * 0.92, fr.y + fr.h - 8 - aa * 34);
      for (let i = lines.length - 1; i >= 0; --i) {
        const s = ellipsize(lines[i], Math.max(0, fr.w * 0.9 - 2 * pad), px, F.sans), tw = measure(s, px, F.sans);
        const plate = { x: fr.x + (fr.w - tw) * 0.5 - pad, y: y - bh, w: tw + 2 * pad, h: bh };
        p.rect(plate.x, plate.y, plate.w, plate.h, rc, { fill: scrim(0.62 * a) });
        p.text(s, plate.x + pad, baseline(plate.y + bh * 0.5, px), px, F.sans, fade(col("white"), a));
        y -= bh + 2;
      }
    };
    drawSubtitle(subLeaving, subLeaveAmt.value);
    drawSubtitle(subShown, subAmt.value);
    // the capture button, left of the caption (Grade - R-UI-11)
    const ca = captureAmt.value;
    captureRect = null;
    if (ca > 0.001) {
      const b = { x: capLeft - 18, y: INSET + 6, w: 18, h: 18 }, hv = captureHover.amount(0);
      p.rect(b.x, b.y, b.w, b.h, rc, { fill: lerp(scrim(0.72), whiteA(0.18), hv * 0.6) });
      p.add(glyph.camera({ x: b.x + 3.5, y: b.y + 3.5, w: b.w - 7, h: b.h - 7 }, css(fade(lerp(muted, col("white"), hv), ca))));
      if (ca > 0.5) captureRect = b;
    }
    p.end();
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    const more = advance(now);
    paint();
    return more;
  }

  // ---------------------------------------------------------------- input (Monitor::handleGesture)
  let downAt = null;
  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e);
    if (wipeDrag) {
      wipeTarget = wipeAtPoint(pt); wipeLive.set(wipeTarget); wipeLast = wipeTarget;
      vm.notify("view wipe --at " + num(wipeTarget));
    } else if (panning) {
      const ir = imageRect();
      if (ir.w > 0 && ir.h > 0) {
        centre = { x: centre.x - (pt.x - panLast.x) / ir.w, y: centre.y - (pt.y - panLast.y) / ir.h };
        centre = centreFor(Math.max(1, zoom.value));
      }
      panLast = pt;
    } else {
      captureHover.setHovered(captureRect && inRect(pt, captureRect) ? 0 : -1);
      angleHover.setHovered(angleAt(pt) - 1);
      host.style.cursor = nearWipe(pt) ? (wipeVertical ? "ew-resize" : "ns-resize") : zoomTarget > 1 + 1e-9 && inRect(pt, frameRect()) ? "grab" : "default";
    }
    loop.kick();
  });
  host.addEventListener("pointerleave", () => { captureHover.clear(); angleHover.clear(); loop.kick(); });
  host.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    const pt = localPoint(host, e);
    downAt = pt;
    if (nearWipe(pt)) {
      wipeDrag = true;
      wipeTarget = wipeAtPoint(pt); wipeLive.set(wipeTarget); wipeLast = wipeTarget;
      vm.notify("view wipe --at " + num(wipeTarget));
      host.setPointerCapture(e.pointerId);
    } else if (zoomTarget > 1 + 1e-9 && inRect(pt, frameRect()) && !(captureRect && inRect(pt, captureRect)) && !angleAt(pt)) {
      centre = centreFor(Math.max(1, zoom.value));
      anchored = false;
      panning = true;
      panLast = pt;
      host.setPointerCapture(e.pointerId);
    }
    loop.kick();
  });
  const up = () => {
    if (wipeDrag) vm.cmd("view wipe --at " + num(wipeTarget));   // the last split, said once with a reply
    panning = false; wipeDrag = false;
  };
  host.addEventListener("pointerup", up);
  host.addEventListener("pointercancel", up);
  host.addEventListener("click", (e) => {
    const pt = localPoint(host, e);
    if (downAt && Math.hypot(pt.x - downAt.x, pt.y - downAt.y) > 4) return;   // a drag, not a click
    const k = angleAt(pt);
    if (k) { vm.cmd("multicam angle " + k); return; }
    if (captureRect && inRect(pt, captureRect)) {
      const b = ctx.box.peek();
      openCaptureMenu(vm, ctx, { x: b.x + captureRect.x, y: b.y + captureRect.y, w: captureRect.w, h: captureRect.h });
    }
  });
  host.addEventListener("dblclick", (e) => {
    const pt = localPoint(host, e);
    if (inRect(pt, frameRect()) && !(captureRect && inRect(pt, captureRect)) && !angleAt(pt)) { resetZoom(); loop.kick(); }
  });
  host.addEventListener("wheel", (e) => {
    if (!e.ctrlKey) return;                        // cosmo's R-ZOOM-1: Ctrl + wheel; a plain wheel does nothing here
    e.preventDefault();
    zoomAbout(Math.pow(ZOOM_NOTCH, -wheelPx(e) / (SHELL.wheelNotchPx || 40)), localPoint(host, e));
    loop.kick();
  }, { passive: false });
  loop.kick();
}
