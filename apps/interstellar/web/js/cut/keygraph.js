/*
 * Interstellar — cut/keygraph.js: the curve band the key lane opens under a chosen lane. Ports the
 * drawing and the key choice of app/widgets/KeyGraph.{h,cpp} (headerless, as KeyLane shows it).
 *
 * Not a module of its own: cut/keylane.js owns one and paints it with the kit Painter in the band's rect.
 *   bind(model, animIds, t0, t1)   the curves shown (the front one last, on top) over the clip's clock window
 *   setNow(t) / setFront(address)  the playhead on the curve's clock; whose curve is in front
 *   setPlotSpan(x0, x1)            the plot exactly the clip it is drawn over (band-local x)
 *   paint(p, rect, alpha)          KeyGraph::onPaint: the plot bg, quarter grid, the now line, every curve
 *                                  (sampled every 2 px with the service's anim.eval), the keys as diamonds
 *                                  (chosen: filled accent), the value ends and the chosen key in words
 *   keyAt(rect, x, y) / selectKey(curve, k, add) / selectionList() / selectionCount() / curveCount()
 * A value range eases (KeyGraph.cpp:132-145) when the keys change: 220 ms.
 */
import { tok } from "../kit/tokens.js";
import { col, fade, primaryA, whiteA, Anim, Ease, F } from "../kit/paint.js";
import { ellipsize } from "../kit/textfit.js";
import { glyph } from "../kit/glyphs.js";
import { num } from "../kit/cmd.js";
import { fromModel, evalCurve, sideName, fmtG } from "./key/anim.js";

const PAD_X = 9.75, PAD_Y = 6.5, KEY_R = 4.0;

export class KeyGraph {
  constructor() {
    this.curves = []; this.t0 = 0; this.t1 = 1; this.now = 0; this.front = ""; this.x0 = -1; this.x1 = -1;
    this.sel = new Set();       // "<address>@<t>" of the chosen keys
    this.ranges = new Map();    // anim id -> {lo, hi: Anim, loL, hiL}
  }
  bind(m, ids, t0, t1) {
    const by = new Map((m.anims || []).map((a) => [a.id, a]));
    this.curves = ids.map((id) => by.get(id)).filter(Boolean).map((a) => ({ id: a.id, address: a.address, shape: !!a.shape,
      keys: (a.keys || []).map(fromModel).sort((p, q2) => p.t - q2.t), min: a.min ?? 0, max: a.max ?? 1 }));
    this.t0 = t0; this.t1 = t1 > t0 ? t1 : t0 + 1;
    const live = new Set(this.curves.flatMap((c) => c.keys.map((k) => c.address + "@" + num(k.t))));
    for (const s of [...this.sel]) if (!live.has(s)) this.sel.delete(s);
  }
  setNow(t) { this.now = t; }
  setFront(a) { this.front = a; }
  setPlotSpan(x0, x1) { this.x0 = x0; this.x1 = x1; }
  curveCount() { return this.curves.length; }
  frontIndex() { const i = this.curves.findIndex((c) => c.address === this.front); return i >= 0 ? i : this.curves.length - 1; }
  selectionList() { return [...this.sel].join(","); }
  selectionCount() { return this.sel.size; }
  selectKey(c, k, add) {
    const cv = this.curves[c];
    if (!cv || !cv.keys[k]) return;
    const id = cv.address + "@" + num(cv.keys[k].t);
    if (!add) this.sel.clear();
    if (add && this.sel.has(id)) this.sel.delete(id); else this.sel.add(id);
  }
  clearSelection() { this.sel.clear(); }
  plot(r) {
    const x0 = this.x0 >= 0 ? r.x + this.x0 : r.x + PAD_X, x1 = this.x0 >= 0 ? r.x + this.x1 : r.x + r.w - PAD_X;
    return { x: x0, y: r.y + PAD_Y, w: Math.max(0, x1 - x0), h: Math.max(0, r.h - 2 * PAD_Y) };
  }
  xAt(p, t) { return p.x + (t - this.t0) / (this.t1 - this.t0) * p.w; }
  tAt(p, x) { return p.w > 0 ? this.t0 + (x - p.x) / p.w * (this.t1 - this.t0) : this.t0; }
  /** KeyGraph.cpp:96-146: the keys and what the curve does between them, padded 18 %, eased. */
  advance(now) {
    let moving = false;
    for (const c of this.curves) {
      let lo = -1, hi = 1;
      if (!c.shape && c.keys.length) {
        lo = hi = c.keys[0].v;
        for (let i = 0; i <= 64; i++) { const v = evalCurve(c.keys, this.t0 + (this.t1 - this.t0) * i / 64); lo = Math.min(lo, v); hi = Math.max(hi, v); }
        for (const k of c.keys) { lo = Math.min(lo, k.v); hi = Math.max(hi, k.v); }
        const minSpan = Math.max(1e-3, (c.max - c.min) * 0.04);
        if (hi - lo < minSpan) { const m = 0.5 * (hi + lo); lo = m - minSpan * 0.5; hi = m + minSpan * 0.5; }
        const pad = (hi - lo) * 0.18; lo -= pad; hi += pad;
      }
      let r = this.ranges.get(c.id);
      if (!r) { r = { lo: new Anim(lo), hi: new Anim(hi), loL: lo, hiL: hi }; this.ranges.set(c.id, r); }
      if (r.loL !== lo) { r.lo.animateTo(lo, tok.ms("motion-slide"), Ease.EaseOutCubic, now); r.loL = lo; }
      if (r.hiL !== hi) { r.hi.animateTo(hi, tok.ms("motion-slide"), Ease.EaseOutCubic, now); r.hiL = hi; }
      moving = r.lo.update(now) || moving;
      moving = r.hi.update(now) || moving;
    }
    return moving;
  }
  yOf(p, c, v) {
    const r = this.ranges.get(this.curves[c].id);
    if (this.curves[c].shape) return p.y + p.h * 0.5;
    const lo = r ? r.lo.value : 0, hi = r ? r.hi.value : 1;
    return p.y + p.h - (hi > lo ? (v - lo) / (hi - lo) : 0.5) * p.h;
  }
  /** The key under (x, y) in rect r: {curve, key} or null — the front curve first. */
  keyAt(r, x, y) {
    const p = this.plot(r), f = this.frontIndex();
    const order = [f, ...this.curves.map((_, i) => i).filter((i) => i !== f)];
    for (const c of order) {
      const ks = this.curves[c] ? this.curves[c].keys : [];
      for (let k = ks.length - 1; k >= 0; k--)
        if (Math.abs(this.xAt(p, ks[k].t) - x) <= KEY_R + 3 && Math.abs(this.yOf(p, c, ks[k].v) - y) <= KEY_R + 3) return { curve: c, key: k };
    }
    return null;
  }
  paint(P, r, alpha) {
    if (!(r.w > 0) || !(r.h > 0)) return;
    P.group(alpha);
    P.rect(r.x, r.y, r.w, r.h, 0, { fill: col("curve-plot-bg") });
    const fr = this.frontIndex();
    if (!this.curves.length || fr < 0) { P.restore(); return; }
    const p = this.plot(r);
    P.clip(p.x - KEY_R - 1, p.y - KEY_R - 1, p.w + 2 * KEY_R + 2, p.h + 2 * KEY_R + 2);
    for (let i = 0; i <= 4; i++) { const y = p.y + p.h * i / 4; P.line(p.x, y, p.x + p.w, y, whiteA(i === 0 || i === 4 ? 0.06 : 0.035), 1); }
    const nx = this.xAt(p, this.now);
    if (nx >= p.x && nx <= p.x + p.w) P.line(nx, p.y, nx, p.y + p.h, primaryA(0.45), 1);
    const order = [...this.curves.map((_, i) => i).filter((i) => i !== fr), fr];
    for (const c of order) {
      const cv = this.curves[c], ks = cv.keys;
      if (!ks.length) continue;
      const front = c === fr, a = front ? 0.95 : 0.4;
      if (cv.shape) {
        const y = this.yOf(p, c, 0);
        P.line(this.xAt(p, ks[0].t), y, this.xAt(p, ks[ks.length - 1].t), y, primaryA(a), front ? 1.5 : 1);
      } else {
        const n = Math.max(2, Math.floor(p.w / 2));
        let d = "";
        for (let i = 0; i <= n; i++) {
          const y = this.yOf(p, c, evalCurve(ks, this.t0 + (this.t1 - this.t0) * i / n));
          d += (i ? "L" : "M") + (p.x + p.w * i / n).toFixed(2) + " " + y.toFixed(2);
        }
        P.path(d, { stroke: front ? primaryA(a) : whiteA(a), sw: front ? 1.5 : 1 });
      }
      for (let i = 0; i < ks.length; i++) {
        const x = this.xAt(p, ks[i].t), y = this.yOf(p, c, ks[i].v), sel = this.sel.has(cv.address + "@" + num(ks[i].t));
        P.path(`M${x} ${y - KEY_R}L${x + KEY_R} ${y}L${x} ${y + KEY_R}L${x - KEY_R} ${y}Z`,
               { fill: sel ? col("primary") : col("card"), stroke: sel ? col("primary") : fade(col("foreground"), front ? 1 : 0.55), sw: 1 });
      }
    }
    P.restore();
    const cv = this.curves[fr], mut = fade(col("muted-foreground"), 0.85);
    if (!cv.shape) {
      const rg = this.ranges.get(cv.id);
      if (rg) {
        P.text(fmtG(rg.hi.value, 3), p.x + 3, p.y + 9, 8.5, F.mono, mut);
        P.text(fmtG(rg.lo.value, 3), p.x + 3, p.y + p.h - 3, 8.5, F.mono, mut);
      }
    }
    // the chosen key in words
    const one = [...this.sel].find((s) => s.startsWith(cv.address + "@"));
    if (one) {
      const t = +one.slice(cv.address.length + 1), k = cv.keys.find((z) => Math.abs(z.t - t) < 5e-4);
      if (k) {
        const words = cv.shape ? `t ${k.t.toFixed(3)} s  ·  in ${sideName(k.in)}  out ${sideName(k.out)}`
                               : `t ${k.t.toFixed(3)} s  ·  ${fmtG(k.v, 4)}  ·  in ${sideName(k.in)}  out ${sideName(k.out)}`;
        P.text(ellipsize(words, Math.max(0, p.w - 8), 8.5, F.mono), p.x + 40, p.y + 9, 8.5, F.mono, mut);
      }
    }
    P.restore();
    void glyph;
  }
}
