/*
 * Interstellar — edit/scopes.js: the Grade column's SCOPES (R-UI-15). Ports app/widgets/ScopePanel.{h,cpp}
 * and Scopes.{h,cpp} (scopesOf), wired as App::showScopes does.
 *
 * A header of modes — Histogram · Waveform · Parade · Vector — and a CLIP switch; the body; and a
 * readout line in words: ▲ clipped at white, ▼ crushed at black, levels used of 256, the preview's depth.
 * The body is computed here from the monitor's own picture (vm.monitor.frame): drawn to an offscreen
 * canvas, read with getImageData, and run through scopesOf's maths (log-scaled densities, BT.709 luma,
 * the parade per channel, the RGB waveform additive, Cb/Cr for the vectorscope). The Histogram mode
 * draws R, G, B as translucent areas and luma as a line (sqrt-scaled); the other modes stretch their
 * rasters over the body with their graticules. A mode change cross-fades (motion-cross-fade, EaseOutCubic),
 * the waveform's Luma | RGB switch cross-fades, the CLIP switch's look eases (motion-hover), the tab
 * underline follows the selection (motion-select), hover washes fade (HoverFade).
 *
 * onPaint (the body) is a <canvas>; onOverlay (the header, the switch, the readout) is an SVG painted
 * over it, as the native's overlay pass. While mounted, view.lossless = true (the monitor asks for the
 * exact PNG pixels at rest); off on unmount. The CLIP switch writes view.clipWarning (the monitor draws
 * the mask). The native shot's view (view.fixture.grade.scopeMode / scopeWaveRgb) is honoured.
 */
import { own } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { Painter, canvasSvg, col, css, fade, lerp, whiteA, hoverWash, primaryA, Anim, HoverFade, frameLoop,
         localPoint, inRect, Ease, F } from "../kit/paint.js";

// ScopePanel.h / .cpp
const HEADER_H = 24, READOUT_H = 20, PAD_X = 9.75;
const MODES = ["Histogram", "Waveform", "Parade", "Vector"];
const MODE_W = [62, 60, 46, 44];                    // measured once for 9.5 px sans + padding
const HISTOGRAM = 0, WAVEFORM = 1, PARADE = 2, VECTOR = 3;
const CLIP_ID = 10, LUMA_ID = 11, RGB_ID = 12;
// Scopes.h
const SW = 256, SH = 128, PW = 128, VN = 128, BINS = 256;

// ------------------------------------------------------------------ scopesOf (Scopes.cpp), over RGBA bytes
function level(n, logMax) {
  if (n === 0 || logMax <= 0) return 0;
  const v = Math.log1p(n) / logMax;
  return Math.max(0, Math.min(255, Math.round(40 + 215 * Math.pow(v, 0.7))));
}
function paintRaster(out, w, h, cnt, r, g, b, x0 = 0, outW = w) {
  let mx = 0;
  for (let i = 0; i < cnt.length; ++i) if (cnt[i] > mx) mx = cnt[i];
  const lm = Math.log1p(mx);
  for (let y = 0; y < h; ++y)
    for (let x = 0; x < w; ++x) {
      const a = level(cnt[y * w + x], lm), o = (y * outW + x0 + x) * 4;
      out[o] = (r * a / 255) | 0; out[o + 1] = (g * a / 255) | 0; out[o + 2] = (b * a / 255) | 0; out[o + 3] = a;
    }
}
const lround = (v) => Math.sign(v) * Math.round(Math.abs(v));   // std::lround: half away from zero
export function scopesOf(px, fw, fh) {
  const d = { valid: false };
  const n = fw * fh;
  if (!n) return d;
  d.valid = true; d.width = fw; d.height = fh;
  const wave = new Uint32Array(SW * SH), vec = new Uint32Array(VN * VN);
  const par = [new Uint32Array(PW * SH), new Uint32Array(PW * SH), new Uint32Array(PW * SH)];
  const rgb = [new Uint32Array(SW * SH), new Uint32Array(SW * SH), new Uint32Array(SW * SH)];
  const hist = { r: new Uint32Array(BINS), g: new Uint32Array(BINS), b: new Uint32Array(BINS), lum: new Uint32Array(BINS), maxCount: 0 };
  const used = [new Uint8Array(256), new Uint8Array(256), new Uint8Array(256)];
  const hi = [0, 0, 0], lo = [0, 0, 0];
  const step = Math.max(1, Math.floor(n / 120000));   // the scopes sample; the counts see every pixel
  for (let i = 0; i < n; ++i) {
    const o = i * 4, R = px[o], G = px[o + 1], B = px[o + 2];
    used[0][R] = 1; used[1][G] = 1; used[2][B] = 1;
    if (R === 255) hi[0]++; else if (R === 0) lo[0]++;
    if (G === 255) hi[1]++; else if (G === 0) lo[1]++;
    if (B === 255) hi[2]++; else if (B === 0) lo[2]++;
    if (i % step) continue;
    const x = i % fw;
    const y709 = 0.2126 * R + 0.7152 * G + 0.0722 * B;
    const lum = Math.max(0, Math.min(255, lround(y709)));
    hist.r[R]++; hist.g[G]++; hist.b[B]++; hist.lum[lum]++;
    const col = Math.floor(x * SW / fw), row = Math.floor((255 - lum) * (SH - 1) / 255);
    wave[row * SW + col]++;
    const pcol = Math.floor(x * PW / fw);
    const rr = Math.floor((255 - R) * (SH - 1) / 255), rg = Math.floor((255 - G) * (SH - 1) / 255), rb = Math.floor((255 - B) * (SH - 1) / 255);
    par[0][rr * PW + pcol]++; par[1][rg * PW + pcol]++; par[2][rb * PW + pcol]++;
    rgb[0][rr * SW + col]++; rgb[1][rg * SW + col]++; rgb[2][rb * SW + col]++;
    // chroma: Cb right, Cr up — red sits upper-left, as on any vectorscope
    const cb = (B - y709) / (1.8556 * 255), cr = (R - y709) / (1.5748 * 255);
    const vx = Math.max(0, Math.min(VN - 1, lround((cb + 0.5) * (VN - 1))));
    const vy = Math.max(0, Math.min(VN - 1, lround((0.5 - cr) * (VN - 1))));
    vec[vy * VN + vx]++;
  }
  for (let b = 0; b < BINS; ++b) hist.maxCount = Math.max(hist.maxCount, hist.r[b], hist.g[b], hist.b[b], hist.lum[b]);
  d.hist = hist;
  d.clipHi = hi.map((v) => 100 * v / n);
  d.clipLo = lo.map((v) => 100 * v / n);
  d.levels = used.map((u) => u.reduce((s, v) => s + v, 0));
  d.waveform = new Uint8ClampedArray(SW * SH * 4);
  paintRaster(d.waveform, SW, SH, wave, 225, 236, 228);
  {
    // additive: each channel's density lights its own primary, so agreement reads white
    let mx = 0;
    for (const c of rgb) for (let i = 0; i < c.length; ++i) if (c[i] > mx) mx = c[i];
    const lm = Math.log1p(mx);
    d.waveformRgb = new Uint8ClampedArray(SW * SH * 4);
    for (let i = 0; i < SW * SH; ++i) {
      const o = i * 4, a = level(rgb[0][i], lm), b = level(rgb[1][i], lm), c = level(rgb[2][i], lm);
      d.waveformRgb[o] = a; d.waveformRgb[o + 1] = b; d.waveformRgb[o + 2] = c; d.waveformRgb[o + 3] = Math.max(a, b, c);
    }
  }
  d.parade = new Uint8ClampedArray(3 * PW * SH * 4);
  paintRaster(d.parade, PW, SH, par[0], 236, 92, 92, 0, 3 * PW);
  paintRaster(d.parade, PW, SH, par[1], 104, 214, 112, PW, 3 * PW);
  paintRaster(d.parade, PW, SH, par[2], 110, 152, 246, 2 * PW, 3 * PW);
  d.vector = new Uint8ClampedArray(VN * VN * 4);
  paintRaster(d.vector, VN, VN, vec, 232, 232, 232);
  return d;
}
const clipHiAny = (d) => Math.max(...d.clipHi);
const clipLoAny = (d) => Math.max(...d.clipLo);
const levelsUsed = (d) => Math.min(...d.levels);

/** a raster as an offscreen canvas (ImageSlot) */
function rasterCanvas(bytes, w, h) {
  const c = document.createElement("canvas");
  c.width = w; c.height = h;
  c.getContext("2d").putImageData(new ImageData(bytes, w, h), 0, 0);
  return c;
}

export function mount(host, vm, ctx) {
  useCss("css/edit/scopes.css");
  host.classList.add("sp");
  const cv = document.createElement("canvas");
  cv.className = "sp-body";
  const svg = canvasSvg();
  host.append(cv, svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  let W = 0, H = 0;
  let data = { valid: false }, images = null, sourceBits = 0;
  let mode = HISTOGRAM, modeApplied = -1, rgbOn = false, rgbApplied = false, clipOn = false, clipApplied = false, hoverInside = false;
  const modeAmt = MODES.map((_, m) => new Anim(m === mode ? 1 : 0));
  const rgbAmt = new Anim(0), clipAmt = new Anim(0);
  const hover = new HoverFade(), sel = new HoverFade();
  sel.setHovered(mode); sel.amt = new Map([[mode, 1]]);   // the panel exists from the start natively: settled
  let clipInit = false;

  // ---------------------------------------------------------------- lossless while shown (App::showScopes reads the exact pixels)
  vm.view.lossless.value = true;
  own(host, () => { vm.view.lossless.value = false; });

  // ---------------------------------------------------------------- bind from the vm
  let pending = 0, measuredUrl = null;
  own(host, effect(() => {
    const f = vm.monitor.frame.value;
    if (!f || !f.url) { if (!f) { data = { valid: false }; images = null; measuredUrl = null; loop.kick(); } return; }
    if (f.url === measuredUrl) return;
    const seq = ++pending, img = new Image();
    img.src = f.url;
    (img.decode ? img.decode() : Promise.resolve()).then(() => {
      if (seq !== pending) return;                  // a newer picture overtook this one
      const w = img.naturalWidth, h = img.naturalHeight;
      if (!w || !h) return;
      const c = document.createElement("canvas");
      c.width = w; c.height = h;
      const g = c.getContext("2d", { willReadFrequently: true });
      g.drawImage(img, 0, 0);
      setData(scopesOf(g.getImageData(0, 0, w, h).data, w, h));
      measuredUrl = f.url;
      loop.kick();
    }, () => {});
  }));
  own(host, effect(() => { clipOn = !!vm.view.clipWarning.value; loop.kick(); }));
  own(host, effect(() => {                          // the native shot's view (tests only)
    const fx = vm.view.fixture.value, gr = fx && fx.grade;
    if (!gr) return;
    if (gr.scopeMode >= 0 && gr.scopeMode < MODES.length && gr.scopeMode !== mode) {
      mode = gr.scopeMode; modeApplied = mode; sel.setHovered(mode); sel.amt = new Map([[mode, 1]]);
      modeAmt.forEach((a, m) => a.set(m === mode ? 1 : 0));
    }
    if (typeof gr.scopeWaveRgb === "boolean") { rgbOn = gr.scopeWaveRgb; rgbApplied = rgbOn; rgbAmt.set(rgbOn ? 1 : 0); }
    loop.kick();
  }));
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; loop.kick(); }));

  function setData(d) {
    data = d;
    if (!d.valid) { images = null; return; }
    images = {
      wave: rasterCanvas(d.waveform, SW, SH), waveRgb: rasterCanvas(d.waveformRgb, SW, SH),
      parade: rasterCanvas(d.parade, 3 * PW, SH), vector: rasterCanvas(d.vector, VN, VN),
    };
  }

  // ---------------------------------------------------------------- geometry (ScopePanel.cpp)
  function modeRect(m) {
    let x = PAD_X - 4;
    for (let i = 0; i < m; ++i) x += MODE_W[i];
    return { x, y: 3, w: MODE_W[m], h: HEADER_H - 6 };
  }
  const clipRect = () => ({ x: W - PAD_X - 44, y: 3, w: 44, h: HEADER_H - 6 });
  const bodyRect = () => ({ x: PAD_X, y: HEADER_H + 2, w: Math.max(0, W - 2 * PAD_X), h: Math.max(0, H - HEADER_H - READOUT_H - 4) });
  function waveSwitchRect(i) {
    const b = bodyRect(), w0 = 30, w1 = 30, h = 15, x = b.x + b.w - 4 - w0 - w1;
    return i === 0 ? { x, y: b.y + 4, w: w0, h } : { x: x + w0, y: b.y + 4, w: w1, h };
  }
  function readout() {
    if (!data.valid) return "no frame";
    let s = `▲ ${clipHiAny(data).toFixed(1)}%  ▼ ${clipLoAny(data).toFixed(1)}%  ·  levels ${levelsUsed(data)}/256`;
    s += sourceBits > 0 ? `  ·  ${sourceBits}-bit source, 8-bit preview` : "  ·  8-bit preview";
    return s;
  }

  // ---------------------------------------------------------------- advance (ScopePanel::advance)
  function advance(now) {
    let moving = false;
    if (modeApplied < 0) modeApplied = mode;
    else if (modeApplied !== mode) {
      modeAmt.forEach((a, m) => a.animateTo(m === mode ? 1 : 0, tok.ms("motion-cross-fade"), Ease.EaseOutCubic, now));
      modeApplied = mode;
    }
    for (const a of modeAmt) moving = a.update(now) || moving;
    if (!clipInit) { clipAmt.set(clipOn ? 1 : 0); clipApplied = clipOn; clipInit = true; }
    if (clipOn !== clipApplied) { clipAmt.animateTo(clipOn ? 1 : 0, tok.ms("motion-hover"), Ease.EaseOutCubic, now); clipApplied = clipOn; }
    moving = clipAmt.update(now) || moving;
    if (rgbOn !== rgbApplied) { rgbAmt.animateTo(rgbOn ? 1 : 0, tok.ms("motion-cross-fade"), Ease.EaseOutCubic, now); rgbApplied = rgbOn; }
    moving = rgbAmt.update(now) || moving;
    if (!hoverInside) hover.clear();
    moving = hover.advance(now) || moving;
    moving = sel.advance(now, tok.ms("motion-select")) || moving;
    return moving;
  }

  // ---------------------------------------------------------------- onPaint: the body, on the canvas
  function paintBody() {
    const k = (window.devicePixelRatio || 1) * ((ctx.scale && ctx.scale()) || 1);
    const bw = Math.max(1, Math.round(W * k)), bh = Math.max(1, Math.round(H * k));
    if (cv.width !== bw || cv.height !== bh) { cv.width = bw; cv.height = bh; }
    cv.style.width = W + "px"; cv.style.height = H + "px";
    const t = cv.getContext("2d");
    t.setTransform(k, 0, 0, k, 0, 0);
    t.clearRect(0, 0, W, H);
    t.globalAlpha = 1;
    t.fillStyle = css(col("histogram-bg"));
    t.fillRect(0, 0, W, H);
    const b = bodyRect();
    const line = (x0, y0, x1, y1, c) => {
      t.strokeStyle = css(c); t.lineWidth = 1;
      t.beginPath(); t.moveTo(x0, y0); t.lineTo(x1, y1); t.stroke();
    };
    const grid = (a) => {
      for (let i = 0; i <= 4; ++i) {
        const y = b.y + b.h * i / 4;
        line(b.x, y, b.x + b.w, y, fade(whiteA(i === 0 || i === 4 ? 0.16 : 0.08), a));
      }
    };
    const image = (c, r, a) => {
      t.save(); t.globalAlpha = Math.max(0, Math.min(1, a)); t.imageSmoothingEnabled = true;
      t.drawImage(c, r.x, r.y, r.w, r.h); t.restore();
    };
    // the histogram: R, G, B as translucent areas in cosmo's channel colours, luma as a line
    let a = modeAmt[HISTOGRAM].value;
    if (a > 0.001 && data.valid && data.hist.maxCount > 0) {
      grid(a);
      const mx = Math.sqrt(data.hist.maxCount);   // sqrt: the tails stay visible
      const area = (bins, c, fill) => {
        t.beginPath();
        t.moveTo(b.x, b.y + b.h);
        for (let i = 0; i < BINS; ++i) t.lineTo(b.x + b.w * i / (BINS - 1), b.y + b.h - b.h * Math.sqrt(bins[i]) / mx);
        t.lineTo(b.x + b.w, b.y + b.h);
        t.closePath();
        if (fill) { t.fillStyle = css(fade(c, 0.32 * a)); t.fill(); }
        else { t.strokeStyle = css(fade(c, 0.55 * a)); t.lineWidth = 1; t.stroke(); }
      };
      area(data.hist.r, [230 / 255, 82 / 255, 82 / 255, 1], true);
      area(data.hist.g, [97 / 255, 204 / 255, 107 / 255, 1], true);
      area(data.hist.b, [107 / 255, 148 / 255, 245 / 255, 1], true);
      area(data.hist.lum, col("white"), false);
    }
    // waveform and parade: the rasters stretched over the body, graticule under them
    a = modeAmt[WAVEFORM].value;
    if (a > 0.001) {
      grid(a);
      const rgb = rgbAmt.value;   // Luma and RGB cross-fade into each other (R-UI-15, amended)
      if (images && rgb < 0.999) image(images.wave, b, a * (1 - rgb));
      if (images && rgb > 0.001) image(images.waveRgb, b, a * rgb);
    }
    a = modeAmt[PARADE].value;
    if (a > 0.001) {
      grid(a);
      for (let i = 1; i < 3; ++i) line(b.x + b.w * i / 3, b.y, b.x + b.w * i / 3, b.y + b.h, fade(col("border"), a));
      if (images) image(images.parade, b, a);
    }
    a = modeAmt[VECTOR].value;
    if (a > 0.001) {
      // a square, centred: the 100 % circle, the 75 % targets, the skin-tone line
      const side = Math.min(b.w, b.h);
      const sq = { x: b.x + (b.w - side) * 0.5, y: b.y + (b.h - side) * 0.5, w: side, h: side };
      const cx = sq.x + side * 0.5, cy = sq.y + side * 0.5, r = side * 0.5;
      t.strokeStyle = css(fade(whiteA(0.14), a)); t.lineWidth = 1;
      t.beginPath();
      for (let i = 0; i <= 64; ++i) {
        const th = 2 * Math.PI * i / 64, x = cx + r * Math.cos(th), y = cy + r * Math.sin(th);
        if (i === 0) t.moveTo(x, y); else t.lineTo(x, y);
      }
      t.stroke();
      line(sq.x, cy, sq.x + side, cy, fade(whiteA(0.07), a));
      line(cx, sq.y, cx, sq.y + side, fade(whiteA(0.07), a));
      if (images) image(images.vector, sq, a);
      // the six 75 % targets, where pure R G B C M Y of 75 % land
      const T = [[0.75, 0, 0, "R"], [0.75, 0.75, 0, "Yl"], [0, 0.75, 0, "G"], [0, 0.75, 0.75, "Cy"], [0, 0, 0.75, "B"], [0.75, 0, 0.75, "Mg"]];
      t.font = `7.5px "${F.sans}"`;
      for (const [tr, tg, tb, nm] of T) {
        const y = 0.2126 * tr + 0.7152 * tg + 0.0722 * tb;
        const cb = (tb - y) / 1.8556, cr = (tr - y) / 1.5748;
        const px = cx + cb * 2 * r, py = cy - cr * 2 * r;
        t.strokeStyle = css(fade(whiteA(0.45), a)); t.lineWidth = 1;
        t.beginPath(); t.roundRect ? t.roundRect(px - 3, py - 3, 6, 6, tok.px("radius-hairline")) : t.rect(px - 3, py - 3, 6, 6); t.stroke();
        t.fillStyle = css(fade(col("muted-foreground"), a));
        t.fillText(nm, px + 5, py + 3);
      }
      // the skin-tone ("I") line: about 123° from +Cb, counter-clockwise
      const th = 123 * Math.PI / 180;
      line(cx, cy, cx + r * Math.cos(th), cy - r * Math.sin(th), fade(primaryA(0.6), a));
    }
    if (!data.valid) {
      const s = "no frame to measure";
      t.font = `10px "${F.sans}"`;
      t.fillStyle = css(col("muted-foreground"));
      t.fillText(s, b.x + (b.w - measure(s, 10, F.sans)) * 0.5, b.y + b.h * 0.5 + 3.5);
    }
  }

  // ---------------------------------------------------------------- onOverlay: header, switch, readout (SVG)
  function paintOverlay() {
    p.begin();
    const muted = col("muted-foreground"), fg = col("foreground"), border = col("border");
    const rc = tok.px("radius-control"), rh = tok.px("radius-hairline");
    p.rect(0, 0, W, HEADER_H, 0, { fill: col("histogram-bg") });
    for (let m = 0; m < MODES.length; ++m) {
      const r = modeRect(m), s = sel.amount(m), hv = hover.amount(m);
      if (hv > 0.001) p.rect(r.x, r.y, r.w, r.h, rc, { fill: hoverWash(hv) });
      const tw = measure(MODES[m], 9.5, F.medium);
      p.text(MODES[m], r.x + (r.w - tw) * 0.5, baseline(r.y + r.h * 0.5, 9.5), 9.5, F.medium, lerp(muted, fg, Math.max(s, 0.6 * hv)));
      if (s > 0.001) p.rect(r.x + (r.w - tw) * 0.5, r.y + r.h - 1, tw, 2, 9999, { fill: primaryA(s) });
    }
    {
      const r = clipRect(), on = clipAmt.value, hv = hover.amount(CLIP_ID);
      p.rect(r.x, r.y, r.w, r.h, rc, { fill: lerp(hoverWash(hv), primaryA(0.22), on), stroke: lerp(border, col("primary"), on), sw: 1 });
      const c = lerp(muted, fg, Math.max(on, hv));
      p.add(glyph.warn({ x: r.x + 5, y: r.y + 4, w: 10, h: 10 }, css(c), 1));
      p.text("Clip", r.x + 18, baseline(r.y + r.h * 0.5, 9.5), 9.5, F.sans, c);
    }
    p.line(0, HEADER_H - 0.5, W, HEADER_H - 0.5, border, 1);
    // the waveform's Luma | RGB switch — with the waveform's own fade
    const a = modeAmt[WAVEFORM].value;
    if (a > 0.001) {
      const names = ["Luma", "RGB"], r0 = waveSwitchRect(0), r1 = waveSwitchRect(1);
      p.rect(r0.x, r0.y, r0.w + r1.w, r0.h, rc, { fill: fade(col("segmented-bg"), a), stroke: fade(border, a), sw: 1 });
      for (let i = 0; i < 2; ++i) {
        const r = waveSwitchRect(i), on = i === 1 ? rgbAmt.value : 1 - rgbAmt.value, hv = hover.amount(LUMA_ID + i);
        if (on > 0.001) p.rect(r.x + 1.5, r.y + 1.5, r.w - 3, r.h - 3, rh, { fill: fade(col("primary"), on * a) });
        else if (hv > 0.001) p.rect(r.x + 1.5, r.y + 1.5, r.w - 3, r.h - 3, rh, { fill: fade(hoverWash(hv), a) });
        const tw = measure(names[i], 8.5, F.medium);
        p.text(names[i], r.x + (r.w - tw) * 0.5, baseline(r.y + r.h * 0.5, 8.5), 8.5, F.medium, fade(lerp(muted, col("white"), on), a));
      }
    }
    // the readout: what is wrong, in words (mono numbers), red when something clips
    const ry = H - READOUT_H;
    p.rect(0, ry, W, READOUT_H, 0, { fill: col("histogram-bg") });
    const bad = data.valid && (clipHiAny(data) >= 0.5 || clipLoAny(data) >= 0.5);
    p.text(ellipsize(readout(), W - 2 * PAD_X, 9, F.mono), PAD_X, baseline(ry + READOUT_H * 0.5, 9), 9, F.mono, bad ? col("destructive") : muted);
    p.line(0, H - 0.5, W, H - 0.5, border, 1);
    p.end();
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    const more = advance(now);
    paintBody();
    paintOverlay();
    return more;
  }

  // ---------------------------------------------------------------- input (ScopePanel::handleGesture)
  const modeAt = (pt) => { for (let m = 0; m < MODES.length; ++m) if (inRect(pt, modeRect(m))) return m; return -1; };
  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e), m = modeAt(pt), wave = mode === WAVEFORM;
    hoverInside = true;
    hover.setHovered(m >= 0 ? m : inRect(pt, clipRect()) ? CLIP_ID
      : wave && inRect(pt, waveSwitchRect(0)) ? LUMA_ID : wave && inRect(pt, waveSwitchRect(1)) ? RGB_ID : -1);
    loop.kick();
  });
  host.addEventListener("pointerleave", () => { hoverInside = false; hover.clear(); loop.kick(); });
  host.addEventListener("click", (e) => {
    const pt = localPoint(host, e), m = modeAt(pt);
    if (m >= 0) { if (m !== mode) { mode = m; sel.setHovered(m); } }    // the cross-fade starts in advance
    else if (inRect(pt, clipRect())) vm.view.clipWarning.value = !clipOn;   // ScopePanel::onClipWarning -> the monitor
    else if (mode === WAVEFORM && inRect(pt, waveSwitchRect(0))) rgbOn = false;
    else if (mode === WAVEFORM && inRect(pt, waveSwitchRect(1))) rgbOn = true;
    loop.kick();
  });
  loop.kick();
}
