# cosmo_v2 ↔ cosmo — feature parity gap

Verified 2026-07-06 by comparing `cosmo/` (original) against `cosmo_v2/`. Each gap is a
requirement to implement or explicitly descope (see [REQUIREMENTS.md](REQUIREMENTS.md)).
"Present" = cosmo_v2 already has an equivalent (possibly a different visual form).

## Missing in cosmo_v2 (present in cosmo)

| # | Feature | cosmo source | Status in cosmo_v2 | Req |
|---|---------|--------------|--------------------|-----|
| 1 | **Mask overlay on the photo** — interactive positioning/resize/paint of the selected local-adjustment mask directly over the image (radial/linear/brush), editing `MaskParams` geometry. | `widgets/MaskOverlay.{h,cpp}` | **Missing.** cosmo_v2 has `MaskPanel` (right-column controls) only — no on-photo overlay. | R-MASK |
| 2 | **Zoom & pan inside the photo** — Ctrl+scroll to magnify about the cursor, drag to pan; preview res scales with zoom. | `CosmoApp::wheel` + `ImageView::zoomAbout/panBy` | **Missing / stubbed.** `App::wheel` has a TODO (App.cpp:343). The `ImageView` capability already exists — just unwired. | R-ZOOM |
| 3 | **Crop overlay on the photo** — drag-to-crop rectangle with corner/edge handles + aspect lock, over the full (uncropped) frame while the Transform/Xform tab is active. | `widgets/CropOverlay.{h,cpp}` | **Partial.** cosmo_v2 `XformPanel` exposes numeric crop (`onCropChange`, cropX/Y/W/H) but there is **no interactive crop box on the photo**. | R-PARITY (crop) |
| 4 | **Preset category picker modal** — choose which categories (tone/color/detail/…) to apply / save / export, instead of all-or-nothing. | `widgets/PresetDialog.{h,cpp}` | **Missing.** cosmo_v2 applies every present category immediately (documented stub in App.h:59-60). | R-PARITY (preset-picker) |
| 5 | **App/engine settings panel** — preview quality (render resolution: speed vs detail) and CPU thread count for the multicore engine. | `panels/SettingsPanel.{h,cpp}` | **Missing.** cosmo_v2's "Settings" menu is only Copy/Paste-settings; no engine-quality/threads UI. | R-PARITY (settings) |
| 6 | **Draggable split divider** in before/after compare — slide the seam to reveal the difference. | `widgets/CompareView.{h,cpp}` | **Partial.** cosmo_v2 `PhotoCanvas` has Before/Split/After but the split seam is **fixed at 50%** (not draggable). | R-PARITY (split-drag) |

## Present in both (no gap)

- Before / Split / After compare (cosmo_v2 `PhotoCanvas`; cosmo `CompareView` — see #6 for the
  one sub-gap).
- Preset browser tree, save/export/import preset, quick-apply (`PresetTree` ↔ `PresetPanel`/`PresetBar`).
- Workspace save/load, group tree + group offsets, batch open.
- Git-tree History view, undo/redo.
- Mixer, Curve/Tone, Color Grade, Xform (numeric), Histogram panels.
- Filmstrip, breadcrumb, context menus, menu bar.

## Notes

- Items **1 and 2** are the user's explicit asks (R-MASK, R-ZOOM) and are implemented first.
- Items **3–6** are the remaining parity backlog; none is descoped yet — each needs an
  implement-or-descope decision.
</content>
