/*
 * Cosmo by arstro — overlays.js: what the edit panels draw ON the photo, through the stage seam
 * (stage = { layer, rect, toNorm, fromNorm, frame, size } - desktop/stage.js).
 *
 * MaskOverlay (widgets/MaskOverlay.{h,cpp}) - active iff the Mask tab shows and a mask is
 * selected (App.cpp:1025-1030); click-through when inactive.
 * Coordinates are normalised to the displayed image rect (zoom and pan included), NOT clamped
 * (a sanity bound of +-8 only). Lines primary; handle dots primary ringed canvasBg 1.5, r 6,
 * picked within 12 px. Radial: ellipse 1.5, centre dot r 4.2, handles at (cx+rx, cy) and
 * (cx, cy+ry); pick X, Y, then inside = move (absolute). Linear: the 0 % / 100 % lines through
 * p0 / p1 perpendicular to p0->p1, extended by max(fit.w, fit.h); the axis at 0.5, 1 px. Brush:
 * every dab outlined at 0.7; any press paints - one dab (r 0.08, flow 1) per Down / DragStart /
 * Drag. Draw (path): maskPathPolygon closed, 2 points = one 0.45 segment; smooth points show
 * in->out handle lines (0.5) and r 3.6 dots; pick handles, then points, else a NEW point placed
 * on the press and dragged by the same gesture; Alt at the press on a point pulls its out
 * handle; handles mirror; a double-click on a point removes it. Every change sends the whole
 * mask - `mask set <i>` with the 25 fields of editcmd::maskFields - live, then final at the
 * release; re-seeds are ignored while dragging.
 *
 * CropOverlay (widgets/CropOverlay.{h,cpp} + CropGeometry.h) - while the Xform tab shows (and
 * through its fade): the four 0.62 black shades around the box inside the photo, thirds at
 * white 0.18, a 0.85 border, corner brackets (arm min(18, 0.4 min(w, h)), thickness 2 + hv,
 * alpha 0.85 + 0.15 hv; hv = HoverFade of that corner), all x the 180 ms EaseOutCubic appear
 * fade. Grab band 13 px, corners beat edges, inside = move; the press records its grab offset;
 * a drag sends `set crop=` live, the release the final one; with a ratio lock (the Xform chips)
 * resizes keep the shape (normalised by the photo's pixel size).
 *
 * The crop preview (App.cpp:1032-1090, R-CROP-5/7): while Xform is open the photo is shown
 * UNCROPPED - adapter method `uncropped` -> blob `uncropped` (this page only) - so the box can
 * move and grow past the current crop; the framing eases between the crop and the whole photo
 * over 220 ms EaseOutCubic (the image is placed so framing F fills the rect F fits into, which
 * is the native's zoom reveal), and the box refuses gestures until it has settled. Without that
 * method it works inside the current crop (CORE GAP G1): the framing is the crop, and during a
 * drag the displayed frame is frozen under the box so the photo does not re-frame beneath it.
 *
 * Pointer events: the overlay's hit layer takes the pointer only where it wants it (decided on
 * each move over the canvas), so pan, the split seam and the compare pill keep theirs.
 *
 * Deviations (R-G-1): the mask overlay fades with the tab (180 ms) where the native shows / hides
 * it in one frame; mask geometry and the crop box that arrive from the model (another client, a
 * ratio chip) ease over 220 ms; the zoom reveal keeps the stage's zoom factor and centre offset
 * (the native zooms the image view itself).
 */
import { h, bindEffect } from "../core/dom.js";
import { computed, untracked } from "../core/signal.js";
import { Tween, Ease } from "../core/motion.js";
import { num, fmt, formatCurve, LOCAL_ADJUST } from "../model/params.js";
import { tok, css, S, setA, HoverFade, gestures, toLocal } from "../ui/controls.js";
import { panelState, quiet } from "../panels/page.js";
import { maskPathPolygon, point, clonePts, fr, Part, partAt, resizeBy, moveTo, normalisedRatio } from "../panels/geometry.js";

const PICK = 12, HANDLE = 6, GRAB = 13, BRUSH_R = 0.08;
const geomClamp = (v) => (!(v > -8) ? -8 : v > 8 ? 8 : v);
const posRadius = (r) => (r > 1e-4 ? Math.min(r, 8) : 1e-4);

/** editcmd::maskFields: the whole mask, 25 keys in the native's order. */
export function maskFields(m) {
  const f = { type: String(m.type | 0), inverted: m.inverted ? "1" : "0", feather: num(fr(m.feather)),
    cx: num(fr(m.cx)), cy: num(fr(m.cy)), rx: num(fr(m.rx)), ry: num(fr(m.ry)),
    x0: num(fr(m.x0)), y0: num(fr(m.y0)), x1: num(fr(m.x1)), y1: num(fr(m.y1)) };
  for (const k of LOCAL_ADJUST) f["adjust." + k] = num(fr(m.adjust[k] || 0));
  f.dabs = m.dabs.map((d) => [d.x, d.y, d.radius, d.flow].map((v) => num(fr(v))).join(":")).join(";");
  f.path = formatCurve(m.path);
  return f;
}
const cloneMask = (m) => ({ ...m, adjust: { ...m.adjust }, dabs: m.dabs.map((d) => ({ ...d })), path: clonePts(m.path) });
const geomKey = (m) => JSON.stringify(maskFields(m));

function fitRect(aspect, cw, ch) {
  if (!(aspect > 0 && cw > 0 && ch > 0)) return { x: 0, y: 0, w: 0, h: 0 };
  const s = Math.min(cw / aspect, ch);
  const w = aspect * s, hh = s;
  return { x: (cw - w) / 2, y: (ch - hh) / 2, w, h: hh };
}

export function mountOverlays(stage, vm, ctx = {}) {
  const C = tok();
  const st = panelState(vm);
  const layer = stage.layer;
  const root = h("div.cp-overlay");
  layer.append(root);
  const reveal = h("div.cp-reveal");
  const revImg = h("img", { alt: "", draggable: "false" });
  reveal.append(revImg);
  reveal.style.opacity = "0";
  const svg = S("svg", { width: 1, height: 1, class: "cp-ov-svg" });
  const maskG = S("g", { class: "cp-maskg" }), cropG = S("g");
  svg.append(maskG, cropG);
  const hit = h("div.cp-hitlayer");
  root.append(reveal, svg, hit);

  // ---- canvas size
  const sizeOf = () => (stage.size ? stage.size.peek() : { w: layer.offsetWidth, h: layer.offsetHeight });
  function resize() { const s = sizeOf(); setA(svg, { width: s.w, height: s.h, viewBox: `0 0 ${s.w} ${s.h}` }); redrawAll(); }

  const tab = computed(() => vm.view.tab.value);
  const loc = (e) => toLocal(layer, e);

  // crop state (declared first: the mask side's hit test reads it)
  let cropPart = Part.None, grab = { x: 0, y: 0 }, crop = { x: 0, y: 0, w: 1, h: 1 };
  let cropHold = 0, heldCrop = "", frozen = null;       // frozen = {F, R, url} during a degraded drag
  let unc = null;                                        // {url, w, h, slot} the uncropped frame
  let cropEase = null;

  // ================================================================== MASK
  let work = null;           // the mask being drawn: the model's, or the drag's working copy
  let dragging = null;       // {handle}
  let holdUntil = 0, heldKey = "";
  let easeT = null, maskClear = 0;
  const maskActive = computed(() => tab.value === "mask" && st.selMask.value >= 0 && !!st.own.value.masks[st.selMask.value]);
  const fit = () => stage.rect.peek();
  const toPx = (nx, ny) => { const r = fit(); return [r.x + nx * r.w, r.y + ny * r.h]; };
  const toN = (p) => { const r = fit(); return [geomClamp(r.w > 0 ? (p.x - r.x) / r.w : 0), geomClamp(r.h > 0 ? (p.y - r.y) / r.h : 0)]; };

  function drawMask() {
    maskG.replaceChildren();
    if (!work) return;
    const m = work, r = fit(), line = C.primary;
    const dot = (x, y, rad) => S("circle", { cx: x, cy: y, r: rad, fill: css(C.primary), stroke: css(C.canvasBg), "stroke-width": 1.5 });
    const stroke = (a, wd) => ({ fill: "none", stroke: css(line, a), "stroke-width": wd });
    if (m.type === 1) {
      const [ax, ay] = toPx(m.x0, m.y0), [bx, by] = toPx(m.x1, m.y1);
      const dx = bx - ax, dy = by - ay, len = Math.max(1e-3, Math.hypot(dx, dy)), px = -dy / len, py = dx / len;
      const guide = Math.max(r.w, r.h);
      for (const [x, y] of [[ax, ay], [bx, by]]) maskG.append(S("path", { d: `M${x - px * guide} ${y - py * guide}L${x + px * guide} ${y + py * guide}`, ...stroke(1, 1.5) }));
      maskG.append(S("path", { d: `M${ax} ${ay}L${bx} ${by}`, ...stroke(0.5, 1) }));
      maskG.append(dot(ax, ay, HANDLE), dot(bx, by, HANDLE));
    } else if (m.type === 2) {
      for (const d of m.dabs) { const [x, y] = toPx(d.x, d.y); maskG.append(S("ellipse", { cx: x, cy: y, rx: Math.abs(d.radius * r.w), ry: Math.abs(d.radius * r.h), ...stroke(0.7, 1.5) })); }
    } else if (m.type === 3) {
      const poly = maskPathPolygon(m.path);
      if (poly.length >= 3) maskG.append(S("path", { d: poly.map(([x, y], i) => { const [X, Y] = toPx(x, y); return (i ? "L" : "M") + X + " " + Y; }).join("") + "Z", ...stroke(1, 1.5) }));
      else if (m.path.length === 2) { const [ax, ay] = toPx(m.path[0].x, m.path[0].y), [bx, by] = toPx(m.path[1].x, m.path[1].y); maskG.append(S("path", { d: `M${ax} ${ay}L${bx} ${by}`, ...stroke(0.45, 1.5) })); }
      for (const p of m.path) {
        if (!p.smooth) continue;
        const [ix, iy] = toPx(p.x + p.ix, p.y + p.iy), [ox, oy] = toPx(p.x + p.ox, p.y + p.oy);
        maskG.append(S("path", { d: `M${ix} ${iy}L${ox} ${oy}`, ...stroke(0.5, 1) }), dot(ix, iy, HANDLE * 0.6), dot(ox, oy, HANDLE * 0.6));
      }
      for (const p of m.path) { const [x, y] = toPx(p.x, p.y); maskG.append(dot(x, y, HANDLE)); }
    } else {
      const [cx, cy] = toPx(m.cx, m.cy);
      maskG.append(S("ellipse", { cx, cy, rx: Math.abs(m.rx * r.w), ry: Math.abs(m.ry * r.h), ...stroke(1, 1.5) }));
      maskG.append(dot(cx, cy, HANDLE * 0.7), dot(cx + m.rx * r.w, cy, HANDLE), dot(cx, cy + m.ry * r.h, HANDLE));
    }
  }
  const near = (p, x, y) => Math.hypot(p.x - x, p.y - y) <= PICK;
  const K_POINT = 1000, K_IN = 2000, K_OUT = 3000;
  function pickMask(p) {
    const m = work;
    if (!m) return -1;
    if (m.type === 1) { if (near(p, ...toPx(m.x0, m.y0))) return 0; if (near(p, ...toPx(m.x1, m.y1))) return 1; return -1; }
    if (m.type === 2) return 99;
    if (m.type === 3) {
      for (let i = 0; i < m.path.length; i++) {
        const q = m.path[i];
        if (!q.smooth) continue;
        if (near(p, ...toPx(q.x + q.ix, q.y + q.iy))) return K_IN + i;
        if (near(p, ...toPx(q.x + q.ox, q.y + q.oy))) return K_OUT + i;
      }
      for (let i = 0; i < m.path.length; i++) if (near(p, ...toPx(m.path[i].x, m.path[i].y))) return K_POINT + i;
      return K_POINT + m.path.length;
    }
    if (near(p, ...toPx(m.cx + m.rx, m.cy))) return 1;
    if (near(p, ...toPx(m.cx, m.cy + m.ry))) return 2;
    const [nx, ny] = toN(p);
    const dx = (nx - m.cx) / (m.rx > 1e-4 ? m.rx : 1e-4), dy = (ny - m.cy) / (m.ry > 1e-4 ? m.ry : 1e-4);
    return dx * dx + dy * dy <= 1 ? 0 : -1;
  }
  function applyMask(handle, p) {
    const m = work, [nx, ny] = toN(p);
    if (m.type === 3) {
      const kind = handle - (handle % 1000), i = handle % 1000;
      if (kind === K_POINT) {
        if (i >= m.path.length) { if (i > m.path.length) return; m.path.push(point(nx, ny)); }
        else { m.path[i].x = fr(nx); m.path[i].y = fr(ny); }
      } else if (i < m.path.length) {
        const q = m.path[i], dx = fr(nx - q.x), dy = fr(ny - q.y);
        q.smooth = true;
        if (kind === K_IN) { q.ix = dx; q.iy = dy; q.ox = -dx; q.oy = -dy; } else { q.ox = dx; q.oy = dy; q.ix = -dx; q.iy = -dy; }
      }
    } else if (m.type === 1) {
      if (handle === 0) { m.x0 = fr(nx); m.y0 = fr(ny); } else { m.x1 = fr(nx); m.y1 = fr(ny); }
    } else if (m.type === 2) {
      m.dabs.push({ x: fr(nx), y: fr(ny), radius: fr(BRUSH_R), flow: 1 });
    } else {
      if (handle === 0) { m.cx = fr(nx); m.cy = fr(ny); }
      else if (handle === 1) m.rx = fr(posRadius(Math.abs(nx - m.cx)));
      else if (handle === 2) m.ry = fr(posRadius(Math.abs(ny - m.cy)));
    }
    sendMask(true);
  }
  let changed = false;
  function sendMask(live) {
    changed = true;
    drawMask();
    quiet(vm.maskSet(st.selMask.peek(), maskFields(work), { live }));
  }

  // model -> overlay (own masks), eased when another client moves the geometry
  bindEffect(root, () => {
    const active = maskActive.value, s = st.selMask.value, own = st.own.value;
    untracked(() => {
      maskG.style.opacity = active ? "1" : "0";
      if (!active) { dragging = null; clearTimeout(maskClear); maskClear = setTimeout(() => { if (!maskActive.peek()) { work = null; drawMask(); } }, 220); updateHit(); return; }
      if (dragging) return;
      const m = own.masks[s];
      const key = geomKey(m);
      if (holdUntil) { if (performance.now() < holdUntil && key !== heldKey) return; holdUntil = 0; }
      const prev = work;
      if (prev && prev.__sel === s && prev.type === m.type && geomKey(prev) !== key) {
        const from = cloneMask(prev), to = cloneMask(m);
        to.__sel = s;
        if (easeT) { easeT.onUpdate = null; easeT.set(1); }
        const L = (a, b, k) => a + (b - a) * k;
        easeT = new Tween(0, (k) => {
          const x = cloneMask(to);
          x.__sel = s;
          for (const f of ["cx", "cy", "rx", "ry", "x0", "y0", "x1", "y1"]) x[f] = L(from[f], to[f], k);
          if (from.path.length === to.path.length) x.path = to.path.map((q, i) => ({ ...q, x: L(from.path[i].x, q.x, k), y: L(from.path[i].y, q.y, k) }));
          work = k >= 1 ? to : x;
          drawMask();
        });
        easeT.to(1, 220, Ease.EaseOutCubic);
        return;
      }
      work = cloneMask(m);
      work.__sel = s;
      drawMask();
      updateHit();
    });
  });

  // ================================================================== CROP
  const cropHover = new HoverFade(drawCrop);
  const appear = new Tween(0, drawCrop);
  const revealT = new Tween(0, () => { placeReveal(); drawCrop(); });
  const xformOn = computed(() => tab.value === "xform");
  const cropActive = () => xformOn.peek() || revealT.value > 0.001;
  const lerpR = (a, b, t) => ({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, w: a.w + (b.w - a.w) * t, h: a.h + (b.h - a.h) * t });
  function framedRect(aspect) {
    const s = sizeOf(), b = fitRect(aspect, s.w, s.h), cur = stage.rect.peek(), f = stage.frame ? stage.frame.peek() : null;
    const b0 = f && f.w && f.h ? fitRect(f.w / f.h, s.w, s.h) : cur;
    const z = b0.w > 0 ? cur.w / b0.w : 1;
    if (!(Math.abs(z - 1) > 1e-3)) return b;
    const ox = cur.x + cur.w / 2 - (b0.x + b0.w / 2), oy = cur.y + cur.h / 2 - (b0.y + b0.h / 2);
    const r = { w: b.w * z, h: b.h * z };
    r.x = b.x + b.w / 2 + ox - r.w / 2; r.y = b.y + b.h / 2 + oy - r.h / 2;
    if (r.x > 0) r.x = 0; if (r.x + r.w < s.w) r.x += s.w - (r.x + r.w);
    if (r.y > 0) r.y = 0; if (r.y + r.h < s.h) r.y += s.h - (r.y + r.h);
    return r;
  }
  /** {F: the framing shown (source-normalised), R: where it is displayed (layer px)}. */
  function framing() {
    if (frozen) return frozen;
    if (unc && revealT.value > 0) {
      const F = lerpR(crop, { x: 0, y: 0, w: 1, h: 1 }, revealT.value);
      return { F, R: framedRect((unc.w / unc.h) * (F.w / F.h)) };
    }
    return { F: crop, R: stage.rect.peek() };
  }
  function boxPx() {
    const { F, R } = framing(), fw = F.w > 1e-9 ? F.w : 1, fh = F.h > 1e-9 ? F.h : 1;
    return { x: R.x + ((crop.x - F.x) / fw) * R.w, y: R.y + ((crop.y - F.y) / fh) * R.h, w: (crop.w / fw) * R.w, h: (crop.h / fh) * R.h };
  }
  function placeReveal() {
    const t = revealT.value;
    if (frozen && frozen.url) {
      reveal.style.opacity = "1";
      revImg.src !== frozen.url && (revImg.src = frozen.url);
      Object.assign(revImg.style, { left: frozen.R.x + "px", top: frozen.R.y + "px", width: frozen.R.w + "px", height: frozen.R.h + "px" });
      return;
    }
    if (!unc || t <= 0) { reveal.style.opacity = "0"; return; }
    const { F, R } = framing();
    if (revImg.src !== unc.url) revImg.src = unc.url;
    const iw = R.w / F.w, ih = R.h / F.h;
    Object.assign(revImg.style, { left: R.x - (F.x / F.w) * R.w + "px", top: R.y - (F.y / F.h) * R.h + "px", width: iw + "px", height: ih + "px" });
    reveal.style.opacity = "1";
  }
  function drawCrop() {
    cropG.replaceChildren();
    const a = appear.value;
    const { R } = framing();
    if (a <= 0.001 || R.w <= 0 || R.h <= 0) return;
    const b0 = boxPx();
    const box = { x: Math.max(b0.x, R.x), y: Math.max(b0.y, R.y) };
    box.w = Math.min(b0.w, R.x + R.w - box.x); box.h = Math.min(b0.h, R.y + R.h - box.y);
    if (box.w <= 0 || box.h <= 0) return;
    const r = (x, y, w, hh, fill) => cropG.append(S("rect", { x, y, width: Math.max(0, w), height: Math.max(0, hh), fill }));
    const shade = css([0, 0, 0, 0.62], a);                                    // CropOverlay.cpp kDimAlpha
    r(R.x, R.y, R.w, box.y - R.y, shade);
    r(R.x, box.y + box.h, R.w, R.y + R.h - (box.y + box.h), shade);
    r(R.x, box.y, box.x - R.x, box.h, shade);
    r(box.x + box.w, box.y, R.x + R.w - (box.x + box.w), box.h, shade);
    const third = css(C.white, 0.18 * a);
    for (let i = 1; i <= 2; i++) { r(box.x + (box.w * i) / 3 - 0.5, box.y, 1, box.h, third); r(box.x, box.y + (box.h * i) / 3 - 0.5, box.w, 1, third); }
    const border = css(C.white, 0.85 * a);
    r(box.x, box.y - 0.5, box.w, 1, border); r(box.x, box.y + box.h - 0.5, box.w, 1, border);
    r(box.x - 0.5, box.y, 1, box.h, border); r(box.x + box.w - 0.5, box.y, 1, box.h, border);
    const L = Math.min(18, Math.min(box.w, box.h) * 0.4);
    for (const [x, y, sx, sy, part] of [[box.x, box.y, 1, 1, Part.TopLeft], [box.x + box.w, box.y, -1, 1, Part.TopRight],
      [box.x + box.w, box.y + box.h, -1, -1, Part.BottomRight], [box.x, box.y + box.h, 1, -1, Part.BottomLeft]]) {
      const hv = cropHover.amount(part), w = 2 + hv, col = css(C.white, (0.85 + 0.15 * hv) * a);
      r(sx > 0 ? x : x - L, sy > 0 ? y : y - w, L, w, col);
      r(sx > 0 ? x : x - w, sy > 0 ? y : y - L, w, L, col);
    }
  }
  function partUnder(p) {
    if (!cropActive() || !xformOn.peek()) return Part.None;
    const { R } = framing();
    if (R.w <= 0 || R.h <= 0) return Part.None;
    return partAt(boxPx(), p, GRAB);
  }
  const nOf = (px, py) => { const { F, R } = framing(); return [F.x + ((px - R.x) / R.w) * F.w, F.y + ((py - R.y) / R.h) * F.h]; };
  function sendCrop(live) {
    const c = { x: fr(crop.x), y: fr(crop.y), w: fr(crop.w), h: fr(crop.h) };
    quiet(vm.setFields({ crop: fmt.crop(c) }, { live }));
  }

  // model -> crop box
  bindEffect(root, () => {
    const c = st.own.value.crop;
    vm.frame.value;
    untracked(() => {
      if (cropPart !== Part.None) return;
      const key = fmt.crop(c);
      if (cropHold) { if (performance.now() < cropHold && key !== heldCrop) return; cropHold = 0; }
      // A degraded drag's frozen frame stays until a frame of the new crop is on screen.
      if (frozen && !frozen.dragging && key === heldCrop && frozen.seq !== (vm.frame.peek() && vm.frame.peek().meta && vm.frame.peek().meta.seq)) { frozen = null; placeReveal(); drawCrop(); }
      const next = { x: c.x, y: c.y, w: c.w, h: c.h };
      if (fmt.crop(next) === fmt.crop(crop)) return;
      // A crop that arrives from the model (another client, a ratio chip) eases the box there
      // (R-G-1); our own drag has already put it in place.
      if (appear.value > 0.001 && !frozen) {
        const from = { ...crop };
        if (cropEase) { cropEase.onUpdate = null; cropEase.set(1); }
        cropEase = new Tween(0, (k) => { crop = k >= 1 ? next : lerpR(from, next, k); placeReveal(); drawCrop(); });
        cropEase.to(1, 220, Ease.EaseOutCubic);
        return;
      }
      crop = next;
      placeReveal();
      drawCrop();
    });
  });
  bindEffect(root, () => {
    const on = xformOn.value;
    untracked(() => {
      if (on) { appear.to(1, 180, Ease.EaseOutCubic); requestUncropped(); if (unc) revealT.to(1, 220, Ease.EaseOutCubic); }
      else { revealT.to(0, 220, Ease.EaseOutCubic); cropHover.set(-1); fadeWhenClosed(); }
      updateHit();
    });
  });
  function fadeWhenClosed() {
    // Active for the whole zoom back, so the box fades after it (CropOverlay::advance).
    const wait = () => { if (xformOn.peek()) return; if (revealT.value > 0.001) { requestAnimationFrame(wait); return; } appear.to(0, 180, Ease.EaseOutCubic); frozen = null; placeReveal(); };
    wait();
  }

  // ---- the uncropped frame (adapter method, blob to this page)
  let uncBusy = false, uncAgain = false, uncMissing = false, uncKey = "";
  const bridge = vm.session && vm.session.bridge;
  if (bridge && bridge.onBlob) {
    bridge.onBlob("uncropped", (blob, hd) => {
      const meta = (hd && hd.meta) || {};
      if (meta.slot !== undefined && meta.slot !== vm.currentSlot.peek()) return;
      const url = URL.createObjectURL(blob);
      const img = new Image();
      img.onload = () => {
        const old = unc;
        unc = { url, w: meta.w || img.naturalWidth, h: meta.h || img.naturalHeight, slot: meta.slot };
        if (old) setTimeout(() => URL.revokeObjectURL(old.url), 2000);
        if (xformOn.peek()) revealT.to(1, 220, Ease.EaseOutCubic);
        placeReveal(); drawCrop();
      };
      img.src = url;
    });
  }
  function requestUncropped() {
    if (!xformOn.peek() || uncMissing || !vm.session || !st.hasTarget.peek()) return;
    if (uncBusy) { uncAgain = true; return; }
    uncBusy = true;
    vm.session.call("uncropped").catch((e) => {
      if (/no method/i.test((e && e.message) || "")) uncMissing = true;
    }).finally(() => { uncBusy = false; if (uncAgain) { uncAgain = false; requestUncropped(); } });
  }
  // Re-render the uncropped frame when anything but the crop changes (another photo, an edit).
  let uncTimer = 0;
  bindEffect(root, () => {
    const p = vm.params.value, slot = vm.currentSlot.value;
    const key = slot + "|" + Object.entries(p.raw || {}).filter(([k]) => k !== "crop").map(([k, v]) => k + "=" + v).join(";") +
      "|" + p.masks.length;
    untracked(() => {
      if (key === uncKey) return;
      const slotChanged = !uncKey.startsWith(slot + "|");
      uncKey = key;
      if (slotChanged && unc && unc.slot !== slot) { unc = null; revealT.set(0); placeReveal(); drawCrop(); }
      clearTimeout(uncTimer);
      uncTimer = setTimeout(requestUncropped, 150);
    });
  });

  // ================================================================== input
  function wants(p) {
    if (cropPart !== Part.None || dragging) return true;
    if (xformOn.peek() && partUnder(p) !== Part.None) return true;
    if (maskActive.peek() && work) return pickMask(p) >= 0;
    return false;
  }
  function updateHit(p) { hit.classList.toggle("on", !!(p ? wants(p) : cropPart !== Part.None || dragging)); }
  const canvas = layer.parentElement || layer;
  canvas.addEventListener("pointermove", (e) => {
    if (cropPart !== Part.None || dragging) return;
    const p = loc(e);
    if (xformOn.peek()) { const part = partUnder(p); cropHover.set(isCornerPart(part) ? part : -1); }
    updateHit(p);
  });
  canvas.addEventListener("pointerleave", () => { if (cropPart === Part.None && !dragging) { cropHover.set(-1); hit.classList.remove("on"); } });
  const isCornerPart = (pt) => pt === Part.TopLeft || pt === Part.TopRight || pt === Part.BottomRight || pt === Part.BottomLeft;

  gestures(hit, {
    down: (lp, e) => {
      const p = loc(e);
      // crop first (it sits above the mask overlay)
      if (xformOn.peek()) {
        if (!(!unc || revealT.value >= 1 - 1e-4)) return false;     // not settled: refuse
        const part = partUnder(p);
        if (part === Part.None) return false;
        const b = boxPx();
        const anchors = { [Part.Move]: [b.x, b.y], [Part.Left]: [b.x, p.y], [Part.Right]: [b.x + b.w, p.y], [Part.Top]: [p.x, b.y],
          [Part.Bottom]: [p.x, b.y + b.h], [Part.TopLeft]: [b.x, b.y], [Part.TopRight]: [b.x + b.w, b.y],
          [Part.BottomRight]: [b.x + b.w, b.y + b.h], [Part.BottomLeft]: [b.x, b.y + b.h] };
        const [ax, ay] = anchors[part];
        grab = { x: p.x - ax, y: p.y - ay };
        if (!unc || revealT.value <= 0) {
          const f = stage.frame ? stage.frame.peek() : null;
          frozen = { ...framing(), url: f && f.url, dragging: true };
        }
        if (cropEase) { cropEase.onUpdate = null; cropEase.set(1); cropEase = null; }
        cropPart = part;
        cropHover.set(isCornerPart(part) ? part : -1);
        e.preventDefault();
        return;
      }
      if (!maskActive.peek() || !work) return false;
      let hd = pickMask(p);
      if (hd < 0) return false;
      if (work.type === 3 && p.alt && hd >= K_POINT && hd < K_IN && hd - K_POINT < work.path.length) hd = K_OUT + (hd - K_POINT);
      if (easeT) { easeT.onUpdate = null; easeT.set(1); easeT = null; }
      dragging = { handle: hd };
      changed = false;
      if (hd === 99 || (work.type === 3 && hd >= K_POINT)) applyMask(hd, p);
      e.preventDefault();
    },
    dragStart: (lp, e) => { if (dragging) applyMask(dragging.handle, loc(e)); },
    drag: (lp, e) => {
      const p = loc(e);
      if (cropPart !== Part.None) {
        const [nx, ny] = nOf(p.x - grab.x, p.y - grab.y);
        if (cropPart === Part.Move) crop = moveTo(crop, nx, ny);
        else { const src = st.source.peek(); crop = resizeBy(crop, cropPart, nx, ny, normalisedRatio(st.lock.peek(), src.w, src.h)); }
        drawCrop();
        sendCrop(true);
        return;
      }
      if (dragging) applyMask(dragging.handle, p);
    },
    up: () => {
      if (cropPart !== Part.None) {
        cropPart = Part.None;
        grab = { x: 0, y: 0 };
        heldCrop = fmt.crop({ x: fr(crop.x), y: fr(crop.y), w: fr(crop.w), h: fr(crop.h) });
        cropHold = performance.now() + 900;
        if (frozen) { frozen.dragging = false; const f = vm.frame.peek(); frozen.seq = f && f.meta && f.meta.seq; }
        sendCrop(false);
        cropHover.set(-1);
      }
      if (dragging) {
        dragging = null;
        if (changed) { heldKey = geomKey(work); holdUntil = performance.now() + 900; quiet(vm.maskSet(st.selMask.peek(), maskFields(work))); }
      }
      updateHit();
    },
    doubleClick: (lp, e) => {
      if (!maskActive.peek() || !work || work.type !== 3) return;
      const p = loc(e);
      for (let i = 0; i < work.path.length; i++) {
        if (near(p, ...toPx(work.path[i].x, work.path[i].y))) {
          work.path.splice(i, 1);
          heldKey = geomKey(work); holdUntil = performance.now() + 900;
          drawMask();
          quiet(vm.maskSet(st.selMask.peek(), maskFields(work)));
          return;
        }
      }
    },
  });

  function redrawAll() { drawMask(); placeReveal(); drawCrop(); }
  if (stage.size) bindEffect(root, () => { stage.size.value; untracked(resize); });
  else { const ro = new ResizeObserver(resize); ro.observe(layer); }
  bindEffect(root, () => { stage.rect.value; untracked(redrawAll); });
  redrawAll();
  return { destroy() { root.remove(); } };
}
