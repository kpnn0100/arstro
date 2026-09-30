/*
 * Cosmo by arstro — rightcol.js: the desktop editor's right column (widgets/RightColumn,
 * EditStackTabs, ActionBar, HistogramWidget), 324 px wide.
 *
 *   band        y            h
 *   Histogram   0            94.325
 *   Tab strip   94.325       27        Basic/Detail | Mask | Mixer/Curve | Grade | Xform
 *   Page        121.325      H - 160.325
 *   ActionBar   H - 39       39        Save / Import / Export
 *
 * The whole column is filled with `card` first. The tab strip (EditStackTabs.cpp): background
 * strip, the active tab in card extending 2 px down so it welds into the page, idle-tab hover
 * washes (HoverFade), the hairline at y 27 broken under the active tab, the primary accent bar
 * (1.5 high) whose x / width tween over 200 ms EaseOutCubic, labels Roboto Medium 10 (active
 * primary, idle lerp(muted, primary, 0.5 hv)). A switch swaps the page at once and covers it
 * with card fading out over 180 ms EaseOutCubic. Tab widths: pad = max(6, (w - sum est) / 2n),
 * est = bytes x 6.
 *
 * The wheel anywhere over the column scrolls the ACTIVE page by 10 px a notch (App.cpp:910-919);
 * sliders never take the wheel. The bypass scrim (R-BYPASS-4) covers the strip + page when the
 * edit target's filter is off: rgba(5,5,5,0.62 a) (RightColumn.cpp:650, the native literal)
 * plus the "FILTER DISABLED" pill, eased 180 ms EaseInOutCubic, never blocking input.
 *
 * The ActionBar's preset flows are dialogs (owned by the dialog host): Save / Export are skipped
 * with no photos; each sets vm.view.dialog = {name: "preset", mode}. CORE GAPS G5-G7: the
 * category choices have no command yet.
 *
 * Deviations (R-G-1): the active-tab card and label colours cross-fade with the page (180 ms)
 * where the native flips them.
 */
import { h, bindEffect, destroy } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { TABS } from "../vm/editor.js";
import { tok, css, lerpColor, brighten, S, setA, svgBox, T, rect, place, estW, FONT, HoverFade, gestures, icon } from "../ui/controls.js";
import { histogram, HIST_H } from "../ui/histogram.js";
import { panelState } from "../panels/page.js";
import { basicPage } from "../panels/basic.js";
import { maskPage } from "../panels/mask.js";
import { mixerCurvePage } from "../panels/mixer.js";
import { gradePage } from "../panels/grade.js";
import { xformPage } from "../panels/xform.js";

const W = 324, TAB_H = 27, BAR_H = 39;
const TITLES = ["Basic/Detail", "Mask", "Mixer/Curve", "Grade", "Xform"];

export function mountRightColumn(host, vm, ctx = {}) {
  const C = tok();
  const st = panelState(vm);
  const root = h("div.cp-col");
  host.append(root);
  const popupHost = h("div.cp-abs.cp-popups");
  const pctx = { ...ctx, popupHost };

  // ---- histogram
  const hist = histogram(W);
  root.append(hist.el);
  bindEffect(root, () => { const f = vm.frame.value; if (f && f.meta && f.meta.hist) hist.set(f.meta.hist); });

  // ---- tab strip
  const est = TITLES.map((t) => estW(t, 10));
  const pad = Math.max(6, (W - est.reduce((a, b) => a + b, 0)) / (2 * TITLES.length));
  const tabW = est.map((e) => e + 2 * pad);
  const tabX = tabW.map((_, i) => tabW.slice(0, i).reduce((a, b) => a + b, 0));
  const strip = h("div.cp-abs.cp-tabs");
  place(strip, 0, HIST_H, W, TAB_H);
  const ts = svgBox(W, TAB_H + 2);
  ts.append(S("rect", { x: 0, y: 0, width: W, height: TAB_H, fill: css(C.background) }));
  ts.append(S("path", { d: `M0 ${TAB_H}L${W} ${TAB_H}`, stroke: css(C.border), "stroke-width": 1, fill: "none" }));
  const activeBg = TITLES.map((_, i) => { const r = rect(tabX[i], 0, tabW[i], TAB_H + 2, 0, { fill: css(C.card), opacity: 0 }); ts.append(r); return r; });
  const washes = TITLES.map((_, i) => { const r = rect(tabX[i], 0, tabW[i], TAB_H, 0, { fill: css(C.white), opacity: 0 }); ts.append(r); return r; });
  const bar = rect(0, 0, 0, 1.5, 0, { fill: css(C.primary) });
  ts.append(bar);
  const labels = TITLES.map((t, i) => { const l = T(tabX[i] + (tabW[i] - est[i]) * 0.5, TAB_H * 0.5 + 10 * 0.35, t, 10, FONT.medium); ts.append(l); return l; });
  strip.append(ts);
  root.append(strip);
  let selected = Math.max(0, TABS.indexOf(vm.view.tab.peek()));
  const act = TITLES.map((_, i) => new Tween(i === selected ? 1 : 0, paintTabs));
  const hover = new HoverFade(paintTabs);
  const indX = new Tween(tabX[selected], paintBar), indW = new Tween(tabW[selected], paintBar);
  function paintBar() { setA(bar, { x: indX.value, width: indW.value }); }
  function paintTabs() {
    TITLES.forEach((_, i) => {
      const a = act[i].value, hv = i === selected ? 0 : hover.amount(i);
      activeBg[i].setAttribute("opacity", a);
      washes[i].setAttribute("opacity", 0.06 * hv);
      labels[i].setAttribute("fill", css(lerpColor(lerpColor(C.muted, C.primary, 0.5 * hv), C.primary, a)));
    });
  }
  paintBar(); paintTabs();
  const tabAt = (x) => tabX.findIndex((tx, i) => x >= tx && x < tx + tabW[i]);
  gestures(strip, {
    hover: (p) => { const i = p.y <= TAB_H ? tabAt(p.x) : -1; hover.set(i >= 0 && i !== selected ? i : -1); },
    leave: () => hover.set(-1),
    click: (p) => { const i = tabAt(p.x); if (i >= 0 && p.y <= TAB_H) vm.view.tab.value = TABS[i]; },
  });

  // ---- pages
  const pagesEl = h("div.cp-abs.cp-pages");
  root.append(pagesEl);
  const makers = [basicPage, maskPage, mixerCurvePage, gradePage, xformPage];
  const pages = makers.map((mk) => {
    try { return mk(vm, pctx); }
    catch (e) { console.error(e); return { el: h("div.cp-abs.cp-page"), setViewHeight() {}, scrollBy() {} }; }
  });
  pages.forEach((p, i) => { p.el.classList.toggle("hidden", i !== selected); pagesEl.append(p.el); });
  const wash = h("div.cp-abs.cp-wash");
  root.append(wash);

  bindEffect(root, () => {
    const i = Math.max(0, TABS.indexOf(vm.view.tab.value));
    if (i === selected) return;
    selected = i;
    pages.forEach((p, k) => p.el.classList.toggle("hidden", k !== i));
    act.forEach((t, k) => t.to(k === i ? 1 : 0, 180, Ease.EaseOutCubic));
    indX.to(tabX[i], 200, Ease.EaseOutCubic);
    indW.to(tabW[i], 200, Ease.EaseOutCubic);
    hover.set(-1);
    // Page swapped: cover it with the card surface, then ease the cover away.
    wash.classList.remove("fading");
    wash.style.opacity = "1";
    void wash.offsetWidth;
    wash.classList.add("fading");
    wash.style.opacity = "0";
  });

  // ---- action bar
  const barEl = h("div.cp-abs.cp-actionbar");
  const bs = svgBox(W, BAR_H);
  bs.append(S("rect", { x: 0, y: 0, width: W, height: BAR_H, fill: css(C.background) }));
  bs.append(S("path", { d: `M0 0L${W} 0`, stroke: css(C.border), "stroke-width": 1, fill: "none" }));
  const BW = (W - 2 * 9.75 - 2 * 4.875) / 3, BH = 20.75, BY = (BAR_H - BH) * 0.5;
  const NAMES = ["Save", "Import", "Export"], ICONS = ["save", "upload", "download"];
  const btns = NAMES.map((name, i) => {
    const x = 9.75 + i * (BW + 4.875);
    const box = rect(x, BY, BW, BH, 2, { "stroke-width": i ? 1 : null });
    const lw = estW(name, 10), gw = 10 + 4.875 + lw, gx = x + (BW - gw) * 0.5;
    const label = T(gx + 10 + 4.875, BY + BH * 0.5 + 10 * 0.35, name, 10, FONT.medium);
    bs.append(box, label);
    const g = icon(ICONS[i], 10);
    g.classList.add("cp-glyph");
    place(g, gx, BY + (BH - 10) * 0.5, 10, 10);
    return { x, box, label, g };
  });
  barEl.append(bs);
  btns.forEach((b) => barEl.append(b.g));
  root.append(barEl);
  const barHover = new HoverFade(paintBarBtns);
  function paintBarBtns() {
    btns.forEach((b, i) => {
      const hv = barHover.amount(i);
      if (i === 0) setA(b.box, { fill: css(brighten(C.primary, 0.14 * hv)) });
      else setA(b.box, { fill: hv > 0 ? css(C.primary, 0.10 * hv) : "none", stroke: css(lerpColor(C.border, C.primary, 0.5 * hv)) });
      const fg = i === 0 ? C.white : lerpColor(C.muted, C.foreground, 0.6 * hv);
      b.label.setAttribute("fill", css(fg));
      b.g.style.color = css(fg);
    });
  }
  paintBarBtns();
  const btnAt = (p) => (p.y < BY || p.y > BY + BH ? -1 : btns.findIndex((b) => p.x >= b.x && p.x <= b.x + BW));
  gestures(barEl, {
    hover: (p) => barHover.set(btnAt(p)),
    leave: () => barHover.set(-1),
    click: (p) => {
      const i = btnAt(p);
      if (i < 0) return;
      if (i !== 1 && !vm.imageCount.peek()) return;             // Save / Export need photos
      vm.view.dialog.value = { name: "preset", mode: ["save", "import", "export"][i] };
    },
  });

  // ---- bypass scrim (R-BYPASS-4)
  const scrim = h("div.cp-abs.cp-scrim");
  const ss = svgBox(W, 10);
  const scrimFill = S("rect", { x: 0, y: 0, width: W, height: 10, fill: "rgba(5, 5, 5, 0.62)" });  // RightColumn.cpp:650
  const pillLabel = "FILTER DISABLED", pw = estW(pillLabel, 9) + 18, pillY = TAB_H + 8;
  const pill = S("g", { transform: `translate(${(W - pw) * 0.5} ${pillY})` });
  pill.append(rect(0, 0, pw, 19, 2, { fill: css(C.secondary), stroke: css(C.border), "stroke-width": 1 }));
  const pt = T(9 + 11, 19 * 0.5 + 3, pillLabel, 9, FONT.medium);
  pt.setAttribute("fill", "rgba(255, 255, 255, 0.66)");                                            // RightColumn.cpp:666
  pill.append(pt);
  ss.append(scrimFill, pill);
  scrim.append(ss);
  const ban = icon("ban", 9, 1.1);
  ban.classList.add("cp-glyph");
  ban.style.color = "rgba(255, 255, 255, 0.55)";                                                   // RightColumn.cpp:663
  scrim.append(ban);
  root.append(scrim);
  const dim = new Tween(0, (a) => { scrim.style.opacity = a; scrim.style.visibility = a > 0.001 ? "visible" : "hidden"; });
  dim.set(0);
  bindEffect(root, () => { dim.to(st.bypassed.value ? 1 : 0, 180, Ease.EaseInOutCubic); });

  root.append(popupHost);

  // ---- layout (height follows the host)
  // Chrome snaps a transformed box's layout offset to whole pixels (the host sits at y 29.25);
  // the native draws at the fractional offset, so the remainder is put back as a translate.
  function snapFix() {
    let a = host;
    while (a && a !== document.body && getComputedStyle(a).transform === "none") a = a.parentElement;
    const base = a && a !== document.body ? a.getBoundingClientRect() : { left: 0, top: 0 };
    const r = host.getBoundingClientRect(), sc = host.offsetWidth ? r.width / host.offsetWidth : 1;
    const ox = (r.left - base.left) / (sc || 1), oy = (r.top - base.top) / (sc || 1);
    root.style.transform = `translate(${f4(ox - Math.round(ox))}px, ${f4(oy - Math.round(oy))}px)`;
  }
  const f4 = (v) => Math.round(v * 1e4) / 1e4;
  let lastH = -1;
  function layout() {
    snapFix();
    const H = root.offsetHeight;
    if (H === lastH) return;
    lastH = H;
    const pageY = HIST_H + TAB_H, pageH = Math.max(0, H - pageY - BAR_H);
    place(pagesEl, 0, pageY, W, pageH);
    place(wash, 0, pageY, W, pageH);
    pages.forEach((p) => { place(p.el, 0, 0, W, pageH); if (p.setViewHeight) p.setViewHeight(pageH); });
    place(barEl, 0, Math.max(0, H - BAR_H), W, BAR_H);
    const bandH = Math.max(0, H - BAR_H - HIST_H);
    place(scrim, 0, HIST_H, W, bandH);
    setA(ss, { height: bandH, viewBox: `0 0 ${W} ${Math.max(1, bandH)}` });
    setA(scrimFill, { height: bandH });
    const fits = pillY + 19 <= bandH;
    pill.style.display = ban.style.display = fits ? "" : "none";
    place(ban, (W - pw) * 0.5 + 7, pillY + 5, 9, 9);
  }
  const ro = new ResizeObserver(layout);
  ro.observe(root);
  layout();

  // ---- the wheel scrolls the active page, over the whole column
  root.addEventListener("wheel", (e) => {
    if (e.ctrlKey) return;
    e.preventDefault();
    const notches = e.deltaMode === 1 ? e.deltaY / 3 : e.deltaMode === 2 ? e.deltaY * 3 : e.deltaY / 100;
    const p = pages[selected];
    if (p && p.scrollBy) p.scrollBy(-notches * 10);
  }, { passive: false });

  return () => { ro.disconnect(); destroy(root); root.remove(); };
}
