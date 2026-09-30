/*
 * Cosmo by arstro — preset.js: PresetDialog, the category picker (widgets/PresetDialog.cpp), and
 * the preset flows it fronts (App.cpp:1234-1262).
 *
 * vm.view.dialog = {name: "preset", mode: "save" | "export" | "import"}.
 * Card 340; pad 18, header 34, rows 26, section gap 8, footer 40, buttons 92 x 26, checkbox 15
 * (radius 3), font 12; h = 152 + 26 n. "Select all" row (Roboto Medium) at (18, 52), a hairline at
 * 82, the category rows from 86 (Roboto); box at (row.x, row.y + 5.5), label at (row.x + 25,
 * row.y + 17), foreground when ticked, muted when not; hover wash (x - 4, y, w + 8, 26). Cancel at
 * (c.right - 210, c.bottom - 44), the confirm button at (c.right - 110, c.bottom - 44).
 * All categories start ticked; Select all ticks all when any is unticked, else unticks all.
 *
 *   save    "Save preset" / Continue -> the name prompt ("preset name", Enter = Save)
 *           -> `preset save "<name>"`. The command cannot carry categories [CORE GAP G8]: the
 *           rows show what is saved - every category - ticked and inert, faded by
 *           kDisabledFade, with the reason on hover.
 *   export  "Export preset" / Continue  - no .apf command in the core [CORE GAP G9]: the rows
 *           are inert and Continue is disabled, with the reason.
 *   import  there is no .apf import to pick from [CORE GAP G9]: a ConfirmDialog says so.
 */
import { h } from "../core/dom.js";
import { openModal } from "../ui/modal.js";
import { svg, at } from "./paint.js";
import { T, box, button, frame, checkbox } from "./kit.js";
import { prompt, open as openConfirm } from "./confirm.js";

const W = 340, PAD = 18, HEADER = 34, ROW = 26, GAP = 8, FOOTER = 40, BTN_W = 92, BTN_H = 26, BOX = 15, FONT = 12;

// core/PresetLibrary.cpp:62-76, in apfImageCategories() order
export const CATEGORIES = [
  ["basic", "Basic (exposure, contrast, tone)"], ["color", "Color (white balance, vibrance)"],
  ["presence", "Presence (texture, clarity, dehaze)"], ["effects", "Effects (grain)"],
  ["detail", "Detail (sharpen, noise reduction)"], ["lens", "Lens corrections"], ["curve", "Tone curve"],
  ["mixer", "Color mixer"], ["grade", "Color grading"], ["transform", "Crop and rotate"], ["masks", "Masks (local adjustments)"],
];
const CAN_PICK = false;           // G8: `preset save` has no --categories
const G8 = "Every category is saved: the core cannot pick categories yet";
const G9 = "Presets cannot be exported or imported as .apf files from here yet (no core command)";

export function open(host, spec) {
  const vm = host.vm;
  const mode = spec.mode || "save";
  if (mode === "import") {
    return openConfirm(host, { ...spec, title: "Import preset", message: "Importing a .apf preset is not available here yet.",
                               buttons: [{ label: "OK", primary: true }] });
  }
  const title = mode === "export" ? "Export preset" : "Save preset";
  const ok = "Continue";
  const rows = CATEGORIES.map(([key, label]) => ({ key, label, on: true }));
  const hgt = PAD + HEADER + ROW + GAP + rows.length * ROW + GAP + FOOTER + PAD;
  let md;
  const close = () => { if (!md.closing) { md.close(); host.closed(spec); } };
  md = openModal(host.layer, { width: W, height: hgt, cls: "dg-preset", onOutside: close, onKey: () => false, passKeys: true });
  const c = md.card;
  frame(c);
  T(c, title, PAD, PAD + 16, 14, "semibold", "dg-fg");
  const inert = mode === "export" || !CAN_PICK;
  const why = mode === "export" ? G9 : G8;

  const rowEl = (y, label, bold, onClick) => {
    const r = h("button.dg-prow" + (inert ? ".off" : ""), { title: inert ? why : null,
      style: { left: PAD + "px", top: y + "px", width: W - 2 * PAD + "px", height: ROW + "px" } });
    box(r, "dg-wash", -4, 0, W - 2 * PAD + 8, ROW);
    r.check = checkbox(r, 0, (ROW - BOX) / 2, BOX, 3);
    T(r, label, BOX + 10, ROW / 2 + 4, FONT, bold ? "medium" : "sans");
    r.addEventListener("click", (e) => { e.stopPropagation(); if (!inert) onClick(); });
    c.append(r);
    return r;
  };
  const all = rowEl(PAD + HEADER, "Select all", true, () => {
    const want = !rows.every((x) => x.on);
    rows.forEach((x) => { x.on = want; });
    sync();
  });
  const hair = svg("svg", { class: "dg-hair", width: W - 2 * PAD, height: 1 });
  hair.append(svg("line", { x1: 0, y1: 0, x2: W - 2 * PAD, y2: 0 }));
  c.append(at(hair, PAD, PAD + HEADER + ROW + GAP / 2));
  rows.forEach((r, i) => { r.el = rowEl(PAD + HEADER + ROW + GAP + i * ROW, r.label, false, () => { r.on = !r.on; sync(); }); });
  function sync() {
    const allOn = rows.every((x) => x.on);
    all.check.set(allOn ? 1 : 0); all.classList.toggle("on", allOn);
    for (const r of rows) { r.el.check.set(r.on ? 1 : 0); r.el.classList.toggle("on", r.on); }
    okBtn.classList.toggle("disabled", mode === "export");
  }
  button(c, { x: W - PAD - 2 * BTN_W - 8, y: hgt - PAD - BTN_H, w: BTN_W, h: BTN_H, label: "Cancel", px: FONT, base: BTN_H / 2 + 4, onClick: close });
  const okBtn = button(c, { x: W - PAD - BTN_W, y: hgt - PAD - BTN_H, w: BTN_W, h: BTN_H, label: ok, kind: "primary", px: FONT,
    base: BTN_H / 2 + 4, onClick: async () => {
      if (mode === "export") return;
      close();
      const name = await prompt(vm, { title: "Save preset", placeholder: "preset name", ok: "Save" });
      if (name) vm.savePreset(name).catch(() => {});
    } });
  if (mode === "export") okBtn.title = G9;
  sync();
  return md;
}
