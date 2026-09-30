/*
 * Cosmo by arstro — mask.js: the Mask page (widgets/MaskPanel + RightColumn.cpp:131-187).
 *
 *   "Add Mask" header, then 4 outline chips Radial / Linear / Brush / Draw at y 27.95
 *   (w 72.469, gap 4.875, h 22.75, Roboto 10 muted); with a mask selected: the select row at
 *   60.45 - ComboBox 242.8 wide ("<Type> <i+1>" + " (N pts)" / " (N)" for Draw, " (inv)"),
 *   the "Inv" pill (29.2, Roboto 9) and the trash IconButton (22.75^2) - then Feather (0..100),
 *   "Tone" Exposure (-400..400 = EV x 80) / Highlights / Shadows, "Colour" Temperature
 *   (relative, no Kelvin, no ramp) / Saturation, "Presence" Dehaze (0..100).
 *
 * Commands: chip i -> `set mask=<i>,0,0.5,0.5,0.5,0.3,0.3,0.5,0.35,0.5,0.65` and select the new
 * last mask; combo -> selection only (view state); Inv -> `mask set <i> inverted=0|1`; trash ->
 * `mask delete <i>`; Feather -> `mask set <i> feather=<t/100>`; any adjust row -> the 12
 * `adjust.*` keys in order, from the selected mask's own adjust with that field replaced.
 * The list is the edit target's OWN masks (effective params append group masks).
 *
 * Deviation (R-G-1): the per-mask controls fade in / out (180 ms) where the native shows /
 * hides them in one frame.
 */
import { h, bindEffect } from "../core/dom.js";
import { untracked } from "../core/signal.js";
import { num, LOCAL_ADJUST } from "../model/params.js";
import { tok, place, sectionHeader, pillButton, iconButton, comboBox, SECTION_H } from "../ui/controls.js";
import { sliderRow } from "../ui/slider.js";
import { scrollPage, panelState, PAD, ROW_W, PAD_BOTTOM, quiet } from "./page.js";
import { toEv, fromEv } from "./geometry.js";

const TYPES = ["Radial", "Linear", "Brush", "Draw"];
export function maskLabel(m, i) {
  const t = m.type >= 0 && m.type < 4 ? m.type : 0;
  let s = TYPES[t] + " " + (i + 1);
  if (t === 3) s += m.path.length < 3 ? ` (${m.path.length} pts)` : ` (${m.path.length})`;
  if (m.inverted) s += " (inv)";
  return s;
}

export function maskPage(vm, ctx) {
  const C = tok();
  const st = panelState(vm);
  const page = scrollPage();
  const c = page.content;
  const hd = sectionHeader("Add Mask", { w: ROW_W });
  place(hd, PAD, 0);
  c.append(hd);
  const chipW = (ROW_W - 3 * 4.875) / 4;
  TYPES.forEach((name, i) => {
    const chip = pillButton({ label: name, x: PAD + i * (chipW + 4.875), y: SECTION_H, w: chipW, h: 22.75, size: 10,
      onClick: () => {
        if (!st.hasTarget.peek()) return;
        const n = st.own.peek().masks.length;
        quiet(vm.maskAdd(`${i},0,0.5,0.5,0.5,0.3,0.3,0.5,0.35,0.5,0.65`));
        vm.view.maskIndex.value = n;        // the new mask is the last one (set mask= appends)
      } });
    c.append(chip.el);
  });

  // ---- the per-mask group (select row + sliders), faded as one
  const grp = h("div.cp-abs.cp-fade");
  place(grp, 0, 0, 324, 0);
  c.append(grp);
  let y = 60.45;
  const combo = comboBox({ x: PAD, y, w: 324 - PAD - 22.75 - 4.875 - 29.2 - 4.875 - PAD, h: 22.75, host: ctx.popupHost || page.el,
    onChange: (j) => { vm.view.maskIndex.value = j; } });
  const inv = pillButton({ label: "Inv", x: 324 - PAD - 22.75 - 4.875 - 29.2, y, w: 29.2, h: 22.75, size: 9,
    onClick: () => { const s = st.selMask.peek(), m = st.own.peek().masks[s]; if (m) quiet(vm.maskSet(s, { inverted: m.inverted ? "0" : "1" })); } });
  const trash = iconButton({ icon: "deleteBin", size: 22.75, x: 324 - PAD - 22.75, y, idleColor: C.muted, activeColor: C.destructive,
    onClick: () => {
      const s = st.selMask.peek(), n = st.own.peek().masks.length;
      if (s < 0 || s >= n) return;
      quiet(vm.maskDelete(s));
      vm.view.maskIndex.value = n - 1 > 0 ? Math.min(s, n - 2) : -1;
    } });
  grp.append(combo.el, inv.el, trash.el);
  y += 22.75 + 8.125;

  const sel = () => { const s = st.selMask.peek(); return s >= 0 ? st.own.peek().masks[s] : null; };
  function adjustFields(a) { const f = {}; for (const k of LOCAL_ADJUST) f["adjust." + k] = num(a[k] || 0); return f; }
  const rows = [];
  function addRow(label, min, max, key, toE = (t) => t, toT = (v) => v) {
    const row = sliderRow({ label, min, max, width: ROW_W,
      onChange: (t, { live }) => {
        const s = st.selMask.peek(), m = sel();
        if (!m) return;
        if (key === "feather") { quiet(vm.maskSet(s, { feather: num(Math.fround(t / 100)) }, { live })); return; }
        // MaskPanel's mEditing: the selected mask's adjust with every field this panel sent
        // recently (the model may not have echoed them yet) and this one replaced.
        const now = performance.now(), a = { ...m.adjust };
        for (const k in working) if (working[k].i === s && now - working[k].t < 1500) a[k] = working[k].v;
        a[key] = Math.fround(toE(t));
        working[key] = { v: a[key], t: now, i: s };
        quiet(vm.maskSet(s, adjustFields(a), { live }));
      } });
    place(row.el, PAD, y);
    grp.append(row.el);
    rows.push({ row, key, toT });
    y += 20;
    return row;
  }
  const working = {};    // key -> {v, t, i}: adjust fields sent, perhaps not yet in the model
  const header = (label) => { const e = sectionHeader(label, { w: ROW_W }); place(e, PAD, y); grp.append(e); y += SECTION_H; };
  addRow("Feather", 0, 100, "feather");
  header("Tone");
  addRow("Exposure", -400, 400, "exposure", toEv, fromEv);
  addRow("Highlights", -100, 100, "highlights");
  addRow("Shadows", -100, 100, "shadows");
  header("Colour");
  addRow("Temperature", -100, 100, "temp");
  addRow("Saturation", -100, 100, "saturation");
  header("Presence");
  addRow("Dehaze", 0, 100, "dehaze");
  const fullH = y + PAD_BOTTOM, emptyH = 60.45 + PAD_BOTTOM;

  bindEffect(page.el, () => {
    const masks = st.own.value.masks, s = st.selMask.value;
    const has = s >= 0 && s < masks.length;
    grp.classList.toggle("off", !has);
    page.setContentHeight(has ? fullH : emptyH);
    if (!has) return;
    const m = masks[s];
    untracked(() => {
      combo.setOptions(masks.map(maskLabel), s);
      for (const r of rows) r.row.set(r.key === "feather" ? m.feather * 100 : r.toT(m.adjust[r.key] || 0));
    });
  });
  return page;
}
