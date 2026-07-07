# cosmo_v2 — Requirements

The single shared source of truth for cosmo_v2 behavior. **Every agent must read this
before implementing, and check any new/changed requirement here for conflict before writing
code** (mirrors the `implement_artboard` V-model, step 1). cosmo_v2 is an *application* built
on the Artboard library + `cosmo_core::EditSession`; the Artboard library keeps its own
`Artboard/docs/`. Requirements below are numbered `R-<area>-<n>`.

## Global rules (apply to every requirement)

- **R-G-1 Everything animates, nothing snaps.** No component may suddenly change size, appear,
  disappear, move, recolor, or reflow in a single frame. Every visible property change
  (position, size, show/hide via fade, color, radius, scroll/zoom offset, panel open/close,
  list insert/remove) changes through an animation primitive (`AnimatedProperty` / `Property`
  / `Spring`), never by direct assignment of the visible value. Show/hide is a fade or a
  size-to-zero tween, not a `visible` flip. Collapses to the final state only under
  `artboard::reducedMotion()`.
- **R-G-2 Figma is the spec.** Screens that reference a Figma frame must match it (spacing,
  type ramp, colors, radii pulled from `Theme`), not approximate it.
- **R-G-3 Everything interactive hovers.** Every button and every clickable region shows an
  animated hover treatment under the pointer — never a hard flip. Child-`Segment` controls
  (`Button`/`PillButton`/`IconButton`/`ComboBox`/`ToggleSwitch`/`Slider`) key off the framework's
  animated `hoverAmount()` via `artboard::hoverBox()`; self-drawn multi-region widgets (menu items,
  breadcrumb crumbs, filmstrip cells, tabs, tree rows, dialog rows/buttons, history nodes, home
  cards/actions, action-bar buttons) track a hovered-region id from `Gesture::Type::Move` and drive
  it through the shared `HoverFade` helper (`widgets/HoverFade.h`), which gives EACH sub-region its
  own eased 0..1 amount: the hovered one eases toward 1 and every other toward 0, so moving between
  items CROSS-FADES (the old item fades out while the new fades in) instead of the highlight jumping.
  The treatment is a `palette::hoverWash()` (or an accent-tinted border/label lift), eased (R-G-1) and
  collapsing under `reducedMotion()`. `Theme::hoverWash()`/`primaryAlpha()` are the shared tokens so
  hover reads identically app-wide (consistency lock). Known framework limit: true cross-fades
  between sibling children (e.g. Mixer channel editor, per-mask control blocks) need per-subtree
  opacity, which `IRenderTarget`/`Segment` do not expose — those switches are left instant (or given
  an overlay-scrim reveal where practical) pending an Artboard change; scroll offsets, panel/dialog
  open-close, and the edit-stack page swap all animate.

## R-LOADING — Animated open-project transition & loading screen — ✅ IMPLEMENTED

Opening a project decodes its images (~1 s for a multi-photo project). That decode runs on a
**background thread** (`linux_main.cpp` `decodeWorker`) so it never stalls the UI; the GTK main
thread polls finished results (`pollLoad`) and applies them to the session in entry order, so the
animated loading screen renders at full frame rate and the progress bar reflects real progress.
Applies to the two "open a project" paths — a recent-project card and Open Project… —
(`onOpenRecentRequested`, `openProjectDialog`). Orchestrated by `App` as a third screen state
(`Screen::Loading`) drawn in `App::renderTransition`; the star-sky backdrop is `widgets/Starfield.h`.
Honors `reducedMotion()` (the eases collapse; the load still streams in).

- **R-LOADING-0 Animation first, loading later.** The intro animation always plays to completion
  before the reveal begins, regardless of how fast the load finishes (`finishOpenTransition` only
  flags completion; `renderTransition` starts the reveal once the intro is done AND the load is
  complete). Combined with the background-thread decode, the transition never hitches on I/O.

- **R-LOADING-1 Intro (home → loading).** On open, the home `cosmo.` wordmark flies to the
  editor top-bar wordmark slot (46 px → 13 px, home position → top-left), the clicked project's
  cover lifts off its card and moves to the centre of the window over a cleared (dark) backdrop,
  and the project name grows in centred beneath it. Cover fly-from-card uses the clicked card's
  screen rect (`HomeScreen::lastOpenCardRect`); Open-dialog opens (no source card) settle the
  cover at centre.
- **R-LOADING-2 Loading screen.** A near-black star-sky backdrop of small white particles that
  each randomly fade in and out (twinkle). The project cover sits centred; a colour (accent)
  progress bar is pinned near the bottom and fills 0→1 with the real decode progress
  (`setLoadProgress`), eased so it never jumps backwards visually.
- **R-LOADING-3 Reveal (loading → editor).** When the load completes (and the intro has played),
  the centred cover expands into the editor photo's **exact fitted world rect**
  (`App::photoStageRect` from `ImageView::fittedRect` × `worldTransform`) and its content is swapped
  to the editor's rendered preview frame, so it lands **pixel-aligned in both rect and content**
  with the edit-page image ("expand and join the first preview") while the editor cross-fades in.
- **R-LOADING-4 Non-interactive.** Input (pointer/keys/wheel) is swallowed during the transition
  so nothing behind the loading screen is touched.

## R-LOG — File logging & crash diagnostics — ✅ IMPLEMENTED

- **R-LOG-1** The app writes a timestamped, levelled log to `~/.config/cosmo_v2/cosmo_v2.log`
  (`ProjectStore::configDir()`), appended across sessions with a per-session header. Every
  `g_print`/`g_printerr` diagnostic is routed through it (GLib print handlers), so the console
  output and the file stay in sync. Facility: `cosmo_v2/Log.{h,cpp}` (`log::init`, `LOGI/LOGW/LOGE`).
  Platform-note: logging does OS I/O, so it lives in the app layer, never in the platform-free
  Artboard core.
- **R-LOG-2** A fatal-signal handler (SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL) appends a backtrace to
  the log (async-signal-safe `write`/`backtrace_symbols_fd`) and re-raises the default handler, so a
  field crash leaves a diagnosable trail. Installed at startup (`log::installCrashHandler`).

## R-BUGFIX — Reset-then-open segfault — ✅ FIXED

`EditSession::resetWorkspace()` cleared the per-slot vectors but `RenderService` kept incrementing
its monotonic slot-id counter, so the first image opened after a reset (New Project / Open Project /
Import Catalog / Reset Workspace, all of which reset then open) got a slot id past the end of the
freshly-emptied vectors → out-of-bounds `mSlotPaths[mCurrentSlot]` in `currentSourcePath()`
(and siblings) → SIGSEGV. Fix: `RenderService::reset()` (+ `EditEngine::clearImages()`) drops all
engine slots and restarts id assignment from 0 on reset, restoring the "slot id == index, grow-only"
invariant; the slot accessors also got upper-bound guards as defence in depth. Regression-guarded by
the reset→open reproduction.

## R-MASK — Mask adjustable inside the photo (item 1) — ✅ IMPLEMENTED

Status: implemented. `cosmo_v2/widgets/MaskOverlay.{h,cpp}` (ported from cosmo), owned by
`PhotoCanvas` (above the image, below the Before/Split/After pill), bridged through
`RightColumn::{activeTab,selectedMaskParams,writeSelectedMask}` and synced each frame in
`App::render`. Reference: original cosmo's `cosmo/widgets/MaskOverlay.{h,cpp}`.

- **R-MASK-1** When a local-adjustment mask is selected (Mask tab), an interactive overlay is
  drawn over the photo on the center stage that lets the user position/resize the mask *inside
  the photo*, in the photo's normalised framed-image coordinates (the space the engine's
  `maskCoverage` uses). The UI edits `MaskParams` geometry only — it never touches pixels.
  - Radial: drag centre to move, edge handles to resize.
  - Linear: drag the two endpoints (0% and 100% gradient lines).
  - Brush: drag anywhere to paint coverage dabs.
- **R-MASK-2** With no mask selected the overlay is fully click-through (does not intercept
  zoom/pan or the Before/Split/After pill).
- **R-MASK-3** The overlay maps to the ImageView's current fitted rect **including zoom/pan**
  (R-ZOOM), so handles track the photo as it is magnified/panned.
- **R-MASK-4** Handle/geometry changes animate their on-screen position (R-G-1); the committed
  `MaskParams` value itself is not eased (it is data), only its rendered handles.

## R-ZOOM — Zoom & pan inside the photo (item 2) — ✅ IMPLEMENTED (except R-ZOOM-5)

Status: implemented in `PhotoCanvas` (owns zoom/pan, mirrors to before+after views) +
`App::wheel`. Uses `artboard::ImageView::zoomAbout/panBy/resetView` and
`EditSession::setPreviewZoom/resetPreviewResolution`. **R-ZOOM-5 (eased zoom) NOT yet done** —
zoom applies in 1.15× notches; smooth easing belongs in Artboard's `ImageView` (a library
change under the Artboard skill) and is tracked there, not faked at the app layer.

- **R-ZOOM-1** Ctrl + mouse-wheel over the photo zooms about the cursor: scroll up = zoom in,
  down = zoom out. Zoom is clamped 1×–8× (ImageView clamp). Plain wheel over the photo does
  nothing (it is reserved for panel/rail scrolling elsewhere).
- **R-ZOOM-2** While zoomed (>1×), press-drag on the photo pans the view; pan is clamped so the
  image always covers the view (ImageView `clampPan`).
- **R-ZOOM-3** Before and After (split) image views share one zoom/pan state so the split seam
  stays pixel-aligned — zoom/pan is routed through `PhotoCanvas`, applied to both views.
- **R-ZOOM-4** On each zoom step the preview render resolution scales with the zoom
  (`EditSession::setPreviewZoom`) so a high-res original stays sharp when magnified; resets to
  base when the view returns to 1× or the selected image changes.
- **R-ZOOM-5** Zoom and pan **animate** toward their target (R-G-1) rather than snapping per
  wheel notch / per drag frame — magnification eases, it does not jump.

## R-PARITY — Feature parity gap vs. original cosmo (item 3)

cosmo_v2 must reach behavioral parity with `cosmo/`. The tracked gap list (verified 2026-07-06)
lives in [PARITY.md](PARITY.md); each row is a requirement to either implement or explicitly
descope with a reason. PARITY #1 (R-MASK) and #2 (R-ZOOM) are done. Remaining rows:

- **R-PARITY-CROP** (PARITY #3) — interactive crop box over the photo (corner/edge handles,
  aspect lock) while the Xform tab is active; writes normalised crop x/y/w/h. *Open.*
- **R-PARITY-PRESETPICK** (PARITY #4) — see R-PRESETPICK below. *Implementing now.*
- **R-PARITY-SETTINGS** (PARITY #5) — see R-SETTINGS below. *Implementing now.*
- **R-PARITY-SPLIT** (PARITY #6) — draggable split divider in Before/Split/After. *Open.*

### R-PRESETPICK — Preset category-picker modal (PARITY #4)

Reference: `cosmo/widgets/PresetDialog.{h,cpp}`. Today cosmo_v2 applies every present category of
a preset immediately (documented stub in `App.h`).

- **R-PRESETPICK-1** Applying a preset (from the preset tree / Preset menu) opens a modal that
  lists the categories the preset actually contains (e.g. Tone, Colour, Detail, Effects, Curve,
  Grade, Mixer, Xform, Masks — whichever are present) as toggleable checkboxes, all on by
  default, with Apply / Cancel.
- **R-PRESETPICK-2** Apply applies only the checked categories to the current image; Cancel
  applies nothing. The same picker backs Save/Export (choose which categories to write).
- **R-PRESETPICK-3** The modal is a real overlay drawn in the overlay pass (dim scrim behind,
  centered card), it opens/closes with a fade+scale (R-G-1), and it is click-outside/Esc
  dismissable. Only one modal open at a time (coordinate with the History modal).
- **R-PRESETPICK-4** Backed by the engine's category masking: a `PresetCategory` bitset chooses
  which `EditParams` fields copy from the preset onto the current params.

### R-SETTINGS — Engine settings panel (PARITY #5)

Reference: `cosmo/panels/SettingsPanel.{h,cpp}`. Exposes engine/app settings that map onto the
`RenderService`.

- **R-SETTINGS-1** A settings surface (opened from the Settings menu) exposes: **Preview
  quality** — the base preview render resolution (speed vs. detail), and **CPU threads** — the
  worker count for the multicore engine. Both map directly onto `RenderService` /
  `EditSession` (`mPreviewEdge` / thread count).
- **R-SETTINGS-2** Changing preview quality updates the base preview edge and re-renders;
  changing thread count reconfigures the engine's parallelism. Values persist for the session.
- **R-SETTINGS-3** Presented as a modal overlay consistent with R-PRESETPICK-3 (scrim, centered
  card, fade+scale open/close, Esc/click-outside dismiss).

## R-HOME — Home screen & projects (item 4) — ✅ IMPLEMENTED

Status: implemented. `widgets/HomeScreen.{h,cpp}` (Figma-faithful launcher) + a screen
state machine in `App` (`Home`/`Loading`/`Editor`, cross-fade scrim on switch) + `cosmo_core/
ProjectStore.{h,cpp}` (persisted recent index) + host dialogs in `linux_main.cpp`. The app
starts on Home. Notes vs. the spec below: the recent index is a small **line-based TSV** in
the config dir (not literally JSON); the Figma **LoadingScreen** is implemented as the animated
open-project transition (see **R-LOADING**); a `.cmp` is exactly the existing workspace catalog
with a `.cmp` extension. Search is a focusable `artboard::TextBox` fed by host key/text events.


Reference Figma: `ref/2/extracted/src/app/App.tsx` → `HomeScreenDesktop` (lines ~214–386) and
`LoadingScreen`. Must match 100% (R-G-2): left 300px sidebar (`#161616`) with wordmark
`cosmo.` (accent dot), tagline, New Project / Open Project… / Import Catalog… actions, bottom
Settings / What's New / Help & Documentation links + version; right pane with a "Recent
Projects" header + count + search box, and an `auto-fill minmax(220px,1fr)` grid of project
cards (16:9 cover thumbnail, "Edited" badge, name, `N photos · size · date`) plus a dashed
"New Project" card. Empty-search state shows the "No projects match" placeholder.

- **R-HOME-1 Screen state machine.** App has three screens: `loading → home → editor`. The
  editor is the current cosmo_v2 UI. Clicking the top-left **`cosmo.` wordmark** in the editor
  returns to home; if the project has **unsaved changes** (`EditSession::isDirty()`), a modal
  first asks to **Save** (accent) or **Discard** (red/destructive), with Cancel/click-outside to
  stay (`ConfirmDialog`). Screen transitions cross-fade (R-G-1).
- **R-HOME-1b Project name in the top bar.** The open project's name (the `.cmp` stem) is shown
  centred in the editor top bar; set on New/Open/Import/Recent-open.
- **R-HOME-2 Project file format `.cmp` = catalog/manifest.** A `.cmp` is a JSON catalog that
  **references the original image files on disk** (absolute/relative paths) plus each image's
  edit settings and the group tree — i.e. it reuses cosmo_v2's existing workspace serialization
  (`writeWorkspaceFile`/`readWorkspaceFile`) with a `.cmp` extension and a `name` field.
  Rationale: matches "Import Catalog" (Lightroom model), small/fast, no gigabyte copies.
  Consequence: if an original is moved/renamed the reference is stale (acceptable for now).
  Project "size" shown on cards = sum of the referenced files' sizes. All create/open/save go
  through one `ProjectStore` seam so the format can evolve without touching the UI.
- **R-HOME-3 New Project.** "New Project" (sidebar button, dashed grid card, or the empty-state
  card) opens a popup to enter a project name (and location); confirming creates a new `.cmp`
  and opens the editor on it, where the user adds photos (the current editor screen / import).
- **R-HOME-4 Open Project.** "Open Project…" shows a native file dialog filtered to `.cmp` and
  opens the chosen project in the editor.
- **R-HOME-5 Import Catalog.** "Import Catalog…" shows a native multi-select image dialog and
  opens the selected images inside a new project.
- **R-HOME-6 Recent Projects (real, persisted).** The grid lists recent projects from a
  **persisted recent-projects index** (a JSON file in the app config dir), each entry storing
  the project name, `.cmp` path, photo count, last-opened time, and a cached thumbnail of the
  project's first image. Each card shows: thumbnail = first image of that project, name, photo
  count, and last-opened time (relative, e.g. "2h ago"). Creating/opening a project updates the
  index (path → front, refresh last-opened).
- **R-HOME-7 Search.** The search box filters recent projects by name (case-insensitive,
  substring); no matches shows the empty-state placeholder.
- **R-HOME-8 Reserved.** Settings / What's New / Help & Documentation are present but inert
  (reserved), matching the Figma affordances without behavior.
</content>
</invoke>
