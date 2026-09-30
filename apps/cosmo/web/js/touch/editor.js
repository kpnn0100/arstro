/*
 * Cosmo by arstro — touch/editor.js: the phone editor screen.
 *
 * Ports PhoneApp.cpp's EditorScreen (:895-1054): the 48 px top bar (drawer, back, title, undo,
 * redo, more), the #0A0A0A stage with the Contain-fit photo, the Before / Split / After pill
 * (BeforeAfterPill :245-269), the breadcrumb (24) and filmstrip (72) and the tray (tray.js).
 *
 * Deliberate deviations, all required (screens_touch.md §14.5, §14.15, §14.16, R-TOUCH-2/3):
 *  - the photo box SHRINKS as the tray rises (photoH = H - 48 - 24 - 72 - trayH) and the
 *    breadcrumb + filmstrip ride on top of the tray - nothing is ever drawn over the photo;
 *  - landscape is two panes: photo / breadcrumb / filmstrip on the left, the tray as a fixed panel
 *    on the right (round(0.4 W)); a rotation re-flows every rect over 280 ms EaseOutCubic;
 *  - the pill's highlight slides (200 ms) and switches the photo: Before and Split show the
 *    service's geometry-only `before` render (adapter method, to this client only), Split with a
 *    1.5 px accent seam that a finger can drag;
 *  - pinch-zoom 1x..8x about the pinch midpoint, two-finger pan, one-finger pan while zoomed,
 *    double-tap fit <-> 100 % (brief §7.1) - view state of this page (vm.view.zoom);
 *  - the filmstrip scrolls, a long-press adds to the selection without the trailing click
 *    re-selecting, double-tap a group drills in (view state, vm.openGroup) and the breadcrumb
 *    navigates back; back asks about unsaved changes (ConfirmDialog's three buttons);
 *  - undo / redo / select go through the view-model like every other client, never the session.
 */
import { h, bindEffect, bindText, bindClass, keyed, own } from "../core/dom.js";
import { signal, computed, untracked } from "../core/signal.js";
import { Tween, Ease } from "../core/motion.js";
import { icon } from "../ui/icons.js";
import { ROOT } from "../vm/editor.js";
import { K, tx, rect, ticon, gestures, pressWash, baselineBox, clamp, lerp } from "./tk.js";
import { mountTray } from "./tray.js";

const quiet = (p) => { if (p && p.catch) p.catch(() => {}); return p; };

export function mountEditor(parent, vm, ctx, env) {
  const el = h("div.t-editor");
  parent.append(el);
  const { W, H, sheets } = env;
  const land = computed(() => W.value > H.value);

  // gesture on / off around every press in the editor (R-PREVIEW-1; App.cpp:838-859).
  const down = new Set();
  el.addEventListener("pointerdown", (e) => { if (!down.has(e.pointerId)) { down.add(e.pointerId); vm.gestureOn(); } }, true);
  const release = (e) => { if (down.delete(e.pointerId)) vm.gestureOff(); };
  window.addEventListener("pointerup", release, true);
  window.addEventListener("pointercancel", release, true);
  own(el, () => { window.removeEventListener("pointerup", release, true); window.removeEventListener("pointercancel", release, true); });

  const stage = h("div.t-stage");
  const photoBox = h("div.t-photo");
  const pill = beforeAfterPill(vm);
  const crumb = breadcrumb(vm);
  const film = filmstrip(vm);
  el.append(stage, photoBox, pill, crumb, film);
  const photo = photoView(photoBox, vm, ctx);

  // ---------------------------------------------------------------- the tray's height and the layout
  const detent = signal("open");                                   // PhoneApp: initial detent Open
  const openH = () => 0.62 * H.peek();
  const trayW = signal(W.peek());
  const trayH = new Tween(openH(), () => layout());
  const reflow = new Tween(1, () => layout());
  let from = null, last = null;
  const env2 = {
    W: trayW, H, land, detent, sheets,
    setDetent(d) { detent.value = d; trayH.to(d === "rail" ? K.rail : openH(), 280, Ease.EaseOutCubic); },
    dragTo(hpx) { trayH.set(clamp(hpx, K.rail, openH())); },
    snap() { env2.setDetent(trayH.value < (K.rail + openH()) / 2 ? "rail" : "open"); },
  };
  const tray = mountTray(el, vm, ctx, env2);
  const top = topBar(vm, ctx, env);
  el.append(top);

  function targets() {
    const w = W.peek(), hh = H.peek();
    if (!land.peek()) {
      const th = trayH.value;
      return { photo: { x: 0, y: K.topBar, w, h: Math.max(0, hh - K.topBar - K.crumb - K.film - th) },
               crumb: { x: 0, y: hh - th - K.crumb - K.film, w }, film: { x: 0, y: hh - th - K.film, w },
               tray: { x: 0, y: hh - th, w, h: th } };
    }
    const wr = Math.round(0.4 * w), wl = w - wr;                   // §14.15: the right pane is round(0.4 W)
    return { photo: { x: 0, y: K.topBar, w: wl, h: Math.max(0, hh - K.topBar - K.crumb - K.film) },
             crumb: { x: 0, y: hh - K.crumb - K.film, w: wl }, film: { x: 0, y: hh - K.film, w: wl },
             tray: { x: wl, y: K.topBar, w: wr, h: hh - K.topBar } };
  }
  const mix = (a, b, t) => { const o = {}; for (const k in b) { o[k] = {}; for (const f in b[k]) o[k][f] = lerp(a[k][f], b[k][f], t); } return o; };
  function layout() {
    const T = targets();
    const R = from && reflow.value < 1 ? mix(from, T, reflow.value) : T;
    last = R;
    rect(photoBox, R.photo.x, R.photo.y, R.photo.w, R.photo.h);
    photo.layout(R.photo.w, R.photo.h);
    pill.style.left = R.photo.x + R.photo.w / 2 - 95 + "px";
    pill.style.top = R.photo.y + R.photo.h - 46 + "px";
    rect(crumb, R.crumb.x, R.crumb.y, R.crumb.w);
    rect(film, R.film.x, R.film.y, R.film.w);
    rect(tray.el, R.tray.x, R.tray.y, R.tray.w, R.tray.h);
    const tw = Math.round(R.tray.w);
    if (trayW.peek() !== tw) trayW.value = tw;
  }
  // Resize: re-lay out at once; a change of orientation re-flows every rect (R-TOUCH-3).
  let wasLand = land.peek();
  bindEffect(el, () => {
    W.value; H.value;
    const l = land.value;
    untracked(() => {
      if (!l && !trayH.animating) trayH.set(detent.peek() === "rail" ? K.rail : openH());
      if (l !== wasLand && last) { from = last; reflow.set(0); reflow.to(1, 280, Ease.EaseOutCubic); }
      wasLand = l;
      el.classList.toggle("land", l);
      layout();
    });
  });

  // Entering the editor over a project that is already loaded: ask for a frame (PhoneApp.cpp:1447).
  bindEffect(el, () => {
    if (vm.screen.value !== "editor") return;
    untracked(() => { if (!vm.frame.peek() && vm.selectedNode.peek() >= 0) quiet(vm.select(vm.selectedNode.peek())); });
  });
  return { el };
}

// ---------------------------------------------------------------- top bar
function topBar(vm, ctx, env) {
  const bar = h("div.t-top");
  const hit = (x, w, fn, cls) => {
    const b = h("div.t-hit" + (cls ? "." + cls : ""));
    b.style.left = x; b.style.width = w + "px";
    gestures(b, { down: () => pressWash(b), tap: fn });
    bar.append(b);
    return b;
  };
  const pl = icon("panelLeft", 20, 1.5);
  pl.classList.add("t-ic"); pl.style.left = "10px";
  const back = ticon("back", 20, 1.75); back.classList.add("t-ic"); back.style.left = "54px";
  const undo = ticon("undo", 20, 1.75); undo.classList.add("t-ic"); undo.style.left = "calc(100% - 132px)";
  const redo = ticon("redo", 20, 1.75); redo.classList.add("t-ic"); redo.style.left = "calc(100% - 88px)";
  const more = ticon("more", 20, 1.75); more.classList.add("t-ic"); more.style.left = "calc(100% - 44px)";
  bindClass(undo, "dim", () => !vm.history.value.canUndo);
  bindClass(redo, "dim", () => !vm.history.value.canRedo);
  const title = tx("", "50%", 29, 14, "medium", "center", "t-title");
  const group = computed(() => { const g = vm.editGroup.value; const n = g >= 0 ? vm.nodeById.value.get(g) : null; return n && n.kind === "group" ? n : null; });
  bindText(title, () => (group.value ? "Group: " + group.value.name : vm.project.value.name || "Untitled"));
  bindClass(title, "grp", () => !!group.value);
  bar.append(pl, back, title, undo, redo, more, h("i.t-hl.bar"));
  hit("0px", 44, () => env.sheets.drawer());
  hit("44px", 44, () => goHome(vm, env));
  hit("calc(100% - 140px)", 44, () => { if (vm.history.peek().canUndo) quiet(vm.undo()); });
  hit("calc(100% - 96px)", 44, () => { if (vm.history.peek().canRedo) quiet(vm.redo()); });
  hit("calc(100% - 52px)", 52, () => env.sheets.overflow());
  return bar;
}

/** Back: a dirty project asks first (ConfirmDialog "Unsaved changes", screens_touch.md §10). */
export async function goHome(vm, env) {
  if (!vm.project.peek().dirty) { vm.goHome(); return; }
  const r = await env.sheets.confirm({
    title: "Unsaved changes", message: "Save your changes to this project before leaving?",
    buttons: [{ label: "Cancel", value: "cancel" }, { label: "Discard", value: "discard", kind: "destructive" }, { label: "Save", value: "save", kind: "primary" }],
  });
  if (r === "discard") quiet(vm.closeProject());
  else if (r === "save") { try { await vm.save(); vm.goHome(); } catch { /* toasted */ } }
}

// ---------------------------------------------------------------- the photo
function photoView(box, vm, ctx) {
  const layer = h("div.t-layer");
  const before = h("img.t-before", { alt: "" });
  const seam = h("i.t-seam");
  box.append(layer, before, seam);
  let bw = 0, bh = 0, aspect = 1.5, cur = null, curUrl = "";
  let D = { x: 0, y: 0, w: 0, h: 0 };

  function place() {
    const z = vm.view.zoom.peek();
    let fw = bw, fh = bw / aspect;
    if (fh > bh) { fh = bh; fw = bh * aspect; }
    const w = fw * z.z, hh = fh * z.z;
    const cx = w <= bw ? 0.5 : clamp(z.x, bw / 2 / w, 1 - bw / 2 / w);
    const cy = hh <= bh ? 0.5 : clamp(z.y, bh / 2 / hh, 1 - bh / 2 / hh);
    D = { x: bw / 2 - cx * w, y: bh / 2 - cy * hh, w, h: hh, fw, fh };
    for (const img of [...layer.children, before]) rect(img, D.x, D.y, D.w, D.h);
    const s = vm.view.seam.peek();
    rect(seam, D.x + s * D.w - 0.75, D.y, 1.5, D.h);
  }
  bindEffect(box, () => { vm.view.zoom.value; vm.view.seam.value; place(); });

  // the newest preview: decode off-screen, then dissolve over the previous one (160 ms linear)
  bindEffect(box, () => {
    const f = vm.frame.value;
    if (!f || f.url === curUrl) return;
    curUrl = f.url;
    if (f.meta && f.meta.w > 0 && f.meta.h > 0) aspect = f.meta.w / f.meta.h;
    const img = new Image();
    img.className = "t-shot";
    img.src = f.url;
    img.decode().catch(() => {}).then(() => {
      if (curUrl !== f.url) return;
      if (img.naturalWidth) aspect = img.naturalWidth / img.naturalHeight;
      const prev = cur;
      cur = img;
      layer.append(img);
      place();
      if (prev) { img.animate([{ opacity: 0 }, { opacity: 1 }], { duration: 160, easing: "linear" }).onfinish = () => prev.remove(); }
      else img.animate([{ opacity: 0 }, { opacity: 1 }], { duration: 160, easing: "linear" });
    });
  });
  bindClass(box, "empty", () => !vm.imageCount.value);

  // Before / Split: the geometry-only render, asked for this client only (adapter `before`).
  let beforeUrl = "", beforeMissing = false, askTimer = 0;
  const off = vm.session.bridge.onBlob("before", (blob) => {
    const url = URL.createObjectURL(blob);
    const old = beforeUrl;
    beforeUrl = url;
    before.src = url;
    if (old) setTimeout(() => URL.revokeObjectURL(old), 4000);
  });
  own(box, () => { off(); clearTimeout(askTimer); });
  const geometry = computed(() => { const r = vm.ownParams.value.raw; return [vm.currentSlot.value, vm.editGroup.value, r.crop, r.rotation, r.quarterTurns].join("|"); });
  let askedFor = "";
  bindEffect(box, () => {
    const mode = vm.view.compare.value;
    const key = geometry.value;
    box.classList.toggle("cmp-before", mode === "before");
    box.classList.toggle("cmp-split", mode === "split");
    if (mode === "after") { askedFor = ""; return; }
    if (beforeMissing || key === askedFor) return;
    askedFor = key;
    clearTimeout(askTimer);
    askTimer = setTimeout(() => {
      vm.session.call("before").catch((e) => {
        if (/no method/i.test(String(e && e.message))) { beforeMissing = true; ctx.toast("Before needs a newer cosmo on the board"); }
      });
    }, 120);
  });
  bindEffect(box, () => { vm.view.seam.value; const s = vm.view.seam.value; before.style.clipPath = `inset(0 ${(1 - s) * 100}% 0 0)`; });

  // ---- pinch / pan / double-tap / seam (view state of this page)
  const zt = new Tween([1, 0.5, 0.5], (v) => { vm.view.zoom.value = { z: v[0], x: v[1], y: v[2] }; });
  const zoomTo = (z, x, y) => { const c = vm.view.zoom.peek(); zt.set([c.z, c.x, c.y]); zt.to([z, x, y], 220, Ease.EaseOutCubic); };
  // Reset to fit when the photo changes (brief §7.1). Effects here re-run on every model push, so
  // each one compares what it keys on.
  let slotSeen = vm.currentSlot.peek();
  bindEffect(box, () => {
    const s = vm.currentSlot.value;
    if (s === slotSeen) return;
    slotSeen = s;
    untracked(() => { if (vm.view.zoom.peek().z !== 1) zoomTo(1, 0.5, 0.5); });
  });
  const P = new Map();
  let pinch = null, pan = null, seamDrag = false, lastTap = null;
  const lp = (e) => { const r = box.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
  box.addEventListener("pointerdown", (e) => {
    if (e.pointerType === "mouse" && e.button !== 0) return;
    try { box.setPointerCapture(e.pointerId); } catch { /* gone */ }
    P.set(e.pointerId, lp(e));
    if (P.size === 2) {
      const [a, b] = [...P.values()];
      const z = vm.view.zoom.peek(), m = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
      pinch = { d0: Math.hypot(a.x - b.x, a.y - b.y) || 1, z0: z.z, u: (m.x - D.x) / D.w, v: (m.y - D.y) / D.h };
      pan = null; seamDrag = false;
    } else if (P.size === 1) {
      const p = lp(e);
      const z = vm.view.zoom.peek();
      seamDrag = vm.view.compare.peek() === "split" && Math.abs(p.x - (D.x + vm.view.seam.peek() * D.w)) < 24;
      pan = { p0: p, x0: z.x, y0: z.y, moved: false };
    }
  });
  box.addEventListener("pointermove", (e) => {
    if (!P.has(e.pointerId)) return;
    P.set(e.pointerId, lp(e));
    if (pinch && P.size >= 2) {
      const [a, b] = [...P.values()];
      const d = Math.hypot(a.x - b.x, a.y - b.y), m = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
      const z = clamp(pinch.z0 * d / pinch.d0, 1, 8);
      const w = D.fw * z, hh = D.fh * z;
      vm.view.zoom.value = { z, x: pinch.u - (m.x - bw / 2) / w, y: pinch.v - (m.y - bh / 2) / hh };
      return;
    }
    if (!pan) return;
    const p = lp(e), dx = p.x - pan.p0.x, dy = p.y - pan.p0.y;
    if (!pan.moved && Math.hypot(dx, dy) > 5) pan.moved = true;
    if (!pan.moved) return;
    if (seamDrag) { vm.view.seam.value = clamp((p.x - D.x) / (D.w || 1), 0, 1); return; }
    const z = vm.view.zoom.peek();
    if (z.z > 1) vm.view.zoom.value = { z: z.z, x: pan.x0 - dx / D.w, y: pan.y0 - dy / D.h };
  });
  const up = (e, cancelled) => {
    if (!P.has(e.pointerId)) return;
    P.delete(e.pointerId);
    if (pinch) { if (P.size < 2) { pinch = null; pan = null; } return; }
    if (pan && !pan.moved && !cancelled) {
      const now = performance.now(), p = pan.p0;
      if (lastTap && now - lastTap.t <= 300 && Math.hypot(p.x - lastTap.x, p.y - lastTap.y) <= 16) {
        lastTap = null;
        const z = vm.view.zoom.peek();
        if (z.z > 1.01) zoomTo(1, 0.5, 0.5);
        else {
          const f = vm.frame.peek();
          const full = f && f.meta && f.meta.w ? clamp(f.meta.w / (D.fw || 1), 1, 8) : 2;
          zoomTo(Math.max(full, 1.5), clamp((p.x - D.x) / D.w, 0, 1), clamp((p.y - D.y) / D.h, 0, 1));
        }
      } else lastTap = { t: now, x: p.x, y: p.y };
    }
    pan = null; seamDrag = false;
  };
  box.addEventListener("pointerup", (e) => up(e, false));
  box.addEventListener("pointercancel", (e) => up(e, true));

  return { layout(w, hh) { if (w === bw && hh === bh) return; bw = w; bh = hh; place(); } };
}

// ---------------------------------------------------------------- Before / Split / After
/** 190 x 34, fill rgba(0,0,0,.7) r 17 + 1 px BORDER; segments of 63.33, the selected one
 *  (i·63.33+2, 3, 59.33, 28) r 14 accent; Roboto Medium 12, baseline 21 (PhoneApp.cpp:245-269). */
function beforeAfterPill(vm) {
  const el = h("div.t-pill");
  const hl = h("i.t-pill-hl");
  el.append(hl);
  const modes = ["before", "split", "after"];
  const labels = ["Before", "Split", "After"].map((l, i) => {
    const t = tx(l, (i + 0.5) * (190 / 3), 21, 12, "medium", "center");
    el.append(t);
    return t;
  });
  bindEffect(el, () => {
    const i = Math.max(0, modes.indexOf(vm.view.compare.value));
    hl.style.transform = `translateX(${i * (190 / 3)}px)`;
    labels.forEach((t, k) => t.classList.toggle("on", k === i));
  });
  gestures(el, { tap: (e, p) => { vm.view.compare.value = modes[clamp(Math.floor(p.x / (190 / 3)), 0, 2)]; } });
  return el;
}

// ---------------------------------------------------------------- breadcrumb
/** (0, crumbY, W, 24) BG, hairlines top and bottom; crumbs from x 12, Roboto 11 baseline +16,
 *  the last FG, the others MUTED, ">" 10 px after each (PhoneApp.cpp:1003-1022). */
function breadcrumb(vm) {
  const el = h("div.t-crumb", {}, h("i.t-hl.top"), h("i.t-hl.bot"));
  const row = h("div.t-crumbs.hscroll");
  el.append(row);
  const items = computed(() => [{ node: ROOT, name: "All Photos" }, ...vm.breadcrumb.value.map((n) => ({ node: n.node, name: n.name || "Group" }))]);
  keyed(row, () => items.value, (it) => it.node, (it) => {
    const c = h("div.t-crumb-i");
    const t = h("span.t-crumb-t");
    baselineBox(t, 24, 16, 11, "sans");
    const sep = h("span.t-crumb-sep", { text: ">" });
    baselineBox(sep, 24, 16, 10, "sans");
    c.append(t, sep);
    c.__t = t;
    gestures(c, { tap: () => vm.openGroup(it.node) });
    return c;
  }, (c, it, i) => {
    c.__t.textContent = it.name;
    c.classList.toggle("last", i === items.peek().length - 1);
  });
  return el;
}

// ---------------------------------------------------------------- filmstrip
/** Cells 72x54 at x = 8 + 78 i, y 9: images INPUT r2 + thumbnail, groups a 1.5 px BORDER + the
 *  folder glyph + "<name> . <count>"; the selection a 2 px accent ring (PhoneApp.cpp:1024-1043). */
function filmstrip(vm) {
  const el = h("div.t-film");
  const strip = h("div.t-cells.hscroll");
  el.append(strip);
  keyed(strip, () => vm.cells.value, (n) => n.node, (n) => {
    const c = h("div.t-cell" + (n.kind === "group" ? ".grp" : ""));
    if (n.kind === "group") {
      const f = ticon("folderOpen", 18, 1.4);
      f.classList.add("t-cell-f");
      const lab = tx("", "50%", 47, 9, "sans", "center", "t-cell-l");
      c.append(f, lab);
      c.__lab = lab;
    } else {
      const img = h("img.t-thumb", { alt: "" });
      c.append(img);
      c.__img = img;
    }
    c.append(h("i.t-ring"));
    gestures(c, {
      tap: () => quiet(vm.select(n.node)),
      longPress: () => { pressWash(c); quiet(vm.select(n.node, "add")); },
      doubleTap: n.kind === "group" ? () => vm.openGroup(n.node) : null,
    });
    return c;
  }, (c, n) => {
    if (c.__lab) c.__lab.textContent = `${n.name || "Group"} . ${vm.childrenOf(n.node).length}`;
    if (c.__img) {
      const t = vm.thumbs.value.get(n.node);
      const url = t ? t.url : "";
      if (c.__img.dataset.src !== url) { c.__img.dataset.src = url; if (url) c.__img.src = url; c.__img.classList.toggle("on", !!url); }
    }
  });
  const isSel = (n) => {
    if (vm.selectedIds.value.has(n.node)) return true;
    if (n.kind === "group") return n.node === vm.editGroup.value;
    return n.slot === vm.currentSlot.value && vm.editGroup.value < 0;
  };
  let selKey = "";
  bindEffect(strip, () => {
    const cells = vm.cells.value;
    let first = -1, key = "";
    [...strip.children].forEach((c) => {
      const n = cells.find((x) => x.node === c.__key);
      const on = !!n && isSel(n);
      c.classList.toggle("sel", on);
      if (on) key += c.__key + ",";
      if (on && first < 0) first = c.offsetLeft;
    });
    if (key === selKey) return;                   // bring a NEW selection into view, only
    selKey = key;
    if (first >= 0) {
      const l = strip.scrollLeft, w = strip.clientWidth;
      if (first < l + 8 || first + 72 > l + w - 8) strip.scrollTo({ left: Math.max(0, first - 8), behavior: "smooth" });
    }
  });
  return el;
}
