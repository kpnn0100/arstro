/*
 * Interstellar — kit/fadepage.js: one tab's content, shown and hidden by an opacity FADE (R1, R-UI-3).
 *
 * A port of app/widgets/FadePage.h. The three Edit tabs (Grade · Cut · Deliver) are three of these
 * stacked in one place; a tab switch cross-fades them over motion::kCrossFadeMs (200 ms EaseOutCubic).
 * Show/hide is opacity, never display:none - nothing is rebuilt or reloaded, every widget in a page
 * lives as long as the page. Input follows the INTENT, not the eased opacity (FadePage.h:9-13): the
 * page you asked for takes input from the first frame, the one you left takes none (`inert`) while
 * it is still fading out - a click mid-fade never lands on a control the user is leaving.
 *
 *   const page = fadePage(el)          el: any positioned element (the screen sizes it)
 *   page.setShown(on)                  intent; the fade starts now (a setter here HAS a clock)
 *   page.setShownImmediate(on)         first placement only - there is nowhere to fade from
 *   page.shown()                       the intent
 *   page.fadeValue()                   the LIVE eased opacity (what a test reads to tell a fade from a snap)
 *
 * A fully faded-out page also gets visibility:hidden, so it costs no paint (Segment culls at 1e-3).
 */
import { Tween, Ease } from "../core/motion.js";
import { tok } from "./tokens.js";

export function fadePage(el) {
  let wanted = false;
  const apply = (v) => {
    el.style.opacity = String(v);
    el.style.visibility = v <= 0.001 && !wanted ? "hidden" : "";
  };
  const tw = new Tween(0, apply);
  apply(0);
  el.inert = true;
  return {
    el,
    setShown(on) {
      if (on === wanted && tw.target === (on ? 1 : 0)) return;
      wanted = !!on;
      el.inert = !wanted;
      if (wanted) el.style.visibility = "";
      tw.to(wanted ? 1 : 0, tok.ms("motion-cross-fade"), Ease.EaseOutCubic);
    },
    setShownImmediate(on) {
      wanted = !!on;
      el.inert = !wanted;
      tw.set(wanted ? 1 : 0);
    },
    shown: () => wanted,
    fadeValue: () => tw.value,
  };
}
