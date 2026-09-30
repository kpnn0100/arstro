/*
 * Cosmo by arstro — touch/tray.js: the control tray of the phone editor.
 *
 * Ports PhoneApp.cpp's Tray (:271-675): the CARD panel (radius 10) with its grab handle, the
 * expanded header (tab title, chevron-down close, histogram), the five tab bodies (Basic with its
 * section chips, Mask, Curve/Mixer, Grade, Xform), the Save / Import / Export action bar and the
 * five-tab tool bar with its sliding underline, and its detents (Rail 64, Open 0.62·H) and drag.
 * The tray's height itself is owned by the editor (the photo shrinks with it, R-TOUCH-2); this
 * module asks for detents and live heights through `env`.
 *
 * Every control sends the same intents as the desktop column (vm.setFields / maskAdd / maskSet …)
 * in the same lines (panels.md §2.6), so an edit here is an edit on every client.
 *
 * Deliberate deviations from the native, each required by R-TOUCH / R-G-1 or by agreement with the
 * desktop (screens_touch.md §14.6-14.7, §14.16):
 *  - the body between its top and the action bar scrolls and is clipped (native rows overlap the
 *    action bar), and the section-chip row scrolls horizontally;
 *  - Grade: the Hue Range Remap toggle has its own 38 px row before the remap rows (native draws
 *    them over it); Remap Strength is sent /100 like the desktop (native writes 0..100 raw);
 *  - Curve: the plot starts 16 px lower so the Reset label no longer sits on the curve's end node
 *    (native overlap); Mixer points use the engine's domain (hue 0..360, y −1..1, default
 *    0,0;120,0;240,0 as the desktop) instead of the native's 0..1 square;
 *  - Temperature uses the shared catalogue conversion (the `controls` method, R-TOUCH-1: one
 *    mapping) instead of the native touch's own linear 6500 ± 3500 K;
 *  - Xform aspect chips apply the desktop's crop maths with the source aspect (native touch
 *    ignores it, so its "1:1" is not square); Free resets the crop (native touch);
 *  - the expanded content fades as the tray collapses instead of vanishing in one frame;
 *  - the action bar's Save saves a preset (brief §8.8); Import / Export say they are not available
 *    (core gap G9) instead of doing nothing.
 */
import { h, bindEffect, bindText, bindClass, keyed, destroy, own } from "../core/dom.js";
import { signal, computed, effect, untracked } from "../core/signal.js";
import { Tween, Ease } from "../core/motion.js";
import { num, fmt } from "../model/params.js";
import { toEngine, toTrack } from "../model/controls.js";
import { icon } from "../ui/icons.js";
import { K, tx, rect, ticon, gestures, pressWash, slideIn, baselineBox, baseOffset, clamp, canvasFor, tok } from "./tk.js";
import { sliderRow, segmented, toggle } from "./controls.js";
import { curvePlot } from "./curve.js";

const TAB_IDS = ["basic", "mask", "mixer", "grade", "xform"];
const TAB_LABELS = ["Basic", "Mask", "Curve", "Grade", "Xform"];
const TAB_ICONS = ["tabBasic", "tabMask", "tabCurve", "tabGrade", "tabXform"];

// ------------------------------------------------------------------ conversions
const M_N = 1e6 / 6500, M_W = 1e6 / 2000, M_C = 1e6 / 19500;           // UnitConversions.h (mired)
const kelvinOf = (t) => { const v = clamp(t, -100, 100); return 1e6 / (v <= 0 ? M_N + (-v / 100) * (M_W - M_N) : M_N - (v / 100) * (M_N - M_C)); };
const trackOfKelvin = (k) => { const m = 1e6 / clamp(k, 2000, 19500); return m >= M_N ? -100 * (m - M_N) / (M_W - M_N) : 100 * (M_N - m) / (M_N - M_C); };

function catalogue(vm, field) {
  const c = vm.controls.value;
  if (!Array.isArray(c)) return null;
  for (const s of c) for (const x of s.controls || []) if (x.field === field) return x;
  return null;
}
/** Engine <-> the touch row's UI units. Only Temperature is non-linear; it goes through the
 *  catalogue the desktop uses (R-TOUCH-1), or its exact formula until `controls` arrives. */
function conv(vm, row) {
  if (row.field === "temp") return {
    toEng: (t) => { const c = catalogue(vm, "temp"); return c ? toEngine(c, t) : kelvinOf(t); },
    fromEng: (e) => { const c = catalogue(vm, "temp"); return c ? toTrack(c, e) : trackOfKelvin(e); },
  };
  if (row.field === "tint") return { toEng: (t) => t * 1.5, fromEng: (e) => e / 1.5 };
  return { toEng: (t) => t, fromEng: (e) => e };
}

// PhoneApp.cpp:72-106 - the touch sections (labels, ranges, defaults as the native).
const TEMP_GRAD = ["var(--t-temp-from)", "var(--t-temp-to)"];
const TINT_GRAD = ["var(--t-tint-from)", "var(--t-tint-to)"];
const SECTIONS = [
  { name: "TONE", rows: [["Exposure", "exposure", -5, 5, 0], ["Contrast", "contrast"], ["Highlights", "highlights"],
                         ["Shadows", "shadows"], ["Whites", "whites"], ["Blacks", "blacks"]] },
  { name: "COLOUR", rows: [["Temperature", "temp", -100, 100, 0, TEMP_GRAD], ["Tint", "tint", -100, 100, 0, TINT_GRAD],
                           ["Vibrance", "vibrance"], ["Saturation", "saturation"]] },
  { name: "PRESENCE", rows: [["Texture", "texture"], ["Clarity", "clarity"]] },
  { name: "EFFECTS", rows: [["Dehaze", "dehaze"], ["Grain Amount", "grainAmount", 0, 100, 0], ["Grain Size", "grainSize", 0, 100, 25]] },
  { name: "SHARPENING", rows: [["Amount", "sharpenAmount", 0, 150, 0], ["Radius", "sharpenRadius", 0, 3, 1], ["Masking", "sharpenMasking", 0, 100, 0]] },
  { name: "NOISE", rows: [["Luminance", "nrLuminance", 0, 100, 0], ["Colour", "nrColor", 0, 100, 0]] },
  { name: "LENS", rows: [["Distortion", "lensDistortion"], ["Chromatic Ab.", "lensCA", 0, 100, 0], ["Vignette", "lensVignette"]] },
].map((s) => ({ name: s.name, rows: s.rows.map(([label, field, min = -100, max = 100, def = 0, grad = null]) => ({ label, field, min, max, def, grad })) }));

const MASK_TYPES = ["Radial", "Linear", "Brush", "Draw"];
const ASPECTS = [["Free", 0], ["1:1", 1], ["4:3", 4 / 3], ["16:9", 16 / 9], ["3:2", 3 / 2], ["5:4", 5 / 4]];

// CropGeometry.h (panels.md §9.2), verbatim.
const MIN = 0.02;
function fitKeepingRatio(w, hh, nr, maxW, maxH) {
  if (nr <= 0) return [w, hh];
  if (w < MIN) { w = MIN; hh = w / nr; } if (hh < MIN) { hh = MIN; w = hh * nr; }
  let s = 1; if (maxW > 0 && w > maxW) s = Math.min(s, maxW / w); if (maxH > 0 && hh > maxH) s = Math.min(s, maxH / hh);
  if (s < 1) { w *= s; hh *= s; }
  let s2 = 1; if (w > 1) s2 = Math.min(s2, 1 / w); if (hh > 1) s2 = Math.min(s2, 1 / hh);
  if (s2 < 1) { w *= s2; hh *= s2; }
  return [w, hh];
}
function applyRatio(r, nr) {
  const cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  let w = r.w, hh = r.w / nr;
  if (hh > 1 || hh > r.h * 4) { hh = r.h; w = r.h * nr; }
  [w, hh] = fitKeepingRatio(w, hh, nr, 2 * Math.min(cx, 1 - cx), 2 * Math.min(cy, 1 - cy));
  const x = clamp(cx - w / 2, 0, Math.max(0, 1 - w)), y = clamp(cy - hh / 2, 0, Math.max(0, 1 - hh));
  return { x, y, w, h: hh };
}

/** A hairline at y (1 px centred on y, as Cairo strokes it). */
function hline(y, cls = "") { const l = h("i.t-hl" + (cls ? "." + cls : "")); l.style.top = y - 0.5 + "px"; return l; }

const quiet = (p) => { if (p && p.catch) p.catch(() => {}); return p; };

/**
 * mountTray(parent, vm, ctx, env)
 *   env = { W, H, land (signals: tray width, screen height, landscape), detent (signal "rail"|"open"),
 *           trayH (() => current height), setDetent(d), dragTo(h), snap(), sheets }
 */
export function mountTray(parent, vm, ctx, env) {
  const el = h("div.t-tray");
  parent.append(el);
  const tabIndex = computed(() => Math.max(0, TAB_IDS.indexOf(vm.view.tab.value)));
  const expanded = computed(() => env.land.value || env.detent.value === "open");
  const handleH = computed(() => (env.land.value ? 0 : K.handle));
  const hasTarget = () => (vm.hasEditTarget ? !!vm.hasEditTarget.peek() : !!vm.editTarget.peek());

  // Pure view state of the tray (PhoneApp Tray members): which section / channel / region.
  const st = {
    section: signal(0), curveMode: signal(0), curveCh: signal(0), mixerCh: signal(0),
    gradeRegion: signal(1), aspect: signal(0),
  };

  // --- grab handle
  const handle = h("i.t-handle");
  el.append(handle);
  bindClass(handle, "off", () => env.land.value);

  // --- the expanded panel: header, histogram, body, action bar (a column above the tool bar)
  const panel = h("div.t-panel");
  el.append(panel);
  bindClass(panel, "off", () => !expanded.value);

  const head = h("div.t-thead");
  bindEffect(head, () => { head.style.height = handleH.value + K.header + "px"; });
  const title = tx("", 16, 0, 13, "semibold", "left", "t-fg");
  bindEffect(title, () => { title.textContent = TAB_LABELS[tabIndex.value].toUpperCase(); });
  // PhoneApp.cpp:312 - baseline kHandle + kHeader/2 + 4
  bindEffect(title, () => { title.style.top = handleH.value + K.header * 0.5 + 4 - baseOffset(13, "semibold") + "px"; });
  const chev = icon("chevronDown", 24, 1.6);
  chev.classList.add("t-chev");
  bindEffect(chev, () => { chev.style.top = handleH.value + 6 + "px"; });
  bindClass(chev, "off", () => env.land.value);
  const headLine = h("i.t-hl");
  bindEffect(headLine, () => { headLine.style.top = handleH.value + K.header - 0.5 + "px"; });
  head.append(title, chev, headLine);

  const histWrap = h("div.t-histband");
  const histBox = h("div.t-hist");
  histWrap.append(histBox, hline(K.hist));
  const hist = histogram(histBox, vm);

  const body = h("div.t-body.vscroll");
  const actions = actionBar(vm, ctx, env);
  panel.append(head, histWrap, body, actions);

  // --- the tool bar
  const tools = h("div.t-tools");
  tools.append(hline(0));
  const under = h("i.t-under");
  tools.append(under);
  const tabs = TAB_LABELS.map((label, i) => {
    const t = h("div.t-tab");
    t.style.left = i * 20 + "%";
    const ic = ticon(TAB_ICONS[i], 20, 1.6);
    ic.classList.add("t-tab-ic");
    t.append(ic, tx(label, "50%", 44, 10, "sans", "center", "t-tab-l"));
    tools.append(t);
    return t;
  });
  el.append(tools);
  let firstUnder = true;
  bindEffect(tools, () => {
    const i = tabIndex.value, ex = expanded.value;
    tabs.forEach((t, k) => t.classList.toggle("on", k === i && ex));
    if (firstUnder) { firstUnder = false; under.style.transition = "none"; requestAnimationFrame(() => { under.style.transition = ""; }); }
    under.style.left = `calc(${(i + 0.5) * 20}% - 14px)`;
    under.classList.toggle("off", !ex);
  });

  // --- the tray's own gestures (PhoneApp.cpp:344-365): the tool bar, the handle band, the chevron.
  let dragging = false;
  const W = () => el.clientWidth;
  gestures(el, {
    down: (e, p) => {
      const hh = el.clientHeight, tbY = hh - K.toolBar;
      if (p.y >= tbY) { pressWash(tabs[clamp(Math.floor(p.x / (W() / 5)), 0, 4)]); return true; }
      if (env.land.peek()) return false;
      if (p.y <= 34) return true;
      if (expanded.peek() && p.x >= W() - 48 && p.y <= handleH.peek() + K.header) return true;
      return false;
    },
    dragStart: (e, p0) => {
      if (env.land.peek() || p0.y > 34) return false;
      dragging = true;
      el.classList.add("dragging");
    },
    drag: (e) => { if (dragging) env.dragTo(env.H.peek() - (e.clientY - parent.getBoundingClientRect().top)); },
    dragEnd: () => { if (!dragging) return; dragging = false; el.classList.remove("dragging"); env.snap(); },
    tap: (e, p) => {
      const hh = el.clientHeight, tbY = hh - K.toolBar;
      if (p.y >= tbY) return onTab(clamp(Math.floor(p.x / (W() / 5)), 0, 4));
      if (env.land.peek()) return;
      if (p.y <= K.handle) return env.setDetent(env.detent.peek() === "rail" ? "open" : "rail");
      if (expanded.peek() && p.x >= W() - 48 && p.y <= K.handle + K.header) return env.setDetent("rail");
    },
  });

  function onTab(i) {
    if (!env.land.peek() && i === tabIndex.peek() && expanded.peek()) { env.setDetent("rail"); return; }
    const changed = i !== tabIndex.peek();
    vm.view.tab.value = TAB_IDS[i];
    if (!env.land.peek()) env.setDetent("open");
    if (!changed) slideIn(bodyIn, W() * 0.10);
  }

  // --- the body: rebuilt per tab, slid in from +10% W (Tray::onTab -> rebuildBody + slideIn)
  const B = { vm, ctx, env, st, hasTarget, W: env.W, bodyH: signal(0), expanded };
  const ro = new ResizeObserver(() => { B.bodyH.value = body.clientHeight; });
  ro.observe(body);
  own(el, () => ro.disconnect());
  let bodyIn = null;
  let firstBody = true, shownTab = -1;
  bindEffect(el, () => {
    const i = tabIndex.value;
    if (i === shownTab) return;
    shownTab = i;
    untracked(() => {
      if (bodyIn) { destroy(bodyIn); bodyIn.remove(); }
      bodyIn = BODIES[i](B);
      body.scrollTop = 0;
      body.append(bodyIn);
      if (!firstBody) slideIn(bodyIn, W() * 0.10);
      firstBody = false;
    });
  });

  return { el, expanded };
}

// ------------------------------------------------------------------ histogram
/** Histogram (12, 62, W-24, 48) r2 #0F0F0F: per channel v = ln(1+count)/ln(1+max), R/G/B filled
 *  α .45 in CHR/CHG/CHB, luminance a 1.2 px white polyline (PhoneApp.cpp:419-434). The data is the
 *  preview meta `hist` (64 bins; 256 once the core sends them), eased between frames (R-G-1). */
function histogram(box, vm) {
  const cv = canvasFor(box);
  let cols = null;
  let data = null;                                  // [r..., g..., b..., lum...] normalised
  let n = 0;
  const tw = new Tween(null, (v) => { data = v; draw(); });
  function draw() {
    const [w, hh] = cv.size();
    const c = cv.ctx;
    c.clearRect(0, 0, w, hh);
    if (!data || !n) return;
    if (!cols || !cols[0]) cols = box.isConnected ? ["--t-chr", "--t-chg", "--t-chb"].map((k) => tok(box, k)) : null;
    if (!cols) return;
    for (let ch = 0; ch < 3; ch++) {
      c.beginPath();
      c.moveTo(0, hh);
      for (let i = 0; i < n; i++) c.lineTo((i / (n - 1)) * w, hh - data[ch * n + i] * hh);
      c.lineTo(w, hh);
      c.closePath();
      c.globalAlpha = 0.45;
      c.fillStyle = cols[ch];
      c.fill();
    }
    c.globalAlpha = 1;
    c.beginPath();
    for (let i = 0; i < n; i++) { const x = (i / (n - 1)) * w, y = hh - data[3 * n + i] * hh; if (i) c.lineTo(x, y); else c.moveTo(x, y); }
    c.strokeStyle = tok(box, "--white");
    c.lineWidth = 1.2;
    c.stroke();
  }
  bindEffect(box, () => {
    const f = vm.frame.value;
    const hs = f && f.meta && f.meta.hist;
    if (!hs || !hs.r) return;
    const chans = [hs.r, hs.g, hs.b, hs.lum || hs.l || hs.r];
    const len = chans[0].length;
    let max = hs.max || 1;
    if (!hs.max) for (const a of chans) for (const v of a) if (v > max) max = v;
    const lm = Math.log(1 + Math.max(1, max));
    const next = [];
    for (const a of chans) for (let i = 0; i < len; i++) next.push(Math.min(1, Math.log(1 + (a[i] || 0)) / lm));
    if (len !== n || !data) { n = len; tw.set(next); } else tw.to(next, 160, Ease.EaseOutCubic);
  });
  const ro = new ResizeObserver(() => draw());
  ro.observe(box);
  own(box, () => ro.disconnect());
  return { draw };
}

// ------------------------------------------------------------------ action bar
/** Save (accent, half) / Import / Export (quarters), h 38 at y 7, gap 8 (PhoneApp.cpp:318-326). */
function actionBar(vm, ctx, env) {
  const bar = h("div.t-actions");
  bar.append(hline(0));
  const mk = (label, cls, x, w, fn) => {
    const b = h("div.t-abtn." + cls);
    b.style.left = x; b.style.width = w;
    baselineBox(b, 38, 23.5, 13, cls === "save" ? "semibold" : "sans");
    b.textContent = label;
    gestures(b, { down: () => pressWash(b), tap: fn });
    bar.append(b);
  };
  mk("Save", "save", "16px", "calc((100% - 48px) / 2)", async () => {
    if (!vm.imageCount.peek()) return;
    const name = await env.sheets.prompt({ title: "Save preset", placeholder: "preset name", confirm: "Save" });
    if (name) quiet(vm.savePreset(name));
  });
  const q = "calc((100% - 48px) / 4)";
  mk("Import", "side", "calc(16px + (100% - 48px) / 2 + 8px)", q, () => ctx.toast("Preset import is not available from the browser yet"));
  mk("Export", "side", "calc(16px + (100% - 48px) * 0.75 + 16px)", q, () => ctx.toast("Preset export is not available from the browser yet"));
  return bar;
}

// ------------------------------------------------------------------ bodies
function bodyEl(hh) { const b = h("div.t-bodyin"); if (hh !== undefined) b.style.height = hh + "px"; return b; }

/** A swapping region: rebuilds its content when `key()` changes and slides it in. */
function swapper(parent, B, key, build) {
  let cur = null, first = true, last;
  bindEffect(parent, () => {
    const k = key();
    if (!first && k === last) return;            // effects re-run on every model push
    last = k;
    untracked(() => {
      if (cur) { destroy(cur); cur.remove(); }
      cur = build(k);
      parent.append(cur);
      if (!first) slideIn(cur, (B.W.peek() || 360) * 0.10);
      first = false;
    });
  });
}

/** A basic slider row bound to an engine scalar (own + group reach), sending `set`. */
function scalarRow(B, d) {
  const { vm } = B;
  const c = conv(vm, d);
  return sliderRow(d, {
    value: () => {
      const o = vm.scalar(d.field, vm.ownParams);
      const stack = (vm.params.value[d.field] ?? o) - (vm.ownParams.value[d.field] ?? o);
      return { own: c.fromEng(o), eff: c.fromEng(o + stack) };
    },
    set: (ui, live) => { if (B.hasTarget()) quiet(vm.setFields({ [d.field]: num(c.toEng(ui)) }, { live })); },
  });
}

function rowsAt(y, rows) {
  const box = h("div.t-rows");
  box.style.top = y + "px";
  box.style.height = rows.length * K.row + "px";
  box.append(...rows);
  return box;
}

// ---- Basic: section chips (filter), then that section's rows from y 52 (PhoneApp.cpp:436-440)
function basicBody(B) {
  const el = bodyEl();
  const chips = h("div.t-chips.hscroll");
  SECTIONS.forEach((s, i) => {
    const c = h("div.t-chip", { text: s.name });
    baselineBox(c, 28, 18, 11, "medium");
    bindClass(c, "on", () => B.st.section.value === i);
    gestures(c, { down: () => pressWash(c), tap: () => { B.st.section.value = i; } });
    chips.append(c);
  });
  el.append(chips, hline(44));
  const rowsWrap = h("div.t-rowswrap");
  el.append(rowsWrap);
  swapper(rowsWrap, B, () => B.st.section.value, (i) => {
    const rows = SECTIONS[i].rows.map((d) => scalarRow(B, d));
    const box = rowsAt(52, rows);
    el.style.height = 52 + rows.length * K.row + 8 + "px";
    return box;
  });
  return el;
}

// ---- Mask: add buttons, the list, the selected mask's Invert + rows (PhoneApp.cpp:441-460)
function maskBody(B) {
  const { vm } = B;
  const el = bodyEl();
  el.append(tx("ADD MASK", 16, 20, 11, "medium", "left", "t-muted"));
  const masks = computed(() => vm.ownParams.value.masks);
  const count = computed(() => masks.value.length);
  // The mask just added is selected once the model has it (the reply and the model push race);
  // a selection past the end (another client deleted masks) falls back to the last one.
  let wantSel = -1, prevCount = untracked(() => count.value);
  bindEffect(el, () => {
    const n = count.value;
    untracked(() => {
      if (wantSel >= 0 && n > wantSel) { vm.view.maskIndex.value = wantSel; wantSel = -1; }
      else if (n < prevCount && vm.view.maskIndex.peek() >= n) vm.view.maskIndex.value = n - 1;
      prevCount = n;
    });
  });
  ["Radial", "Linear", "Brush"].forEach((label, i) => {
    const b = h("div.t-mbtn");
    b.style.left = `calc(16px + ${i} * ((100% - 48px) / 3 + 8px))`;
    b.style.width = "calc((100% - 48px) / 3)";
    b.append(tx(label, "50%", 22, 13, "sans", "center", "t-fg"));
    gestures(b, {
      down: () => pressWash(b),
      tap: () => {
        if (!B.hasTarget()) return;
        wantSel = count.peek();
        // EditCommands.cpp:107-115 maskBlob(): 11 numbers - appends; the new mask is selected.
        quiet(vm.maskAdd(`${i},0,0.5,0.5,0.5,0.3,0.3,0.5,0.35,0.5,0.65`));
      },
    });
    el.append(b);
  });
  const empty = tx("Tap a button above to add a mask", "50%", 96, 13, "sans", "center", "t-muted t-fade");
  bindClass(empty, "off", () => count.value > 0);
  el.append(empty);

  const list = h("div.t-mlist");
  list.style.top = "74px";
  el.append(list);
  keyed(list, () => masks.value.map((m, i) => ({ m, i })), (x) => x.i, (x) => {
    const r = h("div.t-mrow");
    r.style.top = x.i * 40 + "px";
    const lab = tx("", 8, 23, 13, "sans", "left");
    r.append(lab);
    r.__lab = lab;
    bindClass(r, "on", () => vm.view.maskIndex.value === x.i);
    gestures(r, { down: () => pressWash(r), tap: () => { vm.view.maskIndex.value = x.i; } });
    return r;
  }, (r, x) => { r.__lab.textContent = `${MASK_TYPES[x.m.type] || "Radial"} ${x.i + 1}`; });

  const ctl = h("div.t-mctl");
  el.append(ctl);
  bindEffect(ctl, () => {
    const n = count.value;
    const top = 74 + n * 40;
    ctl.style.top = top + "px";
    const sel = vm.view.maskIndex.value;
    el.style.height = top + (sel >= 0 && sel < n ? 34 + 4 * K.row : 0) + 8 + "px";
  });
  swapper(ctl, B, () => { const s = vm.view.maskIndex.value; return s >= 0 && s < count.value ? s : -1; }, (mi) => {
    const box = h("div");
    if (mi < 0) return box;
    const mask = () => vm.ownParams.value.masks[mi] || vm.ownParams.value.masks[vm.ownParams.value.masks.length - 1];
    const tg = toggle({ on: () => !!(mask() && mask().inverted), flip: () => { const m = mask(); if (m) quiet(vm.maskSet(mi, { inverted: m.inverted ? "0" : "1" })); } });
    tg.style.left = "16px"; tg.style.top = "4px";
    box.append(tg, tx("Invert", 68, 22, 13, "sans", "left", "t-fg"));
    // PhoneApp.cpp:588-594 - Feather (0..100, 50, /100), Exposure (EV), Contrast, Clarity.
    const adjustRow = (label, key, min, max) => sliderRow({ label, min, max, def: 0 }, {
      value: () => { const m = mask(); return { own: m ? m.adjust[key] || 0 : 0 }; },
      set: (ui, live) => {
        const m = mask(); if (!m) return;
        const f = {};
        for (const k of ["exposure", "contrast", "highlights", "shadows", "whites", "blacks", "temp", "tint", "saturation", "texture", "clarity", "dehaze"])
          f["adjust." + k] = num(k === key ? ui : m.adjust[k] || 0);
        quiet(vm.maskSet(mi, f, { live }));
      },
    });
    box.append(rowsAt(34, [
      sliderRow({ label: "Feather", min: 0, max: 100, def: 50 }, {
        value: () => { const m = mask(); return { own: m ? m.feather * 100 : 50 }; },
        set: (ui, live) => quiet(vm.maskSet(mi, { feather: num(ui / 100) }, { live })),
      }),
      adjustRow("Exposure", "exposure", -5, 5),
      adjustRow("Contrast", "contrast", -100, 100),
      adjustRow("Clarity", "clarity", -100, 100),
    ]));
    return box;
  });
  return el;
}

// ---- Curve: Curve | Mixer, the channel picker, Reset, the in-tray editor (PhoneApp.cpp:461-472)
const CH_COLORS = ["var(--primary)", "var(--t-chr)", "var(--t-chg)", "var(--t-chb)"];
function curveBody(B) {
  const { vm, st } = B;
  const el = bodyEl();
  const s1 = segmented(["Curve", "Mixer"], { sel: () => st.curveMode.value, pick: (i) => { st.curveMode.value = i; } });
  rect(s1, 16, 8, "calc(100% - 32px)", 30);
  el.append(s1);
  const s2wrap = h("div.t-abs");
  rect(s2wrap, 16, 46, "calc(100% - 32px)", 30);
  el.append(s2wrap);
  swapper(s2wrap, B, () => st.curveMode.value, (mode) => mode === 0
    ? segmented(["RGB", "R", "G", "B"], { sel: () => st.curveCh.value, pick: (i) => { st.curveCh.value = i; }, colors: () => CH_COLORS })
    : segmented(["Hue", "Sat", "Lum"], { sel: () => st.mixerCh.value, pick: (i) => { st.mixerCh.value = i; } }));
  const reset = h("div.t-reset");
  reset.append(tx("Reset", "100%", 15, 11, "sans", "right", "t-muted"));
  gestures(reset, {
    down: () => pressWash(reset),
    tap: () => {
      if (!B.hasTarget()) return;
      if (st.curveMode.peek() === 0) {
        const key = ["curve", "curveR", "curveG", "curveB"][st.curveCh.peek()];
        quiet(vm.setFields({ [key]: "0,0;1,1" }));
      } else quiet(vm.setFields({ ["mixer" + st.mixerCh.peek()]: "0,0;120,0;240,0" }));   // MixerPanel reset
    },
  });
  el.append(reset);
  const plotBox = h("div.t-abs");
  el.append(plotBox);
  bindEffect(plotBox, () => {
    const ph = Math.max(120, B.bodyH.value - 100 - 12);
    rect(plotBox, 16, 100, "calc(100% - 32px)", ph);
    el.style.height = 100 + ph + 12 + "px";
  });
  curvePlot(plotBox, vm, {
    mode: () => st.curveMode.value, ch: () => (st.curveMode.value === 0 ? st.curveCh.value : st.mixerCh.value),
    hasTarget: B.hasTarget, inTray: true,
    openFull: () => B.env.sheets.curve({ mode: st.curveMode, curveCh: st.curveCh, mixerCh: st.mixerCh, hasTarget: B.hasTarget }),
  });
  return el;
}

// ---- Grade: region picker, H/S/L + Balance, the remap toggle row, the remap rows (PhoneApp.cpp:473-479, 573-584)
function gradeBody(B) {
  const { vm, st } = B;
  const el = bodyEl();
  const seg = segmented(["Shadows", "Midtones", "Highlights"], { sel: () => st.gradeRegion.value, pick: (i) => { st.gradeRegion.value = i; } });
  rect(seg, 16, 8, "calc(100% - 32px)", 30);
  el.append(seg);
  const rowsWrap = h("div.t-rowswrap");
  el.append(rowsWrap);
  const gradeOf = (r) => {
    const d = vm.drafts.value.get("grade" + r);
    if (d) { const n = d.split(",").map(Number); return { hue: n[0] || 0, sat: n[1] || 0, lum: n[2] || 0 }; }
    return vm.ownParams.value.grade[r];
  };
  swapper(rowsWrap, B, () => st.gradeRegion.value, (r) => {
    const gRow = (label, key, min, max) => sliderRow({ label, min, max, def: 0 }, {
      value: () => ({ own: gradeOf(r)[key] }),
      set: (ui, live) => {
        if (!B.hasTarget()) return;
        const g = { ...untracked(() => gradeOf(r)), [key]: ui };
        quiet(vm.setFields({ ["grade" + r]: fmt.grade(g) }, { live }));
      },
    });
    return rowsAt(46, [gRow("Hue", "hue", 0, 360), gRow("Saturation", "sat", 0, 100), gRow("Luminance", "lum", -100, 100),
                       scalarRow(B, { label: "Balance", field: "balance", min: -100, max: 100, def: 0 })]);
  });
  // the toggle row (38 px, its own space - required, native overlaps it)
  const on = computed(() => { const d = vm.drafts.value.get("remapEnable"); return d !== undefined ? d !== "0" : !!vm.ownParams.value.remapEnable; });
  const tg = toggle({ on: () => on.value, flip: () => { if (B.hasTarget()) quiet(vm.setFields({ remapEnable: on.peek() ? "0" : "1" })); } });
  tg.style.left = "calc(100% - 60px)"; tg.style.top = 46 + 4 * K.row + 6 + "px";
  el.append(tg, tx("Hue Range Remap", 16, 46 + 4 * K.row + 23, 13, "sans", "left", "t-fg"));
  const remapTop = 46 + 4 * K.row + 38;
  const remap = h("div.t-remap");
  remap.style.top = remapTop + "px";
  const rv = (field) => {
    const o = vm.scalar(field, vm.ownParams);
    return field === "remapStrength" ? o * 100 : o;
  };
  const remapRow = (label, field, min, max, def) => sliderRow({ label, min, max, def }, {
    value: () => ({ own: rv(field) }),
    set: (ui, live) => {
      if (!B.hasTarget()) return;
      const f = {};
      for (const k of ["remapSrc", "remapRange", "remapDst", "remapStrength"]) {
        const v = k === field ? ui : untracked(() => rv(k));
        f[k] = num(k === "remapStrength" ? v / 100 : v);
      }
      quiet(vm.setFields(f, { live }));
    },
  });
  remap.append(rowsAt(0, [remapRow("Source Hue", "remapSrc", 0, 360, 0), remapRow("Range", "remapRange", 0, 180, 30),
                          remapRow("Target Hue", "remapDst", 0, 360, 0), remapRow("Strength", "remapStrength", 0, 100, 0)]));
  el.append(remap);
  bindEffect(el, () => {
    const show = on.value;
    remap.classList.toggle("off", !show);
    el.style.height = remapTop + (show ? 4 * K.row : 0) + 8 + "px";
  });
  return el;
}

// ---- Xform: rotation, -90 / +90 / Reset, ASPECT chips (PhoneApp.cpp:480-493)
function xformBody(B) {
  const { vm, st } = B;
  const el = bodyEl(130 + 2 * 40 + 8);
  const rot = h("div.t-abs");
  rect(rot, 0, 8, "100%", K.row);
  rot.append(sliderRow({ label: "", min: -45, max: 45, def: 0, noReadout: true }, {
    value: () => ({ own: vm.scalar("rotation", vm.ownParams) }),
    set: (ui, live) => { if (B.hasTarget()) quiet(vm.setFields({ rotation: num(ui) }, { live })); },
  }));
  el.append(rot);
  const ro = tx("", "calc(100% - 16px)", 22, 13, "mono", "right", "t-fg");
  bindText(ro, () => (vm.scalar("rotation", vm.ownParams) || 0).toFixed(1) + " deg");
  el.append(ro);
  const qt = () => vm.ownParams.peek().quarterTurns | 0;
  [["-90", () => ({ quarterTurns: String((qt() + 3) % 4) })], ["+90", () => ({ quarterTurns: String((qt() + 1) % 4) })],
   ["Reset", () => ({ rotation: "0", quarterTurns: "0" })]].forEach(([label, f], i) => {
    const b = h("div.t-xbtn" + (i === 2 ? ".dim" : ""));
    rect(b, 16 + 84 * i, 64, 76, 32);
    b.append(tx(label, "50%", 21, 13, "sans", "center"));
    gestures(b, { down: () => pressWash(b), tap: () => { if (B.hasTarget()) quiet(vm.setFields(f())); } });
    el.append(b);
  });
  el.append(tx("ASPECT", 16, 120, 11, "medium", "left", "t-muted"));
  const wrap = h("div.t-aspects");
  ASPECTS.forEach(([label, ratio], i) => {
    const c = h("div.t-achip", { text: label });
    baselineBox(c, 32, 21, 13, "sans");
    bindClass(c, "on", () => st.aspect.value === i);
    gestures(c, {
      down: () => pressWash(c),
      tap: () => {
        st.aspect.value = i;
        if (!B.hasTarget()) return;
        if (!ratio) { quiet(vm.setFields({ crop: "0,0,1,1" })); return; }
        const src = vm.source ? vm.source.peek() : { w: 0, h: 0 };
        const sw = src.w || 0, sh = src.h || 0;
        if (!(sw > 0 && sh > 0)) return;
        const crop = applyRatio(vm.ownParams.peek().crop, (ratio * sh) / sw);
        quiet(vm.setFields({ crop: fmt.crop(crop) }));
      },
    });
    wrap.append(c);
  });
  el.append(wrap);
  return el;
}

const BODIES = [basicBody, maskBody, curveBody, gradeBody, xformBody];
