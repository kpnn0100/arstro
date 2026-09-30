# cosmo — parity backlog (carried over from the original cosmo)

This app began as `cosmo_v2` next to an original `apps/cosmo/` GUI. It has since fully replaced that
app and been renamed to `cosmo`; the original was **removed**. This file is the historical
backlog of features from the original that were not yet ported — each is a requirement to
implement or explicitly descope (see [REQUIREMENTS.md](REQUIREMENTS.md)). The "Original source"
column points at files in the now-removed app, kept for provenance.

## Not yet ported from the original

| # | Feature | Original source | Status | Req |
|---|---------|-----------------|--------|-----|
| 1 | **Mask overlay on the photo** — interactive positioning/resize/paint of the selected local-adjustment mask directly over the image (radial/linear/brush), editing `MaskParams` geometry. | `widgets/MaskOverlay.{h,cpp}` | **Done** — ported (R-MASK). | R-MASK |
| 2 | **Zoom & pan inside the photo** — Ctrl+scroll to magnify about the cursor, drag to pan; preview res scales with zoom. | `CosmoApp::wheel` + `ImageView::zoomAbout/panBy` | **Done** (R-ZOOM). | R-ZOOM |
| 3 | **Crop overlay on the photo** — drag-to-crop rectangle with corner/edge handles + aspect lock, over the full (uncropped) frame while the Transform/Xform tab is active. | `widgets/CropOverlay.{h,cpp}` | **Done** (R-CROP-5). Corner + edge handles, drag-inside to move the region, dimmed discard area, thirds grid, ratio lock maintained on resize and ignored on move, rendered over the UNCROPPED frame (`setCropPreviewMode`, which existed and had never been called). | R-CROP |
| 4 | **Preset category picker modal** — choose which categories (tone/color/detail/…) to apply / save / export, instead of all-or-nothing. | `widgets/PresetDialog.{h,cpp}` | **Missing.** cosmo applies every present category immediately (documented stub in `App.h`). | R-PARITY (preset-picker) |
| 5 | **App/engine settings panel** — preview quality (render resolution: speed vs detail) and CPU thread count for the multicore engine. | `panels/SettingsPanel.{h,cpp}` | **Done** (R-SETTINGS). `SettingsDialog` carries UI scale, preview quality, engine threads, CPU limit, GPU and touch rows. **This row was stale** — it still said "Missing" after the dialog shipped; corrected 2026-08-27 during the release audit. | R-SETTINGS |
| 6 | **Draggable split divider** in before/after compare — slide the seam to reveal the difference. | `widgets/CompareView.{h,cpp}` | **Done** (R-VIEW-3). Grab radius = `metrics::anchorHitRadius()`, moves by the drag not to the pointer, clamped inside the canvas, brightens + thickens on hover, double-click re-centres eased. | R-VIEW-3 |

## Already at parity (were present in both)

- Before / Split / After compare (`PhotoCanvas`; original `CompareView` — see #6 for the one sub-gap).
- Preset browser tree, save/export/import preset, quick-apply (`PresetTree`).
- Workspace save/load, group tree + group offsets, batch open.
- Git-tree History view, undo/redo (now also persisted with the project).
- Mixer, Curve/Tone, Color Grade, Xform (numeric), Histogram panels.
- Filmstrip, breadcrumb, context menus, menu bar.

## Notes

- Items **1 and 2** were the user's explicit asks (R-MASK, R-ZOOM) and are done.
- Items **3–6** are the remaining backlog; none is descoped yet — each needs an
  implement-or-descope decision.

## The web front end (R-NTWB) vs the GTK window

Everything below is reachable from the browser *today* through the command grammar (the Info
tab's event lines show it, `arstro-remote apps call cosmo command '{"line": ...}'` drives it) -
what is missing is the **panel** that draws it. Each row is a design task
(`arstro.cosmo.design.implement`); none needs a core change, because R-NTWB-1 gives the browser
the same commands the window sends.

| # | Surface | Window | Web (2026-09-30) | Status | Req |
|---|---------|--------|------------------|--------|-----|
| W1 | Basic/Detail sliders, WB pick, tone/colour/presence/effects/sharpen/NR/lens | RightColumn | same catalogue (EditControls.h) | **Done** | R-NTWB-4/5 |
| W2 | Project tree, bypass, group, filmstrip, breadcrumb, histogram | LeftRail, Filmstrip, Breadcrumb, HistogramWidget | yes | **Done** | R-NTWB-5 |
| W3 | Home, recents, open, import, save, export, settings | HomeScreen, dialogs | yes (a folder picker over `browse`) | **Done** | R-NTWB-5/6 |
| W4 | Tone curve (master + RGB) | CurvePanel | - (`set curve=...` works) | Missing | R-NTWB-5 |
| W5 | Colour mixer + hue remap | MixerPanel, HueCurveEditor | - (`set mixer0=...`) | Missing | R-NTWB-5 |
| W6 | Colour grading wheels | GradePanel | - (`set grade0=...`) | Missing | R-NTWB-5 |
| W7 | Masks panel + on-photo overlay | MaskPanel, MaskOverlay | - (`set mask=`, `mask set`) | Missing | R-NTWB-5 |
| W8 | Crop overlay, rotate, quarter turns | CropOverlay | - (`set crop=`, `rotation=`) | Missing | R-NTWB-5 |
| W9 | Presets tree (apply / save) | PresetTree, PresetDialog | - (`preset apply "..."`); needs a preset list in the model | Missing (core: list) | R-NTWB-3 |
| W10 | History as a branching tree | HistoryView | step count + label only | Partial | R-NTWB-5 |
| W11 | Zoom / pan / before-after split on the photo | PhotoCanvas | fit only | Missing | R-NTWB-5 |
| W12 | Group rename, delete, context menu | ContextMenu | - (`group rename`, `delete`) | Missing | R-NTWB-5 |
| W13 | RightColumn reads its sliders from EditControls.h (one catalogue) | own copy | - | Task NTWB-T3 | R-NTWB-4 |
