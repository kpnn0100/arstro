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
| 3 | **Crop overlay on the photo** — drag-to-crop rectangle with corner/edge handles + aspect lock, over the full (uncropped) frame while the Transform/Xform tab is active. | `widgets/CropOverlay.{h,cpp}` | **Partial.** `XformPanel` exposes numeric crop (`onCropChange`, cropX/Y/W/H) but there is **no interactive crop box on the photo**. | R-PARITY (crop) |
| 4 | **Preset category picker modal** — choose which categories (tone/color/detail/…) to apply / save / export, instead of all-or-nothing. | `widgets/PresetDialog.{h,cpp}` | **Missing.** cosmo applies every present category immediately (documented stub in `App.h`). | R-PARITY (preset-picker) |
| 5 | **App/engine settings panel** — preview quality (render resolution: speed vs detail) and CPU thread count for the multicore engine. | `panels/SettingsPanel.{h,cpp}` | **Missing.** cosmo's "Settings" menu is only Copy/Paste-settings; no engine-quality/threads UI. | R-PARITY (settings) |
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
