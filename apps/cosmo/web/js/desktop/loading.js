/*
 * Cosmo by arstro — loading.js: the open-project transition, its return, and the boot splash,
 * drawn into one canvas over the whole window like the native draws them over its root.
 *
 *   Transition   App::renderTransition / renderReturn (App.cpp:1396-1767): the star sky
 *                (widgets/Starfield.h, 220 stars, the same LCG and seed), the recent card
 *                flying to the centre (widgets/ProjectCard.cpp chrome + the cover), the flying
 *                wordmark, the status line and the three-band progress bar; the reveal that
 *                dissolves it into the editor; the three-part return.
 *   Splash       widgets/SplashScreen.cpp (420 x 260 composition, centred) while the page
 *                has no model yet.
 *
 * Pure view state: every tween here is this page's. What drives them - the load's events and
 * the model's load fields - is read, never written. Durations/easings are App.cpp:28-38.
 * Twinkle and the in-flight pulse run under reduced motion too (the native does not gate them).
 */
import { Tween, Ease } from "../core/motion.js";
import { tokens, fade, withAlpha, css, est, canvasFont, measure, surface } from "../dialogs/paint.js";

// App.cpp:28-38
export const MS = { intro: 460, reveal: 520, progress: 200, ret: 480, enter: 300, hold: 200, exit: 320, minLoad: 260, maxLoad: 2500 };
const META = 46;

// widgets/Starfield.h
function makeStars(n) {
  let s = 0x9E3779B9 >>> 0;
  const rnd = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return (s >>> 8) / 16777216; };
  return Array.from({ length: n }, () => ({ u: rnd(), v: rnd(), size: 0.35 + rnd() * 0.8, phase: rnd() * 6.2831853,
                                            speed: 0.4 + rnd() * 1.1, baseA: 0.25 + rnd() * 0.55 }));
}
const STARS = makeStars(220);

function drawStars(g, W, H, alpha, now) {
  if (alpha <= 0.001) return;
  for (const st of STARS) {
    const a = alpha * st.baseA * (0.5 + 0.5 * Math.sin(now * 0.001 * st.speed + st.phase));
    if (a <= 0.004) continue;
    g.fillStyle = `rgba(255, 255, 255, ${a})`;          // Starfield.h:55 Color{1,1,1,a}
    g.beginPath();
    g.arc(st.u * W, st.v * H, st.size, 0, Math.PI * 2);
    g.fill();
  }
}

function rr(g, x, y, w, h, r) { g.beginPath(); g.roundRect(x, y, w, h, Math.min(r, w / 2, h / 2)); }

/** drawProjectCardChrome (ProjectCard.cpp:10-48) at hover 0, faded by alpha. */
export function drawCardChrome(g, s, d, alpha) {
  if (alpha <= 0.001) return;
  const tk = tokens();
  const th = s.w * 9 / 16;
  rr(g, s.x, s.y, s.w, s.h, 2);
  g.fillStyle = css(fade(tk["folder-chip-bg"], alpha)); g.fill();
  g.lineWidth = 1; g.strokeStyle = css(fade(tk.border, alpha)); g.stroke();
  g.fillStyle = `rgba(17, 17, 17, ${alpha})`;            // ProjectCard.cpp:23 Color::hex(0x111111)
  g.fillRect(s.x, s.y, s.w, th);
  const mx = s.x + 12;
  g.fillStyle = css(fade(tk.foreground, alpha));
  g.font = canvasFont(11, "medium");
  g.fillText(d.name || "", mx, s.y + th + 18);
  const left = (d.photos || "") + (d.size ? "  ·  " + d.size : "");
  g.fillStyle = css(fade(tk["muted-foreground"], alpha));
  g.font = canvasFont(10, "sans");
  if (left) g.fillText(left, mx, s.y + th + 33);
  if (d.date) {
    g.fillStyle = css(fade(tk["muted-foreground"], 0.6 * alpha));
    g.font = canvasFont(9, "sans");
    g.fillText(d.date, s.x + s.w - 12 - est(d.date, 9), s.y + th + 33);
  }
}

/** ImageView Fit::Cover into a rect. */
function drawCover(g, img, r) {
  const iw = img.naturalWidth || img.width, ih = img.naturalHeight || img.height;
  if (!iw || !ih) return;
  const k = Math.max(r.w / iw, r.h / ih);
  const sw = r.w / k, sh = r.h / k;
  g.save();
  g.beginPath(); g.rect(r.x, r.y, r.w, r.h); g.clip();
  g.imageSmoothingQuality = "high";
  g.drawImage(img, (iw - sw) / 2, (ih - sh) / 2, sw, sh, r.x, r.y, r.w, r.h);
  g.restore();
}

/** App::drawWordmark (App.cpp:1498-1517): p 0 = the Home sidebar slot, 1 = the top-bar slot. */
function drawWordmark(g, p, alpha) {
  if (alpha <= 0.001) return;
  const tk = tokens();
  const sz = 46 + (13 - 46) * p, x = 32 + (9.75 - 32) * p, base = 96 + (19.2 - 96) * p, sp = -0.03 * sz;
  g.font = canvasFont(sz, "semibold");
  g.letterSpacing = sp + "px";
  g.fillStyle = css(fade(tk.foreground, alpha));
  g.fillText("cosmo", x, base);
  g.fillStyle = css(fade(tk.primary, alpha));
  g.fillText(".", x + measure("cosmo", sz, "semibold", sp), base);
  g.letterSpacing = "0px";
}

const lerpRect = (a, b, t) => ({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, w: a.w + (b.w - a.w) * t, h: a.h + (b.h - a.h) * t });
const clamp01 = (v) => Math.min(1, Math.max(0, v));

// ================================================================== the transition
export class Transition {
  /** host = the logical root; `hooks` = {onPhase(phase), editorVisible(b), homeVisible(b), onReturnLoad(), onDone(kind)} */
  constructor(host, hooks) {
    this.hooks = hooks;
    this.s = surface(host, "scr-fx");
    this.s.cv.style.display = "none";
    this.phase = "none";
    this.returning = false;
    this.intro = new Tween(0); this.reveal = new Tween(0); this.progress = new Tween(0); this.inFlight = new Tween(0);
    this.coverFade = new Tween(0); this.barFade = new Tween(0);
    this.enter = new Tween(0); this.exit = new Tween(0); this.ret = new Tween(0);
    this.raf = 0;
    this.status = "";
    this.cover = null;
    this.total = 0;
  }
  get active() { return this.phase !== "none"; }

  _setPhase(p) {
    this.phase = p;
    this.t0 = performance.now();
    this.s.cv.classList.toggle("block", p !== "none");
    if (this.hooks.onPhase) this.hooks.onPhase(p);
  }
  _run() {
    this.s.cv.style.display = "";
    if (!this.raf) this.raf = requestAnimationFrame((t) => this._frame(t));
  }

  // ---------------------------------------------------------------- open (App::beginOpenTransition)
  open({ name, from = null, card = null, coverUrl = null, complete = false }) {
    this.returning = false;
    this.name = name || "";
    this.status = "Preparing…";
    this.from = from;
    this.card = { name: this.name, photos: "", size: "", date: "", ...(card || {}) };
    this.card.name = this.name;
    this.cover = null; this.coverReady = false;
    this.loadComplete = !!complete; this.usable = !!complete;
    this.loadStart = performance.now();
    this.intro.set(0); this.intro.to(1, MS.intro, Ease.EaseOutCubic);
    this.reveal.set(0); this.progress.set(complete ? 1 : 0); this.inFlight.set(0); this.coverFade.set(0); this.barFade.set(0);
    this.total = 0;
    if (coverUrl) this.setCover(coverUrl);
    this._setPhase("intro");
    this.hooks.editorVisible(false);
    this.hooks.homeVisible(false);
    this._run();
  }
  setCover(url) {
    if (!url || this.coverReady || this.returning) return;
    const img = new Image();
    img.onload = () => {
      if (this.coverReady || this.phase === "none" || this.returning) return;
      this.cover = img; this.coverReady = true;
      this.coverFade.set(0); this.coverFade.to(1, 240, Ease.EaseOutCubic);
    };
    img.src = url;
  }
  setStatus(text) { if (text) this.status = text; }
  setTotal(n) { if (n > 0) this.total = n; }
  /** load.progress (a, b): done / total over 200 ms. */
  setProgress(done, total) {
    if (total > 0) this.total = total;
    const f = total > 0 ? clamp01(done / total) : 0;
    this.progress.to(f, MS.progress, Ease.EaseOutCubic);
  }
  /** entry.progress: the model's loadPermille, monotonic, 90 ms. */
  setFraction(f) {
    f = clamp01(f);
    if (f + 1e-6 < this.progress.value) return;
    if (Math.abs(f - this.progress.target) < 1e-6) return;
    this.progress.to(f, 90, Ease.EaseOutCubic);
  }
  setInFlight(started, total) {
    if (total > 0) this.total = total;
    const f = this.total > 0 ? clamp01(started / this.total) : 0;
    if (Math.abs(f - this.inFlight.target) < 1e-6) return;
    this.inFlight.to(f, MS.progress * 2, Ease.EaseOutCubic);
  }
  setUsable() { this.usable = true; }
  finish() {
    if (this.returning || this.phase === "none") return;
    this.progress.to(1, 120, Ease.EaseOutCubic);
    this.loadComplete = true;
  }
  /** Abort (the project went away mid-load): straight to `kind`'s screen. */
  cancel() {
    this._setPhase("none");
    this.s.cv.style.display = "none";
  }

  // ---------------------------------------------------------------- return (App::showHome from the editor)
  back() {
    this.returning = true;
    this._setPhase("returnEnter");
    this.enter.set(0); this.enter.to(1, MS.enter, Ease.EaseOutCubic);
    this.exit.set(0);
    this.ret.set(1); this.ret.to(0, MS.ret, Ease.EaseOutCubic);
    this.hooks.editorVisible(true);
    this._run();
  }

  // ---------------------------------------------------------------- frame
  _frame(now) {
    this.raf = 0;
    if (this.phase === "none") { this.s.cv.style.display = "none"; return; }
    const { W, H } = this.s.fit();
    const g = this.s.g;
    g.clearRect(0, 0, W, H);
    if (this.returning) this._return(g, W, H, now); else this._open(g, W, H, now);
    if (this.phase === "none") { this.s.cv.style.display = "none"; g.clearRect(0, 0, W, H); return; }
    this.raf = requestAnimationFrame((t) => this._frame(t));
  }

  _open(g, W, H, now) {
    const tk = tokens();
    const t = performance.now();
    if (this.phase === "intro" && !this.intro.animating) {
      this._setPhase("loading");
      this.barFade.to(1, 160, Ease.EaseOutCubic);
    }
    if (this.phase === "loading") {
      const since = t - this.loadStart;
      if ((this.loadComplete && since >= MS.minLoad) || (this.usable && since >= MS.maxLoad)) {
        this._setPhase("reveal");
        this.reveal.set(0); this.reveal.to(1, MS.reveal, Ease.EaseOutCubic);
        this.hooks.editorVisible(true);
      }
    }
    if (this.phase === "reveal" && !this.reveal.animating && this.reveal.value >= 1) {
      this._setPhase("none");
      if (this.hooks.onDone) this.hooks.onDone("editor");
      return;
    }
    const cw = this.from ? this.from.w : Math.min(320, Math.max(220, W * 0.22));
    const ch = this.from ? this.from.h : cw * 9 / 16 + META;
    const stackH = ch + 30 + 16;
    const card = { x: (W - cw) * 0.5, y: (H - stackH) * 0.5, w: cw, h: ch };
    const statusBase = card.y + ch + 30;
    const bar = { x: card.x, y: statusBase + 16, w: cw, h: 4 };
    const bg = tk["canvas-bg"];                                // kLoadingBg #0A0A0A (App.cpp:39)

    const drawCard = (cr, alpha) => {
      if (alpha <= 0.001) return;
      drawCardChrome(g, cr, this.card, alpha);
      if (this.coverReady && this.cover) {
        const band = { x: cr.x, y: cr.y, w: cr.w, h: cr.w * 9 / 16 };
        drawCover(g, this.cover, band);
        const a = this.coverFade.value * alpha;
        if (a < 0.999) { g.fillStyle = css(withAlpha(bg, 1 - a)); g.fillRect(band.x, band.y, band.w, band.h); }
      }
    };
    const drawStatusAndBar = (alpha) => {
      if (alpha <= 0.001) return;
      if (this.status) {
        g.fillStyle = css(withAlpha(tk.white, 0.62 * alpha));
        g.font = canvasFont(13, "medium");
        g.fillText(this.status, card.x, statusBase);
      }
      rr(g, bar.x, bar.y, bar.w, bar.h, 2);
      g.fillStyle = css(withAlpha(tk.white, 0.12 * alpha)); g.fill();
      const doneW = bar.w * this.progress.value, flightW = bar.w * this.inFlight.value;
      if (flightW > doneW + 0.5) {
        const pulse = 0.5 + 0.5 * Math.sin(t / 380);
        rr(g, bar.x, bar.y, flightW, bar.h, 2);
        g.fillStyle = css(fade(tk.primary, alpha * (0.22 + 0.20 * pulse))); g.fill();
      }
      if (doneW > 0.5) {
        rr(g, bar.x, bar.y, doneW, bar.h, 2);
        g.fillStyle = css(fade(tk.primary, alpha)); g.fill();
      }
    };

    if (this.phase === "reveal") {
      const r = this.reveal.value;
      const fadeOut = clamp01(r / 0.45), fadeIn = clamp01((r - 0.40) / 0.60);
      // backdrop, and the editor (a layer beneath this canvas) shown through a scrim 1 - fadeIn
      g.fillStyle = css(withAlpha(bg, fadeIn > 0.001 ? 1 - fadeIn : 1));
      g.fillRect(0, 0, W, H);
      if (fadeOut < 0.999) {
        const a = 1 - fadeOut;
        drawStars(g, W, H, a, now);
        drawCard(card, a);
        drawStatusAndBar(a);
      }
      drawWordmark(g, 1, 1 - fadeIn);
      return;
    }
    const intro = this.intro.value;
    g.fillStyle = css(bg);
    g.fillRect(0, 0, W, H);
    drawStars(g, W, H, intro, now);
    drawCard(this.from ? lerpRect(this.from, card, intro) : card, intro);
    drawWordmark(g, intro, 1);
    drawStatusAndBar(this.barFade.value);
  }

  _return(g, W, H, now) {
    const tk = tokens();
    const bg = tk["canvas-bg"];
    const t = performance.now();
    if (this.phase === "returnEnter" && !this.enter.animating) {
      this._setPhase("returnLoad");
      this.hooks.editorVisible(false);
      if (this.hooks.onReturnLoad) this.hooks.onReturnLoad();
    }
    if (this.phase === "returnLoad" && t - this.t0 >= MS.hold) {
      this._setPhase("returnExit");
      this.exit.set(0); this.exit.to(1, MS.exit, Ease.EaseOutCubic);
      this.hooks.homeVisible(true);
    }
    if (this.phase === "returnExit" && !this.exit.animating && this.exit.value >= 1) {
      this.returning = false;
      this._setPhase("none");
      if (this.hooks.onDone) this.hooks.onDone("home");
      return;
    }
    const p = this.ret.value;
    if (this.phase === "returnExit") {
      const out = 1 - this.exit.value;
      g.fillStyle = css(withAlpha(bg, out)); g.fillRect(0, 0, W, H);
      drawStars(g, W, H, out, now);
    } else if (this.phase === "returnEnter") {
      const a = this.enter.value;
      g.fillStyle = css(withAlpha(bg, a)); g.fillRect(0, 0, W, H);
      drawStars(g, W, H, a, now);
    } else {
      g.fillStyle = css(bg); g.fillRect(0, 0, W, H);
      drawStars(g, W, H, 1, now);
    }
    drawWordmark(g, p, 1);
  }
}

// ================================================================== the splash (SplashScreen.cpp)
export class Splash {
  constructor(host) {
    this.host = host;
    this.el = document.createElement("div");
    this.el.className = "scr-splash";
    host.append(this.el);
    this.s = surface(this.el, "");
    this.t0 = performance.now();
    this.rise = new Tween(0); this.tag = new Tween(0); this.dots = new Tween(0); this.barFade = new Tween(0);
    this.mix = new Tween(0); this.exit = new Tween(1); this.prog = new Tween(0);
    this.rise.to(1, 700, Ease.EaseOutCubic);
    setTimeout(() => this.tag.to(1, 700, Ease.EaseOutCubic), 300);
    setTimeout(() => { this.dots.to(1, 500, Ease.EaseOutCubic); this.barFade.to(1, 500, Ease.EaseOutCubic); }, 600);
    this.status = "";
    this.exiting = false;
    this.done = false;
    this.raf = requestAnimationFrame((t) => this._frame(t));
  }
  get age() { return performance.now() - this.t0; }
  setStatus(text) {
    if (text && !this.status) this.mix.to(1, 280, Ease.EaseInOutCubic);
    this.status = text;
  }
  setProgress(p) { this.prog.to(clamp01(p), 220, Ease.EaseOutCubic); }
  /** beginExit: once the eased progress is ~1, exit 1 -> 0 over 260 ms, then `then()`. */
  leave(then) { this.exiting = true; this.then = then; }
  remove() { this.done = true; cancelAnimationFrame(this.raf); this.el.remove(); }
  _frame(now) {
    if (this.done) return;
    const tk = tokens();
    const { W, H } = this.s.fit();
    const g = this.s.g;
    g.clearRect(0, 0, W, H);
    if (this.exiting && !this.exit.animating && this.exit.value >= 1 && this.prog.value >= 0.995 && this.age >= 900) {
      this.exit.to(0, 260, Ease.EaseOutCubic);
    }
    if (this.exiting && this.exit.value <= 0.0001 && !this.exit.animating) {
      const then = this.then; this.remove(); if (then) then(); return;
    }
    const ox = Math.round((W - 420) / 2), oy = Math.round((H - 260) / 2);
    g.save(); g.translate(ox, oy);
    const ex = this.exit.value, rise = this.rise.value;
    // wordmark
    const sz = 40 * (0.96 + 0.04 * rise), sp = -0.03 * sz;
    const wordW = measure("cosmo", sz, "semibold", sp) + measure(".", sz, "semibold", sp);
    const wx = (420 - wordW) / 2, wy = 109.2 + (1 - rise) * 8;
    g.font = canvasFont(sz, "semibold"); g.letterSpacing = sp + "px";
    g.fillStyle = css(fade(tk.foreground, rise * ex)); g.fillText("cosmo", wx, wy);
    g.fillStyle = css(fade(tk.primary, rise * ex)); g.fillText(".", wx + measure("cosmo", sz, "semibold", sp), wy);
    // tagline
    const tagS = "PROFESSIONAL PHOTO EDITOR";
    g.font = canvasFont(9, "sans"); g.letterSpacing = "2.1px";
    g.fillStyle = css(fade(tk["muted-foreground"], this.tag.value * ex));
    g.fillText(tagS, (420 - measure(tagS, 9, "sans", 2.1)) / 2, wy + 22);
    g.letterSpacing = "0px";
    const mix = this.mix.value;
    // dots, before the first status
    for (let i = 0; i < 3; i++) {
      const phase = (((now - this.t0 + 180 * i) % 1400) + 1400) % 1400 / 1400;
      const k = 0.5 - 0.5 * Math.cos(2 * Math.PI * phase);
      const a = (0.3 + 0.7 * k) * this.dots.value * (1 - mix) * ex;
      if (a <= 0.001) continue;
      g.beginPath(); g.arc(210 + (i - 1) * 12, 214, 2 * (1 + 0.4 * k), 0, Math.PI * 2);
      g.fillStyle = css(fade(tk.primary, a)); g.fill();
    }
    // status slot
    const A = mix * ex;
    if (A > 0.001 && this.status) {
      g.font = canvasFont(10, "sans");
      let s = this.status;
      const maxW = 0.72 * 420 - 11 - 8;
      while (s.length > 1 && measure(s, 10) > maxW) s = s.slice(0, -2) + "…";
      const tw = measure(s, 10), unit = 11 + 8 + tw, sx = (420 - unit) / 2;
      const cx = sx + 5.5, cy = 214;
      g.lineWidth = 1.6;
      g.strokeStyle = css(withAlpha(tk.white, 0.12 * A));
      g.beginPath(); g.arc(cx, cy, 5.5, 0, Math.PI * 2); g.stroke();
      const rot = ((now - this.t0) % 900) / 900 * Math.PI * 2;
      g.strokeStyle = css(fade(tk.primary, 0.9 * A));
      g.beginPath(); g.arc(cx, cy, 5.5, rot, rot + Math.PI / 2); g.stroke();
      g.fillStyle = css(withAlpha(tk.white, 0.55 * A));
      g.fillText(s, sx + 11 + 8, 217.6);
    }
    // version
    g.font = canvasFont(9, "mono");
    g.fillStyle = css(withAlpha(tk.white, 0.22 * ex));
    g.fillText("v1.0.0", 408 - measure("v1.0.0", 9, "mono"), 250);
    // bar
    const barA = this.barFade.value * ex;
    if (barA > 0.001) {
      const bw = 332;
      rr(g, 44, 226, bw, 4, 2);
      g.fillStyle = css(withAlpha(tk.white, 0.10 * barA)); g.fill();
      const p = this.prog.value;
      if (p > 0.001) { rr(g, 44, 226, Math.max(bw * p, 4), 4, 2); g.fillStyle = css(fade(tk.primary, barA)); g.fill(); }
    }
    g.restore();
    this.raf = requestAnimationFrame((t) => this._frame(t));
  }
}
