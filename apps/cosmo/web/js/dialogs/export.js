/*
 * Cosmo by arstro — export.js: ExportDialog (widgets/ExportDialog.{h,cpp}), the in-app Export modal.
 *
 * vm.view.dialog = {name: "export"} (File ▸ Export…, only with images). A port of the native
 * onOverlay: the whole modal is drawn, like the native draws it, into one canvas over the window
 * - three faces cross-fading inside one card whose height tweens (form -> progress -> complete,
 * R-EXPORT-6), the tree box interpolating to the manifest, rows lighting green as files land, the
 * card rising 8 px as it fades in over 160 ms, draggable by its header in every state. Geometry,
 * type and colours are ExportDialog.cpp's (card 560, pad 20, header 40, footer 46, tree rows 22 x
 * 8, label column 132, chips, toggles, the 60..100 quality slider, the "more below" fades).
 *
 * What is sent (spec §13.6): `export --outdir <dir> --format jpg|png|tiff --quality <q>
 * [--long-edge 2048|1080|720]` through vm.exportTo, once the collapse has settled; the progress
 * face follows `export.progress` (a = done, b = total, text = the file just written) and
 * `export.finished`; a rejected command runs the native cancelExport (back to the form, 200 ms).
 *
 * What the core cannot do yet is shown disabled, with the reason on hover and in the section
 * header (x 0.4, kDisabledFade-like, no hover):
 *   - the image tree: `export` writes every decoded photo [CORE GAP G4] - all ticked, inert;
 *   - Same as source, filename prefix, subfolder, and the three Metadata toggles [CORE GAP G6].
 * The destination therefore starts as an explicit folder: the last export's (model exportOutDir),
 * else the project's folder (the native's default - the first image's folder - is not in the
 * model). Everything else - format, size, quality, destination, the whole progress face - is live.
 */
import { openModal } from "../ui/modal.js";
import { Tween, Ease, reducedMotion } from "../core/motion.js";
import { tokens, fade, withAlpha, lerpColor, css, est, canvasFont, surface, fitEnd, fitFront } from "./paint.js";
import { pick } from "./picker.js";

const CAN_SUBSET = false;          // G4: no `export --nodes`
const CAN_G6 = false;              // G6: no --same-as-source / --prefix / --subfolder / --exif / --strip-gps / --icc
const WHY_G4 = "The core exports every photo of the project (no per-photo selection yet)";
const WHY_G6 = "Not in the core's export command yet";

// ExportDialog.cpp:24-58
const CARD_W = 560, PAD = 20, HEADER_H = 40, FOOTER_H = 46, MARGIN = 24, COMPLETE_H = 92, HOLD_MS = 950, BAR_H = 4;
const SEC_H = 24, BTN_H = 26, FIELD_H = 26, CHECK = 15, FONT = 11, MONO = 10, SMALL = 10, LABEL_COL = 132;
const GAP_S = 6, GAP_M = 8, GAP_L = 14, ROW_H = 22, TREE_ROWS = 8, TREE_H = TREE_ROWS * ROW_H + 2, INDENT = 14, CHEV_W = 12;
const DEST_H = 30, META_ROW = 26, TOGGLE_W = 30, TOGGLE_H = 16;
const FORMATS = ["JPEG", "PNG", "TIFF"], FORMAT_ARG = ["jpg", "png", "tiff"], EXT = [".jpg", ".png", ".tif"];
const SIZES = ["Original", "2048 px", "1080 px", "720 px"], SIZE_EDGES = [0, 2048, 1080, 720];
const META_LABELS = ["Embed EXIF data", "Strip GPS coordinates", "Embed colour profile (sRGB)"];
const ID = { close: 0, master: 1, change: 2, same: 3, prefixCheck: 4, prefixField: 5, subCheck: 6, subField: 7,
             fmt0: 8, size0: 11, meta0: 15, quality: 18, cancel: 19, export: 20, tree: 64 };
const clamp01 = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);
const inR = (r, p) => r && p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;

// widgets/Icons.cpp glyphs used here, 0..100 boxes (spec §15.2)
const GLYPH = {
  download: ["M50 20 L50 68 M28 46 L50 68 L72 46", "M18 88 L82 88"],
  close: ["M22 22 L78 78 M78 22 L22 78"],
  folder: ["M8 82 L8 20 L42 20 L52 34 L92 34 L92 82 Z"],
  image: ["M12 16 L88 16 L88 84 L12 84 Z", "M42 36 Q42 28 34 28 Q26 28 26 36 Q26 44 34 44 Q42 44 42 36", "M14 78 L42 50 L62 70 L74 58 L87 72"],
  chevronRight: ["M32 18 L68 50 L32 82"],
  chevronDown: ["M18 36 L50 68 L82 36"],
  check: ["M18 52 L42 76 L84 26"],
  checkCircle: ["M92 50 Q92 92 50 92 Q8 92 8 50 Q8 8 50 8 Q92 8 92 50 Z", "M30.68 51.68 L45.8 67.64 L71 34.04"],
};
const PATHS = {};
for (const [k, ds] of Object.entries(GLYPH)) PATHS[k] = ds.map((d) => new Path2D(d));

/** HoverFade (widgets/HoverFade.h): a linear 120 ms ramp per region, read through smoothstep. */
class HoverFade {
  constructor() { this.raw = new Map(); this.id = null; this.t = 0; }
  set(id) { this.id = id; if (id !== null && id !== undefined && !this.raw.has(id)) this.raw.set(id, 0); }
  clear() { this.id = null; }
  advance(now) {
    const dt = this.t ? now - this.t : 0;
    this.t = now;
    const step = reducedMotion() ? 1 : dt / 120;
    for (const [k, v] of this.raw) {
      const tgt = k === this.id ? 1 : 0;
      const nv = v < tgt ? Math.min(tgt, v + step) : Math.max(tgt, v - step);
      if (nv === 0 && k !== this.id) this.raw.delete(k); else this.raw.set(k, nv);
    }
  }
  amount(id) { const t = this.raw.get(id) || 0; return t * t * (3 - 2 * t); }
}

export function open(host, spec) {
  const vm = host.vm;
  if (!vm.imageCount.peek()) return null;
  const tk = tokens();
  const layer = host.layer;

  // ---------------------------------------------------------------- the tree snapshot (ExportDialog::show)
  const src = vm.nodes.peek().filter((n) => n.kind === "group" || n.kind === "image");
  const byId = new Map(src.map((n, i) => [n.node, i]));
  const nodes = src.map((n) => ({ id: n.node, group: n.kind === "group", slot: n.kind === "image" ? n.slot : -1, name: n.name || "",
                                  parent: byId.has(n.parent) ? byId.get(n.parent) : -1, selected: !!n.selected }));
  const kids = nodes.map(() => []), roots = [];
  nodes.forEach((n, i) => (n.parent >= 0 ? kids[n.parent].push(i) : roots.push(i)));
  const expanded = nodes.map(() => true);
  const isLeaf = (i) => !nodes[i].group && nodes[i].slot >= 0;
  const pre = nodes.filter((n, i) => isLeaf(i) && n.selected).length;
  // CAN_SUBSET: the selected images, else every image; the core exports every image anyway.
  const leafOn = nodes.map((n, i) => isLeaf(i) && (!CAN_SUBSET || !pre || n.selected));
  let rows = [];
  const rebuildRows = () => {
    rows = [];
    const stack = roots.slice().reverse().map((n) => ({ node: n, depth: 0 }));
    while (stack.length) {
      const f = stack.pop();
      rows.push(f);
      if (nodes[f.node].group && expanded[f.node]) for (let k = kids[f.node].length - 1; k >= 0; k--) stack.push({ node: kids[f.node][k], depth: f.depth + 1 });
    }
  };
  rebuildRows();
  const leaves = (i, out = []) => { if (!nodes[i].group) { if (nodes[i].slot >= 0) out.push(i); return out; } for (const k of kids[i]) leaves(k, out); return out; };
  const checkState = (i) => { const l = leaves(i); if (!l.length) return 0; const on = l.filter((x) => leafOn[x]).length; return on === 0 ? 0 : on === l.length ? 1 : 2; };
  const rowState = (r) => (nodes[r.node].group ? checkState(r.node) : leafOn[r.node] ? 1 : 0);
  const leafCount = () => nodes.filter((n, i) => isLeaf(i)).length;
  const checkedCount = () => nodes.filter((n, i) => isLeaf(i) && leafOn[i]).length;

  // ---------------------------------------------------------------- form state (ExportDialog.h defaults)
  const st = {
    format: 0, size: 0, quality: 92, meta: [true, false, true], same: false,
    dest: (vm.exporting.peek().outDir || dirOf(vm.project.peek().path) || ""),
    usePrefix: false, prefix: "export_", useSub: false, sub: "exported", focus: -1,
    exporting: false, complete: false, done: 0, total: 0, name: "", firePending: false, pending: null,
    exportRows: [], exportSeq: [], rowFade: [], rowFadeT: -1, completeAt: 0, closing: false,
    drag: null, dragQ: false, offX: 0, offY: 0,
  };
  const appear = new Tween(0), phase = new Tween(0), progress = new Tween(0), completeAmt = new Tween(0);
  const treeScroll = new Tween(0), bodyScroll = new Tween(0);
  let treeTarget = 0, bodyTarget = 0;
  const knob = st.meta.map((m) => new Tween(m ? 1 : 0));
  const hover = new HoverFade();

  const md = openModal(layer, { width: CARD_W, height: 10, scrim: 0, openMs: 0, closeMs: 0, cls: "dg-export",
                                onOutside: () => {}, onKey });
  md.card.style.display = "none";                     // the card is drawn, not laid out
  const s = surface(md.el, "dg-export-cv");
  const g = s.g;
  let W = 0, H = 0;
  appear.to(1, 160, Ease.EaseOutCubic);

  // ---------------------------------------------------------------- geometry (layoutForm & co.)
  const cardX = () => {
    const x = (W - CARD_W) * 0.5 + st.offX;
    const lo = Math.min(4, W - 120), hi = Math.max(lo, W - 120);
    return Math.min(hi, Math.max(lo - (CARD_W - 120), x));
  };
  function layoutForm(top) {
    const L = {};
    const ix = cardX() + PAD, iw = CARD_W - 2 * PAD;
    let y = top;
    y += SEC_H;
    L.master = { x: ix, y, w: 118, h: BTN_H };
    y += BTN_H + GAP_M;
    L.tree = { x: ix, y, w: iw, h: TREE_H };
    y += TREE_H + 7;
    L.selInfo = { x: ix, y, w: iw, h: 14 };
    y += 14 + GAP_L;
    y += SEC_H;
    const sameW = CHECK + GAP_M + est("Same as source", FONT);
    L.dest = { x: ix, y, w: iw - sameW - 12, h: DEST_H };
    L.change = { x: L.dest.x + L.dest.w - 8 - 56, y: y + (DEST_H - 18) * 0.5, w: 56, h: 18 };
    L.same = { x: ix + iw - sameW, y: y + (DEST_H - CHECK) * 0.5 - 2, w: sameW, h: CHECK + 4 };
    y += DEST_H + GAP_S;
    L.preview = { x: ix, y, w: iw, h: 14 };
    y += 14 + 10;
    const fx = ix + CHECK + 10 + LABEL_COL + 10, fw = ix + iw - fx;
    L.prefixCheck = { x: ix, y: y + (FIELD_H - CHECK) * 0.5, w: CHECK, h: CHECK };
    L.prefixField = { x: fx, y, w: fw, h: FIELD_H };
    y += FIELD_H + GAP_S;
    L.subCheck = { x: ix, y: y + (FIELD_H - CHECK) * 0.5, w: CHECK, h: CHECK };
    L.subField = { x: fx, y, w: fw, h: FIELD_H };
    y += FIELD_H + GAP_L;
    y += SEC_H;
    const cw = (iw - 2 * GAP_S) / 3;
    L.fmt = [0, 1, 2].map((i) => ({ x: ix + i * (cw + GAP_S), y, w: cw, h: BTN_H }));
    y += BTN_H + GAP_S;
    const sw = (iw - 3 * GAP_S) / 4;
    L.size = [0, 1, 2, 3].map((i) => ({ x: ix + i * (sw + GAP_S), y, w: sw, h: BTN_H }));
    y += BTN_H + 10;
    if (st.format === 0) {
      L.qLabel = { x: ix, y, w: iw, h: 15 };
      y += 15 + GAP_S;
      L.qSlider = { x: ix, y, w: iw, h: 18 };
      y += 18 + GAP_L;
    }
    y += SEC_H;
    L.meta = [0, 1, 2].map((i) => ({ x: ix + iw - TOGGLE_W, y: y + i * META_ROW + (META_ROW - TOGGLE_H) * 0.5, w: TOGGLE_W, h: TOGGLE_H }));
    y += 3 * META_ROW + 4;
    L.contentH = y - top;
    return L;
  }
  const formContentH = () => layoutForm(0).contentH;
  const progressTreeH = () => Math.min(TREE_ROWS, Math.max(1, st.exportRows.length)) * ROW_H + 2;
  function cardRect() {
    const avail = Math.max(120, H - 2 * MARGIN - HEADER_H - FOOTER_H);
    const progressBody = SEC_H + progressTreeH() + 12 + BAR_H + 8 + 14 + 8;
    const p = phase.value, d = completeAmt.value;
    const formH = (1 - p) * Math.min(formContentH(), avail) + p * Math.min(progressBody, avail);
    const bodyH = (1 - d) * formH + d * Math.min(COMPLETE_H, avail);
    const hh = HEADER_H + bodyH + FOOTER_H;
    const y = (H - hh) * 0.5 + st.offY + (1 - appear.value) * 8;
    return { x: cardX(), y: Math.min(Math.max(4, H - HEADER_H - 4), Math.max(4, y)), w: CARD_W, h: hh };
  }
  const bodyRect = () => { const c = cardRect(); return { x: c.x, y: c.y + HEADER_H, w: c.w, h: Math.max(0, c.h - HEADER_H - FOOTER_H) }; };
  const bodyTop = () => bodyRect().y - bodyScroll.value;
  const treeRect = () => layoutForm(bodyTop()).tree;
  const headerRect = () => { const c = cardRect(); return { x: c.x, y: c.y, w: c.w, h: HEADER_H }; };
  const closeRect = () => { const c = cardRect(); return { x: c.x + c.w - PAD - 18, y: c.y + (HEADER_H - 18) * 0.5, w: 18, h: 18 }; };
  const exportRect = () => {
    const c = cardRect(), w = Math.max(120, est("Export 00 photos", FONT) + 44);
    return { x: c.x + c.w - PAD - w, y: c.y + c.h - FOOTER_H + (FOOTER_H - BTN_H) * 0.5, w, h: BTN_H };
  };
  const cancelRect = () => { const e = exportRect(); return { x: e.x - GAP_M - 72, y: e.y, w: 72, h: BTN_H }; };
  function progressTreeRect() {
    const p = phase.value, b = bodyRect();
    return { x: cardX() + PAD, y: b.y + SEC_H + (BTN_H + GAP_M) * (1 - p), w: CARD_W - 2 * PAD, h: TREE_H + (progressTreeH() - TREE_H) * p };
  }
  const barRect = () => { const t = progressTreeRect(); return { x: t.x, y: t.y + t.h + 12, w: t.w, h: BAR_H }; };
  function rowAt(p) {
    const box = treeRect();
    if (!inR(box, p)) return { row: -1 };
    const i = Math.floor((p.y - (box.y + 1) + treeScroll.value) / ROW_H);
    if (i < 0 || i >= rows.length) return { row: -1 };
    const cx = box.x + 6 + rows[i].depth * INDENT;
    return { row: i, chev: nodes[rows[i].node].group && p.x >= cx && p.x <= cx + CHEV_W };
  }
  function hitId(p) {
    if (st.exporting) return null;
    if (inR(closeRect(), p)) return ID.close;
    if (inR(exportRect(), p)) return ID.export;
    if (inR(cancelRect(), p)) return ID.cancel;
    if (!inR(bodyRect(), p)) return null;
    const r = rowAt(p);
    if (r.row >= 0) return ID.tree + r.row;
    const L = layoutForm(bodyTop());
    if (inR(L.master, p)) return ID.master;
    if (!st.same && inR(L.change, p)) return ID.change;
    if (inR(L.same, p)) return ID.same;
    if (inR(L.prefixCheck, p)) return ID.prefixCheck;
    if (st.usePrefix && inR(L.prefixField, p)) return ID.prefixField;
    if (inR(L.subCheck, p)) return ID.subCheck;
    if (st.useSub && inR(L.subField, p)) return ID.subField;
    for (let i = 0; i < 3; i++) if (inR(L.fmt[i], p)) return ID.fmt0 + i;
    for (let i = 0; i < 4; i++) if (inR(L.size[i], p)) return ID.size0 + i;
    if (st.format === 0 && inR(L.qSlider, p)) return ID.quality;
    for (let i = 0; i < 3; i++) if (inR(L.meta[i], p)) return ID.meta0 + i;
    return null;
  }
  const disabledId = (id) => id !== null && ((!CAN_SUBSET && (id === ID.master || id >= ID.tree)) ||
    (!CAN_G6 && (id === ID.same || id === ID.prefixCheck || id === ID.prefixField || id === ID.subCheck || id === ID.subField ||
                 (id >= ID.meta0 && id < ID.meta0 + 3))) || (id === ID.export && !canExport()));
  const canExport = () => checkedCount() > 0 && (st.same || !!st.dest);

  // ---------------------------------------------------------------- export (beats 1-3)
  function buildExportRows() {
    st.exportRows = []; st.exportSeq = [];
    let seq = 0;
    const walk = (n, depth) => {
      if (nodes[n].group) {
        if (!leaves(n).some((l) => leafOn[l])) return;
        st.exportRows.push({ node: n, depth }); st.exportSeq.push(-1);
        for (const k of kids[n]) walk(k, depth + 1);
        return;
      }
      if (nodes[n].slot < 0 || !leafOn[n]) return;
      st.exportRows.push({ node: n, depth }); st.exportSeq.push(seq++);
    };
    for (const r of roots) walk(r, 0);
    st.rowFade = st.exportRows.map(() => 0); st.rowFadeT = -1;
  }
  function beginExport() {
    st.pending = { outDir: st.dest, format: FORMAT_ARG[st.format], quality: st.quality, longEdge: SIZE_EDGES[st.size] };
    st.firePending = true; st.exporting = true; st.complete = false; st.done = 0; st.total = checkedCount(); st.name = "";
    progress.set(0); completeAmt.set(0); st.focus = -1; st.dragQ = false; hover.clear();
    buildExportRows();
    treeScroll.set(0); treeTarget = 0;
    phase.to(1, 260, Ease.EaseInOutCubic);
  }
  function setProgress(done, total, name) {
    st.total = Math.max(0, total);
    st.done = Math.max(0, Math.min(done, st.total));
    if (name) st.name = name;
    progress.to(st.total > 0 ? st.done / st.total : 0, 180, Ease.EaseOutCubic);
    if (st.total > 0 && st.done >= st.total) beginComplete();
  }
  function beginComplete() {
    if (st.complete) return;
    st.complete = true; st.completeAt = performance.now();
    completeAmt.to(1, 260, Ease.EaseInOutCubic);
  }
  function cancelExport() {
    if (!st.exporting) return;
    st.exporting = false; st.complete = false; st.firePending = false;
    completeAmt.set(0);
    phase.to(0, 200, Ease.EaseInOutCubic);
  }
  const offs = [];
  const onEvent = (name, fn) => vm.on(name, fn);
  async function fire() {
    const req = st.pending;
    offs.push(onEvent("export.progress", (d) => { if (st.exporting) setProgress(d.a || 0, d.b || st.total, d.text || ""); }));
    offs.push(onEvent("export.finished", (d) => {
      if (!st.exporting) return;
      if (d.b > 0 && host.ctx.toast) host.ctx.toast(`${d.b} photo${d.b === 1 ? "" : "s"} could not be written`, true);
      setProgress(st.total, st.total, st.name);
    }));
    try {
      await vm.exportTo(req);
      if (st.exporting) setProgress(st.total, st.total, st.name);
    } catch { cancelExport(); }
  }

  // ---------------------------------------------------------------- close
  function beginClose() {
    if (st.closing) return;
    st.closing = true; st.focus = -1;
    md.el.style.pointerEvents = "none";
    appear.to(0, 120, Ease.EaseOutCubic);
    host.closed(spec);
  }
  const api = { close: () => beginClose(), get closing() { return st.closing; } };

  // ---------------------------------------------------------------- input
  const local = (e) => {
    const r = s.cv.getBoundingClientRect();
    const k = s.cv.clientWidth ? r.width / s.cv.clientWidth : 1;
    return { x: (e.clientX - r.left) / k, y: (e.clientY - r.top) / k };
  };
  let down = null;
  s.cv.addEventListener("pointerdown", (e) => {
    if (st.closing || e.button !== 0) return;
    const p = local(e);
    s.cv.setPointerCapture(e.pointerId);
    down = { p, x: e.clientX, y: e.clientY, moved: false };
    if (inR(headerRect(), p) && !inR(closeRect(), p)) { st.drag = { x: p.x, y: p.y }; return; }
    if (!st.exporting && st.format === 0) {
      const q = layoutForm(bodyTop()).qSlider;
      if (inR(q, p) && inR(bodyRect(), p)) { st.dragQ = true; applyQuality(p.x); }
    }
  });
  s.cv.addEventListener("pointermove", (e) => {
    const p = local(e);
    if (down && Math.hypot(e.clientX - down.x, e.clientY - down.y) > 5) down.moved = true;
    if (st.drag) { st.offX += p.x - st.drag.x; st.offY += p.y - st.drag.y; st.drag = { x: p.x, y: p.y }; return; }
    if (st.dragQ) { applyQuality(p.x); return; }
    const id = hitId(p);
    hover.set(disabledId(id) && id !== ID.export ? null : id);
    s.cv.title = disabledId(id) ? (id === ID.master || id >= ID.tree ? WHY_G4 : id === ID.export ? "" : WHY_G6) : "";
  });
  s.cv.addEventListener("pointerleave", () => hover.clear());
  const up = (e) => {
    const d = down; down = null;
    if (st.drag) { st.drag = null; if (d && d.moved) return; }
    if (st.dragQ) { st.dragQ = false; return; }
    if (!d || d.moved || st.closing) return;
    click(local(e));
  };
  s.cv.addEventListener("pointerup", up);
  s.cv.addEventListener("pointercancel", () => { down = null; st.drag = null; st.dragQ = false; });
  function applyQuality(x) {
    const q = layoutForm(bodyTop()).qSlider;
    if (!q) return;
    st.quality = 60 + Math.round(40 * clamp01((x - q.x) / q.w));
  }
  function click(p) {
    if (st.exporting) return;                               // modal and non-cancellable while writing
    if (!inR(cardRect(), p)) { beginClose(); return; }
    if (inR(closeRect(), p) || inR(cancelRect(), p)) { beginClose(); return; }
    if (inR(exportRect(), p)) { if (canExport()) beginExport(); return; }
    if (!inR(bodyRect(), p)) return;
    const r = rowAt(p);
    if (r.row >= 0) {
      if (r.chev) { const n = rows[r.row].node; expanded[n] = !expanded[n]; rebuildRows(); }
      else if (CAN_SUBSET) {
        const n = rows[r.row].node;
        if (nodes[n].group) { const on = checkState(n) !== 1; for (const l of leaves(n)) leafOn[l] = on; }
        else if (nodes[n].slot >= 0) leafOn[n] = !leafOn[n];
      }
      return;
    }
    const L = layoutForm(bodyTop());
    if (inR(L.master, p)) { if (CAN_SUBSET) { const all = checkedCount() === leafCount() && leafCount() > 0; nodes.forEach((n, i) => { if (isLeaf(i)) leafOn[i] = !all; }); } return; }
    if (inR(L.same, p)) { if (CAN_G6) st.same = !st.same; return; }
    if (!st.same && inR(L.change, p)) {
      pick(vm, { mode: "folder", title: "Export to folder", ok: "Select" }).then((dir) => { if (dir) { st.dest = dir; st.same = false; } });
      return;
    }
    if (CAN_G6) {
      if (inR(L.prefixCheck, p)) { st.usePrefix = !st.usePrefix; if (!st.usePrefix && st.focus === 0) st.focus = -1; return; }
      if (inR(L.subCheck, p)) { st.useSub = !st.useSub; if (!st.useSub && st.focus === 1) st.focus = -1; return; }
      if (st.usePrefix && inR(L.prefixField, p)) { st.focus = 0; return; }
      if (st.useSub && inR(L.subField, p)) { st.focus = 1; return; }
    }
    for (let i = 0; i < 3; i++) if (inR(L.fmt[i], p)) { st.format = i; return; }
    for (let i = 0; i < 4; i++) if (inR(L.size[i], p)) { st.size = i; return; }
    for (let i = 0; i < 3; i++) if (inR(L.meta[i], p)) {
      if (!CAN_G6) return;
      st.meta[i] = !st.meta[i]; knob[i].to(st.meta[i] ? 1 : 0, 150, Ease.EaseOutCubic); return;
    }
    st.focus = -1;
  }
  s.cv.addEventListener("wheel", (e) => {
    e.preventDefault();
    const notch = -e.deltaY / (e.deltaMode === 1 ? 3 : 100);
    const p = local(e);
    if (st.exporting) {
      if (st.complete) return;
      const box = progressTreeRect();
      if (!inR(box, p)) return;
      treeTarget = Math.min(Math.max(0, st.exportRows.length * ROW_H - (box.h - 2)), Math.max(0, treeTarget - notch * 3 * ROW_H));
      treeScroll.to(treeTarget, 160, Ease.EaseOutCubic);
      return;
    }
    const tree = treeRect();
    if (inR(tree, p)) {
      treeTarget = Math.min(Math.max(0, rows.length * ROW_H - (tree.h - 2)), Math.max(0, treeTarget - notch * 3 * ROW_H));
      treeScroll.to(treeTarget, 160, Ease.EaseOutCubic);
      return;
    }
    bodyTarget = Math.min(Math.max(0, formContentH() - bodyRect().h), Math.max(0, bodyTarget - notch * 48));
    bodyScroll.to(bodyTarget, 160, Ease.EaseOutCubic);
  }, { passive: false });
  function onKey(e) {
    if (e.key === "Escape") {
      if (st.exporting) return true;
      if (st.focus >= 0) { st.focus = -1; return true; }
      beginClose(); return true;
    }
    if (st.exporting || st.focus < 0) return true;           // modal: swallowed
    const k = st.focus === 0 ? "prefix" : "sub";
    if (e.key === "Backspace") st[k] = st[k].slice(0, -1);
    else if (e.key === "Enter") st.focus = -1;
    else if (e.key.length === 1 && !e.ctrlKey && !e.metaKey) st[k] += e.key;
    return true;
  }

  // ---------------------------------------------------------------- paint primitives
  const col = (c, a) => css(fade(c, a));
  const white = (al) => withAlpha(tk.white, al);
  function rr(r, rad) { g.beginPath(); g.roundRect(r.x, r.y, r.w, r.h, Math.min(rad, r.w / 2, r.h / 2)); }
  function fillRR(r, rad, c) { if (r.w <= 0 || r.h <= 0) return; rr(r, rad); g.fillStyle = c; g.fill(); }
  function paintRR(r, rad, fill, stroke, lw = 1) { if (r.w <= 0 || r.h <= 0) return; rr(r, rad); if (fill) { g.fillStyle = fill; g.fill(); } if (stroke) { g.lineWidth = lw; g.strokeStyle = stroke; g.stroke(); } }
  function line(x0, y0, x1, y1, c, lw = 1) { g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.lineWidth = lw; g.strokeStyle = c; g.stroke(); }
  function text(s0, x, y, px, fam, c) { g.font = canvasFont(px, fam); g.fillStyle = c; g.fillText(s0, x, y); }
  function glyph(name, r, c, sw) {
    g.save(); g.translate(r.x, r.y); g.scale(r.w / 100, r.h / 100);
    g.lineWidth = sw * 100 / r.w; g.strokeStyle = c; g.lineCap = "butt"; g.lineJoin = "miter";
    for (const p of PATHS[name]) g.stroke(p);
    g.restore();
  }
  function sectionHeader(x, y, w, label, a, note) {
    text(label, x, y + 13, SMALL, "medium", col(tk["muted-foreground"], a));
    let lx = x + est(label, SMALL) + 8;
    if (note) { text(note, lx - 4, y + 13, SMALL, "sans", col(tk["muted-foreground"], 0.6 * a)); lx += est(note, SMALL) + 4; }
    line(lx, y + 9, x + w, y + 9, col(tk.border, a));
  }
  function check(box, state, a, hv = 0, dim = false) {
    const filled = state !== 0;
    let border = tk.border;
    if (hv > 0.001 && !filled) border = lerpColor(border, withAlpha(tk.primary, 0.6), hv);
    const accent = dim ? tk.secondary : tk.primary;
    paintRR(box, 3, filled ? col(accent, a) : col(tk.secondary, a), filled ? null : col(border, a));
    g.lineWidth = 1.8; g.strokeStyle = col(tk["primary-foreground"], a);
    if (state === 1) { g.beginPath(); g.moveTo(box.x + 3.5, box.y + box.h * 0.55); g.lineTo(box.x + box.w * 0.42, box.y + box.h - 4); g.lineTo(box.x + box.w - 3, box.y + 4); g.stroke(); }
    else if (state === 2) line(box.x + 3.5, box.y + box.h * 0.5, box.x + box.w - 3.5, box.y + box.h * 0.5, col(tk["primary-foreground"], a), 1.8);
  }
  function chip(r, label, sel, a, hv, px) {
    let border = sel ? tk.primary : tk.border;
    if (!sel && hv > 0.001) border = lerpColor(border, withAlpha(tk.primary, 0.4), hv);
    paintRR(r, 2, sel ? col(withAlpha(tk.primary, 0.1), a) : null, col(border, a));
    if (!sel && hv > 0.001) fillRR(r, 2, css(withAlpha(tk.white, 0.07 * hv * a)));
    const c = sel ? tk.primary : lerpColor(tk["muted-foreground"], tk.foreground, hv);
    const f = fitEnd(label, r.w - 10, px);
    text(f, r.x + (r.w - est(f, px)) * 0.5, r.y + r.h * 0.5 + px * 0.36, px, "medium", col(c, a));
  }
  function field(r, value, placeholder, active, focused, a) {
    const border = focused ? withAlpha(tk.primary, 0.6) : active ? tk.border : fade(tk.border, 0.5);
    paintRR(r, 2, col(tk.background, a), col(border, a));
    const empty = !value, shown = empty ? placeholder : value;
    const c = !active ? white(0.18) : empty ? white(0.25) : tk.foreground;
    const inner = r.w - 16;
    const f = fitEnd(shown, focused ? inner - 6 : inner, FONT);
    text(f, r.x + 8, r.y + r.h * 0.5 + 4, FONT, "mono", col(c, a));
    if (focused) { const cx = r.x + 8 + est(f, FONT) + 1.5; line(cx, r.y + 6, cx, r.y + r.h - 6, col(tk.primary, a), 1.2); }
  }
  function tree(a, box, list, prog) {
    paintRR(box, 2, col(tk.background, a), col(tk.border, a));
    if (!list.length) {
      const msg = "No images in this project";
      text(msg, box.x + (box.w - est(msg, FONT)) * 0.5, box.y + box.h * 0.5 + 4, FONT, "sans", col(white(0.22), a));
      return;
    }
    g.save();
    g.beginPath(); g.rect(box.x + 1, box.y + 1, box.w - 2, box.h - 2); g.clip();
    const scroll = treeScroll.value;
    const rowA = prog > 0.004 ? 1 : (CAN_SUBSET ? 1 : 0.4);            // G4: the picker is inert
    for (let i = 0; i < list.length; i++) {
      const ry = box.y + 1 + i * ROW_H - scroll;
      if (ry + ROW_H < box.y || ry > box.y + box.h) continue;
      const row = list[i], n = nodes[row.node];
      const state = n.group ? checkState(row.node) : leafOn[row.node] ? 1 : 0;
      const hv = prog < 0.5 ? hover.amount(ID.tree + i) * a * (1 - prog) : 0;
      if (hv > 0.001) fillRR({ x: box.x + 1, y: ry, w: box.w - 2, h: ROW_H }, 0, css(withAlpha(tk.white, 0.07 * hv)));
      let doneAmt = 0;
      if (prog > 0.004) {
        const t0 = st.rowFade[i] || 0;
        doneAmt = t0 * t0 * (3 - 2 * t0);
        const active = st.exportSeq[i] >= 0 && st.exportSeq[i] === st.done && !st.complete;
        const rowR = { x: box.x + 1, y: ry, w: box.w - 2, h: ROW_H };
        if (doneAmt > 0.004) fillRR(rowR, 0, css(withAlpha(tk.success, 0.13 * doneAmt * a)));
        else if (active) fillRR(rowR, 0, css(withAlpha(tk.primary, 0.14 * a * prog)));
      }
      const ra = a * (prog > 0.004 ? 1 : rowA);
      let x = box.x + 6 + row.depth * INDENT;
      if (n.group) {
        const open = prog > 0.004 ? true : expanded[row.node];
        glyph(open ? "chevronDown" : "chevronRight", { x, y: ry + (ROW_H - 10) * 0.5, w: 10, h: 10 }, col(tk["muted-foreground"], a), 1.3);
      }
      x += CHEV_W + 2;
      if (prog < 0.996) check({ x, y: ry + (ROW_H - 13) * 0.5, w: 13, h: 13 }, state, ra * (1 - prog));
      x += 20 * (1 - prog);
      const ic = col(white(state ? 0.42 : 0.22), ra);
      glyph(n.group ? "folder" : "image", { x, y: ry + (ROW_H - 11) * 0.5, w: 11, h: 11 }, ic, 1.1);
      x += 18;
      const suffix = n.group ? String(leaves(row.node).length) : "";
      const suffixW = suffix ? est(suffix, SMALL) + 10 : 0;
      const nameW = box.x + box.w - 14 - suffixW - x;
      const nc = lerpColor(state ? tk.foreground : tk["muted-foreground"], tk.success, doneAmt * 0.55);
      text(fitEnd(n.name, nameW, FONT), x, ry + ROW_H * 0.5 + 4, FONT, n.group ? "medium" : "sans", col(nc, ra));
      if (suffix) text(suffix, box.x + box.w - 14 - est(suffix, SMALL), ry + ROW_H * 0.5 + 3.5, SMALL, "mono", col(white(0.25), ra));
      if (doneAmt > 0.004) glyph("check", { x: box.x + box.w - 14 - 9, y: ry + (ROW_H - 9) * 0.5, w: 9, h: 9 }, css(withAlpha(tk.success, 0.95 * doneAmt * a)), 1.5);
    }
    g.restore();
    const contentH = list.length * ROW_H;
    if (contentH > box.h - 2) {
      const th = Math.max(24, (box.h - 2) * (box.h - 2) / contentH), maxS = contentH - (box.h - 2);
      fillRR({ x: box.x + box.w - 4, y: box.y + (box.h - th) * clamp01(scroll / maxS), w: 3, h: th }, 999, col(white(0.18), a));
    }
  }

  // ---------------------------------------------------------------- faces
  function drawForm(a) {
    const L = layoutForm(bodyTop());
    const ix = cardX() + PAD, iw = CARD_W - 2 * PAD;
    const total = leafCount(), sel = checkedCount(), allOn = total > 0 && sel === total;
    const btnA = a * (1 - clamp01(phase.value * 2.5));
    if (btnA > 0.004) chip(L.master, allOn ? "Select none" : `Select all (${total})`, allOn, btnA * (CAN_SUBSET ? 1 : 0.4), CAN_SUBSET ? hover.amount(ID.master) : 0, FONT);
    if (!st.exporting) {
      sectionHeader(ix, L.master.y - SEC_H, iw, "IMAGES TO EXPORT", a, CAN_SUBSET ? "" : "·  every photo");
      tree(a, L.tree, rows, 0);
    }
    const cnt = String(sel);
    text(cnt, L.selInfo.x, L.selInfo.y + 11, SMALL, "monoMedium", col(white(0.7), a));
    text(` of ${total} photos selected`, L.selInfo.x + est(cnt, SMALL), L.selInfo.y + 11, SMALL, "sans", col(tk["muted-foreground"], a));
    sectionHeader(ix, L.dest.y - SEC_H, iw, "DESTINATION", a);
    const same = st.same;
    paintRR(L.dest, 2, col(tk.background, a), col(same ? fade(tk.border, 0.5) : tk.border, a));
    glyph("folder", { x: L.dest.x + 9, y: L.dest.y + (L.dest.h - 11) * 0.5, w: 11, h: 11 }, col(white(same ? 0.2 : 0.4), a), 1.1);
    const shown = same ? "Each image's own folder" : st.dest || "Choose a folder…";
    const avail = L.change.x - (L.dest.x + 26) - (same ? 8 : 10);
    text(fitFront(shown, avail, MONO), L.dest.x + 26, L.dest.y + L.dest.h * 0.5 + 3.5, MONO, "mono", col(white(same ? 0.28 : 0.62), a));
    if (!same) {
      const hv = hover.amount(ID.change);
      text("Change…", L.change.x + L.change.w - est("Change…", SMALL), L.change.y + L.change.h * 0.5 + 3.5, SMALL, "medium",
           col(lerpColor(tk.primary, withAlpha(tk.primary, 0.7), hv), a));
    }
    {
      const da = CAN_G6 ? a : a * 0.4;
      const hv = CAN_G6 ? hover.amount(ID.same) * a : 0;
      if (hv > 0.001) fillRR({ x: L.same.x - 4, y: L.same.y - 2, w: L.same.w + 8, h: L.same.h + 4 }, 2, css(withAlpha(tk.white, 0.07 * hv)));
      check({ x: L.same.x, y: L.same.y + 2, w: CHECK, h: CHECK }, same ? 1 : 0, da, CAN_G6 ? hover.amount(ID.same) : 0);
      text("Same as source", L.same.x + CHECK + GAP_M, L.same.y + L.same.h * 0.5 + 4, FONT, "sans", col(same ? tk.foreground : tk["muted-foreground"], da));
    }
    {
      const first = rows.map((r) => nodes[r.node]).find((n, i) => !n.group && n.slot >= 0 && leafOn[rows[i].node]);
      let base = first ? first.name : "image.jpg";
      const dot = base.lastIndexOf(".");
      if (dot >= 0) base = base.slice(0, dot);
      const file = (st.usePrefix ? st.prefix : "") + base + EXT[st.format];
      const dir = same ? "<image folder>" : st.dest || "<no folder chosen>";
      const full = dir.replace(/\/$/, "") + "/" + (st.useSub && st.sub ? st.sub + "/" : "") + file;
      text(fitFront(full, iw, MONO), L.preview.x, L.preview.y + 11, MONO, "mono", col(white(0.32), a));
    }
    const modifier = (ck, fd, on, label, value, ph, ckId, idx) => {
      const da = CAN_G6 ? a : a * 0.4;
      const hv = CAN_G6 ? hover.amount(ckId) * a : 0;
      if (hv > 0.001) fillRR({ x: ck.x - 4, y: ck.y - 4, w: LABEL_COL + CHECK + 18, h: CHECK + 8 }, 2, css(withAlpha(tk.white, 0.07 * hv)));
      check(ck, on ? 1 : 0, da, CAN_G6 ? hover.amount(ckId) : 0);
      text(fitEnd(label, LABEL_COL, FONT), ck.x + CHECK + 10, ck.y + CHECK * 0.5 + 4, FONT, "sans", col(on ? tk.foreground : tk["muted-foreground"], da));
      field(fd, value, ph, on, st.focus === idx, da);
    };
    modifier(L.prefixCheck, L.prefixField, st.usePrefix, "Filename prefix", st.prefix, "e.g. export_", ID.prefixCheck, 0);
    modifier(L.subCheck, L.subField, st.useSub, "Export to subfolder", st.sub, "subfolder name", ID.subCheck, 1);
    sectionHeader(ix, L.fmt[0].y - SEC_H, iw, "FORMAT", a);
    for (let i = 0; i < 3; i++) chip(L.fmt[i], FORMATS[i], st.format === i, a, hover.amount(ID.fmt0 + i), FONT);
    for (let i = 0; i < 4; i++) chip(L.size[i], SIZES[i], st.size === i, a, hover.amount(ID.size0 + i), SMALL);
    if (st.format === 0) {
      text("Quality", L.qLabel.x, L.qLabel.y + 11, FONT, "sans", col(tk["muted-foreground"], a));
      const val = st.quality + "%";
      text(val, L.qLabel.x + L.qLabel.w - est(val, FONT), L.qLabel.y + 11, FONT, "monoMedium", col(tk.foreground, a));
      const q = L.qSlider, tv = (st.quality - 60) / 40, ty = q.y + q.h * 0.5;
      fillRR({ x: q.x, y: ty - 2, w: q.w, h: 4 }, 999, col(tk.secondary, a));
      fillRR({ x: q.x, y: ty - 2, w: q.w * tv, h: 4 }, 999, col(tk.primary, a));
      const tr = 4.5 + (st.dragQ ? 1.5 : hover.amount(ID.quality));
      g.beginPath(); g.arc(q.x + q.w * tv, ty, tr, 0, Math.PI * 2); g.fillStyle = col(tk.white, a); g.fill();
    }
    sectionHeader(ix, L.meta[0].y - (META_ROW - TOGGLE_H) * 0.5 - SEC_H, iw, "METADATA", a, CAN_G6 ? "" : "·  not in the core yet");
    for (let i = 0; i < 3; i++) {
      const tg = L.meta[i], rowY = tg.y + TOGGLE_H * 0.5;
      const da = CAN_G6 ? a : a * 0.4;
      const hv = CAN_G6 ? hover.amount(ID.meta0 + i) * a : 0;
      if (hv > 0.001) fillRR({ x: ix - 4, y: rowY - META_ROW * 0.5, w: iw + 8, h: META_ROW }, 2, css(withAlpha(tk.white, 0.07 * hv)));
      text(fitEnd(META_LABELS[i], iw - TOGGLE_W - 16, FONT), ix, rowY + 4, FONT, "sans", col(st.meta[i] ? tk.foreground : tk["muted-foreground"], da));
      const k = knob[i].value;
      fillRR(tg, 999, col(lerpColor(tk["switch-background"], tk.primary, k), da));
      const kr = (TOGGLE_H - 4) * 0.5, kx = tg.x + 2 + kr + (tg.w - 4 - kr * 2) * k;
      g.beginPath(); g.arc(kx, tg.y + 2 + kr, kr, 0, Math.PI * 2); g.fillStyle = col(tk.white, da); g.fill();
    }
  }
  function drawProgress(a) {
    const ix = cardX() + PAD, iw = CARD_W - 2 * PAD;
    const tr = progressTreeRect(), p = phase.value;
    const keepA = appear.value * (1 - completeAmt.value);
    sectionHeader(ix, tr.y - SEC_H, iw, "IMAGES TO EXPORT", keepA);
    tree(keepA, tr, st.exportRows, p);
    const revealA = a * clamp01((p - 0.55) / 0.45);
    if (revealA <= 0.004) return;
    const bar = barRect();
    fillRR(bar, 999, col(tk.secondary, revealA));
    fillRR({ x: bar.x, y: bar.y, w: bar.w * clamp01(progress.value), h: bar.h }, 999, col(tk.primary, revealA));
    const ty = bar.y + bar.h + 8 + 10;
    const name = st.name || "Preparing…";
    const count = `${st.done} / ${st.total}`, cw = est(count, SMALL);
    text(fitFront(name, iw - cw - 12, MONO), ix, ty, MONO, "mono", col(white(0.42), revealA));
    text(count, ix + iw - cw, ty, SMALL, "mono", col(tk["muted-foreground"], revealA));
  }
  function drawComplete(a, body) {
    const cx = body.x + body.w * 0.5, cy = body.y + body.h * 0.5, r = 15;
    glyph("checkCircle", { x: cx - r, y: cy - r - 8, w: 2 * r, h: 2 * r }, col(tk.success, a), 1.5);
    const msg = `Exported ${st.total} ${st.total === 1 ? "photo" : "photos"}`;
    text(msg, cx - est(msg, 12) * 0.5, cy + r + 8, 12, "medium", col(tk.foreground, a));
  }
  function drawFooter(a0, foot) {
    const a = a0 * (1 - completeAmt.value);
    if (a <= 0.004) return;
    const p = phase.value, sel = checkedCount();
    const leaveA = a * (1 - clamp01(p / 0.45)), arriveA = a * clamp01((p - 0.55) / 0.45);
    if (leaveA > 0.004) {
      let sum = `${sel}${sel === 1 ? " photo · " : " photos · "}${FORMATS[st.format]} · ${SIZES[st.size]}`;
      if (st.usePrefix && st.prefix) sum += " · prefix: " + st.prefix;
      text(fitEnd(sum, cancelRect().x - foot.x - PAD - 12, SMALL), foot.x + PAD, foot.y + foot.h * 0.5 + 3.5, SMALL, "sans", col(tk["muted-foreground"], leaveA));
      const btn = (r, label, primary, hv, disabled) => {
        let fill = primary ? tk.primary : tk.secondary, border = primary ? tk.primary : tk.border;
        if (disabled) { fill = tk.secondary; border = fade(tk.border, 0.6); }
        paintRR(r, 2, col(fill, leaveA), col(border, leaveA));
        if (!disabled && hv > 0.001) fillRR(r, 2, css(withAlpha(tk.white, 0.07 * hv * leaveA)));
        const lc = disabled ? white(0.22) : primary ? tk["primary-foreground"] : tk.foreground;
        let tx = r.x + (r.w - est(label, FONT)) * 0.5;
        if (primary) {
          tx = r.x + (r.w - (est(label, FONT) + 15)) * 0.5 + 15;
          glyph("download", { x: tx - 14, y: r.y + (r.h - 10) * 0.5, w: 10, h: 10 }, col(lc, leaveA), 1.2);
        }
        text(label, tx, r.y + r.h * 0.5 + 4, FONT, "medium", col(lc, leaveA));
      };
      btn(cancelRect(), "Cancel", false, hover.amount(ID.cancel), false);
      btn(exportRect(), `Export ${sel} ${sel === 1 ? "photo" : "photos"}`, true, hover.amount(ID.export), !canExport());
    }
    if (arriveA > 0.004) {
      const msg = "Writing files — please wait";
      text(msg, foot.x + foot.w - PAD - est(msg, SMALL), foot.y + foot.h * 0.5 + 3.5, SMALL, "sans", col(tk["muted-foreground"], arriveA));
    }
  }

  // ---------------------------------------------------------------- frame (advance + onOverlay)
  let raf = 0;
  function advanceRowFades(now) {
    const dt = st.rowFadeT < 0 ? 0 : now - st.rowFadeT;
    st.rowFadeT = now;
    const step = reducedMotion() ? 1 : Math.min(1, Math.max(0, dt / 220));
    for (let i = 0; i < st.rowFade.length; i++) {
      let done;
      if (st.exportSeq[i] >= 0) done = st.exportSeq[i] < st.done;
      else {
        done = true;
        for (let j = i + 1; j < st.exportRows.length; j++) {
          if (st.exportRows[j].depth <= st.exportRows[i].depth) break;
          if (st.exportSeq[j] >= 0 && st.exportSeq[j] >= st.done) { done = false; break; }
        }
      }
      const tgt = done ? 1 : 0;
      st.rowFade[i] = st.rowFade[i] < tgt ? Math.min(tgt, st.rowFade[i] + step) : Math.max(tgt, st.rowFade[i] - step);
    }
  }
  function followActive() {
    const box = progressTreeRect(), view = box.h - 2, contentH = st.exportRows.length * ROW_H;
    if (view <= 0 || contentH <= view) return;
    let active = st.exportSeq.indexOf(st.done);
    if (active < 0) active = st.exportRows.length - 1;
    const top = active * ROW_H, bottom = top + ROW_H;
    let t = treeTarget;
    if (top < t) t = top; else if (bottom > t + view) t = bottom - view;
    t = Math.min(contentH - view, Math.max(0, t));
    if (Math.abs(t - treeTarget) > 0.5) { treeTarget = t; treeScroll.to(t, 220, Ease.EaseOutCubic); }
  }
  function frame(now) {
    raf = 0;
    ({ W, H } = s.fit());
    hover.advance(now);
    if (st.exporting) {
      if (st.firePending && !phase.animating) { st.firePending = false; fire(); }
      advanceRowFades(now);
      if (!st.complete) followActive();
      else if (!st.closing && performance.now() - st.completeAt >= HOLD_MS) beginClose();
    }
    if (st.closing && !appear.animating && appear.value <= 0.001) {
      offs.forEach((f) => f());
      md.close();
      return;
    }
    const a = appear.value;
    g.clearRect(0, 0, W, H);
    const p = phase.value, d = completeAmt.value;
    const formA = (1 - p) * (1 - d) * a, progA = p * (1 - d) * a, doneA = d * a;
    g.fillStyle = `rgba(0, 0, 0, ${0.6 * a})`;           // ExportDialog.cpp:901 Color{0, 0, 0, 0.6}
    g.fillRect(0, 0, W, H);
    const c = cardRect();
    paintRR(c, 2, col(tk.card, a), col(tk.border, a));   // filledStroked; the bands then cover its inner half
    // header band
    const head = { x: c.x, y: c.y, w: c.w, h: HEADER_H };
    g.fillStyle = col(tk.muted, a); g.fillRect(head.x, head.y, head.w, head.h);
    line(head.x, head.y + head.h, head.x + head.w, head.y + head.h, col(tk.border, a));
    glyph("download", { x: head.x + PAD, y: head.y + (HEADER_H - 13) * 0.5, w: 13, h: 13 }, col(tk.primary, a), 1.3);
    text("Export", head.x + PAD + 21, head.y + HEADER_H * 0.5 + 4.5, 13, "semibold", col(tk.foreground, a));
    const cl = closeRect(), ch = hover.amount(ID.close);
    if (ch * a > 0.001) fillRR(cl, 2, css(withAlpha(tk.white, 0.07 * ch * a)));
    glyph("close", { x: cl.x + 4, y: cl.y + 4, w: 10, h: 10 }, col(lerpColor(tk["muted-foreground"], tk.foreground, ch), a), 1.3);
    // body
    const body = bodyRect();
    g.save();
    g.beginPath(); g.rect(body.x, body.y, body.w, body.h); g.clip();
    if (formA > 0.004) drawForm(formA);
    if (progA > 0.004) drawProgress(progA);
    if (doneA > 0.004) drawComplete(doneA, body);
    if (formA > 0.004 && !st.exporting) {
      const maxS = Math.max(0, formContentH() - body.h), sc = bodyScroll.value, F = 22;
      const solid = col(tk.card, formA), clear = css(withAlpha(tk.card, 0));
      if (sc < maxS - 0.5) { const gr = g.createLinearGradient(0, body.y + body.h - F, 0, body.y + body.h); gr.addColorStop(0, clear); gr.addColorStop(1, solid); g.fillStyle = gr; g.fillRect(body.x, body.y + body.h - F, body.w, F); }
      if (sc > 0.5) { const gr = g.createLinearGradient(0, body.y, 0, body.y + F); gr.addColorStop(0, solid); gr.addColorStop(1, clear); g.fillStyle = gr; g.fillRect(body.x, body.y, body.w, F); }
    }
    g.restore();
    // footer
    const foot = { x: c.x, y: c.y + c.h - FOOTER_H, w: c.w, h: FOOTER_H };
    g.fillStyle = col(tk.muted, a); g.fillRect(foot.x, foot.y, foot.w, foot.h);
    line(foot.x, foot.y, foot.x + foot.w, foot.y, col(tk.border, a));
    drawFooter(a, foot);
    raf = requestAnimationFrame(frame);
  }
  raf = requestAnimationFrame(frame);
  return api;
}

function dirOf(p) { if (!p) return ""; const i = p.lastIndexOf("/"); return i > 0 ? p.slice(0, i) : ""; }
