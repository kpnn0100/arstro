/*
 * Interstellar — kit/easedscroll.js: the five co-located parts of a scrollable surface, once (R6).
 *
 * A port of app/widgets/EasedScroll.h, for the self-drawn lists (rack tree, source bin, render queue,
 * version dropdown, Home's grid): a plain TARGET that scrollBy moves and clamps at BOTH ends; an eased
 * live value (motion::kScrollMs 180 ms EaseOutCubic); the viewport and content extents re-measured
 * every layout; "unscrollable" reported so the wheel can BUBBLE to the parent; a bar only when there
 * is something to scroll. ONE viewport rectangle: hand setExtent the same top/height the rows are laid
 * out in, clipped to and culled against.
 *
 *   const sc = new EasedScroll(() => relayout())   onChange: every frame the drawn offset moves
 *   sc.setExtent(viewTop, viewH, contentH)         every layout; clamps the target if content shrank
 *   sc.scrollBy(px) -> bool                        false = nothing to scroll: let the wheel bubble
 *   sc.reveal(top, h)                              bring a content band into view, minimum travel
 *   sc.reset()                                     snap to 0 - only for a NEW list
 *   sc.value / sc.target / sc.maxScroll / sc.scrollable / sc.viewTop / sc.viewH / sc.contentH
 *   sc.bandVisible(top, h)                         is a content band at least partly in view NOW
 *   sc.bar(right) -> {x, y, w, h} | null           the thumb (EasedScroll.h:83-93): 3 wide at right-4,
 *                                                  >= 18 tall, whiteAlpha(0.14), radius pill - draw it
 *   wheelPx(e)                                     a WheelEvent as the native's pixels: one Chrome notch
 *                                                  (100 px) = shell::wheelNotchPx (40) - App::wheel
 */
import { Tween, Ease } from "../core/motion.js";
import { tok } from "./tokens.js";

export class EasedScroll {
  constructor(onChange) {
    this._target = 0;
    this._viewTop = 0; this._viewH = 0; this._contentH = 0;
    this._live = new Tween(0, () => { if (onChange) onChange(this._live.value); });
  }
  setExtent(viewTop, viewH, contentH) {
    this._viewTop = viewTop;
    this._viewH = Math.max(0, viewH);
    this._contentH = Math.max(0, contentH);
    const t = Math.min(Math.max(this._target, 0), this.maxScroll);
    if (t !== this._target) this._go(t);
  }
  get maxScroll() { return Math.max(0, this._contentH - this._viewH); }
  get scrollable() { return this.maxScroll > 0.5; }
  scrollBy(px) {
    if (!this.scrollable) return false;
    this._go(Math.min(Math.max(this._target + px, 0), this.maxScroll));
    return true;
  }
  reveal(top, h) {
    let t = this._target;
    if (top < t) t = top;
    else if (top + h > t + this._viewH) t = top + h - this._viewH;
    this._go(Math.min(Math.max(t, 0), this.maxScroll));
  }
  reset() { this._target = 0; this._live.set(0); }
  _go(t) {
    if (t === this._target) return;
    this._target = t;
    this._live.to(t, tok.ms("motion-scroll"), Ease.EaseOutCubic);
  }
  get value() { return this._live.value; }
  get target() { return this._target; }
  get viewTop() { return this._viewTop; }
  get viewH() { return this._viewH; }
  get contentH() { return this._contentH; }
  get moving() { return this._live.animating; }
  bandVisible(top, h) {
    const y = top - this.value;
    return y + h > 0 && y < this._viewH;
  }
  bar(right) {
    if (!this.scrollable || this._viewH <= 0) return null;
    const thumbH = Math.max(18, this._viewH * (this._viewH / this._contentH));
    const y = this._viewTop + (this._viewH - thumbH) * (this.value / this.maxScroll);
    return { x: right - 4, y, w: 3, h: thumbH };
  }
}

export function wheelPx(e) {
  const notch = tok.px("shell-wheel-notch") || 40;
  if (e.deltaMode === 1) return (e.deltaY * notch) / 3;     // lines: three to a notch
  if (e.deltaMode === 2) return e.deltaY * notch * 10;      // pages
  return (e.deltaY / 100) * notch;
}
