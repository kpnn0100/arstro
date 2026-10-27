/*
 * Interstellar — grade/deck/nodegraph.js: the Grade target's grade as nodes and links (R-CLR-3).
 * Ports app/widgets/NodeGraph.{h,cpp}: INPUT · the source · its PARALLEL nodes (stacked in its column,
 * joined at a "+" mixer) · each group it is in (SERIAL) · OUTPUT. Click a node: `rack select <bind>`.
 * Painted into GradeDeck's painter (the deck cross-fades it); nodeAt() is its hit-test.
 */
import { tok } from "../../kit/tokens.js";
import { width as measure, ellipsize, baseline } from "../../kit/textfit.js";
import { col, fade, hoverWash, Anim, Ease, F } from "../../kit/paint.js";

const NODE_H = 24, GAP = 22, PAD = 9.75, MIX_D = 14;                  // NodeGraph.cpp:15

/** NodeGraph::bind: the nodes of the model's Grade target. */
export function graphNodes(m) {
  const rack = (m && m.rack) || [];
  let s = m ? m.selectedRack ?? -1 : -1;
  if (s >= 0 && s < rack.length && rack[s].parallelOf) { const k = rack.findIndex((n) => n.rackObj === rack[s].parallelOf); if (k >= 0) s = k; }
  if (s >= 0 && s < rack.length && rack[s].group) {
    let first = -1;
    const stack = [s];
    while (stack.length && first < 0) {
      const g = stack.pop();
      rack.forEach((n, i) => { if (n.parent === g) { if (n.group) stack.push(i); else if (first < 0) first = i; } });
    }
    s = first;
  }
  const nodes = [];
  if (s >= 0 && s < rack.length) {
    const src = rack[s], media = (src.media || "").split(/[\\/]/).pop();
    nodes.push({ key: "in", bind: "", label: "INPUT · " + media, kind: "input" });
    nodes.push({ key: src.rackObj, bind: src.bindName, label: src.bindName, kind: "source", bypassed: !!src.bypass });
    let parallel = false;
    for (const n of rack) if (n.parallelOf && n.parallelOf === src.rackObj) {
      const mix = n.parallelMix ?? 1;
      nodes.push({ key: n.rackObj, bind: n.bindName, label: n.bindName + (mix < 0.999 ? " · " + Math.round(mix * 100) + "%" : ""), kind: "parallel", bypassed: !!n.bypass, mix });
      parallel = true;
    }
    if (parallel) nodes.push({ key: "mix", bind: "", label: "+", kind: "mixer" });
    for (let pi = src.parent; pi >= 0 && pi < rack.length; pi = rack[pi].parent)
      nodes.push({ key: rack[pi].rackObj, bind: rack[pi].bindName, label: rack[pi].bindName, kind: "group", bypassed: !!rack[pi].bypass });
    nodes.push({ key: "out", bind: "", label: "OUTPUT", kind: "output" });
  }
  return nodes;
}

const famOf = (n) => (n.kind === "input" || n.kind === "output" ? F.semibold : F.sans);

/** NodeGraph::place: each node's rect in `box`. */
export function placeNodes(nodes, box) {
  const cy = box.y + box.h * 0.5, rects = new Map();
  const widthOf = (n) => (n.kind === "mixer" ? MIX_D : Math.min(Math.max(measure(n.label, 10, famOf(n)) + 20, 54), 150));
  let colW = 0;
  for (const n of nodes) if (n.kind === "source" || n.kind === "parallel") colW = Math.max(colW, widthOf(n));
  let x = box.x + PAD, sourceX = 0, pi = 0;
  for (const n of nodes) {
    const w = n.kind === "source" || n.kind === "parallel" ? colW : widthOf(n);
    let nx = x, ny = cy - NODE_H * 0.5, h = NODE_H;
    if (n.kind === "mixer") { ny = cy - MIX_D * 0.5; h = MIX_D; }
    if (n.kind === "parallel") { const side = pi % 2 === 0 ? -1 : 1, rank = Math.floor(pi / 2) + 1; nx = sourceX; ny = cy - NODE_H * 0.5 + side * rank * (NODE_H + 6); ++pi; }
    rects.set(n.key, { x: nx, y: ny, w, h });
    if (n.kind === "source") sourceX = nx;
    if (n.kind !== "parallel") x = nx + w + GAP;
  }
  return rects;
}

/** The node under `pt` (index into nodes), or -1. */
export function nodeAt(nodes, rects, pt) {
  for (let i = 0; i < nodes.length; ++i) { const r = rects.get(nodes[i].key); if (r && pt.x >= r.x && pt.x < r.x + r.w && pt.y >= r.y && pt.y < r.y + r.h) return i; }
  return -1;
}

/** NodeGraph's motion: each node keeps its own eased rect and opacity, keyed by its rack node - a node
 *  added fades in where it belongs (cross-fade ms), the ones after it slide along (220 ms). */
export class GraphMotion {
  constructor() { this.a = new Map(); this.moving = false; }
  place(nodes, rects, now) {
    let moving = false;
    const out = new Map();
    for (const n of nodes) {
      const t = rects.get(n.key);
      let a = this.a.get(n.key);
      if (!a) {
        a = { x: new Anim(t.x), y: new Anim(t.y), w: new Anim(t.w), alpha: new Anim(0), tx: t.x, ty: t.y, tw: t.w };
        a.alpha.animateTo(1, tok.ms("motion-cross-fade"), Ease.EaseOutCubic, now);
        this.a.set(n.key, a);
      } else if (Math.abs(a.tx - t.x) > 0.01 || Math.abs(a.ty - t.y) > 0.01 || Math.abs(a.tw - t.w) > 0.01) {
        a.x.animateTo(t.x, 220, Ease.EaseOutCubic, now); a.y.animateTo(t.y, 220, Ease.EaseOutCubic, now); a.w.animateTo(t.w, 220, Ease.EaseOutCubic, now);
        a.tx = t.x; a.ty = t.y; a.tw = t.w;
      }
      for (const k of ["x", "y", "w", "alpha"]) moving = a[k].update(now) || moving;
      out.set(n.key, { x: a.x.value, y: a.y.value, w: a.w.value, h: t.h, alpha: a.alpha.value });
    }
    for (const k of [...this.a.keys()]) if (!rects.has(k)) this.a.delete(k);
    this.moving = moving;
    return out;
  }
}

/** NodeGraph::onPaint, into the deck's painter. */
export function paintNodeGraph(p, m, box, hover, motion, now) {
  const nodes = graphNodes(m), muted = col("muted-foreground"), fg = col("foreground");
  if (!nodes.length) {
    const s = "Select a source to see its grade as nodes.";
    p.text(s, box.x + (box.w - measure(s, 11, F.sans)) * 0.5, box.y + box.h * 0.5 + 4, 11, F.sans, muted);
    return { nodes, rects: new Map() };
  }
  const target = placeNodes(nodes, box), rects = motion ? motion.place(nodes, target, now) : target, rc = tok.px("radius-control"), hair = tok.px("radius-hairline") || 1, pill = tok.px("radius-pill");
  const R = (k) => { const r = rects.get(k); return { x: r.x + r.w, y: r.y + r.h * 0.5 }; };
  const L = (k) => { const r = rects.get(k); return { x: r.x, y: r.y + r.h * 0.5 }; };
  const al = (k) => rects.get(k)?.alpha ?? 1;
  const link = (a, b, alpha) => {
    const c = fade(muted, 0.8 * alpha);
    const mx = (a.x + b.x) * 0.5;
    if (Math.abs(a.y - b.y) < 0.5) p.line(a.x, a.y, b.x, b.y, c, 1.2);
    else { p.line(a.x, a.y, mx, a.y, c, 1.2); p.line(mx, a.y, mx, b.y, c, 1.2); p.line(mx, b.y, b.x, b.y, c, 1.2); }
    p.line(b.x - 4, b.y - 3, b.x, b.y, c, 1.2);
    p.line(b.x - 4, b.y + 3, b.x, b.y, c, 1.2);
  };
  const hasMix = nodes.some((n) => n.kind === "mixer");
  let prev = "in";
  for (const n of nodes) {
    if (n.kind === "input") continue;
    if (n.kind === "parallel") { link(R("in"), L(n.key), Math.min(al("in"), al(n.key))); if (hasMix) link(R(n.key), L("mix"), Math.min(al(n.key), al("mix"))); continue; }
    link(R(prev), L(n.key), Math.min(al(n.key), al(prev)));
    prev = n.key;
  }
  const selKey = m.selectedRack >= 0 && m.selectedRack < m.rack.length ? m.rack[m.selectedRack].rackObj : "";
  nodes.forEach((n, i) => {
    const r = rects.get(n.key), hv = hover ? hover.amount(i) : 0, a = r.alpha ?? 1;
    if (a <= 0.001) return;
    if (n.kind === "mixer") {
      p.rect(r.x, r.y, r.w, r.h, pill, { fill: fade(col("card"), a), stroke: fade(muted, a), sw: 1 });
      p.line(r.x + 4, r.y + r.h * 0.5, r.x + r.w - 4, r.y + r.h * 0.5, fade(fg, a), 1.2);
      p.line(r.x + r.w * 0.5, r.y + 4, r.x + r.w * 0.5, r.y + r.h - 4, fade(fg, a), 1.2);
      return;
    }
    const io = n.kind === "input" || n.kind === "output", sel = !!n.key && n.key === selKey;
    p.rect(r.x, r.y, r.w, r.h, rc, { fill: fade(col(io ? "muted" : "secondary"), a), stroke: fade(sel ? col("primary") : col("border"), a), sw: sel ? 1.5 : 1 });
    if (hv > 0.001) p.rect(r.x, r.y, r.w, r.h, rc, { fill: hoverWash(hv * a) });
    if (n.kind === "group") p.rect(r.x, r.y, 3, r.h, hair, { fill: fade(muted, 0.6 * a) });
    if (n.kind === "parallel") p.rect(r.x, r.y, 3, r.h, hair, { fill: fade(col("primary"), 0.7 * a) });
    const fam = famOf(n);
    p.text(ellipsize(n.label, Math.max(0, r.w - 16), 10, fam), r.x + 10, baseline(r.y + r.h * 0.5, 10), 10, fam, fade(n.bypassed || io ? muted : fg, a));
  });
  return { nodes, rects, moving: !!(motion && motion.moving) };
}
