/*
 * Cosmo by arstro — page.js: what the five edit pages share.
 *
 *   scrollPage()   the eased page scroll of ParamPanel / MaskPanel / GradePanel / StackPanel
 *                  (ParamPanel.cpp scrollBy/advance): the target moves by whole wheel steps,
 *                  clamped to [0, contentH - viewH]; the offset follows it over 180 ms
 *                  EaseOutCubic from wherever it is. No scrollbar, no drag-scroll, no overscroll.
 *   panelState(vm) the column's own view state that the on-photo overlays read too (the aspect
 *                  lock, the selected mask) - one per view-model, i.e. per page.
 *   target(vm)     hasEditTarget (CORE GAP G2 until the model says it: editGroup >= 0 ||
 *                  currentSlot >= 0), the bypass flag, the photo's source size.
 */
import { h } from "../core/dom.js";
import { signal, computed } from "../core/signal.js";
import { Tween, Ease } from "../core/motion.js";

export const PAD = 9.75;          // every panel's kPadX
export const ROW_W = 304.5;       // 324 - 2 x 9.75
export const PAD_BOTTOM = 13;     // kPadBottom

export function scrollPage() {
  const el = h("div.cp-abs.cp-page");
  const content = h("div.cp-abs.cp-page-content");
  el.append(content);
  let contentH = 0, viewH = 0, target = 0;
  const tw = new Tween(0, (v) => { content.style.transform = `translateY(${-v}px)`; });
  const maxScroll = () => Math.max(0, contentH - viewH);
  function retarget() {
    const t = Math.min(maxScroll(), Math.max(0, target));
    if (t !== target) { target = t; tw.to(target, 180, Ease.EaseOutCubic); }
  }
  return {
    el, content,
    setContentHeight(hh) { contentH = hh; content.style.height = hh + "px"; retarget(); },
    setViewHeight(hh) { viewH = hh; retarget(); },
    /** scrollBy(d): target = clamp(target - d) - d > 0 moves toward the top. */
    scrollBy(d) {
      const t = Math.min(maxScroll(), Math.max(0, target - d));
      if (t === target) return;
      target = t;
      tw.to(target, 180, Ease.EaseOutCubic);
    },
    get scroll() { return tw.value; },
  };
}

const STATES = new WeakMap();
/** The column's view state shared with the overlays (never sent; one set per page). */
export function panelState(vm) {
  let s = STATES.get(vm);
  if (s) return s;
  const hasTarget = computed(() => vm.hasEditTarget.value);
  // The frozen own params: with no edit target the panels keep what they showed (syncToSlot
  // returns early) - so this only follows the model while there is a target.
  let lastOwn = null, lastEff = null;
  const own = computed(() => { const p = vm.ownParams.value; if (hasTarget.value || !lastOwn) lastOwn = p; return lastOwn; });
  const eff = computed(() => { const p = vm.params.value; if (hasTarget.value || !lastEff) lastEff = p; return lastEff; });
  // The photo's pixel size (sourceWidth/Height) - for aspect-ratio crops. 0 = unknown.
  const source = computed(() => vm.source.value, (a, b) => a.w === b.w && a.h === b.h);
  // Mask selection (RightColumn::syncToSlot): when masks exist one is always selected.
  let lastSel = -1;
  const selMask = computed(() => {
    const n = own.value.masks.length, s0 = vm.view.maskIndex.value;
    if (!hasTarget.value) return lastSel;
    lastSel = n ? Math.min(s0 < 0 ? 0 : s0, n - 1) : -1;
    return lastSel;
  });
  // R-BYPASS-4: target = editGroup >= 0 ? editGroup : the node shown in currentSlot.
  const bypassed = computed(() => {
    const g = vm.editGroup.value, nodes = vm.nodes.value;
    const t = g >= 0 ? nodes.find((n) => n.node === g) : nodes.find((n) => n.slot === vm.currentSlot.value && n.kind === "image");
    return !!(t && t.bypass);
  });
  s = {
    hasTarget, own, eff, source, selMask, bypassed,
    aspect: signal(0),            // XformPanel's selected chip (0 Free .. 6 Custom)
    customW: signal(16), customH: signal(10),
    lock: signal(0),              // the locked pixel ratio w/h, 0 = free (R-CROP-2)
  };
  STATES.set(vm, s);
  return s;
}

/** Swallow a rejected intent: the view-model has already toasted the service's reason. */
export const quiet = (p) => { if (p && p.catch) p.catch(() => {}); return p; };
