/*
 * Cosmo by arstro — grade.js: the Grade page as the native really is (widgets/GradePanel.{h,cpp}):
 * no colour wheels - a region picker drives three sliders.
 *
 *   Shadows/Midtones/Highlights picker (x 9.75, y 6.5, 304.5 x 22.75, Roboto 10)
 *   header = the region's name (32.5); Hue 0..360, Saturation 0..100, Luminance -100..100
 *   header "Balance" (120.45); Balance -100..100
 *   header "Hue Remap" (168.4); "Enable" (Roboto 10 muted, baseline 209.6) + ToggleSwitch
 *   (x 291.5, y 199.1); Source 0..360, Range 0..180, Target 0..360, Strength 0..100
 *   content height 308.85
 *
 * Commands: a region slider -> `set grade<r>=<hue>,<sat>,<lum>` (all three current values);
 * Balance -> `set balance=`; the toggle -> `set remapEnable=1|0`; any remap slider -> all four
 * `set remapSrc= remapRange= remapDst= remapStrength=<0..1>` in one line. The region is view
 * state (re-pushes that region's values). No green reach on these rows.
 */
import { h, bindEffect } from "../core/dom.js";
import { untracked } from "../core/signal.js";
import { num } from "../model/params.js";
import { tok, css, place, sectionHeader, toggleSwitch, svgBox, T, FONT, SECTION_H } from "../ui/controls.js";
import { sliderRow } from "../ui/slider.js";
import { segmented } from "../ui/segmented.js";
import { scrollPage, panelState, PAD, ROW_W, PAD_BOTTOM, quiet } from "./page.js";

const REGIONS = ["Shadows", "Midtones", "Highlights"];

export function gradePage(vm, ctx) {
  const C = tok();
  const st = panelState(vm);
  const page = scrollPage();
  const c = page.content;
  let region = 0;
  const picker = segmented({ labels: REGIONS, x: PAD, y: 6.5, w: ROW_W, h: 22.75, size: 10,
    onChange: (i) => { region = i; regionHd.setLabel(REGIONS[i]); pushRegion(); } });
  c.append(picker.el);
  let y = 6.5 + 22.75 + 3.25;
  const regionHd = sectionHeader(REGIONS[0], { w: ROW_W });
  place(regionHd, PAD, y); c.append(regionHd); y += SECTION_H;
  const row = (label, min, max, onChange) => { const r = sliderRow({ label, min, max, width: ROW_W, onChange }); place(r.el, PAD, y); c.append(r.el); y += 20; return r; };
  const sendGrade = (live) => quiet(vm.setFields({ ["grade" + region]: [hue.value, sat.value, lum.value].map(num).join(",") }, { live }));
  const hue = row("Hue", 0, 360, (_, o) => sendGrade(o.live));
  const sat = row("Saturation", 0, 100, (_, o) => sendGrade(o.live));
  const lum = row("Luminance", -100, 100, (_, o) => sendGrade(o.live));
  const hd2 = sectionHeader("Balance", { w: ROW_W }); place(hd2, PAD, y); c.append(hd2); y += SECTION_H;
  const balance = row("Balance", -100, 100, (v, o) => quiet(vm.setFields({ balance: num(v) }, o)));
  const hd3 = sectionHeader("Hue Remap", { w: ROW_W }); place(hd3, PAD, y); c.append(hd3); y += SECTION_H;
  const enableH = 10 * 1.3 + 6.5;
  const lab = h("div.cp-abs");
  place(lab, PAD, y, 60, enableH);
  const ls = svgBox(60, enableH);
  const lt = T(0, enableH * 0.5 + 10 * 0.35, "Enable", 10, FONT.sans);
  lt.setAttribute("fill", css(C.muted));
  ls.append(lt); lab.append(ls); c.append(lab);
  const toggle = toggleSwitch({ x: 324 - PAD - 22.75, y: y + (enableH - 14) / 2,
    onChange: (on) => quiet(vm.setFields({ remapEnable: on ? "1" : "0" })) });
  c.append(toggle.el);
  y += enableH;
  const sendRemap = (live) => quiet(vm.setFields({ remapSrc: num(src.value), remapRange: num(range.value), remapDst: num(dst.value),
    remapStrength: num(strength.value / 100) }, { live }));
  const src = row("Source", 0, 360, (_, o) => sendRemap(o.live));
  const range = row("Range", 0, 180, (_, o) => sendRemap(o.live));
  const dst = row("Target", 0, 360, (_, o) => sendRemap(o.live));
  const strength = row("Strength", 0, 100, (_, o) => sendRemap(o.live));
  page.setContentHeight(y + PAD_BOTTOM);

  function pushRegion() {
    const g = st.own.peek().grade[region];
    hue.set(g.hue); sat.set(g.sat); lum.set(g.lum);
  }
  let first = true;
  bindEffect(page.el, () => {
    const p = st.own.value;
    untracked(() => {
      pushRegion();
      balance.set(p.balance);
      toggle.set(p.remapEnable, first);
      src.set(p.remapSrc); range.set(p.remapRange); dst.set(p.remapDst); strength.set(p.remapStrength * 100);
      first = false;
    });
  });
  return page;
}
