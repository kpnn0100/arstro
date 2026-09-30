/*
 * Cosmo by arstro — xform.js: the Xform page (widgets/XformPanel.{h,cpp}); it does not scroll.
 *
 *   "Rotate" header; the readout (curvePlotBg + border r2, 9.75 / 27.95, 178.2 x 24.05,
 *   JetBrains Mono 11 primary, centred by estimate: sign + whole + "." + lround(tenths) + "deg" -
 *   tenths can read 10, a native quirk kept); "-90deg" / "+90deg" outline pills (43 x 24.05);
 *   reset (rotateCcw, 24.05^2 at 290.2)
 *   "Aspect Ratio" (61.75); chips Free 1:1 4:3 16:9 3:2 5:4 Custom (y 89.7, h 16.25, gap 3.25,
 *   radius 1; active = primary 0.10 fill + primary stroke + primary label)
 *   Custom row (only with Custom): TextBox W, ":" (mono 10 muted), TextBox H at 110.825
 *   "Flip" (115.7, +20 with the Custom row) Horizontal / Vertical; "Auto" Auto Horizon /
 *   Auto Geometry - inert, as in the native (they hover and send nothing)
 *
 * Commands: readout scrub (drag, 0.15 deg/px from the drag start, clamped +-45) -> `set
 * rotation=` live + final; +-90 -> `set quarterTurns=((q +- 1) % 4 + 4) % 4`; reset ->
 * `set rotation=0`; a ratio chip (not Free) -> lock + `set crop=` of applyRatio(crop, ratio sh/sw)
 * when the source size is known; Free -> lock 0, crop untouched. The Custom fields re-apply the
 * ratio when both parse > 0 and differ from the stored pair. The chip / lock is view state
 * (panelState) shared with the crop overlay.
 *
 * Deviations (R-G-1): the readout's number eases (220 ms) to a rotation that arrives from the
 * model; the Custom row and the rows under it slide / fade (180 ms) instead of jumping.
 */
import { h, bindEffect } from "../core/dom.js";
import { untracked } from "../core/signal.js";
import { Tween, Ease } from "../core/motion.js";
import { num, fmt } from "../model/params.js";
import { tok, css, place, sectionHeader, pillButton, iconButton, textBox, svgBox, T, rect, estW, FONT, gestures, SECTION_H } from "../ui/controls.js";
import { panelState, PAD, ROW_W, quiet } from "./page.js";
import { applyRatio, normalisedRatio, clamp } from "./geometry.js";

const NAMES = ["Free", "1:1", "4:3", "16:9", "3:2", "5:4", "Custom"];
const RATIOS = [0, 1, 4 / 3, 16 / 9, 3 / 2, 5 / 4, 0];

function readoutText(rot) {
  const neg = rot < 0, a = Math.abs(Math.fround(rot)), whole = Math.trunc(a);
  const tenths = Math.floor(Math.fround((a - whole) * 10) + 0.5);          // lround of a non-negative
  return (neg ? "-" : "+") + whole + "." + tenths + "°";
}

export function xformPage(vm, ctx) {
  const C = tok();
  const st = panelState(vm);
  const el = h("div.cp-abs.cp-page");
  const RH = 4.875 * 2 + 11 * 1.3;          // kRotateRowH 24.05
  const hd = sectionHeader("Rotate", { w: ROW_W });
  place(hd, PAD, 0);
  el.append(hd);
  let y = SECTION_H;
  const turnW = estW("+90°", 10) + 13, gap = 6.5, inner = 3.25;
  const roW = ROW_W - (turnW * 2 + inner) - gap - RH - gap;
  const ro = h("div.cp-abs.cp-readout");
  place(ro, PAD, y, roW, RH);
  const rs = svgBox(roW, RH);
  rs.append(rect(0, 0, roW, RH, 2, { fill: css(C.curvePlotBg), stroke: css(C.border), "stroke-width": 1 }));
  const rt = T(0, RH * 0.5 + 11 * 0.35, "", 11, FONT.mono);
  rt.setAttribute("fill", css(C.primary));
  rs.append(rt);
  ro.append(rs);
  el.append(ro);
  const shown = new Tween(0, (v) => { const t = readoutText(v); rt.textContent = t; rt.setAttribute("x", (roW - estW(t, 11)) * 0.5); });
  shown.set(0);
  const own = () => st.own.peek();
  const m90 = pillButton({ label: "-90°", x: PAD + roW + gap, y, w: turnW, h: RH,
    onClick: () => { if (st.hasTarget.peek()) quiet(vm.setFields({ quarterTurns: num((((own().quarterTurns - 1) % 4) + 4) % 4) })); } });
  const p90 = pillButton({ label: "+90°", x: PAD + roW + gap + turnW + inner, y, w: turnW, h: RH,
    onClick: () => { if (st.hasTarget.peek()) quiet(vm.setFields({ quarterTurns: num((((own().quarterTurns + 1) % 4) + 4) % 4) })); } });
  const reset = iconButton({ icon: "rotateCcw", size: RH, x: PAD + roW + gap + 2 * turnW + inner + gap, y, idleColor: C.muted, activeColor: C.foreground,
    onClick: () => { quiet(vm.setFields({ rotation: "0" })); } });
  el.append(m90.el, p90.el, reset.el);

  // readout scrub (XformPanel::handleGesture)
  let scrub = null, holdUntil = 0, held = 0, rot = 0;
  gestures(ro, {
    dragStart: (p) => { scrub = { x0: p.x, r0: rot }; },
    drag: (p) => {
      if (!scrub) return;
      rot = Math.fround(clamp(scrub.r0 + (p.x - scrub.x0) * 0.15, -45, 45));
      shown.set(rot);
      quiet(vm.setFields({ rotation: num(rot) }, { live: true }));
    },
    drop: () => { if (!scrub) return; scrub = null; holdUntil = performance.now() + 900; held = rot; quiet(vm.setFields({ rotation: num(rot) })); },
  });
  y += RH + 9.75;

  const hd2 = sectionHeader("Aspect Ratio", { w: ROW_W });
  place(hd2, PAD, y);
  el.append(hd2);
  y += SECTION_H;
  const chipH = 1.625 * 2 + 10 * 1.3;
  let cx = PAD;
  const chips = NAMES.map((name, i) => {
    const w = estW(name, 10) + 13;
    const chip = pillButton({ label: name, x: cx, y, w, h: chipH, radius: 1, activeText: C.primary,
      activeBox: { fill: [C.primary[0], C.primary[1], C.primary[2], 0.10], stroke: C.primary }, active: i === st.aspect.peek(),
      onClick: () => pick(i) });
    cx += w + 3.25;
    el.append(chip.el);
    return chip;
  });
  y += chipH + 4.875;
  const customY = y;
  const colonW = estW(":", 10) + 6.5, fieldW = Math.max(28, (ROW_W - colonW) * 0.5);
  const custom = h("div.cp-abs.cp-fade");
  place(custom, 0, customY, 324, 20);
  const wf = textBox({ x: PAD, y: 0, w: fieldW, value: String(st.customW.peek()), onInput: pollCustom });
  const hf = textBox({ x: PAD + fieldW + colonW, y: 0, w: fieldW, value: String(st.customH.peek()), onInput: pollCustom });
  const colon = h("div.cp-abs");
  place(colon, PAD + fieldW, 0, colonW, 20);
  const cs = svgBox(colonW, 20);
  const ct = T(colonW * 0.5 - estW(":", 10) * 0.5, 10 + 10 * 0.35, ":", 10, FONT.mono);
  ct.setAttribute("fill", css(C.muted));
  cs.append(ct); colon.append(cs);
  custom.append(wf, colon, hf);
  el.append(custom);
  // Flip + Auto slide down by the Custom row's 20 px while it shows.
  const lower = h("div.cp-abs.cp-slide");
  place(lower, 0, 0, 324, 200);
  el.append(lower);
  let ly = 4.875;
  const hd3 = sectionHeader("Flip", { w: ROW_W }); place(hd3, PAD, ly); lower.append(hd3); ly += SECTION_H;
  const flipW = (ROW_W - 4.875) * 0.5;
  lower.append(pillButton({ label: "Horizontal", x: PAD, y: ly, w: flipW, h: RH }).el, pillButton({ label: "Vertical", x: PAD + flipW + 4.875, y: ly, w: flipW, h: RH }).el);
  ly += RH + 9.75;
  const hd4 = sectionHeader("Auto", { w: ROW_W }); place(hd4, PAD, ly); lower.append(hd4); ly += SECTION_H;
  lower.append(pillButton({ label: "Auto Horizon", x: PAD, y: ly, w: flipW, h: RH }).el, pillButton({ label: "Auto Geometry", x: PAD + flipW + 4.875, y: ly, w: flipW, h: RH }).el);

  function lockedRatio(i = st.aspect.peek()) {
    if (i === 0) return 0;
    if (i === 6) { const w = st.customW.peek(), hh = st.customH.peek(); return w > 0 && hh > 0 ? w / hh : 0; }
    return RATIOS[i];
  }
  function applySelected() {
    const r = lockedRatio();
    if (r <= 0 || !st.hasTarget.peek()) return;
    const src = st.source.peek(), nr = normalisedRatio(r, src.w, src.h);
    if (nr <= 0) return;
    const c = own().crop;
    const f = applyRatio({ x: c.x, y: c.y, w: c.w, h: c.h }, nr);
    quiet(vm.setFields({ crop: fmt.crop({ x: Math.fround(f.x), y: Math.fround(f.y), w: Math.fround(f.w), h: Math.fround(f.h) }) }));
  }
  function pick(i) {
    st.aspect.value = i;
    st.lock.value = lockedRatio(i);
    showSelection();
    if (i !== 0) applySelected();
  }
  function pollCustom() {
    if (st.aspect.peek() !== 6) return;
    const w = parseFloat(wf.value) || 0, hh = parseFloat(hf.value) || 0;
    if (w > 0 && hh > 0 && (w !== st.customW.peek() || hh !== st.customH.peek())) {
      st.customW.value = w; st.customH.value = hh;
      st.lock.value = lockedRatio();
      applySelected();
    }
  }
  function showSelection(snap) {
    const i = st.aspect.peek();
    chips.forEach((c, k) => c.set(k === i, snap));
    const showCustom = i === 6;
    custom.classList.toggle("off", !showCustom);
    lower.style.transform = `translateY(${customY + (showCustom ? 20 : 0)}px)`;
  }
  showSelection(true);

  bindEffect(el, () => {
    const p = st.own.value;
    untracked(() => {
      if (scrub) return;
      if (holdUntil) { if (performance.now() < holdUntil && Math.abs(p.rotation - held) > 1e-4) return; holdUntil = 0; }
      if (p.rotation !== rot) { rot = p.rotation; shown.to(rot, 220, Ease.EaseOutCubic); }
    });
  });
  return { el, setViewHeight() {}, setContentHeight() {}, scrollBy() {} };
}
