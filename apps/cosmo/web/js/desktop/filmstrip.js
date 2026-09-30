/*
 * Cosmo by arstro — desktop/filmstrip.js: the photo rack under the breadcrumb.
 *
 * Ports widgets/Filmstrip.cpp and App::refreshLibrary (App.cpp:703-737). 86 high, filmstripBg,
 * hairline on top; cells 62 high from y 12 - photos 86 wide, group chips 78 - at 9.75 + Σ(w +
 * 4.875) minus the eased scroll. Photos show their `thumb` Fit = Cover; a still-decoding photo a
 * dim plate with the native's squarish track and a quarter arc sweeping once per 900 ms; a failed
 * one nothing but its outline; a group a dashed chip with its name and "<n> items".
 *
 * Over the cells, in the native's order: hover wash (not on the primary), the bypass wash +
 * badge (a per-cell linear 160 ms ramp through smoothstep; a rebuilt list starts at rest), the
 * outline of every non-primary photo (1 px @ 0.08, or 2 px primary @ 0.65 when selected,
 * greying with bypass), and the primary ring - 2 px at offset 3, sliding 200 ms EaseOutCubic
 * between cells and following the scroll - with the name bar on a photo. The streaming bar
 * (R-LOADUX-3) runs along the top while a load is still bringing photos in.
 *
 * Cells are the children of the group THIS page shows (vm.cells - view state; a double click
 * on a group drills in via vm.openGroup). Selection is the model's: click = `select <n>`,
 * Shift = range, Ctrl = add; right click selects an unselected cell first and opens the context
 * menu (an empty spot opens the photo menu). The primary is the edited group's cell, else the
 * cell of the current slot. A different cell list (another node sequence) resets the scroll
 * to 0 at once; the same list re-pushed keeps it.
 *
 * Scroll: one wheel notch = one photo cell + gap (90.875 px), eased 180 ms; arrow-key stepping
 * scrolls the minimum that brings the new cell in (ed.reveal).
 *
 * Deviation: a left-drag on the strip scrolls it directly (the lead asked for drag-scroll; the
 * native has none). Cells that arrive or leave within one list fade (R-G-1).
 */
import { computed, untracked } from "../core/signal.js";
import { h, bindEffect, keyed, own } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { icon } from "../ui/icons.js";
import { text, setText, bytes, gestures, local, wheelDelta, STRIP_H } from "./editor.js";

const CELL_Y = 12, CELL_H = 62, PHOTO_W = 86, GROUP_W = 78, GAP = 4.875, PAD_X = 9.75;
const WHEEL_STEP = PHOTO_W + GAP;                 // Filmstrip::kWheelStep
const smooth = (t) => t * t * (3 - 2 * t);
const lerp = (a, b, t) => a + (b - a) * t;
const mix = (c1, c2, t) => `rgba(${[0, 1, 2].map((k) => Math.round(lerp(c1[k], c2[k], t))).join(",")},${+lerp(c1[3], c2[3], t).toFixed(4)})`;

/** The native's squarish pending track (four quads with the corner as control point). */
function trackPath(cx, cy, r) {
  return `M${cx + r} ${cy}Q${cx + r} ${cy + r} ${cx} ${cy + r}Q${cx - r} ${cy + r} ${cx - r} ${cy}` +
         `Q${cx - r} ${cy - r} ${cx} ${cy - r}Q${cx + r} ${cy - r} ${cx + r} ${cy}Z`;
}
function arcPoints(cx, cy, r) {                    // a quarter turn in 8 segments
  const pts = [];
  for (let i = 0; i <= 8; i++) { const a = (i / 8) * Math.PI / 2; pts.push(`${cx + Math.cos(a) * r},${cy + Math.sin(a) * r}`); }
  return pts.join(" ");
}
/** A group chip's dashed edge: n = max(1, floor(len / 6)) equal dashes, every other one drawn. */
function dashes(x0, y0, x1, y1) {
  const len = Math.hypot(x1 - x0, y1 - y0), n = Math.max(1, Math.floor(len / 6));
  let d = "";
  for (let k = 0; k < n; k += 2) {
    const t0 = k / n, t1 = Math.min(1, (k + 1) / n);
    d += `M${x0 + (x1 - x0) * t0} ${y0 + (y1 - y0) * t0}L${x0 + (x1 - x0) * t1} ${y0 + (y1 - y0) * t1}`;
  }
  return d;
}

export function mountFilmstrip(center, vm, ed) {
  const fs = h("div.fs");
  const track = h("div.fs-track");
  const cellsEl = h("div.fs-cells");
  const ring = h("div.fs-ring");
  const nameText = text("", { y: 9, size: 7, font: "medium", cls: "white" });
  const namebar = h("div.fs-namebar", {}, nameText);
  track.append(cellsEl, ring, namebar);
  const loadTrack = h("i.fs-load-track"), loadFill = h("i.fs-load-fill");
  const loadText = text("", { y: STRIP_H - 3, size: 9, font: "medium", cls: "fs-load-text" });
  const load = h("div.fs-load", {}, loadTrack, loadFill, loadText);
  fs.append(track, load);
  center.append(fs, h("div.fs-line"));

  const css = getComputedStyle(document.documentElement);
  const rgb = (name) => css.getPropertyValue(name).split(",").map((v) => +v.trim());
  const PRIMARY = rgb("--primary-rgb"), WHITE = rgb("--white-rgb");
  const P = (a) => [...PRIMARY, a], Wt = (a) => [...WHITE, a];
  const NAMEBAR_BY = [41, 41, 41, 0.85];          // Color{0.16,0.16,0.16,0.85}, Filmstrip.cpp:381

  // ---- the cells of the shown group, with their geometry
  const cells = computed(() => vm.cells.value.map((n) => ({
    node: n.node, kind: n.kind, name: n.name, slot: n.slot, bypass: !!n.bypass,
    count: n.kind === "group" ? vm.childrenOf(n.node).length : 0,
  })), (a, b) => JSON.stringify(a) === JSON.stringify(b));
  let xs = [], ws = [], list = [], contentW = PAD_X;
  const primary = computed(() => {
    const l = cells.value, g = vm.editGroup.value, s = vm.currentSlot.value;
    return l.findIndex((c) => (g >= 0 ? c.node === g : c.kind !== "group" && c.slot === s && s >= 0));
  });

  // ---- scroll
  let target = 0;
  const scroll = new Tween(0, (v) => { track.style.transform = `translateX(${-v}px)`; });
  const viewW = () => fs.offsetWidth;
  const maxScroll = () => Math.max(0, contentW - viewW());
  function scrollTo(t, eased = true) {
    target = Math.min(maxScroll(), Math.max(0, t));
    if (eased) scroll.to(target, 180, Ease.EaseOutCubic); else scroll.set(target);
  }
  function scrollCellIntoView(i) {                 // Filmstrip::scrollCellIntoView
    if (i < 0 || i >= list.length) return;
    const x = xs[i] - scroll.value;
    if (x >= 0 && x + ws[i] <= viewW()) return;
    const left = xs[i], right = left + ws[i], view = viewW();
    let t = target;
    if (left - PAD_X < t) t = left - PAD_X;
    else if (right + PAD_X > t + view) t = right + PAD_X - view;
    scrollTo(t);
  }
  ed.reveal = (node) => scrollCellIntoView(list.findIndex((c) => c.node === node));

  // ---- bypass fades: per node, linear 160 ms, drawn through smoothstep
  const by = new Map();                            // node -> {v, t}
  let byRaf = 0, byLast = 0;
  function byAmount(node) { const b = by.get(node); return b ? smooth(b.v) : 0; }
  function stepBy(now) {
    const dt = byLast ? now - byLast : 16;
    byLast = now;
    let moving = false;
    const step = matchMedia("(prefers-reduced-motion: reduce)").matches ? 1 : Math.min(1, dt / 160);
    for (const b of by.values()) {
      if (b.v < b.t) b.v = Math.min(b.t, b.v + step); else if (b.v > b.t) b.v = Math.max(b.t, b.v - step);
      if (b.v !== b.t) moving = true;
    }
    paintBy();
    byRaf = moving ? requestAnimationFrame(stepBy) : 0;
    if (!moving) byLast = 0;
  }
  function kickBy() { if (!byRaf) byRaf = requestAnimationFrame(stepBy); }
  function paintBy() {
    for (const el of cellsEl.children) {
      const b = byAmount(el.__key);
      el.style.setProperty("--by", b.toFixed(4));
      const out = el.querySelector(".fs-out");
      if (out && el.classList.contains("sel")) out.style.borderColor = mix(P(0.65), Wt(0.3), b);
    }
    paintRing();
  }

  // ---- cells
  let prevSeq = "";
  const selIds = () => vm.selectedIds.value;
  keyed(cellsEl, () => {
    const l = cells.value;
    const seq = l.map((c) => c.node).join(",");
    untracked(() => {
      list = l;
      xs = []; ws = [];
      let x = PAD_X;
      for (const c of l) { xs.push(x); const w = c.kind === "group" ? GROUP_W : PHOTO_W; ws.push(w); x += w + GAP; }
      contentW = x;
      if (seq !== prevSeq) {                       // a different list: a fresh view
        prevSeq = seq;
        scroll.set(0); target = 0;
        for (const el of cellsEl.children) el.classList.remove("hover");
        by.clear();
        for (const c of l) by.set(c.node, { v: c.bypass ? 1 : 0, t: c.bypass ? 1 : 0 });
      } else {
        for (const c of l) {
          const b = by.get(c.node);
          if (!b) by.set(c.node, { v: c.bypass ? 1 : 0, t: c.bypass ? 1 : 0 });
          else if (b.t !== (c.bypass ? 1 : 0)) { b.t = c.bypass ? 1 : 0; kickBy(); }
        }
      }
    });
    return l;
  }, (c) => c.node, (c) => {
    const el = h("div.fs-cell");
    el.append(h("div.fs-body"), h("div.fs-hov"), h("div.fs-by"),
      h("div.fs-badge", {}, icon("ban", 8, 1.1)), h("div.fs-out"));
    return el;
  }, (el, c, i) => {
    if (el.dataset.kind !== c.kind || el.dataset.name !== c.name || el.dataset.count !== String(c.count)) fill(el, c);
    el.style.transform = `translateX(${xs[i]}px)`;
    el.style.width = ws[i] + "px";
    const t = c.kind === "image" ? vm.thumbs.value.get(c.node) : null;
    const img = el.querySelector("img.fs-thumb");
    if (img) {
      if (t && img.getAttribute("src") !== t.url) img.src = t.url;
      img.classList.toggle("on", !!t);
    }
    const sel = selIds().has(c.node);
    el.classList.toggle("sel", sel);
    el.classList.toggle("primary", i === primary.value);
    el.style.setProperty("--by", byAmount(c.node).toFixed(4));
    const out = el.querySelector(".fs-out");
    out.style.borderColor = sel ? mix(P(0.65), Wt(0.3), byAmount(c.node)) : "";
  }, { leaveMs: 180 });

  function fill(el, c) {
    el.dataset.kind = c.kind;
    el.dataset.name = c.name;
    el.dataset.count = String(c.count);
    el.classList.toggle("group", c.kind === "group");
    const body = el.querySelector(".fs-body");
    body.replaceChildren();
    if (c.kind === "image") {
      body.append(h("img.fs-thumb", { alt: "", draggable: "false" }));
    } else if (c.kind === "pending") {
      const cx = PHOTO_W / 2, cy = CELL_H / 2;
      body.append(h("div.fs-plate"),
        h("svg.fs-spin", { width: PHOTO_W, height: CELL_H, viewBox: `0 0 ${PHOTO_W} ${CELL_H}` },
          h("path.fs-spin-track", { d: trackPath(cx, cy, 9) }),
          h("polyline.fs-spin-head", { points: arcPoints(cx, cy, 9), style: { transformOrigin: `${cx}px ${cy}px` } })));
    } else if (c.kind === "group") {
      const w = GROUP_W, hh = CELL_H;
      const count = `${c.count} items`;
      body.append(h("div.fs-plate"),
        h("svg.fs-dash", { width: w, height: hh, viewBox: `0 0 ${w} ${hh}` },
          h("path", { d: dashes(0, 0, w, 0) + dashes(w, 0, w, hh) + dashes(w, hh, 0, hh) + dashes(0, hh, 0, 0) })),
        text(c.name, { x: w / 2 - bytes(c.name) * 2.7, y: hh / 2 - 1, size: 9, cls: "muted" }),
        text(count, { x: w / 2 - bytes(count) * 2.4, y: hh / 2 + 11, size: 8, cls: "fs-count" }));
    }
  }

  // ---- the primary ring (+ name bar), sliding between cells
  let ringInit = false, ringTarget = -1;
  const ringPos = new Tween(0, () => paintRing());
  function paintRing() {
    const pi = primary.peek();
    const on = pi >= 0 && pi < list.length;
    ring.classList.toggle("on", on);
    namebar.classList.toggle("on", on && list[pi].kind !== "group");
    if (!on) return;
    const n = list.length;
    const p = Math.max(0, Math.min(n - 1, ringPos.value));
    const a = Math.floor(p), b = Math.min(n - 1, a + 1), f = p - a;
    const rx = lerp(xs[a], xs[b], f), rw = lerp(ws[a], ws[b], f);
    const bp = byAmount(list[pi].node);
    ring.style.transform = `translate(${rx - 4}px, ${CELL_Y - 4}px)`;
    ring.style.width = rw + 8 + "px";
    ring.style.borderColor = mix(P(1), Wt(0.42), bp);
    namebar.style.transform = `translate(${rx}px, ${CELL_Y + CELL_H - 12}px)`;
    namebar.style.width = rw + "px";
    namebar.style.background = mix(P(0.8), NAMEBAR_BY, bp);
    const label = list[pi].name;
    setText(nameText, label);
    nameText.style.left = rw / 2 - bytes(label) * 2.1 + "px";
  }
  bindEffect(ring, () => {
    const pi = primary.value;
    cells.value;
    untracked(() => {
      if (pi >= 0) {
        if (!ringInit) { ringInit = true; ringTarget = pi; ringPos.set(pi); }
        else if (pi !== ringTarget) { ringTarget = pi; ringPos.to(pi, 200, Ease.EaseOutCubic); }
      }
      paintRing();
    });
  });

  // ---- streaming bar (R-LOADUX-3)
  const lf = new Tween(0, (v) => { load.style.opacity = v; });
  const frac = new Tween(0, (v) => { loadFill.style.width = Math.max(0, Math.min(1, v)) * 100 + "%"; });
  bindEffect(load, () => {
    const { done, total } = vm.load.value;
    const streaming = total > 0 && done < total;
    untracked(() => {
      frac.to(total > 0 ? done / total : 0, 200, Ease.EaseOutCubic);
      lf.to(streaming ? 1 : 0, streaming ? 160 : 320, Ease.EaseOutCubic);
      if (total > 0) setText(loadText, `Loading ${done} of ${total}`);
    });
  });

  // ---- input
  const cellAt = (lx) => {
    const x = lx + scroll.value;
    for (let i = 0; i < list.length; i++) if (x >= xs[i] && x <= xs[i] + ws[i]) return i;
    return -1;
  };
  function markHover(i) {
    const node = i >= 0 ? list[i].node : null;
    for (const el of cellsEl.children) el.classList.toggle("hover", el.__key === node);
  }
  fs.addEventListener("pointermove", (e) => { if (!dragFrom) markHover(cellAt(local(fs, e).x)); });
  fs.addEventListener("pointerleave", () => markHover(-1));
  fs.addEventListener("wheel", (e) => {
    if (e.ctrlKey) return;
    e.preventDefault();
    scrollTo(target - wheelDelta(e) * WHEEL_STEP);
  }, { passive: false });
  let dragFrom = null;
  gestures(fs, {
    dragStart: (e, p, p0) => { if (e.buttons & 1) dragFrom = { x: p0.x, s: scroll.value }; },
    drag: (e, p) => { if (dragFrom) scrollTo(dragFrom.s - (p.x - dragFrom.x), false); },
    drop: () => { dragFrom = null; },
    up: () => { dragFrom = null; },
    click: (e, p) => {
      const i = cellAt(p.x);
      if (i < 0) return;
      const mode = e.shiftKey ? "range" : (e.ctrlKey || e.metaKey) ? "add" : undefined;
      vm.select(list[i].node, mode).catch(() => {});
      scrollCellIntoView(i);
    },
    doubleClick: (e, p) => {
      const i = cellAt(p.x);
      if (i >= 0 && list[i].kind === "group") vm.openGroup(list[i].node);
    },
    rightClick: (e, p) => {
      const i = cellAt(p.x);
      const q = local(ed.root, e);
      if (i < 0) { ed.openContext(q.x, q.y, -1); return; }
      const node = list[i].node;
      const open = () => ed.openContext(q.x, q.y, node);
      if (!vm.selectedIds.peek().has(node)) vm.select(node).then(open, () => {});
      else open();
    },
  });
  // A resize keeps the scroll inside the content (the rail animating narrows the strip).
  const ro = new ResizeObserver(() => { if (target > maxScroll()) scrollTo(target, false); });
  ro.observe(fs);
  own(fs, () => ro.disconnect());
}
