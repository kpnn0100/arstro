/*
 * Cosmo by arstro — touch/controls.js: the touch-metric controls of the tray.
 *
 * Ports PhoneApp.cpp's ParamSlider (:109-143: a 48 px row, label at 16, a slider at x 128 with a
 * r 10 thumb, the signed readout right at W-16, the green stacked reach), the Tray's segmented
 * picker with its sliding highlight (:377-393) and its 44x26 toggle (:410-415).
 *
 * Deliberate deviations (screens_touch.md §14.8-14.9, R-TOUCH-4, contract rule 3/4):
 *  - the slider's hit band is the full 48 px row height over the track (+12 px each side);
 *  - a direct drag follows the finger exactly; values that arrive from the model spring there;
 *  - a released value is held until the model agrees (or 1.5 s), so it never flicks back while
 *    the reply and the next model push race;
 *  - the toggle's knob slides (150 ms EaseOutCubic, the desktop toggle timing) instead of snapping.
 */
import { h, bindEffect, own } from "../core/dom.js";
import { signal } from "../core/signal.js";
import { tx, gestures, pressWash, Spring, clamp, readout, K } from "./tk.js";

/**
 * One slider row.
 *   d  = { label, min, max, def, grad: [from, to] | null, noReadout }
 *   io = { value: () => ({ own, eff }) in UI units (reactive; eff omitted = no reach),
 *          set: (ui, live) => void, fmt?: (ui) => string }
 */
export function sliderRow(d, io) {
  const row = h("div.t-row");
  if (d.label) row.append(tx(d.label, 16, 28.5, 13, "sans", "left", "t-muted"));
  const val = tx("", "calc(100% - 16px)", 28.5, 13, "mono", "right", "t-fg");
  if (!d.noReadout) row.append(val);

  const hit = h("div.t-slider");
  const track = h("div.t-track" + (d.grad ? ".grad" : ""));
  if (d.grad) track.style.backgroundImage = `linear-gradient(90deg, ${d.grad[0]}, ${d.grad[1]})`;
  const fill = h("div.t-fill");
  const reach = h("div.t-reach");
  const tick = h("div.t-tick");
  const thumb = h("div.t-knob");
  track.append(fill, reach, tick, thumb);
  if (d.grad) fill.style.display = "none";
  hit.append(track);
  row.append(hit);

  const span = d.max - d.min;
  const norm = (v) => clamp((v - d.min) / span, 0, 1);
  const zero = d.min < 0 && d.max > 0 ? norm(0) : 0;
  let n = 0, r = 0, stacked = false;
  const paint = () => {
    const a = Math.min(zero, n), b = Math.max(zero, n);
    fill.style.left = a * 100 + "%";
    fill.style.width = (b - a) * 100 + "%";
    thumb.style.left = `calc(${n * 100}% - 10px)`;
    const ra = Math.min(n, r), rb = Math.max(n, r);
    reach.style.left = ra * 100 + "%";
    reach.style.width = (rb - ra) * 100 + "%";
    tick.style.left = `calc(${r * 100}% - 0.8px)`;
  };
  const thumbSpring = new Spring(0, (x) => { n = x; paint(); }, 1e-4);
  const reachSpring = new Spring(0, (x) => { r = x; paint(); }, 1e-4);

  let dragging = false;
  let local = 0;                                 // the value under the finger (UI units)
  let hold = null;                               // {v, until}: the released value, until the model agrees
  const wake = signal(0);
  let first = true;
  const fmt = io.fmt || readout;

  bindEffect(row, () => {
    wake.value;
    const { own: o = 0, eff } = io.value() || {};
    if (dragging) return;
    if (hold) {
      if (Math.abs(o - hold.v) > span * 1e-4 && performance.now() < hold.until) return;
      hold = null;
    }
    const stackOn = eff !== undefined && Math.abs(eff - o) > span * 1e-5;
    if (stackOn !== stacked) { stacked = stackOn; row.classList.toggle("stacked", stackOn); }
    if (first) { first = false; thumbSpring.set(norm(o)); reachSpring.set(norm(stackOn ? eff : o)); }
    else { thumbSpring.to(norm(o)); reachSpring.to(norm(stackOn ? eff : o)); }
    val.textContent = fmt(o);
  });

  const valueAt = (p) => {
    const w = track.clientWidth || 1;
    return d.min + span * clamp((p.x - 12) / w, 0, 1);
  };
  const drive = (v, live) => {
    local = v;
    thumbSpring.set(norm(v));
    if (!stacked) reachSpring.set(norm(v));
    val.textContent = fmt(v);
    io.set(v, live);
  };
  gestures(hit, {
    dragStart: (e, p0, p) => { dragging = true; row.classList.add("drag"); drive(valueAt(p), true); },
    drag: (e, p) => { if (dragging) drive(valueAt(p), true); },
    dragEnd: (e, p, cancelled) => {
      if (!dragging) return;
      dragging = false;
      row.classList.remove("drag");
      hold = { v: local, until: performance.now() + 1500 };
      const t = setTimeout(() => { wake.value++; }, 1600);
      own(row, () => clearTimeout(t));
      io.set(local, false);
    },
    doubleTap: () => {                             // reset to the default (native: one `set`)
      hold = { v: d.def, until: performance.now() + 1500 };
      thumbSpring.to(norm(d.def));
      val.textContent = fmt(d.def);
      io.set(d.def, false);
      setTimeout(() => { wake.value++; }, 1600);
    },
  });
  return row;
}

/**
 * Segmented picker: fill INPUT r2 + BORDER, n equal segments, the highlight (x+1, y+1, seg-2, h-2)
 * slides 200 ms EaseOutCubic; labels Roboto Medium 11 centred, baseline h/2 + 4.
 *   o = { sel: () => index (reactive), pick: (i) => void, colors: (() => [css...]) | null, h }
 */
export function segmented(labels, o) {
  const hh = o.h || 30;
  const n = labels.length;
  const el = h("div.t-seg");
  el.style.height = hh + "px";
  const hl = h("div.t-seg-hl");
  hl.style.width = `calc(${100 / n}% - 2px)`;
  el.append(hl);
  const segs = labels.map((label, i) => {
    const s = h("div.t-seg-i");
    s.style.left = (100 / n) * i + "%";
    s.style.width = 100 / n + "%";
    s.append(tx(label, "50%", hh / 2 + 4, 11, "medium", "center"));
    gestures(s, { down: () => pressWash(s), tap: () => o.pick(i) });
    el.append(s);
    return s;
  });
  let first = true;
  bindEffect(el, () => {
    const i = o.sel();
    const cols = o.colors ? o.colors() : null;
    if (first) { first = false; hl.style.transition = "none"; requestAnimationFrame(() => { hl.style.transition = ""; }); }
    hl.style.left = `calc(${(100 / n) * i}% + 1px)`;
    hl.style.background = cols ? cols[i] : "";
    segs.forEach((s, k) => s.classList.toggle("on", k === i));
  });
  return el;
}

/** Toggle 44x26 r13: on accent / off INPUT, a 20x20 white knob at +21 / +3 (PhoneApp.cpp:410-415). */
export function toggle(o) {
  const el = h("div.t-toggle", {}, h("i"));
  bindEffect(el, () => { el.classList.toggle("on", !!o.on()); });
  gestures(el, { tap: () => o.flip() });
  return el;
}

/** A 48 px row container at a given y inside an absolutely laid-out body (height = K.row). */
export function rowAt(y) {
  const el = h("div.t-rowslot");
  el.style.top = y + "px";
  el.style.height = K.row + "px";
  return el;
}
