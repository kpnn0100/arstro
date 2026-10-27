/*
 * Solaris — sheets/settings/controls.js: SettingsSheet's two controls (SettingsSheet.h: SettingCombo, SettingToggle).
 *
 *   settingCombo({w, h, layer, rootBox, onPick})   Artboard's ComboBox in cosmo's tokens (SettingsSheet.cpp comboStyle):
 *       the field 220×28, input fill, a 1 px border easing to the primary on hover, 11 px Roboto at x 10, a caret; the
 *       label CROSS-FADES (motion::kCrossFadeMs EaseOutCubic) when the value changes, whoever changed it. The list
 *       (ComboBox::popup / paintOverlay): 26 px rows, ≤ 8 of them, below unless there is more room above (4 px margin to
 *       the window), opening 160 ms EaseOutCubic — opacity and a 6 px slide from the side it opens towards — centred on
 *       the choice; an opaque backing, the popover card, the chosen row primary @ .22, a gliding hover bar (caret colour
 *       @ .28), and a scroll indicator only while something is out of view. A pick is `onPick(i)`.
 *   settingToggle({onChange})   Artboard's ToggleSwitch drawn by SettingToggle: 40×22 pill, switchBackground → primary,
 *       a white 16 px knob that EASES (motion::kSelectMs EaseOutCubic) — a value from the model slides as a click does.
 */
import { h } from "../../core/dom.js";
import { FONT, ellipsize, textAt } from "../../kit/text.js";

const SVGNS = "http://www.w3.org/2000/svg";
const ROW = 26, MAX_ROWS = 8, OPEN_MS = 160;

export function caretSvg(x, y) {
  const s = document.createElementNS(SVGNS, "svg");
  s.setAttribute("width", 12); s.setAttribute("height", 10);
  s.setAttribute("class", "ss-caret");
  s.style.left = x + "px"; s.style.top = y + "px";
  const p = document.createElementNS(SVGNS, "path");
  p.setAttribute("d", "M2.5 3.2 L6 6.8 L9.5 3.2");
  s.append(p);
  return s;
}

const combos = new Set();
export function closeAllCombos(except) { for (const c of combos) if (c !== except) c.close(); }
export function anyComboOpen() { for (const c of combos) if (c.isOpen) return true; return false; }

export function settingCombo({ w = 220, h: hh = 28, layer, rootBox, onPick }) {
  const el = h("div.ss-combo");
  el.style.width = w + "px"; el.style.height = hh + "px";
  const labels = h("div.ss-combo-labels");
  el.append(labels, caretSvg(w - 22, (hh - 10) * 0.5));
  let options = [], selected = -1, shownText = null, cur = null;

  function setLabel(text, fade) {
    const span = h("span.ss-combo-text");
    span.textContent = ellipsize(text, w - 36, 11, FONT.sans);
    textAt(span, 10, hh * 0.5 + 11 * 0.35, 11, FONT.sans);
    if (!fade) { labels.replaceChildren(span); cur = span; return; }
    const old = cur;
    span.style.opacity = "0";
    labels.append(span);
    cur = span;
    if (old) { old.style.opacity = "0"; setTimeout(() => old.remove(), 260); }
    requestAnimationFrame(() => requestAnimationFrame(() => { span.style.opacity = "1"; }));
  }
  function set(opts, sel) {
    options = opts.slice();
    selected = sel;
    const now = sel >= 0 && sel < options.length ? options[sel] : "";
    if (shownText === null) setLabel(now, false);
    else if (now !== shownText) setLabel(now, true);
    shownText = now;
    if (api.isOpen) paintList();
  }

  // ------------------------------------------------------------------ the list (the overlay pass)
  const pop = h("div.ss-pop");
  const back = h("div.ss-pop-back"), card = h("div.ss-pop-card"), list = h("div.ss-pop-list"), hi = h("div.ss-pop-hi");
  const stroke = h("div.ss-pop-stroke"), bar = h("div.ss-pop-bar"), thumb = h("div.ss-pop-thumb");
  bar.append(thumb);
  card.append(hi, list);
  pop.append(back, card, stroke, bar);
  let geo = null, scroll = 0, closeTimer = 0;

  function geometry() {
    const box = rootBox();                        // {rect, sc, H}: the sheet root on screen, its scale, its logical height
    const er = el.getBoundingClientRect();
    const fx = (er.left - box.rect.left) / box.sc, fy = (er.top - box.rect.top) / box.sc;
    const content = options.length * ROW;
    let rows = Math.min(options.length, MAX_ROWS);
    const below = box.H - (fy + hh) - 4, above = fy - 4;
    const up = rows * ROW > below && above > below;
    const room = up ? above : below;
    if (rows * ROW > room) rows = Math.max(1, Math.floor(room / ROW));
    const ph = rows * ROW;
    return { fx, fy, up, ph, top: up ? -ph : hh, content, maxScroll: Math.max(0, content - ph) };
  }
  function paintList() {
    if (!geo) return;
    list.replaceChildren();
    options.forEach((lab, i) => {
      const r = h("div.ss-pop-row" + (i === selected ? ".sel" : ""));
      r.style.top = i * ROW + "px";
      const t = h("span");
      t.textContent = lab;
      textAt(t, 10, ROW * 0.5 + 11 * 0.35, 11, FONT.sans);
      r.append(t);
      r.addEventListener("pointerenter", () => { hi.style.transform = `translateY(${i * ROW - scroll}px)`; hi.classList.add("on"); });
      r.addEventListener("click", (e) => { e.stopPropagation(); const pick = i; api.close(); if (onPick) onPick(pick); });
      list.append(r);
    });
    applyScroll();
  }
  function applyScroll() {
    list.style.transform = `translateY(${-scroll}px)`;
    bar.style.display = geo.maxScroll > 0 ? "" : "none";
    if (geo.maxScroll > 0) {
      const th = Math.max(18, (geo.ph * geo.ph) / geo.content), travel = geo.ph - th;
      thumb.style.height = th + "px";
      thumb.style.transform = `translateY(${travel * (scroll / geo.maxScroll)}px)`;
    }
  }
  pop.addEventListener("wheel", (e) => {
    e.preventDefault(); e.stopPropagation();
    if (!geo || geo.maxScroll <= 0) return;
    scroll = Math.max(0, Math.min(geo.maxScroll, scroll + e.deltaY));
    hi.classList.remove("on");
    applyScroll();
  }, { passive: false });
  pop.addEventListener("pointerleave", () => hi.classList.remove("on"));
  pop.addEventListener("click", (e) => e.stopPropagation());

  const api = {
    el, set,
    isOpen: false,
    get options() { return options; },
    get selected() { return selected; },
    get shown() { return shownText; },
    open() {
      if (api.isOpen || !options.length) return;
      closeAllCombos(api);
      clearTimeout(closeTimer);
      api.isOpen = true;
      el.classList.add("open");
      geo = geometry();
      scroll = Math.min(geo.maxScroll, Math.max(0, selected * ROW - (geo.ph - ROW) * 0.5));   // the choice, centred
      Object.assign(pop.style, { left: geo.fx - 1 + "px", top: geo.fy + geo.top - 1 + "px", width: w + 2 + "px", height: geo.ph + 2 + "px" });
      pop.classList.toggle("up", geo.up);
      pop.classList.remove("shown");
      hi.classList.remove("on");
      paintList();
      if (!pop.parentNode) layer.append(pop);
      pop.getBoundingClientRect();                // the start state lands before the transition
      pop.classList.add("shown");
    },
    close() {
      if (!api.isOpen) return;
      api.isOpen = false;
      el.classList.remove("open");
      pop.classList.remove("shown");
      closeTimer = setTimeout(() => pop.remove(), OPEN_MS + 20);
    },
    toggle() { if (api.isOpen) api.close(); else api.open(); },
    dispose() { api.close(); pop.remove(); combos.delete(api); },
  };
  el.addEventListener("click", (e) => { e.stopPropagation(); api.toggle(); });
  combos.add(api);
  return api;
}

export function settingToggle({ onChange }) {
  const el = h("div.ss-toggle");
  const knob = h("div.ss-knob");
  el.append(knob);
  let on = false, placed = false;
  function paint() { el.classList.toggle("on", on); }
  el.addEventListener("click", (e) => { e.stopPropagation(); closeAllCombos(); on = !on; paint(); if (onChange) onChange(on); });
  return {
    el,
    set(v) {
      v = !!v;
      if (!placed) { placed = true; el.classList.add("still"); on = v; paint(); requestAnimationFrame(() => el.classList.remove("still")); return; }
      if (v !== on) { on = v; paint(); }
    },
    get on() { return on; },
  };
}
