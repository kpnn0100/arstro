/*
 * Interstellar — dialogs/settings.js: Engine Settings, cosmo_v2::SettingsDialog (cosmo
 * widgets/SettingsDialog.{h,cpp}) as Interstellar opens it (App::openSettings, App.cpp:159-175, 223-231).
 * Ported from cosmo's web port (apps/cosmo/web/js/dialogs/settings.js), drawn in SVG like
 * edit/nameprompt.js. The shell opens it for vm.view.dialog = {name: "settings"}:
 *
 *   open(layer, vm, ctx, spec) -> {close()}
 *
 * Card 360; pad 18, header 34, row label 18 (Roboto Medium 11, tracking 0.06 em, muted), chips 24 high,
 * 6 apart, 12 px padding, wrapped lines 5 apart, rows 16 apart, footer 40, Done 92 × 26. Chip widths and
 * label centring use cosmo's byte estimate (estimateTextWidth = bytes × px × 0.6), as the native does.
 * Card centred (never above 4, 4); scrim black 0.55; popover fill + border; appear 150 ms in / 120 ms out
 * EaseOutCubic; per-chip and Done hover wash (motion-hover). Selected chip = primary fill + white Roboto
 * Medium; disabled × 0.4 and inert. A click moves the selection at once (the fill snaps, as native).
 *
 * Rows and what they send (the Input row is hidden: Interstellar has no touch shell):
 *   Screen scale      THIS page's UI scale (vm.view.uiScale), not `settings set uiScale` - per page;
 *                     every chip is live (the shell fits a too-small window down, see maxScaleHere)
 *   Preview quality   settings set previewEdge=1000|1600|2400
 *   CPU threads       settings set threads=0|2|4|8
 *   CPU limit         settings set cpuPercent=25|50|75|100
 *   GPU acceleration  settings set useGpu=0|1          (On inert without a GPU)
 *   Hardware video    settings set hardwareVideo=0|1   (Interstellar's extra row, R-PLAY-3)
 *   Preview cache     settings set previewCache=0|1    (R-PLAY-1)
 * A click outside the card or Done closes; Esc = Done (web addition, as cosmo's web port).
 */
import { h } from "../core/dom.js";
import { useCss } from "../kit/css.js";
import { tok } from "../kit/tokens.js";
import { UI_SCALES } from "../vm/app.js";
import { Painter, canvasSvg, col, fade, hoverWash, Anim, HoverFade, frameLoop, localPoint, inRect, Ease, F } from "../kit/paint.js";

const CARD_W = 360, PAD = 18, HEADER = 34, LABEL_H = 18, CHIP_H = 24, ROW_GAP = 16, CHIP_GAP = 6, CHIP_PADX = 12;
const FOOTER = 40, BTN_W = 92, BTN_H = 26, FONT = 12, LINE_GAP = 5, DONE = 9999;
const EDGES = [1000, 1600, 2400], THREADS = [0, 2, 4, 8], CPUS = [25, 50, 75, 100];
const enc = new TextEncoder();
const est = (s, px) => enc.encode(s).length * px * 0.6;            // cosmo TextMetrics.h estimateTextWidth

/** App's mMaxUiScale. The native caps the scale at the largest whose minimum window fits the display
 *  (R-SCALE-3); this page needs no cap - shell.js fits the App's minimum DOWN on a small window (its
 *  web-only addition) - so every scale is offered, as the native does on a display with room. */
const maxScaleHere = () => UI_SCALES[UI_SCALES.length - 1];

export function open(layer, vm, ctx, spec) {
  useCss("css/dialogs/settings.css");
  const st = (vm.settings && vm.settings.peek()) || {};
  const maxScale = maxScaleHere(), gpu = !!st.gpuAvailable;
  const set = (k, v) => vm.cmd(`settings set ${k}=${v}`);
  const rows = [
    { label: "Screen scale  ·  smaller fits more" + (maxScale < UI_SCALES[UI_SCALES.length - 1] ? `, up to ${maxScale}% here` : ""),
      chips: UI_SCALES.map((v) => ({ label: v + "%", value: v, disabled: v > maxScale })), sel: vm.view.uiScale.peek(),
      pick: (v) => { vm.view.uiScale.value = v; } },
    { label: "Preview quality", chips: EDGES.map((v, i) => ({ label: ["Draft", "Standard", "High"][i], value: v })), sel: st.previewEdge,
      pick: (v) => set("previewEdge", v) },
    { label: "CPU threads", chips: THREADS.map((v) => ({ label: v === 0 ? "Auto" : String(v), value: v })), sel: st.threads,
      pick: (v) => set("threads", v) },
    { label: "CPU limit  ·  Auto uses this", chips: CPUS.map((v) => ({ label: v + "%", value: v })), sel: st.cpuPercent,
      pick: (v) => set("cpuPercent", v) },
    { label: "GPU acceleration" + (gpu ? "" : "  ·  unavailable"), chips: [{ label: "Off", value: 0 }, { label: "On", value: 1, disabled: !gpu }],
      sel: st.useGpu && gpu ? 1 : 0, pick: (v) => set("useGpu", v) },
    { label: "Hardware video  ·  H.264 / H.265 on the video unit", chips: [{ label: "Off", value: 0 }, { label: "On", value: 1 }],
      sel: st.hardwareVideo ? 1 : 0, pick: (v) => set("hardwareVideo", v) },
    { label: "Preview cache  ·  graded frames, built when idle", chips: [{ label: "Off", value: 0 }, { label: "On", value: 1 }],
      sel: st.previewCache ? 1 : 0, pick: (v) => set("previewCache", v) },
  ];
  // SettingsDialog::layoutChips - relative to the row's chip origin; one walk, wrapping at the card's inner width
  for (const r of rows) {
    let x = 0, y = 0, lines = 1;
    r.chips.forEach((c, i) => {
      c.w = est(c.label, FONT) + 2 * CHIP_PADX;
      if (i > 0 && x + c.w > CARD_W - 2 * PAD) { x = 0; y += CHIP_H + LINE_GAP; lines++; }
      c.x = x; c.y = y;
      x += c.w + CHIP_GAP;
    });
    r.blockH = LABEL_H + 6 + lines * CHIP_H + (lines - 1) * LINE_GAP;
  }
  const CARD_H = PAD + HEADER + rows.reduce((s, r) => s + r.blockH + ROW_GAP, 0) + FOOTER + PAD;

  const el = h("div.sd");
  const svg = canvasSvg();
  el.append(svg);
  layer.append(el);
  const p = new Painter(svg);
  const appear = new Anim(0), hover = new HoverFade();
  let W = layer.offsetWidth, H = layer.offsetHeight, closing = false, gone = false, startPending = true;
  const loop = frameLoop(step);

  const cardRect = () => ({ x: Math.max(4, (W - CARD_W) * 0.5), y: Math.max(4, (H - CARD_H) * 0.5), w: CARD_W, h: CARD_H });
  const doneRect = () => { const c = cardRect(); return { x: c.x + c.w - PAD - BTN_W, y: c.y + c.h - PAD - BTN_H, w: BTN_W, h: BTN_H }; };
  function blockTop(c, ri) { let y = c.y + PAD + HEADER; for (let r = 0; r < ri; ++r) y += rows[r].blockH + ROW_GAP; return y; }
  function chipRect(c, ri, i) { const ch = rows[ri].chips[i]; return { x: c.x + PAD + ch.x, y: blockTop(c, ri) + LABEL_H + 6 + ch.y, w: ch.w, h: CHIP_H }; }
  function hit(pt) {
    if (inRect(pt, doneRect())) return { done: true };
    const c = cardRect();
    for (let r = 0; r < rows.length; ++r) for (let i = 0; i < rows[r].chips.length; ++i) if (inRect(pt, chipRect(c, r, i))) return { row: r, chip: i };
    return null;
  }

  function beginClose() {                            // SettingsDialog::beginClose
    if (closing) return;
    closing = true;
    appear.animateTo(0, tok.ms("motion-modal-close") || 120, Ease.EaseOutCubic, performance.now());
    window.removeEventListener("keydown", onKey, true);
    if (vm.view.dialog.peek() === spec) vm.view.dialog.value = null;   // the shell then calls close(): already closing
    loop.kick();
  }
  function onKey(e) {
    if (closing) return;
    e.stopPropagation();                             // a modal owns the keyboard
    if (e.key === "Escape") { e.preventDefault(); beginClose(); }
  }
  window.addEventListener("keydown", onKey, true);

  function step(now) {
    W = layer.offsetWidth; H = layer.offsetHeight;
    if (startPending) { appear.animateTo(1, tok.ms("motion-modal-open") || 150, Ease.EaseOutCubic, now); startPending = false; }
    let moving = appear.update(now);
    moving = hover.advance(now) || moving;
    paint();
    if (closing && !appear.animating && appear.value <= 0.001) { gone = true; loop.kill(); el.remove(); return false; }
    return moving;
  }

  function paint() {                                 // SettingsDialog::onOverlay
    const a = appear.value;
    p.begin();
    if (a > 0.001) {
      const rc = tok.px("radius-control"), fg = col("foreground"), muted = col("muted-foreground"), border = col("border");
      const primary = col("primary"), pfg = col("primary-foreground"), secondary = col("secondary");
      p.rect(0, 0, W, H, 0, { fill: [0, 0, 0, 0.55 * a] });
      const c = cardRect();
      p.rect(c.x, c.y, c.w, c.h, rc, { fill: fade(col("popover"), a), stroke: fade(border, a), sw: 1 });
      p.text("Settings", c.x + PAD, c.y + PAD + 16, 14, F.semibold, fade(fg, a));
      rows.forEach((r, ri) => {
        const ly = blockTop(c, ri);
        p.text(r.label, c.x + PAD, ly + 12, 11, F.medium, fade(muted, a), 0.06 * 11);
        r.chips.forEach((ch, i) => {
          const b = chipRect(c, ri, i), sel = r.sel === ch.value, da = ch.disabled ? 0.4 : 1;
          if (sel) p.rect(b.x, b.y, b.w, b.h, rc, { fill: fade(primary, a) });
          else p.rect(b.x, b.y, b.w, b.h, rc, { fill: fade(secondary, a * da), stroke: fade(border, a * da), sw: 1 });
          if (!ch.disabled) { const hv = hover.amount(ri * 100 + i) * a; if (hv > 0.001) p.rect(b.x, b.y, b.w, b.h, rc, { fill: hoverWash(hv) }); }
          p.text(ch.label, b.x + (b.w - est(ch.label, FONT)) * 0.5, b.y + b.h * 0.5 + 4, FONT, sel ? F.medium : F.sans, fade(sel ? pfg : fg, a * da));
        });
      });
      const d = doneRect();
      p.rect(d.x, d.y, d.w, d.h, rc, { fill: fade(primary, a) });
      const dhv = hover.amount(DONE) * a;
      if (dhv > 0.001) p.rect(d.x, d.y, d.w, d.h, rc, { fill: hoverWash(dhv) });
      p.text("Done", d.x + (d.w - est("Done", FONT)) * 0.5, d.y + d.h * 0.5 + 4, FONT, F.medium, fade(pfg, a));
    }
    p.end();
  }

  // a modal swallows every pointer; a click outside the card closes (SettingsDialog::handleGesture)
  el.addEventListener("pointermove", (e) => {
    if (closing) return;
    const t = hit(localPoint(el, e));
    hover.setHovered(!t ? -1 : t.done ? DONE : rows[t.row].chips[t.chip].disabled ? -1 : t.row * 100 + t.chip);
    loop.kick();
  });
  el.addEventListener("pointerleave", () => { hover.clear(); loop.kick(); });
  el.addEventListener("pointerdown", (e) => e.preventDefault());
  el.addEventListener("contextmenu", (e) => e.preventDefault());
  el.addEventListener("click", (e) => {
    if (closing) return;
    const pt = localPoint(el, e);
    if (!inRect(pt, cardRect())) { beginClose(); return; }
    const t = hit(pt);
    if (!t) return;                                  // inside the card, no control
    if (t.done) { beginClose(); return; }
    const r = rows[t.row], ch = r.chips[t.chip];
    if (ch.disabled) return;                         // inert (R-SCALE-3, GPU without a backend)
    r.sel = ch.value;                                // the fill snaps, as native
    r.pick(ch.value);
    loop.kick();
  });

  loop.kick();
  return {
    close() { if (!gone) beginClose(); },
  };
}
