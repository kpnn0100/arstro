# Interstellar front end — as built

`apps/interstellar/app/` is Interstellar's UI: a **platform-free Artboard Segment tree** (the `App`
plus its widgets), drawn ONLY from `AppModel` and reporting intent ONLY as **text command lines**
(project-format §8) through `AppHooks`. It links neither `cosmo_core` nor `interstellar_core` —
the service does not exist in this build; the integrator wires the hooks to it. Cosmo's tokens are
**aliased** and cosmo's panels are **compiled from `apps/cosmo/widgets/*.cpp`**, not copied; the
one divergence is the accent, moved to `#CF5AED` at runtime through cosmo's `palette::setAccent`.

- Static library **`interstellar_app`** (App + widgets + the reused cosmo widget TUs + cosmo's
  `Theme.cpp`, `EmbeddedFonts.cpp`, the generated font array and `CairoTarget.cpp`).
- **`interstellar_app_shots`** — headless PNG per named state (`--outdir --only --size --tree --check --list`).
- **`interstellar_app_ui_tests`** — 134 assertions over the assembled App on a fake `AppHooks`.

## Build

```sh
cmake -S apps/interstellar/app -B <dir> -DCMAKE_BUILD_TYPE=Release
cmake --build <dir> -j
ctest --test-dir <dir>                         # interstellar_app_shots --check + interstellar_app_ui
<dir>/interstellar_app_shots --outdir shots    # 31 states x {1440x900, 1024x640} = 62 PNGs
```

Standalone it `add_subdirectory`s `core/Artboard` and `core/ImageProcessing` (EXCLUDE_FROM_ALL;
`CTestCustom.cmake` makes ctest skip their own suites). From a parent it reuses existing
`artboard_core` / `arstro_image` targets (`if(NOT TARGET …)`), and `project()`/`enable_testing()`
are guarded — verified by a scratch parent that defines both targets first. Deps: pkg-config
`cairo`, `freetype2`; `ARTBOARD_CAIRO_FT` is a PUBLIC define. `arstro_image` is linked PUBLIC
(EditParams, `formatCurvePoints`, `Histogram::toLog` behind the reused HistogramWidget) so its
PUBLIC defines match the rest of the final binary.

## Integration status (2026-10-01, by the integrator)

- **Built into the umbrella** (`apps/interstellar/CMakeLists.txt` → `add_subdirectory(app)`), and
  bound to the real service by the GTK host `apps/interstellar/linux_main.cpp` (binary
  `interstellar`) exactly as below. `interstellar_live` (ctest) renders this app over the REAL
  service on a real project — Home, Grade, Cut, Deliver — and fails unless the project reaches Edit,
  the rack lists its sources and the monitor received a composited frame.
- **Changed at integration:** the new-version prompt sends `cmd::bindName(typed)` — timeline names
  are bind names, so "Festival cut" goes out as `Festival_cut` instead of being refused; the OVR
  badge is clickable (`revert <bind>`). `clip trim --in/--out` were confirmed as SOURCE points on the
  service side (it had read them as timeline edges; the service was fixed, not this app).
- Remaining contract requests are open follow-ups in `docs/PROGRESS.md`.

## Menus, settings and screen scale (added by the integrator, 2026-10-01)

- **Menu bar** — cosmo's `MenuStrip` in `EditTopBar`, after the wordmark; filled by
  `App::buildMenus`:

  | menu | item → command / picker |
  |---|---|
  | File | Home → `project close` (behind the save prompt) · Open… → `onPickProjectToOpen` · Save → `project save` · Save As… → `onPickSaveAs` → `project save <path>` · Add Footage… → `onPickFootage` · Export Still… → `onPickStillToExport` → `export-still --timeline <cur> --out <p> --at <playhead>` · Render… → Deliver tab |
  | Edit | Undo → `undo` · Redo → `redo` · Copy Grade → `grade copy <sel>` · Paste Grade to Selected → `grade paste <sel>` · to All → `grade paste --all` · Group Selected… (name prompt) → `rack group new <name> --nodes <sel>` · Ungroup → `rack ungroup <sel>` · Duplicate as Variant → `rack duplicate <sel>` |
  | Settings | Engine Settings… → cosmo's `SettingsDialog` (each chip → `settings set <key>=<v>`) |
  | Workspace | Grade / Cut / Deliver → the tab · Reset Workspace → Grade tab + timeline zoom-to-fit (view only) |
  | Preset | Save Preset… (name prompt) → `preset save <name> --node <sel>` · Import Preset… → `onPickPresetToImport` → `preset import <path>` · Apply <name> (one per library preset) → `preset apply <name> --node <sel>` |

  Accelerators (`App::editKey`): Ctrl+Z `undo`, Ctrl+Shift+Z / Ctrl+Y `redo`, Ctrl+S save,
  Ctrl+Shift+S Save As, Ctrl+O Open, Ctrl+C / Ctrl+V copy/paste the Grade target's grade (Grade tab).
- **Settings** — cosmo's `SettingsDialog` replaces this app's former one-row dialog (Reduce motion
  is the OS setting now, as in cosmo). Its Input row is hidden.
- **Screen scale** — `App` follows `settings.uiScale`, eased; `minPhysicalWidth/Height` for the host.
- **Monitor** — with nothing cut at the playhead and a Grade target, the monitor asks for a frame
  (the service answers with the target's reference frame, graded).
- New shots: `edit_menu_file`, `edit_menu_file_mid`, `edit_menu_edit`, `edit_menu_preset`,
  `edit_settings`, `edit_scale_125` — looked at; UI checks now 151.

## Thumbnails are asynchronous (2026-10-02, D-5/D-6)

`AppHooks::thumbnailEpoch` (optional): the host raises it when a still it could not give at once has
landed; `App::render` then calls `HomeScreen::thumbnailsArrived` / `GradeDeck::thumbnailsArrived` and
re-binds, so missing covers, cells and strip frames are asked for again. Late strip frames and covers
fade in (`kScrollMs`). Filmstrip cells keep cosmo's own behaviour (a thumbnail replaces the plate).

## Selection and the right-click menu (2026-10-02, R-RACK-8 / R-UI-9)

- Rack tree and filmstrip: Shift-click → `rack select <b> --range`, Ctrl-click → `rack select <b>
  --add`, plain click → `rack select <b>`; selected rows carry an eased wash, the filmstrip rings them.
- Right-click (rack row or filmstrip cell) → `App::openRackContext` → cosmo's `ContextMenu`:
  Add Footage… · Group (Selection) → `rack group new` · Ungroup → `rack ungroup <b>` · Enable/Disable
  Filter → `set <b>.bypass=0|1` · Rename… (cosmo's inline rename) → `rack rename <b> <new>` ·
  Duplicate as Variant · Copy Grade → `grade copy <b>` · Paste Grade (to Selection) → `grade paste
  <selected…>` · Remove from Rack → `rack remove <b>` (only for an unused source).
- Ctrl+G → `rack group new`; Edit › Group Selection is the same.
- The weight bar names itself on hover: "weight 80%" cross-fades over the bind name.
- Shots added: `grade_multiselect`, `grade_context_menu`, `grade_weight_caption`. UI checks: 158.

## Grade has no transport; the capture button (2026-10-02, R-UI-3 amended / R-UI-11)

- Grade's monitor shows the Grade target ALONE, graded, at its reference frame — through the optional
  hook `AppHooks::renderSource(bind, t, edge, out)` (`t < 0` = its own reference frame; the host binds
  it to `InterstellarService::renderSourceFrame`). Without the hook, or with a group/no target, the
  monitor falls back to the timeline at the playhead. The caption reads `<bind> · ref <timecode>` at
  the source's own rate (`rack[].mediaFps`).
- The transport eases out on Grade and in on Cut/Deliver (`EditScreen::transportAmount()`, one
  180 ms value); the monitor's height is laid out from the same live value.
- Capture: the transport's 4th button (beside ▶▶) and, in Grade, an 18 px button left of the monitor
  caption (fades with Grade) open cosmo's `ContextMenu`: **Copy Frame** (only if the host binds the
  optional `AppHooks::copyFrame(bind, err)`; GTK puts a `GdkPixbuf` on the CLIPBOARD selection) and
  **Save Frame…** (`App::onPickFrameToSave` → host dialog → `App::frameSavePicked(path)` →
  `capture --out <path> [--source <Grade target>]`). A good copy says so in the toast chip with a ✓
  (`EditScreen::showNotice`); a failed one is a refusal.
- Shots added: `grade_capture_menu`, `cut_capture_menu`, `grade_frame_copied`. UI checks: 183.

## Integrating (the GTK host)

```cpp
arstro::cosmo_v2::registerEmbeddedFonts();          // once, before the first frame (as cosmo's linux_main)
artboard::setReducedMotion(!gtk_enable_animations); // the OS setting, once (design rule §2.6)
arstro::interstellar_v1::AppHooks h;                // model / dispatch / renderFrame [/ thumbnail]
arstro::interstellar_v1::App app(h, w, h);          // installs the accent FIRST, before any widget
app.onPickProjectToOpen   = [&]{ /* dialog */ app.openProjectPicked(path); };
app.onPickProjectToCreate = [&]{ /* dialog */ app.newProjectPicked(path); };
app.onPickFootage         = [&]{ /* dialog */ app.footagePicked(paths); };
// draw:   app.render(cairoTarget /* ONE persistent CairoTarget, re-bound per frame */, nowMs);
// input:  app.pointer(kind /*0 down, 2 up, else move*/, x, y, button, ms, alt, shift, ctrl);
//         app.wheel(x, y, notches /* + = up */, ctrl);  app.key(KeyEvent /* DOM-style codes */);
// size:   app.setSize(w, h);  window minimum = App::minWidth() x App::minHeight() (918 x 560)
// idle:   app.needsRedraw(nowMs) — conservative; never truncates a tween
```

## As-built structure

| file | what it is |
|---|---|
| `Theme.h` | `namespace palette/radius/font/metrics = cosmo_v2::…` aliases + `sharedTheme`; `installInterstellarAccent()`; the 3.25 px ladder (`space::`), the motion table (`motion::`, cosmo's durations named once), Interstellar-only **named** surfaces (`surface::deckBg/rulerBg/trackHeaderBg/laneBg/clipVideo/clipAudio/thumbPlaceholder/skeleton/scrim/marker`, `playhead() = destructive` — a documented domain adaptation), shell metrics (`shell::topBarH/rightW/leftW/transportH/headerWidth/rulerH/trackH/deckH(H)/wheelNotchPx`). Forks nothing. |
| `AppHooks.h` | the contract: `model`, `dispatch`, `renderFrame` + optional `thumbnail` (see deviations). |
| `App.{h,cpp}` | screen state machine (Home/Loading/Edit follow `model().screen`, 260 ms cross-fade), revision-guarded bind once per frame, frame fetch through `renderFrame` (monitor state, dissolve rule, histogram), input routing, keys, host pickers, refusal toast. |
| `widgets/HomeScreen` | cosmo's HomeScreen rhythm (300 sidebar, 32/16 grid, 220 min card); wordmark **sized to fit** with the dot at the measured end; eased reflow; skeletons; empty sentence; covers via `thumbnail`. |
| `widgets/LoadingView` | Screen::Loading: name, sentence, indeterminate ping-pong sweep (1400 ms period). |
| `widgets/EditScreen` | composition: top bar, **monitor + transport as siblings of the tab pages**, three `FadePage`s, toast, cosmo `ConfirmDialog`, `NamePrompt`. Hand-written `layout()` every frame; one deck height and fixed column widths for all tabs so the monitor never moves. |
| `widgets/EditTopBar` | wordmark (→ Home), ellipsized project name + eased dirty dot, `TabSwitcher`, `VersionSwitcher`, save (cosmo `IconButton`, colour eased from the dirty amount). |
| `widgets/TabSwitcher` | [Grade·Cut·Deliver], cosmo's segmented look; travelling highlight (220 ms), `highlightPos()` public. |
| `widgets/VersionSwitcher` | `‹ name ▾ ›` chrome with lock + `@commit`/`frozen` (tag degrades before the name); eased dropdown placed against the root, capped, list scrolled, actions pinned; chrome label cross-fades on a version switch. |
| `widgets/Monitor` | the one picture; registers with the persistent target and releases the id it replaces; content-change dissolve (160 ms linear), playback/scrub hard steps; frame / "decoding · 720p proxy" / "no clip at the playhead" cross-fade; timecode + caption chips. |
| `widgets/Transport` | prev-cut / play-pause (cross-fading glyph) / next-cut, mono timecode, direct-manipulation scrubber with marker ticks; model jumps ease (220 ms). |
| `widgets/RackTree` | Grade left: two-line rows by depth — glyph, name, OVR badge, bind name (mono), weight bar (drag), uses count, bypass toggle; pending/offline/bypass/override **eased per row**; scroll; empty state. |
| `widgets/GradeInspector` | Grade right: **cosmo's** `HistogramWidget` + `EditStackTabs` + `ParamPanel` / `StackPanel`(`MixerPanel`+`CurvePanel`) / `GradePanel` / `XformPanel`, wired as RightColumn wires them, ending in `set` lines. Empty / bypassed / node-switch overlays. |
| `widgets/GradeDeck` | Grade deck: **cosmo's** `Filmstrip` of rack nodes (+ OFFLINE chips) and the reference-frame **strip of frames** under it. |
| `widgets/SourceBin` | Cut left: sources only; highlighted (eased) when the timeline is empty. |
| `widgets/Timeline` | Cut deck: one time origin (`shell::headerWidth()`), provenance drawing, transitions, markers, playhead, eased zoom (`ppsLive()`), drag/snap/trim, eased clip/track/look changes. |
| `widgets/ClipInspector` | Cut right: clip identity/placement/mix in mono, Split / Delete (cosmo `PillButton`s). |
| `widgets/ChecksPanel` | Deliver left: dangling deltas, offline/decoding sources, failed renders, pinned/frozen info. |
| `widgets/OutputSpec` | Deliver right: timeline picker (named, never implied), format (cosmo `SegmentedControl`), path (TextBox, defaulted), Render. |
| `widgets/RenderQueue` | Deliver deck: rows NAME their timeline; eased progress + derived fps rate; state cross-fades. |
| `widgets/NamePrompt`, `SettingsDialog` | cosmo modal skeleton; Settings holds "Reduce motion" (→ `artboard::setReducedMotion`). |
| `widgets/FadePage`, `EasedScroll`, `AnimatedRows`, `ImageSlot`, `TextFit`, `Glyphs`, `CommandLine` | shared mechanics: opacity tab page whose hit-testing follows intent; R6 scroll in one place; keyed list insert/remove/reorder that travels; per-target image registration; measured ellipsize/fit; the line glyphs cosmo's `icon::` lacks; command formatting (precision 7, frame-quantised times, quoting, grade addresses, timecode). |
| `tests/FakeService.h`, `tests/Rig.h` | the fake model + recording dispatch + gradient frames; the Cairo rig shared by both harnesses. |

### Reused from cosmo (compiled, unchanged)
`Theme.cpp`, `EmbeddedFonts.cpp` + `cmake/embed_fonts.cmake`, and widgets `EditStackTabs`,
`ParamPanel`, `SliderRow`, `MixerPanel`, `HueCurveEditor`, `CurvePanel`, `GradePanel`,
`XformPanel`, `HistogramWidget`, `Filmstrip`, `SegmentedControl`, `PillButton`, `IconButton`,
`ConfirmDialog`, `Icons`, `WidgetLog`; headers `HoverFade`, `StackPanel`, `SectionHeader`,
`UnitConversions`. **None pulls in cosmo_core or interstellar_core.** Not reused, and why:
`RightColumn` (built over `cosmo::CosmoService`/`cosmo::Command` — cosmo_core; its wiring is
replicated in `GradeInspector`), `EditCommands` (includes `core/service/Command.h` — its
formatting rules are mirrored in `CommandLine`, the point lists use the same
`arstro::formatCurvePoints`), `HomeScreen`/`TopBar` (cosmo's launcher and menu chrome, not library
widgets), `SegmentedControl` for the main tabs (its eased position is private and a test must read
it — `TabSwitcher` is the same look with `highlightPos()` public), `MaskPanel` (see gaps).

## The exact command lines each control dispatches

| control | line |
|---|---|
| Home card | `project open <path>` |
| Home New project / Open project… (via host picker) | `project new <path>` / `project open <path>` (quoted if it holds a space) |
| wordmark (Edit) | `project close`; with unsaved edits, cosmo's ConfirmDialog → Save: `project save` then `project close`; Don't save: `project close` |
| save button | `project save` |
| rack `+` / empty "Add footage…" (via host picker) | `rack add <path> <path>…` |
| rack row, filmstrip cell, source-bin row | `rack select <bind>` |
| rack bypass toggle | `set <bind>.bypass=1` / `=0` |
| rack weight bar (drag, 0.01 steps) | `set <bind>.weight=<0..1>` |
| reference-frame strip (on release) | `rack frame <bind> --at <t>` (frame-quantised) |
| Basic/Detail sliders | `set <bind>.basic.<exposure·contrast·highlights·shadows·whites·blacks·temp·tint·vibrance·saturation·texture·clarity·dehaze·grainAmount·grainSize>=<v>` and `set <bind>.detail.<sharpenAmount·sharpenRadius·sharpenMasking·nrLuminance·nrColor·lensDistortion·lensCA·lensVignette>=<v>` — engine units via cosmo's `toEv`, `toKelvin`, `toTint`, `toRadiusPx` |
| Mixer curves | `set <bind>.mixer.mixer0|1|2=<x,y;…>` |
| Tone curve | `set <bind>.curve.curve|curveR|curveG|curveB=<x,y;…>` |
| Grade wheels / balance / remap | `set <bind>.grade.grade0|1|2=h,s,l`, `set <bind>.grade.balance=<v>`, `set <bind>.grade.remapEnable=0|1`, `set <bind>.grade.remapSrc=… <bind>.grade.remapRange=… <bind>.grade.remapDst=… <bind>.grade.remapStrength=<0..1>` (one line) |
| Xform | `set <bind>.xform.rotation=<deg>`, `…quarterTurns=<0..3>`, `…crop=x,y,w,h` |
| version chrome ‹ / › / a row | `timeline open <tl>` |
| version actions | `timeline new <name> --base <current>` (via NamePrompt), `timeline pin|unpin <tl>`, `timeline freeze|thaw <tl>`, `timeline rebase <tl>` (Pin/Freeze/Rebase disabled on a root) |
| transport | `play`, `pause`, `playhead prev-cut`, `playhead next-cut`, scrub: `playhead <t>` per frame crossed |
| capture button (transport, or Grade's monitor caption) → Save Frame… (via host picker) | `capture --out <path.png> [--source <Grade target>]`; Copy Frame goes through the `copyFrame` hook, not a line |
| timeline clip click | `clip select <clip>` |
| timeline clip drag | `clip move <clip> --at <snapped t> [--track <trk>]` |
| timeline edge drag | `clip trim <clip> --in <t>` / `--out <t>` (source time) |
| ruler click/drag, empty-lane click | `playhead <t>` |
| inspector Split / Delete; keys `S`, `Delete`/`Backspace` (Cut tab) | `clip split <clip> --at <playhead>`, `clip delete <clip>` |
| keys Space / ← → | `play`·`pause`; `playhead <t ∓ 1 frame>` (keys `1 2 3` switch tabs — presentation) |
| Render | `render --timeline <tl> --out <path> --format h264|prores|png-seq` |

Numbers are `precision(7)` (EditParamsIO's); times are quantised to the project's frame grid;
arguments containing whitespace or quotes are double-quoted with `\"` escapes.

## Decisions

- **Layout constants**: left column 234 (`u(72)`), right column **324** (cosmo's RightColumn width,
  so the reused panels sit at their design width), top bar 29.25, transport 39, deck
  `clamp(0.30·H, 176, 264)` — **one** deck height and one set of column widths for all three tabs,
  so the monitor is pixel-identical across a tab switch (tested). Minimum logical window 918×560,
  derived (`App::minWidth()` = columns + the monitor's 360 floor).
- **Frame selector placement**: the ui-brief puts it "under a video source's cell" — it lives in the
  Grade deck under cosmo's filmstrip, as a strip of frames across the source (thumbnails through the
  hook), not inside the rack-tree row. The rack row shows the bind name / weight / uses instead.
- **Grade address**: filter = cosmo's own Basic/Detail split for scalars; `mixer`, `curve`, `grade`,
  `xform` as named. One `set` line may carry several addresses (remap's four fields).
- **Monitor state** is derived from the model: no non-audio clip under the playhead → "no clip at
  the playhead"; a clip but `renderFrame` fails → "decoding · <proxy>"; else the frame. Proxy edge =
  the monitor's long edge rounded up to 640/960/1280/1920/3840. A new picture **at the same time**
  (grade, version) dissolves; a new **time** (playback, scrub, jump) steps — gotcha 10.
- **Histogram**: cosmo's HistogramWidget fed from the monitor frame the view already holds
  (256-bin, display-referred, sampled). The service has no histogram field (contract request).
- **Home loading** = `model().revision == 0` (nothing published yet) → skeleton cards at the real
  card geometry; recents newest first.
- **Timeline**: snap targets = 0, the model playhead, markers, every other clip's in/out edge, 8 px
  pull at the live zoom; a video clip may change to another *video* track only; a refused drop
  (`dispatch` false) eases the clip back. Clip anims are keyed by id **and look**
  (provenance/offline), so a restyle cross-fades in place. Wheel: plain = pan time (bubbles when
  there is nothing to pan), over headers = tracks, Ctrl = zoom about the pointer.
- **Refused / error state**: a `dispatch` that returns false shows its error over the monitor in a
  destructive toast (150 ms in, 2.6 s hold, 300 ms out); nothing is dropped silently.
- **Reduced motion**: every tween goes through `animateTo`/`HoverFade`, so `setReducedMotion`
  collapses them; the Settings dialog makes it reachable; the host should seed it from the OS.
- **Fonts** are cosmo's five embedded faces; every `drawText` names a family; every string placed
  against something else is measured (`measureText`) and ellipsized against the room left.

## Deviations from the brief (with reasons)

1. **`AppHooks::thumbnail` (optional 4th hook)** — the model carries no pixels, so Home covers
   (`RecentModel::coverPath`) and rack-source cells / the frame strip had nowhere to come from.
   Optional and null-safe: unset, they draw honest placeholder plates. Called once per (path, t) and
   cached; the host should answer from a cache/proxy (a `host/Thumbnailer` is appearing in the tree).
2. **`rack frame <node> --at <t>`** and **`playhead prev-cut|next-cut`** are dispatched although not in
   the brief's command list — both are in project-format §8, and the brief asks for a frame selector
   and ◀◀ ▶▶.
3. **No Mask tab** in the Grade inspector: the brief's filter list has no `mask`, cosmo's
   `MaskPanel` edits by index (`mask set <i>`, cosmo_core's `Command::MaskSet`) and is unusable
   without its on-photo `MaskOverlay`. The white-balance eyedropper is dropped for the same reason
   (it samples a click on the photo).
4. **Settings** holds only "Reduce motion": neither the model nor §8 has preferences.

## Contract requests (fields wanted in AppModel — worked around, not edited)

| wanted | why / current workaround |
|---|---|
| `loadDone`, `loadTotal`, `loadStage` | an honest determinate progress on Screen::Loading — today an indeterminate sweep |
| `RackNodeModel::mediaDuration` | the frame selector's range — today `max(clip.out)` over clips using the source, else `duration` |
| a pixels path for covers / source thumbnails (or `thumbSeq`) | today the optional `thumbnail` hook |
| `recentsLoading` | today "revision 0" stands in |
| `ClipModel::danglingReason` | the clip says "base deleted its target" — the only reason R-VER-4 defines |
| `lint` (list of {severity, text, detail}) | the Deliver Checks column derives what the model already says (dangling, offline, decoding, failed renders, pins) |
| ~~a "revert to base" command for a `#tlgrade` override~~ | **granted at integration**: the grammar has `revert <address>`; a click on the OVR badge sends `revert <bind>` |
| `HistogramData` for the selected node / frame | today computed from the monitor raster |
| `frameTime` (the time `frameSeq` was rendered at) | lets the monitor tell a stale frame from a current one without re-asking `renderFrame` |

## Shots (62 PNGs: each at 1440×900 and 1024×640) — what I verified by looking

| shot | verified |
|---|---|
| `home_populated` | wordmark sized to fit with the **accent dot at the measured word end**; primary New project in purple-pink; 4/2 columns; long Lisbon name ellipsized inside its card; size/date meta; dashed New project card |
| `home_hover` | hovered card border lifts toward the accent, surface brightens |
| `home_empty` | "No projects yet — open some footage to start one." + New project chip, no blank grid |
| `home_loading` | skeleton cards at the real card geometry, no count |
| `home_reflow_mid` | cards caught travelling between column layouts (mid-260 ms); scroll bar appears when the grid overflows |
| `home_settings` | modal over a scrim; Reduce motion toggle track visible on the popover (`switchBackground`, fixed after first look) |
| `loading`, `loading_to_edit_mid` | name + sentence + sweep; the shell cross-fade half-way |
| `grade_populated` | rack rows (depth guides, OVR, bypassed dim, offline in destructive, decoding spinner, "unused" still); **cosmo's sliders fill purple-pink**, Basic/Detail tab label + indicator purple-pink, histogram, filmstrip selection ring purple-pink, cosmo spinner cell for the decoding source (fixed after first look: a placeholder thumb had hidden it), reference-frame strip with the accent marker (redesigned after first look: the timecode overlapped the track and the deck was half empty) |
| `grade_hover` | row hover wash |
| `grade_empty` | "no footage yet — add some" + chip; inspector "Nothing to grade yet. / Add footage…" (message fixed after first look); "no clip at the playhead" |
| `grade_no_target`, `grade_bypassed` | full card wash + sentence; cosmo's scrim-and-pill "NODE BYPASSED" |
| `grade_tab_color`, `grade_tab_grade` | **MixerPanel/CurvePanel/GradePanel in purple-pink** (segmented pickers, curve, slider fills) |
| `grade_versions_open`, `_open_mid` | dropdown against the root, indented by depth with elbows, current row tinted, `Δ` overrides, dangling in destructive, pinned `@commit`, actions; mid-shot visibly half-faded |
| `grade_version_pinned` | lock + `@a41c9e2 · frozen` + full name at both sizes (fixed after first look: at 1024 the tag had squeezed the name to "Deliv…") |
| `grade_new_version_prompt` | NamePrompt with focused ring and typed text, purple Create |
| `grade_refused`, `grade_save_confirm` | destructive toast over the monitor; cosmo ConfirmDialog with Cancel / Don't save / Save |
| `grade_monitor_loading`, `grade_monitor_empty` | "decoding · 540p proxy" with spinner; "no clip at the playhead" |
| `cut_populated` | provenance: Inherited dimmed, Overridden accent edge + stripe, Dangling hatched destructive with reason, offline hatched; transitions as bowties; markers on ruler + through lanes; destructive playhead; track headers incl. M chip |
| `cut_hover`, `cut_drag_mid` | hover wash; dragged clip snapped with the accent guide on the caught edge |
| `cut_zoom_mid` | mid-zoom px/s; labels ride the visible part of clips (fixed after first look: clips starting off-screen had lost their names) |
| `cut_empty` | "drag a source here", lanes greyed, source bin lit with the accent outline |
| `deliver_populated`, `deliver_empty` | checks list; timeline radio picker (editing tag), dangling note, cosmo SegmentedControl in purple-pink, path, Render; queue rows **name their timeline**, done/running/queued/failed; "nothing queued" |
| `tab_grade_to_cut_mid` | highlight between Grade and Cut, both pages half-faded; the transport half slid in and faded, the monitor half-way to its Cut height, the caption's capture glyph half-faded |
| `grade_capture_menu`, `cut_capture_menu` | Copy Frame / Save Frame… under the caption's camera (Grade, no transport, monitor full height) and under the transport's camera beside ▶▶ (Cut) |
| `grade_frame_copied` | "Frame copied to the clipboard" chip with a green ✓ and a neutral border — not the refusal's red |

## Test output

`ctest`: `100% tests passed, 0 tests failed out of 2` (interstellar_app_shots --check: 62 PNGs, none
uniform; interstellar_app_ui: below).

```
accent
  [ok] palette::primary() is #CF5AED after App()
  [ok] cosmo's shared slider fill follows the accent
  [ok] cosmo's tab indicator follows the accent
  [ok] ring() is the accent at 0.5
  [ok] found the Sharpening Amount slider inside cosmo's ParamPanel
      slider fill pixel at (1241,600) = #CF5AED
  [ok] the reused cosmo slider fill is purple-pink on screen
grade: cosmo's panels dispatch `set <bind>.<filter>.<key>=…`
      set s_day01.basic.exposure=3.342618
  [ok] dragging Exposure dispatched set s_day01.basic.exposure=…
  [ok] …in ENGINE units (EV), converted with cosmo's toEv
  [ok] found GradePanel's first slider
      set s_day01.grade.grade0=99.77716,18,-4
  [ok] the grade wheel dispatched set s_day01.grade.grade0=h,s,l
      set s_day01.curve.curve=0,0;0.25,0.21;0.5,0.6453658;1,1
  [ok] dragging the tone curve dispatched set s_day01.curve.curve=x,y;…
rack tree, deck and frame selector
  [ok] clicking a rack row dispatched rack select s_still01
  [ok] the bypass toggle dispatched set s_day01.bypass=1
      set s_day02.weight=0.6
  [ok] dragging the weight bar dispatched set s_day02.weight≈0.6
  [ok] the rack's + asks the host for footage
  [ok] picked footage becomes rack add, quoting the path with a space
  [ok] clicking a filmstrip cell dispatched rack select s_day02
  [ok] the reference-frame selector is a strip of frames at 1440x900
      rack frame s_day01 --at 6.75
  [ok] dragging the frame strip dispatched rack frame s_day01 --at <t> on release
  [ok] …at 75% of the source, on a frame boundary
version switcher
  [ok] clicking the chrome opens the dropdown
  [ok] choosing a version dispatched timeline open delivery
  [ok] …and closed the dropdown
  [ok] the ‹ arrow steps to the previous version
  [ok] Pin dispatched timeline pin social30
  [ok] Freeze dispatched timeline freeze social30
  [ok] Rebase dispatched timeline rebase social30
  [ok] New version… opens the name prompt
  [ok] the prompt dispatched timeline new Festival_cut --base social30 (a legal bind name)
  [ok] Rebase is disabled on a root version
timeline
  [ok] a clip is drawn at timeToX(at) — one time origin
  [ok] ruler and lanes share headerWidth()
  [ok] a drag keeps the grab offset — the clip moved 12 px, it did not teleport
  [ok] mid-drag the start is SNAPPED to the neighbour's edge (10 s)
  [ok] …and the snap guide is fading in (eased, not popped)
  [ok] the drop dispatched the SNAPPED value: clip move c7 --at 10
  [ok] dropping 4 px off the marker snaps: clip move c7 --at 6
  [ok] dropping 4 px off the playhead snaps: clip move c7 --at 5.25
      clip move c7 --at 10 --track v1
  [ok] dropping on V1 dispatched clip move c7 --at 10 --track v1
  [ok] a video clip cannot be dropped on an audio track
  [ok] clicking a clip dispatched clip select c5
      clip trim c5 --out 6.041667
  [ok] dragging the right edge dispatched clip trim c5 --out <earlier>
  [ok] clicking the ruler at 3 s dispatched playhead 3
  [ok] Split at playhead dispatched clip split c2 --at 5.25
  [ok] Delete dispatched clip delete c2
  [ok] …and the inspector says nothing is selected once the clip is gone
transport, deliver
  [ok] ▶ dispatched play
  [ok] the play/pause glyph CROSS-FADES (first frame between)
  [ok] the same button, now ‖, dispatched pause
  [ok] ◀◀ dispatched playhead prev-cut
  [ok] scrubbing dispatched a run of playhead <t> lines
  [ok] Render dispatched render --timeline social30 --out …/social30.mp4 --format h264 — the timeline NAMED
  [ok] picking ProRes and the Delivery version re-derives the path: render --timeline delivery … --format prores
  [ok] the queue grew by the two renders the fake accepted
home, open, close
  [ok] clicking the newest card dispatched project open <its path>
  [ok] …and the loading screen names it
  [ok] New project asks the host for a path
  [ok] …which becomes project new "/tmp/My New Project.isp"
  [ok] the wordmark with unsaved edits asks first (cosmo's ConfirmDialog)
  [ok] …and nothing was dispatched yet
  [ok] Save dispatched project save, then project close
  [ok] the model's Home screen is now the one shown
  [ok] a clean project closes straight away
  [ok] a refused line is SAID: the toast eases in (first frame between 0 and 1)
motion: one frame at a time, first non-zero
  [ok] clicking Cut selects the Cut tab
      highlight 0.203  cut fade 0.221  grade fade 0.779
  [ok] the tab highlight TRAVELS (first frame between Grade and Cut)
  [ok] the Cut page fades in (first frame between 0 and 1)
  [ok] the Grade page fades out (first frame between 1 and 0)
  [ok] the monitor did not move across the tab switch
      dropdown open amount 0.271
  [ok] the version dropdown OPENS eased (first frame between 0 and 1)
      zoom 83.81 -> first 92.30 -> target 125.71 px/s
  [ok] the timeline ZOOM eases (live px/s strictly between)
  [ok] a clip the model moved TRAVELS to its new place
  [ok] a narrower window moves card 3 to a new slot
      card 3 x 878.0 -> first 870.3 -> target 833.3
  [ok] the home grid REFLOW eases (card live x strictly between)
  [ok] Home → Edit CROSS-FADES (Edit's first opacity between 0 and 1)
  [ok] the new picture starts fully under the old one
      dissolve first frame 0.100
  [ok] a new picture at the same time DISSOLVES (linear: ~16/160 on the first frame)
  [ok] a playhead jump from the model EASES the transport there
  [ok] a weight the model changed EASES on the rack row
  [ok] a bypass toggled in the model DIMS the rack row eased, not in one frame
  [ok] a version switch CROSS-FADES the chrome's label
  [ok] a clip whose provenance changed CROSS-FADES to its new look
  [ok] …in place (the new look starts from the old one's geometry)
  [ok] a render that finishes cross-fades running → done
  [ok] dropping a track moves the lanes under it up
  [ok] …and they SLIDE there (mid-tween lane between old and new)
  [ok] the settings modal appears eased
layout: contained, no sibling overlap, at both sizes
  [ok] 1440x900 tab 0/1/2: top bar, monitor, transport and the tab's three columns are inside the window   (x3)
  [ok] 1440x900 tab 0/1/2: no two of them overlap                                                        (x3)
  [ok] the project name stops before the tab switcher
  [ok] the version switcher stops before the save button
  [ok] the monitor keeps its floor
  [ok] 1024x640 tab 0/1/2: … inside the window / no two of them overlap                                  (x6)
  [ok] the project name stops before the tab switcher
  [ok] the version switcher stops before the save button
  [ok] the monitor keeps its floor
  [ok] 1024x640 home: every card inside the grid's width, none overlapping
  [ok] the home wordmark is sized to fit the sidebar
text fits (measured)
  [ok] ellipsized to 40 px measures within it (35.0)
  [ok] ellipsized to 120 px measures within it (117.0)
  [ok] ellipsized to 233 px measures within it (225.0)
  [ok] ellipsizing never cuts a multi-byte code point (every width 20..140 px)
  [ok] no room → nothing drawn, rather than a glyph over a neighbour
monitor: one registered frame, the previous id released
      12 frames fetched, live image ids 19 -> 19
  [ok] each playhead step fetched a frame through renderFrame
  [ok] …and released the id it replaced: no image leaks per step
  [ok] …a dissolve holds ONE extra id while it runs and lets it go after
reach: scroll clamps both ends
  [ok] an unscrollable list reports false, so the wheel bubbles
  [ok] scrolling past the end clamps at content - viewport
  [ok] …and at 0
  [ok] a 48-node rack outgrows the column at 1024x640
  [ok] wheeling down lands exactly at the end (clamped)
  [ok] …where the LAST row is fully reachable
  [ok] wheeling up clamps at the top
  [ok] 33 versions: the dropdown is CAPPED inside the window (placed against the root)
  [ok] …and its list scrolls
  [ok] the wheel scrolls the list to its clamped end
  [ok] …without closing the dropdown
  [ok] Escape closes it (eased to 0)
  [ok] 34 renders outgrow the deck
  [ok] the render queue scrolls to its clamped end
  [ok] …and its last row is reachable
  [ok] six projects outgrow the home grid at 1024x640
  [ok] the home grid scrolls to its clamped end
  [ok] …where the trailing New project card is reachable
  [ok] …and back to the top

interstellar_app_ui_tests: 134 checks passed
```

## Known gaps

- **Not built**: drag a source from the bin onto the timeline (`clip add …`), drag-to-regroup in
  the rack (no regroup command in §8), group collapse in the rack tree, revert-to-base on the OVR
  badge (no command), editing transitions/markers/gain/opacity, a Mask tab (deviation 3), keyboard
  focus traversal between panels, UI scale. The `⋯` overflow in the ui-brief sketch is a save button.
- **Inherited from reused cosmo widgets** (not editable here): a programmatic `SliderRow::setValue`
  re-seats the thumb in one frame — mitigated by the inspector's page cross-fade on a node switch,
  but a value pushed by another view while the same node stays selected still jumps; cosmo's labels
  centre with `estimateTextWidth`; the stacked-reach green is cosmo's literal `0x4CB573`;
  `IconButton`'s `active` colour switch snaps (worked around by driving its idle colour).
- **Overlay order during a tab cross-fade**: the outgoing page's overlays (filmstrip ring, cosmo's
  page wash) draw above the incoming page for those 200 ms, at the outgoing page's fading alpha.
- **Still one-frame**: rows added to the open version dropdown, a removed Home card, a change of the
  monitor's letterbox aspect, digit changes in timecodes/counts (data, not motion).
- **Render rate** is derived from successive models (Δdone/Δt, smoothed); a service-reported fps
  would be truer.
