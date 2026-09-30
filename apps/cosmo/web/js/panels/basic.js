/*
 * Cosmo by arstro — basic.js: the Basic/Detail page (widgets/ParamPanel + RightColumn.cpp:70-129).
 *
 * One scrolling list of the catalogue the model publishes (`controls`, EditControls.h): per
 * section a SectionHeader (27.95) then 20 px SliderRows at x 9.75, w 304.5; content height =
 * last row + 13 (668.65 for the 7 sections / 23 rows). The COLOUR section carries the
 * white-balance eyedropper (R-WB-1): an IconButton 25.95^2 at x 288.3, y header + 1, muted idle,
 * primary while armed; a click toggles vm.view.wbArmed (view state - the stage turns the next
 * click on the photo into `wb pick` and disarms).
 *
 * Rows show the edit target's OWN params in track units and the green stacked reach
 * flat(effective) - flat(own) (DR-EDIT-4); a drag sends `set <field>=<engine>` as live notifies
 * and a final call. Track <-> engine uses the exact UnitConversions.h formulas for the four
 * converted fields and the catalogue's sampled stops for anything else.
 */
import { bindEffect } from "../core/dom.js";
import { untracked } from "../core/signal.js";
import { num } from "../model/params.js";
import { toEngine as stopsToEngine, toTrack as stopsToTrack } from "../model/controls.js";
import { tok, parseColor, place, sectionHeader, iconButton, SECTION_H, SECTION_ACTION } from "../ui/controls.js";
import { sliderRow } from "../ui/slider.js";
import { scrollPage, panelState, PAD, ROW_W, PAD_BOTTOM, quiet } from "./page.js";
import { EXACT } from "./geometry.js";

const convert = (c) => {
  const x = EXACT[c.field];
  return x ? { toEngine: x[0], toTrack: x[1] } : { toEngine: (t) => stopsToEngine(c, t), toTrack: (e) => stopsToTrack(c, e) };
};

export function basicPage(vm, ctx) {
  const C = tok();
  const st = panelState(vm);
  const page = scrollPage();
  const rows = [];            // {c, conv, row}
  let pipette = null;

  function build(catalogue) {
    page.content.replaceChildren();
    rows.length = 0;
    let y = 0;
    for (const sec of catalogue) {
      const hasAction = !!sec.whiteBalancePicker;
      const hd = sectionHeader(sec.title, { w: ROW_W, trailingW: hasAction ? SECTION_ACTION : 0 });
      place(hd, PAD, y);
      page.content.append(hd);
      if (hasAction) {
        pipette = iconButton({ icon: "pipette", size: SECTION_ACTION, idleColor: C.muted, activeColor: C.primary,
          onClick: () => { vm.view.wbArmed.value = !vm.view.wbArmed.peek(); } });
        place(pipette.el, Math.max(0, 324 - PAD - SECTION_ACTION), y + 1);
        pipette.set(vm.view.wbArmed.peek(), true);
        page.content.append(pipette.el);
      }
      y += SECTION_H;
      for (const c of sec.controls) {
        const conv = convert(c);
        const gradient = c.rampFrom && c.rampTo ? [parseColor(c.rampFrom), parseColor(c.rampTo)] : null;
        const row = sliderRow({ label: c.label, min: c.min, max: c.max, width: ROW_W, gradient,
          onChange: (t, { live }) => quiet(vm.setFields({ [c.field]: num(conv.toEngine(t)) }, { live })) });
        place(row.el, PAD, y);
        page.content.append(row.el);
        rows.push({ c, conv, row });
        y += 20;
      }
    }
    page.setContentHeight(y + PAD_BOTTOM);
    untracked(seed);
  }

  function seed() {
    const own = st.own.value, eff = st.eff.value;
    for (const r of rows) {
      const t = r.conv.toTrack(own[r.c.field]);
      r.row.set(t);
      const off = r.conv.toTrack(eff[r.c.field]) - t;
      r.row.setOffset(Math.abs(off) > 1e-9 ? off : 0);
    }
  }

  let built = null;
  bindEffect(page.el, () => {
    const cat = vm.controls.value;
    if (cat && cat !== built) { built = cat; build(cat); }
  });
  bindEffect(page.el, () => { st.own.value; st.eff.value; if (rows.length) seed(); });
  bindEffect(page.el, () => { const armed = vm.view.wbArmed.value; if (pipette) pipette.set(armed); });
  return page;
}
