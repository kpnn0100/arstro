/*
 * Interstellar — grade/inspector.js: the Grade tab's right column. Ports app/widgets/GradeInspector.{h,cpp}
 * (+ the PluginList it hosts), over COSMO'S OWN web kit (BORROW: ui/slider.js sliderRow).
 *
 * Top to bottom, as GradeInspector::layout: the SCOPES (edit/scopes.js, mounted here with a slot,
 * 168 high); the node's IMAGE PROCESSING list (PluginList: Cosmo first - its switch is the node's
 * bypass - then the node's effects; header 26, rows 26, its height eases with its row count); Cosmo's
 * Mix (the node's weight, `set <bind>.weight=`); then cosmo's EditStackTabs - Basic/Detail ·
 * Mixer/Curve · Grade · Xform - with the 1.5 px accent bar that slides (200 ms EaseOutCubic) to the
 * chosen tab and a card cover that eases off the swapped page (180 ms).
 *
 * Basic/Detail is ParamPanel's seven sections of SliderRows, seeded from gradeOwnParams (what an
 * edit changes) with gradeParams - own as the green stacked reach. Every move is ONE line through
 * kit/cmd.js gradeSet: `set <bind>.basic.exposure=<ev>` - vm.notify while dragging, vm.cmd on release,
 * with RightColumn's unit conversions (toEv, toKelvin, toTint, toRadiusPx).
 *
 * No grade target: a card wash (eased, motion-scroll) names what to do. A different node: the page
 * dips under a card wash that eases away. Mixer/Curve, Grade and Xform pages are not ported yet
 * (they show the page's empty card).
 */
import { own, h } from "../core/dom.js";
import { effect, computed, untracked } from "../core/signal.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { num, gradeSet, setLine } from "../kit/cmd.js";
import { width as measure, ellipsize, baseline, fontEpoch } from "../kit/textfit.js";
import { Painter, canvasSvg, col, fade, lerp, whiteA, Anim, HoverFade, frameLoop, localPoint, inRect, Ease, F } from "../kit/paint.js";
import { slot } from "../kit/slot.js";
import { sliderRow } from "../ui/slider.js";

const SCOPE_H = 168, HEADER_H = 26, ROW_H = 26, LIST_PAD = 4.875, PAD_X = 9.75, MIX_H = 22.75, TAB_H = 27;
const SW_W = 24, SW_H = 13;
const TAB_FONT = 10, TAB_MIN_PAD = 6, SLIDE_MS = 200, FADE_MS = 180;
const TITLES = ["Basic/Detail", "Mixer/Curve", "Grade", "Xform"];
const ENC = new TextEncoder();
const estW = (s, px) => ENC.encode(s).length * px * 0.6;

// cosmo UnitConversions.h
const K_N = 1e6 / 6500, K_W = 1e6 / 2000, K_C = 1e6 / 19500;
const toEv = (v) => v / 80, fromEv = (ev) => ev * 80;
const toKelvin = (v) => { v = Math.max(-100, Math.min(100, v)); return 1e6 / (v <= 0 ? K_N + (-v / 100) * (K_W - K_N) : K_N - (v / 100) * (K_N - K_C)); };
const fromKelvin = (k) => { k = Math.max(2000, Math.min(19500, k)); const m = 1e6 / k; return m >= K_N ? -100 * (m - K_N) / (K_W - K_N) : 100 * (K_N - m) / (K_N - K_C); };
const toTint = (v) => v * 1.5, fromTint = (t) => t / 1.5;
const toRadiusPx = (v) => v / 10, fromRadiusPx = (p) => p * 10;

// ParamPanel sections, GradeInspector.cpp:39-82: [label, min, max, filter, key, toEngine, fromEngine, gradient]
const SECTIONS = [
  ["TONE", [["Exposure", -400, 400, "basic", "exposure", toEv, fromEv], ["Contrast", -100, 100, "basic", "contrast"],
    ["Highlights", -100, 100, "basic", "highlights"], ["Shadows", -100, 100, "basic", "shadows"],
    ["Whites", -100, 100, "basic", "whites"], ["Blacks", -100, 100, "basic", "blacks"]]],
  ["COLOUR", [["Temperature", -100, 100, "basic", "temp", toKelvin, fromKelvin, [[74 / 255, 132 / 255, 232 / 255, 1], [240 / 255, 178 / 255, 84 / 255, 1]]],
    ["Tint", -100, 100, "basic", "tint", toTint, fromTint, [[88 / 255, 196 / 255, 118 / 255, 1], [206 / 255, 104 / 255, 196 / 255, 1]]],
    ["Vibrance", -100, 100, "basic", "vibrance"], ["Saturation", -100, 100, "basic", "saturation"]]],
  ["PRESENCE", [["Texture", -100, 100, "basic", "texture"], ["Clarity", -100, 100, "basic", "clarity"], ["Dehaze", 0, 100, "basic", "dehaze"]]],
  ["EFFECTS", [["Grain Amount", 0, 100, "basic", "grainAmount"], ["Grain Size", 0, 100, "basic", "grainSize"]]],
  ["SHARPENING", [["Amount", 0, 150, "detail", "sharpenAmount"], ["Radius", 5, 30, "detail", "sharpenRadius", toRadiusPx, fromRadiusPx],
    ["Masking", 0, 100, "detail", "sharpenMasking"]]],
  ["NOISE REDUCTION", [["Luminance", 0, 100, "detail", "nrLuminance"], ["Colour", 0, 100, "detail", "nrColor"]]],
  ["LENS", [["Distortion", -100, 100, "detail", "lensDistortion"], ["Defringe", 0, 100, "detail", "lensCA"], ["Vignette", -100, 100, "detail", "lensVignette"]]],
];

export function mount(host, vm, ctx) {
  useCss("css/grade/inspector.css");
  host.classList.add("gi");
  const svg = canvasSvg();
  host.append(svg);
  const p = new Painter(svg);
  const loop = frameLoop(step);
  own(host, () => loop.kill());

  let W = 0, H = 0, bind = "", node = null, items = [], has = false, rackEmpty = true, selected = "", lastBind = "";
  let tab = 0, tabPending = false, tabInit = false, init = false, hasApplied = false, swapPending = false, pressed = false;
  const listH = new Anim(HEADER_H + LIST_PAD), indX = new Anim(0), indW = new Anim(0), pageFade = new Anim(1);
  const emptyAmt = new Anim(1), swap = new Anim(1), tabHover = new HoverFade(), rowHover = new HoverFade();
  const swAmt = new Map();   // the switches slide (eased)

  // ---------------------------------------------------------------- the scopes (edit/scopes.js)
  const scopeHost = h("div.gi-scopes");
  host.append(scopeHost);
  const scopeBox = computed(() => { const b = ctx.box.value; return { x: b.x, y: b.y, w: b.w, h: SCOPE_H }; });
  try {
    if (typeof ctx.slot === "function") ctx.slot(scopeHost, "edit/scopes.js", { name: "ScopePanel", box: scopeBox });
    else slot(scopeHost, "edit/scopes.js", vm, ctx.child ? ctx.child(scopeBox) : ctx, { name: "ScopePanel" });
  } catch (e) { console.error("interstellar: GradeInspector scopes:", e); }

  // ---------------------------------------------------------------- one line out (GradeInspector::send)
  const send = (filter, fields, live) => {
    if (!bind) return;
    const line = gradeSet(bind, filter, fields);
    if (live) vm.notify(line); else vm.cmd(line);
  };

  // ---------------------------------------------------------------- Cosmo's Mix (the node's weight)
  const mixHost = h("div.gi-mix");
  host.append(mixHost);
  let mix = null;

  // ---------------------------------------------------------------- Basic/Detail (ParamPanel)
  const page = h("div.gi-page"), content = h("div.gi-page-content");
  page.append(content);
  host.append(page);
  const rows = [];
  let contentH = 0, scrollT = 0;
  const scroll = new Anim(0);
  for (const [title, list] of SECTIONS) {
    const sec = h("div.gi-sec");
    sec.textContent = title;
    content.append(sec);
    contentH += 27.95;
    for (const [label, min, max, filter, key, toE, fromE, gradient] of list) {
      const wrap = h("div.gi-row");
      const r = sliderRow({ label, min, max, width: 304.5, gradient: gradient || null,
        onChange: (v, { live }) => send(filter, [[key, num(toE ? toE(v) : v)]], live) });
      wrap.append(r.el);
      content.append(wrap);
      rows.push({ r, filter, key, fromE: fromE || ((x) => x) });
      contentH += 22.75;
    }
  }
  contentH += 13;
  own(host, () => { rows.length = 0; });

  // ---------------------------------------------------------------- the empty wash
  const wash = h("div.gi-wash"), washA = h("b"), washB = h("span");
  wash.append(washA, washB);
  host.append(wash);

  function ensureMix() {
    if (mix || !(W > 0)) return;
    mix = sliderRow({ label: "Mix", min: 0, max: 100, width: Math.max(0, W - 2 * PAD_X),
      onChange: (v, { live }) => { if (!bind) return; const l = setLine(bind + ".weight", num(v / 100)); if (live) vm.notify(l); else vm.cmd(l); } });
    mixHost.append(mix.el);
    if (node) mix.set(node.weight * 100);
  }

  // ---------------------------------------------------------------- bind (GradeInspector::bind)
  own(host, effect(() => {
    const m = vm.model.value;
    if (!m) return;
    const n = vm.gradeTarget.value;
    untracked(() => {
      has = !!(m.hasGradeTarget && n);
      rackEmpty = !(m.rack && m.rack.length);
      node = has ? n : null;
      bind = has ? n.bindName || "" : "";
      items = [];
      if (has) {
        items.push({ id: "", label: "Cosmo", sub: n.group ? "colour · group" : "colour", enabled: !n.bypass, mix: n.weight });
        for (const e of m.effects || [])
          if (e.node === n.node) {
            let sub = "";
            if (e.params && e.params.length) { const q0 = e.params[0]; sub = num(Math.round(q0.value * (q0.unit ? 1 : 100))) + (q0.unit ? " " + q0.unit : "%"); }
            items.push({ id: e.id, label: e.label, sub, enabled: !!e.enabled, mix: e.mix });
          }
      }
      if (!items.some((it) => it.id === selected)) selected = "";
      if (mix && node && !pressed) mix.set(node.weight * 100);
      if (has && bind !== lastBind) { if (lastBind) swapPending = true; lastBind = bind; seed(m); }
      else if (has && !pressed) seed(m);
      washA.textContent = rackEmpty ? "Nothing to grade yet." : "Nothing selected to grade.";
      washB.textContent = rackEmpty ? "Add footage to the rack to start grading." : "Pick a source or a group in the rack.";
      loop.kick();
    });
  }));
  function seed(m) {
    const own_ = m.gradeOwnParams || {}, eff = m.gradeParams || own_;
    for (const { r, key, fromE } of rows) {
      const ov = own_[key] !== undefined ? fromE(Number(own_[key])) : 0;
      const ev = eff[key] !== undefined ? fromE(Number(eff[key])) : ov;
      r.set(ov);
      if (r.setOffset) r.setOffset(ev - ov);
    }
  }
  own(host, effect(() => { const b = ctx.box.value; W = b.w; H = b.h; fontEpoch.value; untracked(() => { ensureMix(); loop.kick(); }); }));

  // ---------------------------------------------------------------- geometry
  const wantedListH = () => HEADER_H + (has ? items.length * ROW_H : 0) + LIST_PAD;
  const tabsTop = () => SCOPE_H + listH.value + MIX_H + 13;
  function tabW(i) {
    let sum = 0;
    for (const s of TITLES) sum += estW(s, TAB_FONT);
    const pad = Math.max(TAB_MIN_PAD, (W - sum) / (2 * TITLES.length));
    return estW(TITLES[i], TAB_FONT) + 2 * pad;
  }
  const tabX = (i) => { let x = 0; for (let k = 0; k < i; k++) x += tabW(k); return x; };
  const addRect = () => ({ x: W - PAD_X - 46, y: SCOPE_H + 4, w: 46, h: HEADER_H - 8 });
  const rowRect = (i) => ({ x: 0, y: SCOPE_H + HEADER_H + i * ROW_H, w: W, h: ROW_H });
  const switchRect = (i) => { const r = rowRect(i); return { x: PAD_X, y: r.y + (ROW_H - SW_H) * 0.5, w: SW_W, h: SW_H }; };

  function advance(now) {
    let moving = false;
    if (!init) { emptyAmt.set(has ? 0 : 1); hasApplied = has; listH.set(wantedListH()); init = true; }
    if (has !== hasApplied) { emptyAmt.animateTo(has ? 0 : 1, tok.ms("motion-scroll"), Ease.EaseOutCubic, now); hasApplied = has; }
    if (swapPending) { swap.set(0); swap.animateTo(1, tok.ms("motion-scroll"), Ease.EaseOutCubic, now); swapPending = false; }
    const want = wantedListH();
    if (Math.abs(listH.target - want) > 1e-6) listH.animateTo(want, tok.ms("motion-scroll"), Ease.EaseOutCubic, now);
    if (!tabInit && W > 0) { indX.set(tabX(tab)); indW.set(tabW(tab)); tabInit = true; }
    else if (tabPending) {
      indX.animateTo(tabX(tab), SLIDE_MS, Ease.EaseOutCubic, now); indW.animateTo(tabW(tab), SLIDE_MS, Ease.EaseOutCubic, now);
      pageFade.set(0); pageFade.animateTo(1, FADE_MS, Ease.EaseOutCubic, now); tabPending = false;
    }
    items.forEach((it, i) => {
      let a = swAmt.get(it.id || "\u0000");
      if (!a) { a = new Anim(it.enabled ? 1 : 0); a.want = it.enabled; swAmt.set(it.id || "\u0000", a); }
      if (a.want !== it.enabled) { a.animateTo(it.enabled ? 1 : 0, tok.ms("motion-scroll"), Ease.EaseOutCubic, now); a.want = it.enabled; }
      moving = a.update(now) || moving;
    });
    for (const a of [listH, indX, indW, pageFade, emptyAmt, swap, scroll]) moving = a.update(now) || moving;
    moving = tabHover.advance(now) || moving;
    moving = rowHover.advance(now) || moving;
    return moving;
  }

  function place() {
    const lt = SCOPE_H + listH.value, tt = tabsTop();
    Object.assign(scopeHost.style, { top: "0px", width: W + "px", height: SCOPE_H + "px" });
    Object.assign(mixHost.style, { left: PAD_X + "px", top: lt + 6.5 + "px", width: Math.max(0, W - 2 * PAD_X) + "px", height: MIX_H + "px" });
    const pageTop = tt + TAB_H, pageH = Math.max(0, H - pageTop);
    Object.assign(page.style, { top: pageTop + "px", width: W + "px", height: pageH + "px", display: tab === 0 ? "" : "none" });
    content.style.transform = `translateY(${-scroll.value}px)`;
    page.style.opacity = String(pageFade.value * (0.1 + 0.9 * swap.value));
    page.inert = !has;
    mixHost.inert = !has;
    const ea = emptyAmt.value;
    Object.assign(wash.style, { top: SCOPE_H + "px", width: W + "px", height: Math.max(0, H - SCOPE_H) + "px", opacity: String(ea),
      display: ea > 0.001 ? "flex" : "none", paddingTop: Math.max(0, (H - SCOPE_H) * 0.38 - 14) + "px" });
    return pageH;
  }

  function paint() {
    const fg = col("foreground"), muted = col("muted-foreground"), border = col("border"), primary = col("primary"), card = col("card");
    p.begin();
    p.rect(0, 0, W, H, 0, { fill: card });
    // ---- IMAGE PROCESSING (PluginList)
    const hy = SCOPE_H + HEADER_H * 0.5;
    p.text("IMAGE PROCESSING", PAD_X, baseline(hy, 9), 9, F.semibold, muted, 0.13 * 9);
    if (has) {
      const ar = addRect(), hv = rowHover.amount(1000);
      p.text("+", ar.x + 8, baseline(ar.y + ar.h * 0.5, 11), 11, F.sans, lerp(muted, fg, hv));
      p.text("Add", ar.x + 18, baseline(ar.y + ar.h * 0.5, 10), 10, F.sans, lerp(muted, fg, hv));
    } else p.text("select a source or a group", PAD_X + 112, baseline(hy, 9.5), 9.5, F.sans, fade(muted, 0.7));
    p.clip(0, SCOPE_H + HEADER_H, W, Math.max(0, listH.value - HEADER_H));
    items.forEach((it, i) => {
      const r = rowRect(i), cy = r.y + ROW_H * 0.5, sel = it.id === selected, hv = rowHover.amount(i);
      if (sel) p.rect(0, r.y, W, ROW_H, 0, { fill: fade(primary, 0.12) });
      else if (hv > 0.001) p.rect(0, r.y, W, ROW_H, 0, { fill: whiteA(0.05 * hv) });
      const sr = switchRect(i), a = (swAmt.get(it.id || "\u0000") || { value: it.enabled ? 1 : 0 }).value;
      p.rect(sr.x, sr.y, sr.w, sr.h, SW_H * 0.5, { fill: lerp(col("secondary"), primary, a), stroke: border, sw: 1 });
      p.circle(sr.x + SW_H * 0.5 + a * (SW_W - SW_H), sr.y + SW_H * 0.5, SW_H * 0.5 - 2, { fill: col("white") });
      const nx = PAD_X + SW_W + 9, fam = it.id ? F.sans : F.medium;
      const mixS = num(Math.round(it.mix * 100)) + "%", mx = W - PAD_X - measure(mixS, 9, F.mono);
      const nm = ellipsize(it.label, Math.max(0, mx - nx - 70), 10.5, fam);
      p.text(nm, nx, baseline(cy, 10.5), 10.5, fam, it.enabled ? fg : muted);
      const sx = nx + measure(nm, 10.5, fam) + 7;
      if (it.sub && mx - 8 > sx) p.text(ellipsize(it.sub, mx - 8 - sx, 9, F.sans), sx, baseline(cy, 9), 9, F.sans, muted);
      p.text(mixS, mx, baseline(cy, 9), 9, F.mono, muted);
    });
    p.restore();
    p.line(0, SCOPE_H + listH.value - 0.5, W, SCOPE_H + listH.value - 0.5, border, 1);
    // ---- EditStackTabs
    const ty = tabsTop();
    p.rect(0, ty, W, TAB_H, 0, { fill: col("background") });
    const selL = tabX(tab), selR = selL + tabW(tab);
    p.rect(selL, ty, tabW(tab), TAB_H + 2, 0, { fill: card });
    for (let i = 0; i < TITLES.length; i++) {
      if (i === tab) continue;
      const hv = tabHover.amount(i);
      if (hv > 0.001) p.rect(tabX(i), ty, tabW(i), TAB_H, 0, { fill: whiteA(0.06 * hv) });
    }
    p.line(0, ty + TAB_H, selL, ty + TAB_H, border, 1);
    p.line(selR, ty + TAB_H, W, ty + TAB_H, border, 1);
    p.rect(indX.value, ty, indW.value, 1.5, 0, { fill: primary });
    for (let i = 0; i < TITLES.length; i++) {
      const c = i === tab ? primary : lerp(muted, primary, 0.5 * tabHover.amount(i));
      p.text(TITLES[i], tabX(i) + (tabW(i) - estW(TITLES[i], TAB_FONT)) * 0.5, ty + TAB_H * 0.5 + TAB_FONT * 0.35, TAB_FONT, F.medium, c);
    }
    if (tab !== 0 && has) {
      const s = TITLES[tab] + " — not available on the web yet.";
      p.text(s, (W - measure(s, 11, F.sans)) * 0.5, ty + TAB_H + 40, 11, F.sans, fade(muted, pageFade.value));
    }
    p.line(0.5, 0, 0.5, H, border, 1);
    p.end();
  }

  function step(now) {
    if (!(W > 0) || !(H > 0)) return false;
    ensureMix();
    const more = advance(now);
    const pageH = place();
    const maxS = Math.max(0, contentH - pageH);
    if (scrollT > maxS) { scrollT = maxS; scroll.animateTo(scrollT, 180, Ease.EaseOutCubic, now); }
    paint();
    return more;
  }

  // ---------------------------------------------------------------- input
  const tabAt = (pt) => {
    const ty = tabsTop();
    if (pt.y < ty || pt.y >= ty + TAB_H) return -1;
    for (let i = 0; i < TITLES.length; i++) if (pt.x >= tabX(i) && pt.x < tabX(i) + tabW(i)) return i;
    return -1;
  };
  const rowAt = (pt) => { if (!has) return -1; for (let i = 0; i < items.length; i++) if (inRect(pt, rowRect(i))) return i; return -1; };
  host.addEventListener("pointermove", (e) => {
    const pt = localPoint(host, e), t = tabAt(pt);
    tabHover.setHovered(t >= 0 && t !== tab ? t : -1);
    rowHover.setHovered(has && inRect(pt, addRect()) ? 1000 : rowAt(pt));
    loop.kick();
  });
  host.addEventListener("pointerleave", () => { tabHover.clear(); rowHover.clear(); loop.kick(); });
  host.addEventListener("pointerdown", () => { pressed = true; });
  const up = () => { pressed = false; };
  window.addEventListener("pointerup", up);
  own(host, () => window.removeEventListener("pointerup", up));
  host.addEventListener("click", (e) => {
    const pt = localPoint(host, e), t = tabAt(pt);
    if (t >= 0) { if (t !== tab) { tab = t; tabPending = true; scrollT = 0; scroll.set(0); } loop.kick(); return; }
    if (!has) return;
    if (inRect(pt, addRect())) {
      const b = ctx.box.peek ? ctx.box.peek() : ctx.box.value, ar = addRect();
      const add = vm.provided && vm.provided("addEffect");
      if (add) add({ x: b.x + ar.x, y: b.y + ar.y, w: ar.w, h: ar.h }, bind);
      return;
    }
    const i = rowAt(pt);
    if (i < 0) return;
    const it = items[i];
    if (inRect(pt, switchRect(i))) {   // PluginList: the switch is a command line
      vm.cmd(it.id ? setLine(it.id + ".enabled", it.enabled ? "0" : "1") : setLine(bind + ".bypass", it.enabled ? "1" : "0"));
      return;
    }
    selected = it.id;   // presentation only
    loop.kick();
  });
  host.addEventListener("wheel", (e) => {
    const ty = tabsTop() + TAB_H, pt = localPoint(host, e);
    if (tab !== 0 || pt.y < ty) return;
    const pageH = Math.max(0, H - ty), maxS = Math.max(0, contentH - pageH);
    const t = Math.max(0, Math.min(maxS, scrollT + (e.deltaMode === 1 ? e.deltaY * 16 : e.deltaY)));
    if (t === scrollT) return;
    e.preventDefault();
    scrollT = t;
    scroll.animateTo(t, 180, Ease.EaseOutCubic, performance.now());
    loop.kick();
  }, { passive: false });
  loop.kick();
}
