/*
 * Cosmo by arstro — info.js: InfoDialog, "Image information" (widgets/InfoDialog.{h,cpp}).
 *
 * vm.view.dialog = {name: "info", node}. Asks the service (`metadata <node>`, vm.metadata) and
 * opens only when that succeeds AND the model's answer has rows - a failure (e.g. "metadata: a
 * group has no file behind it") is toasted by the view-model and nothing opens. The rows are a
 * snapshot of the model's `metadataName` / `metadata[{label, value}]`, as InfoDialog::show takes.
 *
 * Card 400; pad 20, title 26, row 19.5, body <= 292.5 (15 rows) and <= what the window holds;
 * title JetBrains Mono Medium 13 at (20, 32.55), front-ellipsised to 332; close ✕ (22 x 22 at
 * c.right - 42, c.y + 17) with a hover wash and strokes brightening the muted colour by 0.4;
 * rows: label Roboto 11 muted fitted to 104.5, value 11 px at 110.5 - JetBrains Mono when it
 * looks numeric (a digit / + / - first, or "f/", "ISO ", "R ") or the label is Path / File, else
 * Roboto; Path front-ellipsised, the rest end-ellipsised; a 3 px thumb when it overflows; footer
 * button Close (61 x 28). Closes on ✕, Close, outside, Esc / Enter; other keys are swallowed.
 *
 * Deviation: the wheel scrolls one row (19.5 px) per notch - the native passes the raw +-1, one
 * pixel per notch (App.cpp:868), which no browser wheel could use. Eased 180 ms EaseOutCubic.
 */
import { h } from "../core/dom.js";
import { effect } from "../core/signal.js";
import { openModal } from "../ui/modal.js";
import { est, fitEnd, fitFront, svg, at, tokens, brighten, setVars } from "./paint.js";
import { T, box, button, frame } from "./kit.js";

const W = 400, PAD = 20, TITLE_H = 26, ROW = 19.5, MAX_BODY = 292.5, FOOT_GAP = 16, BTN_H = 28, PX = 11, LABEL_W = 110.5, CLOSE = 22;

const numeric = (v) => /^[0-9+-]/.test(v) || v.startsWith("f/") || v.startsWith("ISO ") || v.startsWith("R ");

/** The view-model's `metadata` answer {node, name, rows} (a signal), asked for by `askMetadata`. */
const answerSig = (vm) => vm.metadata;
const ask = (vm, node) => vm.askMetadata(node >= 0 ? node : undefined);

/** Wait (briefly) for the view-model to carry the answer for `node`: the reply to the command
 *  can arrive before the throttled model push that holds it. */
function answerFor(sig, node) {
  const ok = (a) => a && (node < 0 || a.node === node) && (a.rows || []).length > 0;
  if (ok(sig.peek())) return Promise.resolve(sig.peek());
  return new Promise((resolve) => {
    let stop = null, done = false;
    const t = setTimeout(() => { if (!done) { done = true; if (stop) stop(); resolve(sig.peek()); } }, 900);
    stop = effect(() => {
      const a = sig.value;
      if (!done && ok(a)) { done = true; clearTimeout(t); queueMicrotask(() => stop && stop()); resolve(a); }
    });
  });
}

export async function open(host, spec) {
  const vm = host.vm;
  const node = spec.node === undefined || spec.node === null ? -1 : spec.node;
  const sig = answerSig(vm);
  if (!sig) { console.info("cosmo: the view-model has no metadata answer signal; Image information cannot open"); return null; }
  const have = sig.peek();
  if (!(have && have.node === node && (have.rows || []).length)) {
    try { await ask(vm, node); } catch { return null; }
  }
  const m = (await answerFor(sig, node)) || {};
  const rows = (m.rows || []).map((r) => ({ label: String(r.label || ""), value: String(r.value ?? "") }));
  if (!rows.length) return null;
  const title = m.name || "";

  const layer = host.layer;
  const bodyH = () => {
    const room = layer.clientHeight - 2 * PAD - TITLE_H - FOOT_GAP - BTN_H - 2 * PAD;
    return Math.max(ROW, Math.min(rows.length * ROW, MAX_BODY, Math.max(ROW, room)));
  };
  const cardH = () => PAD + TITLE_H + bodyH() + FOOT_GAP + BTN_H + PAD;
  let md;
  const close = () => { if (!md.closing) { md.close(); host.closed(spec); } };
  md = openModal(layer, { width: W, height: cardH(), cls: "dg-info", onOutside: close,
    onKey: (e) => { if (e.key === "Escape" || e.key === "Enter") { close(); return true; } return false; } });
  const c = md.card;
  frame(c);
  setVars(c, { "dg-x-h": brighten(tokens()["muted-foreground"], 0.4) });     // InfoDialog.cpp: brighten(muted, 0.4 * hover)
  T(c, fitFront(title, W - 2 * PAD - CLOSE - 6, 13), PAD, PAD + 13 * 0.35 + 8, 13, "monoMedium", "dg-fg");
  // ✕
  const x = h("button.dg-info-x", { "aria-label": "Close", style: { left: W - PAD - CLOSE + "px", top: PAD - 3 + "px", width: CLOSE + "px", height: CLOSE + "px" },
    onclick: (e) => { e.stopPropagation(); close(); } });
  box(x, "dg-wash", 0, 0, CLOSE, CLOSE);
  const xs = svg("svg", { width: CLOSE, height: CLOSE, class: "dg-info-xg" });
  xs.append(svg("path", { d: "M7 7 L15 15 M15 7 L7 15", fill: "none", stroke: "currentColor", "stroke-width": 1.3 }));
  x.append(at(xs, 0, 0));
  c.append(x);
  // body
  const body = h("div.dg-info-body", { style: { left: PAD + "px", top: PAD + TITLE_H + "px", width: W - 2 * PAD + "px" } });
  const list = h("div.dg-info-list");
  const thumb = h("div.dg-info-thumb");
  body.append(list, thumb);
  c.append(body);
  const bw = W - 2 * PAD;
  let els = [];
  function paintRows() {
    const over = rows.length * ROW > bodyH();
    const valueW = bw - LABEL_W - (over ? 3 + 4 : 0);
    list.textContent = "";
    els = rows.map((r, i) => {
      const base = i * ROW + ROW * 0.5 + PX * 0.35;
      const path = r.label === "Path";
      T(list, fitEnd(r.label, LABEL_W - 6, PX), 0, base, PX, "sans", "dg-muted");
      return T(list, path ? fitFront(r.value, valueW, PX) : fitEnd(r.value, valueW, PX), LABEL_W, base, PX,
               numeric(r.value) || path || r.label === "File" ? "mono" : "sans", "dg-fg");
    });
    void els;
  }
  let scroll = 0;
  function layout() {
    const bh = bodyH();
    md.setHeight(cardH());
    body.style.height = bh + "px";
    const content = rows.length * ROW;
    scroll = Math.min(Math.max(0, scroll), Math.max(0, content - bh));
    list.style.transform = `translateY(${-scroll}px)`;
    const over = content > bh;
    thumb.classList.toggle("on", over);
    if (over) {
      const th = Math.max(18, bh * bh / content), maxS = content - bh;
      Object.assign(thumb.style, { height: th + "px", transform: `translateY(${maxS <= 0 ? 0 : (scroll / maxS) * (bh - th)}px)` });
    }
    closeBtn.style.top = cardH() - PAD - BTN_H + "px";
  }
  const wClose = est("Close", PX) + 28;
  const closeBtn = button(c, { x: W - PAD - wClose, y: cardH() - PAD - BTN_H, w: wClose, h: BTN_H, label: "Close", px: PX,
                               base: BTN_H / 2 + PX * 0.35, onClick: close });
  paintRows();
  layout();
  body.addEventListener("wheel", (e) => {
    e.preventDefault();
    const notch = -e.deltaY / (e.deltaMode === 1 ? 3 : 100);
    scroll -= notch * ROW;
    layout();
  }, { passive: false });
  const ro = new ResizeObserver(() => { if (!md.closing) { paintRows(); layout(); } });
  ro.observe(layer);
  const origClose = md.close;
  md.close = (r) => { ro.disconnect(); origClose(r); };
  return md;
}
