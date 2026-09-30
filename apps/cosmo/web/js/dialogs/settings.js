/*
 * Cosmo by arstro — settings.js: SettingsDialog (widgets/SettingsDialog.cpp), over Home or the
 * editor (App.cpp:993-1002, 1281-1294).
 *
 * Card 360; pad 18, header 34, row label 18, chip 24 high, 6 apart, 12 px padding, rows 16 apart,
 * wrapped lines 5 apart, footer 40, Done 92 x 26 (SettingsDialog.cpp:25-37). Height =
 * 18 + 34 + sum(rowBlock + 16) + 40 + 18 = 523 with the scales wrapping to two lines; a window
 * too short for it gets the card at y = 4 with its bottom cut off (native has no scroll).
 * Chips: est(label, 12) + 24 wide, left to right, wrapping when x + w > 324 (never the first
 * chip of a line); selected = accent fill + white Roboto Medium, else secondary + border +
 * foreground Roboto; disabled x 0.4, no hover; hover wash per chip; label centred by est(),
 * baseline chip.y + 16. A click moves the selection at once (the fill snaps, as native).
 *
 * Rows and what they send (spec §9):
 *   Screen scale      THIS page's UI scale (vm.view.uiScale) - per client here, one per screen;
 *                     chips > the largest scale whose 584x466 minimum fits the screen are inert.
 *   Preview quality   settings set previewEdge=1000|1600|2400
 *   CPU threads       settings set threads=0|2|4|8      (0 = Auto, never the resolved count)
 *   CPU limit         settings set cpuPercent=25|50|75|100
 *   GPU acceleration  settings set useGpu=0|1           (On inert without a GPU)
 *   Input             settings set touchUi=0|1, and this page swaps to the touch shell
 * Shared rows follow the model: another client's change moves the chip here too. A click shows
 * at once and the model confirms it (a rejected change falls back to the model's value).
 * Esc = Done (web addition the requirements ask for; the native dialog takes no keys).
 */
import { signal, computed } from "../core/signal.js";
import { bindEffect } from "../core/dom.js";
import { openModal } from "../ui/modal.js";
import { est } from "./paint.js";
import { T, box, button, frame } from "./kit.js";

const CARD_W = 360, PAD = 18, HEADER = 34, LABEL_H = 18, CHIP_H = 24, ROW_GAP = 16, CHIP_GAP = 6, CHIP_PADX = 12;
const FOOTER = 40, BTN_W = 92, BTN_H = 26, FONT = 12, LINE_GAP = 5;
const SCALES = [75, 90, 100, 125, 150, 175, 200];      // core/AppSettings.cpp:25-29
const EDGES = [1000, 1600, 2400], THREADS = [0, 2, 4, 8], CPUS = [25, 50, 75, 100];

/** The largest scale whose minimum logical window (584 x 466) fits this screen (App.cpp:761-766). */
function maxScaleHere() {
  const aw = (screen && screen.availWidth) || 0, ah = (screen && screen.availHeight) || 0;
  if (!aw || !ah) return 200;
  let m = SCALES[0];
  for (const s of SCALES) if (584 * s / 100 <= aw && 466 * s / 100 <= ah) m = s;
  return m;
}

export function open(host, spec) {
  const vm = host.vm;
  const maxScale = maxScaleHere();
  const pending = signal({});                  // row -> value clicked, until the model agrees
  const st = computed(() => vm.settings.value);
  const uiScale = () => (vm.view.uiScale ? vm.view.uiScale.value : st.value.uiScale);

  const rows = [
    { key: "scale", label: "Screen scale  ·  smaller fits more" + (maxScale < 200 ? `, up to ${maxScale}% here` : ""),
      chips: SCALES.map((v) => ({ label: v + "%", value: v, disabled: v > maxScale })), value: () => uiScale(),
      set: (v) => { if (vm.view.uiScale) vm.view.uiScale.value = v; else return vm.settingsSet({ uiScale: v }); }, local: true },
    { key: "previewEdge", label: "Preview quality",
      chips: EDGES.map((v, i) => ({ label: ["Draft", "Standard", "High"][i], value: v })), value: () => st.value.previewEdge,
      set: (v) => vm.settingsSet({ previewEdge: v }) },
    { key: "threads", label: "CPU threads",
      chips: THREADS.map((v) => ({ label: v === 0 ? "Auto" : String(v), value: v })), value: () => st.value.threads,
      set: (v) => vm.settingsSet({ threads: v }) },
    { key: "cpuPercent", label: "CPU limit  ·  Auto uses this",
      chips: CPUS.map((v) => ({ label: v + "%", value: v })), value: () => st.value.cpuPercent,
      set: (v) => vm.settingsSet({ cpuPercent: v }) },
    { key: "useGpu", label: "GPU acceleration" + (st.peek().gpuAvailable ? "" : "  ·  unavailable"),
      chips: [{ label: "Off", value: 0 }, { label: "On", value: 1, disabled: !st.peek().gpuAvailable }],
      value: () => (st.value.useGpu && st.value.gpuAvailable ? 1 : 0), set: (v) => vm.settingsSet({ useGpu: v }) },
    { key: "touchUi", label: "Input  ·  touch keeps your project open",
      chips: [{ label: "Mouse", value: 0 }, { label: "Touch", value: 1 }], value: () => (st.value.touchUi ? 1 : 0),
      set: (v) => {
        const p = vm.settingsSet({ touchUi: v });
        // the web's shell swap (linux_main.cpp:1189 cross-fades the shells, keeping the project)
        if (vm.view.layout) setTimeout(() => { vm.view.layout.value = v ? "touch" : "desktop"; }, 140);
        return p;
      } },
  ];

  // SettingsDialog::layoutChips - relative to the row's chip origin
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
  const hgt = PAD + HEADER + rows.reduce((s, r) => s + r.blockH + ROW_GAP, 0) + FOOTER + PAD;

  let m;
  const done = () => { m.close(); host.closed(spec); };
  m = openModal(host.layer, { width: CARD_W, height: hgt, cls: "dg-settings", onOutside: done, passKeys: true,
    onKey: (e) => { if (e.key === "Escape") { done(); return true; } return false; } });
  const c = m.card;
  frame(c);
  T(c, "Settings", PAD, PAD + 16, 14, "semibold", "dg-fg");
  let top = PAD + HEADER;
  for (const r of rows) {
    const ly = top;
    const lbl = T(c, r.label, PAD, ly + 12, 11, "medium", "dg-muted");
    lbl.style.letterSpacing = 0.06 * 11 + "px";
    const shown = () => {
      const p = pending.value[r.key];
      return p !== undefined ? p : r.value();
    };
    for (const chip of r.chips) {
      const b = button(c, { x: PAD + chip.x, y: ly + LABEL_H + 6 + chip.y, w: chip.w, h: CHIP_H, label: chip.label, px: FONT,
        fam: "sans", base: CHIP_H / 2 + 4, cls: "dg-chip",
        onClick: () => {
          if (chip.disabled || m.closing) return;
          pending.value = { ...pending.peek(), [r.key]: chip.value };
          const res = r.set(chip.value);
          const clear = () => { const p = { ...pending.peek() }; delete p[r.key]; pending.value = p; };
          if (r.local || !res || !res.then) clear();
          else res.then(() => setTimeout(clear, 1500), clear);
        } });
      if (chip.disabled) b.classList.add("disabled");
      bindEffect(b, () => {
        const sel = shown() === chip.value;
        b.classList.toggle("sel", sel);
        b.label.style.fontFamily = sel ? "var(--font-sans-medium)" : "var(--font-sans)";
      });
    }
    // once the model reports the clicked value, the pending entry has done its job
    bindEffect(lbl, () => {
      const p = pending.value[r.key];
      if (p !== undefined && r.value() === p) queueMicrotask(() => {
        const cur = { ...pending.peek() };
        if (cur[r.key] === p) { delete cur[r.key]; pending.value = cur; }
      });
    });
    top += r.blockH + ROW_GAP;
  }
  button(c, { x: CARD_W - PAD - BTN_W, y: hgt - PAD - BTN_H, w: BTN_W, h: BTN_H, label: "Done", kind: "primary", px: FONT,
              base: BTN_H / 2 + 4, onClick: done });
  return m;
}
