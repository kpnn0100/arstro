/*
 * Cosmo by arstro — desktop/stage.js: the PhotoCanvas - the photo, its cross-dissolve, Before /
 * Split / After with the split seam, zoom & pan, the white-balance pick, and the seam for the
 * right column's overlays.
 *
 * Ports widgets/PhotoCanvas.cpp, Artboard ImageView (Fit = Contain, zoomAbout, clampPan) and
 * widgets/SegmentedControl.cpp + PillButton.cpp for the compare pill, with App's
 * refreshPhotoForMode (App.cpp:424-448) and the Ctrl+wheel zoom (App.cpp:876-887).
 *
 *   z-order: canvasBg, photoBase, photoTop (only its opacity moves), the split clip holding the
 *   before image (full-canvas Contain, so it stays aligned), the seam, the overlay layer (B's
 *   mask / crop overlays), the compare pill.
 *
 * The dissolve (R-VIEW-1, PhotoCanvas.cpp:165-212): a new frame goes into the layer at weight 0
 * and photoTop's opacity runs to it over 160 ms LINEAR; a frame arriving mid-dissolve is HELD
 * (newest wins) and applied the moment it settles; a frame of another shape, or the first one,
 * goes into both layers at once. Before shows the per-page `before` frame (adapter method
 * `before`, blob `before` to this page only) through the same dissolve; Split shows the after
 * frame and sets the before frame, un-dissolved, into the clip; the clip and the seam fade over
 * 180 ms. The before frame is re-requested when the photo or its geometry changes (the core
 * caches it by geometry).
 *
 * View state, per page: vm.view.compare, vm.view.seam (0..1 of the CANVAS width, default 0.5,
 * dragged directly, double-click eases it back over 180 ms), vm.view.zoom {z, x, y} (z 1..8 by
 * 1.15 per Ctrl+wheel notch about the pointer; x, y = the photo point at the canvas centre;
 * drag pans while z > 1, clamped like ImageView::clampPan - so a zoomed photo narrower than the
 * canvas ends right / bottom aligned, a native quirk kept), vm.view.wbArmed (a click inside
 * the photo picks, `wb pick`, and disarms).
 *
 * The overlay seam: mountOverlays(stage, vm, ctx) from ./overlays.js with
 *   stage = { layer, rect, toNorm, fromNorm, frame, size }
 * `layer` covers the canvas and passes pointer events through except where an overlay takes
 * them; `rect` is a signal of the DISPLAYED image rect {x, y, w, h} in layer px (fit, zoom and
 * pan included); toNorm(clientX, clientY) -> {x, y} 0..1 of that rect; fromNorm(x, y) -> layer
 * px; `frame` = signal of the displayed frame {url, w, h}; `size` = signal of the canvas {w, h}.
 * A press on an overlay element (anything inside `layer` but the layer itself) is left to it.
 *
 * Deviations: preview resolution does not follow the zoom (CORE GAP #5, no command) and the
 * crop-preview framing is not driven (CORE GAP #6); a Before without the `before` method shows
 * the after frame and says so once.
 */
import { signal, computed, untracked } from "../core/signal.js";
import { h, bindEffect, own } from "../core/dom.js";
import { Tween, Ease } from "../core/motion.js";
import { text, est, gestures, local, wheelDelta } from "./editor.js";

const SEAM_HIT = 13;                       // metrics::anchorHitRadius
const PILL_H = 25;
const MODES = ["before", "split", "after"];
const LABELS = ["Before", "Split", "After"];
const rectEq = (a, b) => a && b && a.x === b.x && a.y === b.y && a.w === b.w && a.h === b.h;

function fit(iw, ih, cw, ch) {             // ImageView::baseFittedRect, Fit::Contain
  if (!(iw > 0 && ih > 0 && cw > 0 && ch > 0)) return { x: 0, y: 0, w: 0, h: 0 };
  const s = Math.min(cw / iw, ch / ih);
  const w = iw * s, hh = ih * s;
  return { x: (cw - w) / 2, y: (ch - hh) / 2, w, h: hh };
}
function clampRect(r, cw, ch) {            // ImageView::clampPan, in rect terms
  if (r.x > 0) r.x = 0;
  if (r.x + r.w < cw) r.x += cw - (r.x + r.w);
  if (r.y > 0) r.y = 0;
  if (r.y + r.h < ch) r.y += ch - (r.y + r.h);
  return r;
}
const sameShape = (a, b) => {              // PhotoCanvas::sameShape - half a percent of slack
  if (!a || !b || !(a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0)) return false;
  const x = a.w * b.h, y = b.w * a.h;
  return Math.abs(x - y) <= 0.005 * x;
};
/** The per-corner-radius rect of RoundedRectExt.h as an SVG path (quadratic corners). */
function cornersPath(x, y, w, hh, tl, tr, br, bl) {
  const half = Math.min(w, hh) / 2;
  const c = (r) => Math.max(0, Math.min(half, r));
  tl = c(tl); tr = c(tr); br = c(br); bl = c(bl);
  return `M${x + tl} ${y}L${x + w - tr} ${y}` + (tr > 0 ? `Q${x + w} ${y} ${x + w} ${y + tr}` : `L${x + w} ${y}`) +
    `L${x + w} ${y + hh - br}` + (br > 0 ? `Q${x + w} ${y + hh} ${x + w - br} ${y + hh}` : `L${x + w} ${y + hh}`) +
    `L${x + bl} ${y + hh}` + (bl > 0 ? `Q${x} ${y + hh} ${x} ${y + hh - bl}` : `L${x} ${y + hh}`) +
    `L${x} ${y + tl}` + (tl > 0 ? `Q${x} ${y} ${x + tl} ${y}` : `L${x} ${y}`) + "Z";
}

export function mountStage(center, vm, ed) {
  // Σ (estimate(label, 10) + 2 x 11.375) = 164.25, three equal segments of 54.75.
  const PILL_W = LABELS.reduce((w, l) => w + est(l, 10) + 22.75, 0);
  const SEG_W = PILL_W / 3;
  const pc = h("div.pc");                               // H - 29.25 - 22.75 - 86 high (desktop.css)
  const imgA = h("img.pc-img", { alt: "", draggable: "false" });
  const imgB = h("img.pc-img", { alt: "", draggable: "false" });
  const beforeImg = h("img.pc-img", { alt: "", draggable: "false" });
  const split = h("div.pc-split", {}, beforeImg);
  const seamEl = h("div.pc-seam", {}, h("svg", {}, h("rect", { y: 0 })));
  const layer = h("div.pc-layer");
  const pill = h("div.pc-pill");
  pc.append(imgA, imgB, split, seamEl, layer, pill);
  center.append(pc);

  // ---- canvas size
  const size = signal({ w: 0, h: 0 }, (a, b) => a.w === b.w && a.h === b.h);
  // contentRect keeps the fractional layout size (offsetWidth rounds) - the fit uses it exactly.
  const ro = new ResizeObserver((es) => { const r = es[es.length - 1].contentRect; size.value = { w: r.width, h: r.height }; });
  ro.observe(pc);
  own(pc, () => ro.disconnect());

  // ---- zoom / pan (vm.view.zoom = {z, x, y}: x, y the photo point at the canvas centre)
  function viewRect(f) {
    const { w: cw, h: ch } = size.peek();
    if (!f) return { x: 0, y: 0, w: 0, h: 0 };
    const b = fit(f.w, f.h, cw, ch);
    const z = vm.view.zoom.peek();
    if (!(z.z > 1) || b.w <= 0) return b;
    const r = { w: b.w * z.z, h: b.h * z.z };
    r.x = cw / 2 - z.x * r.w;
    r.y = ch / 2 - z.y * r.h;
    return clampRect(r, cw, ch);
  }
  function storeRect(r, z) {
    const { w: cw, h: ch } = size.peek();
    vm.view.zoom.value = z <= 1 ? { z: 1, x: 0.5, y: 0.5 } : { z, x: (cw / 2 - r.x) / r.w, y: (ch / 2 - r.y) / r.h };
  }
  function zoomAbout(factor, p) {           // ImageView::zoomAbout on every view at once
    const f = shown();
    if (!f) return;
    const { w: cw, h: ch } = size.peek();
    const before = viewRect(f);
    const cur = vm.view.zoom.peek().z || 1;
    const nz = Math.min(8, Math.max(1, cur * factor));
    const u = before.w > 0 ? (p.x - before.x) / before.w : 0.5;
    const v = before.h > 0 ? (p.y - before.y) / before.h : 0.5;
    const b = fit(f.w, f.h, cw, ch);
    const r = { w: b.w * nz, h: b.h * nz };
    r.x = p.x - u * r.w;
    r.y = p.y - v * r.h;
    if (nz > 1) clampRect(r, cw, ch);
    storeRect(r, nz);
  }
  function panBy(dx, dy) {
    const z = vm.view.zoom.peek().z || 1;
    if (z <= 1) return;
    const r = viewRect(shown());
    r.x += dx; r.y += dy;
    const { w: cw, h: ch } = size.peek();
    clampRect(r, cw, ch);
    storeRect(r, z);
  }

  // ---- layout: every view at its rect, the seam, the overlay rect
  const rect = signal({ x: 0, y: 0, w: 0, h: 0 }, rectEq);
  const displayed = signal(null);
  function put(el, r) {
    el.style.transform = `translate(${r.x}px, ${r.y}px)`;
    el.style.width = r.w + "px";
    el.style.height = r.h + "px";
  }
  function layout() {
    const { w: cw } = size.peek();
    for (const s of L) if (s.f) put(s.el, viewRect(s.f));
    if (beforeSlot.f) put(beforeImg, viewRect(beforeSlot.f));
    const seamX = vm.view.seam.peek() * cw;
    split.style.width = Math.max(0, seamX) + "px";
    seamEl.style.transform = `translateX(${seamX}px)`;
    const f = shown();
    displayed.value = f;
    rect.value = viewRect(f);
  }

  // ---- the two after layers and the dissolve
  const L = [{ el: imgA, f: null }, { el: imgB, f: null }];   // [base, top]
  const beforeSlot = { el: beforeImg, f: null };
  let topIsCurrent = false;
  let held = null;
  const topA = new Tween(0, (a) => {
    imgB.style.opacity = a;
    imgA.style.visibility = a >= 0.999 ? "hidden" : "visible";   // R-VIEW-1e, from this frame's alpha
    if (!topA.animating && held) { const f = held; held = null; showPhoto(f); }
    layout();
  });
  topA.set(0);
  function shown() { return (topA.value > 0.5 ? L[1] : L[0]).f; }
  function assign(slot, f) {
    slot.f = f;
    if (slot.el.getAttribute("src") !== f.url) slot.el.src = f.url;
  }
  function setPhoto(f) {
    if (topA.animating) { held = f; return; }                    // R-VIEW-1a: hold, newest wins
    showPhoto(f);
  }
  function showPhoto(f) {
    const cur = topIsCurrent ? L[1] : L[0], other = topIsCurrent ? L[0] : L[1];
    if (!cur.f || !sameShape(cur.f, f)) {                        // empty stage / another shape
      assign(L[0], f); assign(L[1], f);
      layout();
      return;
    }
    assign(other, f);
    topIsCurrent = !topIsCurrent;
    topA.to(topIsCurrent ? 1 : 0, 160, Ease.Linear);
    layout();
  }
  // Frames are decoded before they reach a layer, so a layer never shows a half-loaded image;
  // a frame that finishes decoding after a newer one is dropped.
  let seqIn = 0, seqShown = 0;
  function decode(f) {
    const my = ++seqIn;
    const im = new Image();
    im.src = f.url;
    return im.decode().then(() => {
      if (my < seqShown) return null;
      seqShown = my;
      return { url: f.url, w: im.naturalWidth, h: im.naturalHeight };
    }).catch(() => null);
  }

  // ---- after frames (shared model) and before frames (this page's)
  let lastAfter = null, beforeFrame = null, beforeOk = true, warned = false;
  const mode = () => vm.view.compare.peek();
  // Which photo a before frame shows: its blob says (meta.slot), and one of ANOTHER photo is
  // never shown - until the new one arrives the after frame stands in (the native renders the
  // before synchronously, so it never shows a stale one).
  const beforeCurrent = () => !!beforeFrame && beforeFrame.slot === vm.currentSlot.peek();
  function showBeforeInClip() {
    const ok = mode() === "split" && beforeCurrent();
    beforeImg.style.visibility = ok ? "visible" : "hidden";
    if (ok && beforeSlot.f !== beforeFrame) { beforeSlot.f = beforeFrame; beforeImg.src = beforeFrame.url; }
    layout();
  }
  // A frame is shown only while the workspace has photos: the adapter re-sends its last preview
  // to a late client even after the project emptied (and the model may arrive after it).
  let lastUrl = "";
  bindEffect(pc, () => {
    const fr = vm.frame.value;
    if (!fr || !fr.url || vm.imageCount.value <= 0 || fr.url === lastUrl) return;
    lastUrl = fr.url;
    untracked(() => decode(fr).then((f) => {
      if (!f) return;
      lastAfter = f;
      if (mode() !== "before" || !beforeOk || !beforeCurrent()) setPhoto(f);
      wantBefore();
    }));
  });
  const bridge = vm.session && vm.session.bridge;
  let beforeUrl = null;
  if (bridge && bridge.onBlob) {
    const off = bridge.onBlob("before", (blob, header) => {
      const url = URL.createObjectURL(blob);
      const slot = header && header.meta && Number.isFinite(header.meta.slot) ? header.meta.slot : vm.currentSlot.peek();
      decode({ url }).then((f) => {
        if (!f) { URL.revokeObjectURL(url); return; }
        const old = beforeUrl;
        beforeUrl = url;
        if (old) setTimeout(() => URL.revokeObjectURL(old), 4000);
        beforeFrame = { ...f, slot };
        if (!beforeCurrent()) return;
        if (mode() === "before") setPhoto(f);
        showBeforeInClip();
      });
    });
    if (typeof off === "function") own(pc, off);
  }
  // The before frame depends on the photo and its geometry only (EditSession::renderBefore).
  const geometry = computed(() => {
    const p = vm.params.value, t = vm.editTarget.value;
    return JSON.stringify([t && t.node, vm.currentSlot.value, p.crop, p.rotation, p.quarterTurns, p.lensDistortion, p.lensCA, p.lensVignette]);
  });
  let askedFor = "", beforeTimer = 0;
  function wantBefore(force) {
    showBeforeInClip();
    if (mode() === "after" || !beforeOk) return;
    const key = geometry.peek() + "|" + (lastAfter ? 1 : 0);    // (a render level of the after frame does not matter)
    if (!force && key === askedFor) return;
    askedFor = key;
    clearTimeout(beforeTimer);
    beforeTimer = setTimeout(() => {
      vm.session.call("before").catch((e) => {
        const msg = String((e && e.message) || e);
        askedFor = "";
        // An older core without the method: Before shows the after frame, said once. Anything
        // else ("no photo is on the stage" while a load runs) is transient - asked again later.
        if (/no method named/i.test(msg)) {
          beforeOk = false;
          if (!warned) { warned = true; ed.toast && ed.toast("Before is not available yet"); }
          if (lastAfter) setPhoto(lastAfter);
        }
        console.info("cosmo: before:", msg);
      });
    }, 40);
  }
  bindEffect(pc, () => { geometry.value; untracked(() => wantBefore()); });

  // ---- compare mode (App::refreshPhotoForMode) and the split fade (180 ms)
  const splitA = new Tween(0, (a) => { split.style.opacity = a; seamEl.style.opacity = a; });
  let firstMode = true;
  bindEffect(pc, () => {
    const m = vm.view.compare.value;
    untracked(() => {
      if (firstMode) { splitA.set(m === "split" ? 1 : 0); firstMode = false; }
      else splitA.to(m === "split" ? 1 : 0, 180, Ease.EaseOutCubic);
      pc.classList.toggle("split", m === "split");
      if (m === "before" && beforeOk && beforeCurrent()) setPhoto(beforeFrame);
      else if (lastAfter) setPhoto(lastAfter);
      if (m !== "after") wantBefore(true); else showBeforeInClip();
    });
  });

  bindEffect(pc, () => { size.value; vm.view.zoom.value; vm.view.seam.value; untracked(layout); });

  // ---- the compare pill
  const hiPath = h("path");
  const hiSvg = h("svg.pc-hi", { width: PILL_W, height: PILL_H, viewBox: `0 0 ${PILL_W} ${PILL_H}` }, hiPath);
  pill.append(hiSvg);
  const segs = LABELS.map((label, i) => {
    const seg = h("div.pc-seg", { style: { left: i * SEG_W + "px", width: SEG_W + "px" } },
      text(label, { x: (SEG_W - est(label, 10)) / 2, y: PILL_H / 2 + 10 * 0.35, size: 10 }));
    gestures(seg, { click: () => { vm.view.compare.value = MODES[i]; } });
    pill.append(seg);
    return seg;
  });
  const radii = (i) => [i === 0 ? 6 : 0, i === 2 ? 6 : 0, i === 2 ? 6 : 0, i === 0 ? 6 : 0];   // tl tr br bl
  const hiPos = new Tween(2, (p) => {
    p = Math.max(0, Math.min(2, p));
    const a = Math.floor(p), b = Math.min(2, a + 1), f = p - a;
    const ra = radii(a), rb = radii(b);
    const r = ra.map((v, k) => v + (rb[k] - v) * f);
    hiPath.setAttribute("d", cornersPath(p * SEG_W, 0, SEG_W, PILL_H, r[0], r[1], r[2], r[3]));
  });
  let firstPill = true;
  bindEffect(pill, () => {
    const i = MODES.indexOf(vm.view.compare.value);
    segs.forEach((s, k) => s.classList.toggle("active", k === i));
    untracked(() => { if (firstPill) { hiPos.set(i); firstPill = false; } else hiPos.to(i, 220, Ease.EaseOutCubic); });
  });

  // ---- input (PhotoCanvas::handleGesture, App's Ctrl+wheel)
  const seamX = () => vm.view.seam.peek() * size.peek().w;
  const onSeam = (p) => mode() === "split" && Math.abs(p.x - seamX()) <= SEAM_HIT;
  const inOverlay = (e) => e.target !== layer && layer.contains(e.target);
  const onPill = (e) => pill.contains(e.target);
  let seamDrag = null, panLast = null;
  const seamTween = new Tween(0.5, (v) => { vm.view.seam.value = v; });
  pc.addEventListener("pointermove", (e) => {
    if (seamDrag) return;
    seamEl.classList.toggle("hover", !onPill(e) && !inOverlay(e) && onSeam(local(pc, e)));
  });
  pc.addEventListener("pointerleave", () => { if (!seamDrag) seamEl.classList.remove("hover"); });
  gestures(pc, {
    down: (e, p) => {
      if (onPill(e) || inOverlay(e)) return false;
      if (onSeam(p)) { seamDrag = { dx: p.x - seamX() }; seamEl.classList.add("hover"); return; }
      if ((vm.view.zoom.peek().z || 1) > 1) panLast = p;
    },
    dragStart: (e, p) => {
      if (seamDrag) { if (onSeam(p)) seamDrag.dx = p.x - seamX(); moveSeam(p); return; }
      if (panLast) panLast = p;
    },
    drag: (e, p) => {
      if (seamDrag) { moveSeam(p); return; }
      if (panLast && (vm.view.zoom.peek().z || 1) > 1) { panBy(p.x - panLast.x, p.y - panLast.y); panLast = p; }
    },
    up: () => { seamDrag = null; panLast = null; },
    click: (e, p) => {
      if (!vm.view.wbArmed.peek()) return;                      // R-WB-1: offered first
      const r = rect.peek();
      if (!(r.w > 0 && r.h > 0)) return;
      const nx = (p.x - r.x) / r.w, ny = (p.y - r.y) / r.h;
      if (nx >= 0 && nx <= 1 && ny >= 0 && ny <= 1) vm.wbPick(nx, ny).catch(() => {});
    },
    doubleClick: (e, p) => {
      if (onSeam(p)) { seamTween.set(vm.view.seam.peek()); seamTween.to(0.5, 180, Ease.EaseOutCubic); }
    },
    rightClick: (e) => { const q = local(ed.root, e); ed.openContext(q.x, q.y, -1); },
  });
  function moveSeam(p) {
    const cw = size.peek().w;
    if (cw <= 0) return;
    const x = Math.min(cw - SEAM_HIT, Math.max(SEAM_HIT, p.x - seamDrag.dx));
    vm.view.seam.value = x / cw;                                 // direct: the pointer is the animation
  }
  // A right press on the pill or an overlay still opens the photo menu (the canvas' RightClick).
  pill.addEventListener("pointerup", (e) => {
    if (e.button === 2) { const q = local(ed.root, e); ed.openContext(q.x, q.y, -1); }
  });
  pc.addEventListener("wheel", (e) => {
    if (!e.ctrlKey) return;
    e.preventDefault();
    const d = wheelDelta(e);
    if (d !== 0) zoomAbout(d > 0 ? 1.15 : 1 / 1.15, local(pc, e));
  }, { passive: false });
  // An empty workspace shows no photo (App::resetWorkspace clears the stage and the before view).
  bindEffect(pc, () => {
    if (vm.imageCount.value > 0) return;
    untracked(() => {
      held = null; lastAfter = null; beforeFrame = null; askedFor = ""; lastUrl = "";
      for (const sl of [...L, beforeSlot]) { sl.f = null; sl.el.removeAttribute("src"); }
      topIsCurrent = false;
      topA.set(0);
    });
  });

  // ---- the overlay seam (B)
  const stage = {
    layer, rect, frame: displayed, size,
    toNorm(clientX, clientY) {
      const q = local(layer, { clientX, clientY });
      const r = rect.peek();
      return { x: r.w > 0 ? (q.x - r.x) / r.w : 0, y: r.h > 0 ? (q.y - r.y) / r.h : 0 };
    },
    fromNorm(x, y) { const r = rect.peek(); return { x: r.x + x * r.w, y: r.y + y * r.h }; },
  };
  import("./overlays.js")
    .then((m) => m.mountOverlays(stage, vm, ed.ctx))
    .catch((e) => console.info("cosmo: overlays not mounted:", e.message));
  return stage;
}
