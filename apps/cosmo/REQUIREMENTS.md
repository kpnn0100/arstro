# cosmo — Requirements

The single shared source of truth for cosmo behavior. **Every agent must read this
before implementing, and check any new/changed requirement here for conflict before writing
code** (mirrors the `implement_artboard` V-model, step 1). cosmo is an *application* built
on the Artboard library + `cosmo::EditSession`; the Artboard library keeps its own
`core/Artboard/docs/`. Requirements below are numbered `R-<area>-<n>`.

This file is the numbered-requirement / decision ledger (intent + status). For the **detailed,
source-derived as-built specification and design** — a full functional/non-functional spec,
architecture, per-class detailed design, design rationale, and a PlantUML model — see
[`docs/`](docs/): [`requirements.md`](docs/requirements.md), [`architecture.md`](docs/architecture.md),
[`detailed_design.md`](docs/detailed_design.md), [`design.md`](docs/design.md),
[`architecture.puml`](docs/architecture.puml).

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
- **R-G-2a One wordmark.** The `cosmo.` wordmark is drawn identically everywhere it appears
  (home sidebar, editor top bar, the open/return transition): letter-spacing `-0.03 * size` and the
  accent dot at `x + estimateTextWidth("cosmo", size)`. No per-site spacing tweaks.

## R-EDITSTACK — Right-column edit stack

- **R-EDITSTACK-1 Tabs.** Five merged tabs: **Basic/Detail** (all tone/colour/presence/effects +
  sharpening/noise/lens sections in one scrollable `ParamPanel`), **Mask**, **Mixer/Curve** (the HSL
  mixer above the tone curve in one scrollable `StackPanel`), **Grade**, **Xform**. Tab widths size
  to their label (+ even padding to fill the strip) via `EditStackTabs::tabW/tabX`, so wider merged
  labels are never clipped. The Mask overlay bridge keys off `RightColumn::maskTabActive()`, not a
  hard-coded index.
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

The transition is split into **three linked parts** so every movement flows into the next with no
sudden jump, and the heavy work is isolated to the middle part:

- **R-LOADING-0 Part 1 — transition (animation, decode running behind it).** The intro plays while
  the background decode streams in behind it (see R-LOADING-1's amendment): the home `cosmo.` wordmark flies to the editor top-bar wordmark slot (46 px → 13 px, home
  position → top-left, `App::drawWordmark`), and the clicked project's **WHOLE card — cropped
  thumbnail + name + photo count + total size + last-edit date — lifts off the grid and translates
  to screen centre AT ITS CARD SIZE. It does NOT expand/zoom into a large hero image**; the entire
  recent item simply glides to the middle and fades in over a cleared star-sky backdrop, and the
  small stars fade in. The decode does NOT start yet. The fly uses the clicked card's **full** screen
  rect + its info (`HomeScreen::lastOpenCardRect` / `lastOpenCardInfo`); the centred rest rect is the
  **same size** (a pure move, no scale). The card itself is drawn by the shared
  `widgets/ProjectCard.h` `drawProjectCardChrome` (+ the cover blitted over its thumbnail band), the
  SAME renderer the home grid uses, so the flying item is pixel-identical to the grid item.
  Open-dialog opens (no source card) fade in at centre at a default card size (name only).
- **R-LOADING-1 Part 2 — loading (status text + progress bar under the card).**
  **AMENDED (R-LOADPERF):** the decode now starts with the transition, not after the intro.
  `onLoadingReady` fires as part 1 BEGINS. The deferral existed so a full-resolution decode on the
  UI thread could not hitch the intro — but decoding moved to a worker pool and the per-image apply
  moved off the UI thread with it (R-LOADPERF-1/2), so there is nothing left to hitch on, and
  deferring only bought 460 ms of a dead progress bar reading "Preparing…". Overlapping them means
  the bar is already filling and the status line already naming real photos by the time the intro
  lands. Everything is already placed; a **small accent progress bar the
  SAME WIDTH as the card, positioned directly UNDER it** (not a wide bar at the bottom of the screen)
  fills 0→1 with the real decode fraction (`setLoadProgress`), eased. **Directly above that bar a
  status line — left-aligned to the card's (and bar's) left edge — shows what is currently being
  loaded** (`App::setLoadStatus`, e.g. `Loading  <photo name>` — fed per item by the host as each
  entry is applied; `Preparing…` before the first). The
  status line + bar fade in together at the part-1→part-2 hand-off (they never pop). A short minimum
  keeps the bar from merely flashing on a fast load.
- **R-LOADING-2 Loading-screen look.** A **deep near-black** star-sky backdrop (`kLoadingBg`
  `#0A0A0A`, darker than the home/editor background so the loading screen reads as a dim, focused
  moment; the editor gently brightens in on reveal rather than matching exactly) of **small**
  twinkling white particles
  (`widgets/Starfield.h`). The project's whole card sits centred **at card size** with its **cover
  thumbnail** (already decoded for the home card and cached by the host, so it needs no I/O in part
  1) **cropped (Cover fit)** into the thumbnail band and **faded in** when available (never pops);
  below the card, top-to-bottom: the load-status line and the card-width progress bar (one centred
  stack — the name/meta live inside the card).
- **R-LOADING-3 Part 3 — reveal (loading → editor).** When the load completes (and the intro has
  played), the editor components **materialise on top of the matching dark backdrop** (fade in),
  while the loading elements — the centred card, stars, status line and progress bar
  — **fade out in place** (no move/expand). The wordmark **cross-fades**: the loading copy fades out AS the editor's
  top-bar wordmark fades in, in the same slot at the same time, so it never doubles and never
  re-fades from nothing (`drawWordmark` with an alpha; the reveal drives the editor fade itself so
  `renderEditor` adds no scrim).
- **R-LOADING-4 Non-interactive.** Input (pointer/keys/wheel) is swallowed during the transition
  (open AND return) so nothing behind the loading screen is touched.
- **R-LOADING-5/6 Reverse (project → home), also in 3 parts.** Returning to the launcher (`showHome`
  from the editor → `App::renderReturn`) mirrors the open, through the same dark star-sky:
  - **ReturnEnter** — the editor fades OUT to the star-sky (a dark, matching-bg + stars overlay
    fades IN over the still-rendered editor). The loading screen appears IMMEDIATELY on click — the
    launcher prep is deferred (below), so there is no freeze before the fade.
  - **ReturnLoad** — a brief full star-sky beat, during which `refreshHome()` runs (rebuild recents +
    thumbnails) BEHIND the shown loading screen instead of as a click-time freeze. Thumbnails are
    reused from the host cache (no re-decode on return), so this is cheap.
  - **ReturnExit** — the home screen fades IN from the star-sky (home rendered, the dark+stars
    overlay fades OUT on top).
  Across all three the wordmark flies from the top-bar slot back to its big home position
  (`mReturn`, `App::drawWordmark`), landing as the exit begins; the sidebar's own wordmark is hidden
  (`HomeScreen::setWordmarkHidden`) until it lands, so it reads as one element. Collapses instantly
  under `reducedMotion()`.

## R-LOADPERF — Opening a project is parallel, off-thread and progressive

Opening a catalog of large frames was bounded by three serial costs, all avoidable
(measured on a 24 MP JPEG: ~109 ms to decode, ~72 ms to apply):

- **R-LOADPERF-1 Decode in parallel.** The loader decodes on a **pool** of worker threads
  (`std::thread::hardware_concurrency()`, clamped to 2..8) instead of one, since decoding is
  CPU-bound and independent per image. Workers claim entries with an atomic counter, so they
  finish out of order, but each result is stored **at its entry index** and the UI thread still
  applies them strictly in order — the `.cosmoproj` format identifies a node's parent by entry
  index, so out-of-order application would reparent the tree.
- **R-LOADPERF-1a Bounded in flight.** A decoded 24 MP frame is ~100 MB and N workers outrun the
  single applier, so an unbounded pool would decode a whole catalog into RAM. A worker waits before
  claiming work until the batch is within a small window of the apply cursor **and** total decoded-
  but-unapplied bytes are under a cap. The worker holding the entry the applier needs next is
  always exempt from both limits, so the pipeline can never deadlock against its own cap.
- **R-LOADPERF-2 Apply off the UI thread.** The two costs inside the old apply — the ~100 MB pixel
  copy into the engine and the filmstrip thumbnail's full-image downsample — both move to the
  decode worker: the thumbnail is built there and handed over ready-made, and the decoded buffer is
  **moved** into the engine (`RenderService::addImage(std::vector&&)`) instead of copied. What is
  left on the UI thread per image is bookkeeping, so the loading screen keeps animating instead of
  hitching once per photo.
- **R-LOADPERF-3 Reveal — complete, or capped.** **AMENDED (R-LOADUX):** revealing on the *first*
  image made the loading screen meaningless — it flashed "Preparing…", showed a bar at zero and
  jumped to the editor, so nothing ever reported how long the wait would be. The editor is now
  revealed when the load **completes**, so the progress bar actually fills and means something —
  **or**, for a catalog too big to wait for, once `kMaxLoadingMs` has elapsed AND at least one image
  is usable, after which the rest stream in behind the editor with the rack's spinner cells and its
  own progress bar (R-LOADUX-2/3). The minimum-visible time is measured from when the DECODE
  started, not from the end of the intro: with the two overlapped the intro already gave the bar
  time on screen, so a small project reveals the moment the intro lands instead of sitting through
  a further hold.
  `finishWorkspaceLoad` consequently must **not** steal the selection — it auto-selects the first
  image only when nothing is selected yet, so a photographer who started working during the stream
  is not yanked back to image 1 when the last one lands.

## R-LOG — File logging & crash diagnostics — ✅ IMPLEMENTED

- **R-LOG-1** The app writes a timestamped, levelled log to `~/.config/cosmo/cosmo.log`
  (`ProjectStore::configDir()`), appended across sessions with a per-session header. Every
  `g_print`/`g_printerr` diagnostic is routed through it (GLib print handlers), so the console
  output and the file stay in sync. Facility: `apps/cosmo/Log.{h,cpp}` (`log::init`, `LOGI/LOGW/LOGE`).
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

## R-BUGFIX-2 — Stale editor during the open reveal — ✅ FIXED

Opening a project after another was already open showed the *previous* project's editor (photo,
filmstrip thumbnails, breadcrumb) underneath/through the reveal instead of the new one fading in
from scratch. Cause: `App::resetWorkspace()` reset the `EditSession` but not the editor's *visible*
state — the photo `ImageView`, the split/before `ImageView`, the cached `mLastAfterFrame`, the
`Filmstrip` thumbnails, and the `Breadcrumb` path all retained the old project's content until the
new render landed. Fix: `App::resetWorkspace()` now also `clearImage()`s both photo views, resets
`mLastAfterFrame`, `Filmstrip::clearThumbs()`, and clears the breadcrumb path, so the editor is blank
the instant the workspace is reset (which happens right after `beginOpenTransition`, before the new
decode). `Filmstrip` reuses its `ImageView` child pool (`mThumbCount`) across projects — `Segment`
has no `removeChild`, so `clearThumbs()` restarts the active count at 0 and `addThumb()` refills the
pool in lockstep with the engine's reset slot ids (a new project's `thumbSlot` must not index an old
thumbnail). Headless-verified: red project → reset → editor blank → green project opens fresh, no
stale photo, no crash.

## R-BUGFIX-3 — Mixer curve saved as samples, not bezier points — ✅ FIXED

The colour-mixer (Mixer/Curve tab) is edited as a bezier curve with smooth, Alt-dragged tangent
handles, but the handles were discarded on save: `HueCurveEditor` sampled the curve to a dense
piecewise-linear polyline (~14 points/segment) and stored *that* in `EditParams.mixer[]`. Reopening a
project rebuilt those samples as bare CORNER points — the smooth curve came back as a coarse linear
approximation with no editable handles (the "opens as smaller/linear points" symptom).

Fix: the persisted representation is now the bezier CONTROL points. `EditParams.mixer[]` is
`std::array<std::vector<CurvePoint>,3>` where `CurvePoint{x,y,ix,iy,ox,oy,smooth}` (new
`core/ImageProcessing/src/base/CurvePoint.h`, which also owns the single shared `curve::sample()` so the
editor's on-screen curve and the engine's LUT can never diverge). `HueCurveEditor` now emits/restores
control points (handles + smooth flag intact); `ColorMixer::setCurve` flattens the control points with
`curve::sample()` when building its LUT, so render is unchanged. Serialization writes `x,y` for a
corner point (identical to the old format → pre-bezier files still load, as corners) or
`x,y,ix,iy,ox,oy` for a smooth one (field count flags smooth); the `.apf` preset path matches. The
legacy `apps/cosmo/` v1 editor keeps its sampled-point model via a thin convert-at-the-seam shim in
`CosmoApp`. Regression-tested (`EditParamsIO_mixer_bezier_roundtrip`) + headless-verified: a smooth
mixer point round-trips as 3 control points with handles (vs 43 handle-less corners on the old path)
and the engine renders byte-identically before/after the round-trip.

## R-PERSIST — Save the edit history + save shortcuts — ✅ IMPLEMENTED

- **R-PERSIST-1 History in the project.** A saved project (`.cosmoproj` workspace) persists each
  image's full branching edit history, not just its live params. `saveWorkspaceAs` writes, after an
  image's current-params block, a history header (`hcurrent`/`hmax`/`hcoalesce`) then one `#hnode`
  block per node — `hparent`/`hseq`/`hlabel` + a full params snapshot, in vector-index order so the
  parent indices stay valid. `readWorkspaceFile` parses these back into `WorkspaceEntry::history`, and
  the load path restores them via `EditSession::applyParamsToSlot(slot, params, history)` →
  `History::restore()`, which rebuilds each node's `kids` from its parent, restores `current`, and
  resumes `seq` numbering past the highest loaded seq (so later edits get fresh, non-colliding ids and
  redo still prefers the newest branch). Old projects (no `#hnode`) load with a single-node root, as
  before. Regression-tested (`workspace_history_roundtrip`): a branched tree round-trips with its
  nodes, branch, current node, and undo/redo intact.
- **R-PERSIST-2 Save shortcuts.** `Ctrl+S` saves the project to its current path (prompting only if it
  has none); `Ctrl+Shift+S` is Save As (always prompts). File ▸ Save / Save As… map to the same
  workspace actions (labels show the shortcuts), so "save" means "save the project" everywhere —
  group tree, per-image params, and history. (Bare `s` remains the quick PNG export.)

## R-MASK — Mask adjustable inside the photo (item 1) — ✅ IMPLEMENTED

Status: implemented. `apps/cosmo/widgets/MaskOverlay.{h,cpp}` (ported from cosmo), owned by
`PhotoCanvas` (above the image, below the Before/Split/After pill), bridged through
`RightColumn::{activeTab,selectedMaskParams,writeSelectedMask}` and synced each frame in
`App::render`. Reference: original cosmo's `apps/cosmo/widgets/MaskOverlay.{h,cpp}`.

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

## R-PARITY — Backlog carried over from the original cosmo (item 3)

This app began as `cosmo_v2` alongside an original `apps/cosmo/` GUI; it has since fully replaced
that app and been renamed to `cosmo` (the original was removed). A few features from the old
app were not yet ported — that backlog (verified 2026-07-06 against the original) lives in
[PARITY.md](PARITY.md); each row is a requirement to either implement or explicitly descope
with a reason. PARITY #1 (R-MASK) and #2 (R-ZOOM) are done. Remaining rows:

- **R-PARITY-CROP** (PARITY #3) — interactive crop box over the photo (corner/edge handles,
  aspect lock) while the Xform tab is active; writes normalised crop x/y/w/h. *Open.*
- **R-PARITY-PRESETPICK** (PARITY #4) — see R-PRESETPICK below. *Implementing now.*
- **R-PARITY-SETTINGS** (PARITY #5) — see R-SETTINGS below. *Implementing now.*
- **R-PARITY-SPLIT** (PARITY #6) — draggable split divider in Before/Split/After. *Open.*

### R-PRESETPICK — Preset category-picker modal (PARITY #4)

Reference: the original cosmo's `widgets/PresetDialog.{h,cpp}` (since removed). Today cosmo
applies every present category of a preset immediately (documented stub in `App.h`).

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

Reference: `apps/cosmo/panels/SettingsPanel.{h,cpp}`. Exposes engine/app settings that map onto the
`RenderService`.

- **R-SETTINGS-1** A settings surface (opened from the Settings menu) exposes: **Preview
  quality** — the base preview render resolution (speed vs. detail), **CPU threads** — the
  worker count for the multicore engine, and **GPU acceleration** — off / on-if-available
  (see R-GPU). All map directly onto `RenderService` / `EditSession` (`mPreviewEdge` /
  thread count / prefer-GPU).
- **R-SETTINGS-2** Changing preview quality updates the base preview edge and re-renders;
  changing thread count reconfigures the engine's parallelism. Values persist for the session.
- **R-SETTINGS-3** Presented as a modal overlay consistent with R-PRESETPICK-3 (scrim, centered
  card, fade+scale open/close, Esc/click-outside dismiss).
- **R-SETTINGS-4 Settings persist across launches.** Preview quality, CPU threads and GPU
  acceleration are statements about the **machine**, not about the session — reverting them on every
  launch makes the panel feel broken. They round-trip through `cosmo::AppSettings`, a plain
  `key=value` file in the same user config dir as the recent-projects index (the same file
  convention the workspace format uses). They are **loaded before the first render** and applied to
  the session/engine at startup, and **saved whenever one changes**, so the Settings dialog always
  opens seeded with what is actually in force. A missing, partial or corrupt file falls back to the
  defaults per field rather than failing to start.

## R-GPU — GPU-accelerated image processing (abstract, opt-in) — ✅ IMPLEMENTED (abstraction + OpenGL/Linux backend; more stages + platforms deferred)

The image engine can process on the GPU when a platform GPU backend is available and the user
opts in, behind a **cross-platform abstraction** so concrete per-platform GPU implementations
(Metal / Vulkan / Direct3D / WebGPU / OpenGL) are added **without touching the engine or the UI**.
A first concrete backend — **OpenGL 4.3 compute over surfaceless EGL** — is implemented (Linux;
runs on AMD/Intel/NVIDIA via Mesa/desktop GL, with an llvmpipe software fallback). This mirrors
Artboard's platform-independence rule: the CPU/software path is the reference and the floor, and an
accelerator is used only when its result matches it.

- **R-GPU-1 Abstract seam.** `core/ImageProcessing/src/compute/ComputeBackend.h` defines
  `IComputeBackend` — an *optional* accelerator that renders the per-image edit pipeline (global
  chain + masks + gamma encode) for a `(linear source Image, EditParams)`, returning the encoded
  result and the histogram taps; it exposes `name()`, `kind()` (`Cpu`/`Gpu`), `available()`, and
  `process(...)` which returns **false to decline** a job. A single factory
  `createComputeAccelerator()` returns the platform backend — the **OpenGL 4.3 compute backend**
  where built (`ARSTRO_GL_COMPUTE`), `nullptr` (CPU-only) otherwise, e.g. the web build. It is the
  one per-platform extension point.
- **R-GPU-2 CPU is the reference & the fallback.** The existing CPU pipeline in `EditEngine` is the
  guaranteed fallback **and** the correctness reference: a GPU backend that accepts a job must match
  the CPU path **within a small tolerance** (a hardware backend is not bit-exact in float — the
  OpenGL backend is verified ≤ 2/255 vs CPU). If no backend is available, the toggle is off, or the
  backend declines, the engine renders on the CPU exactly as before — **byte-identical, no behavior
  change** (headless-verified). The accelerator branch lives in the single render choke point
  `EditEngine::renderInto`.
- **R-GPU-3 Setting (opt-in).** Engine Settings… gains a **GPU acceleration** row (Off / On),
  shown **disabled + "unavailable"** when no GPU backend is present. When available and On, the
  engine *prefers* the GPU backend; otherwise CPU. It is a session/engine setting (not persisted to
  the project), plumbed `SettingsDialog → App → EditSession::setUseGpu → RenderService::setPreferGpu
  → EditEngine::setPreferGpu`, mirroring Preview quality / CPU threads.
- **R-GPU-4 Cross-platform & platform-free.** The seam is platform-free (`ImageProcessing` has no OS
  deps and compiles under Emscripten); concrete backends live behind the factory per platform.
  Selection/availability/fallback are regression-tested with a mock backend (the compute-side
  `RecordingTarget`).
- **R-GPU-5 OpenGL compute backend (first concrete, Linux/AMD).**
  `core/ImageProcessing/src/compute/GlComputeBackend.cpp` implements `IComputeBackend` with an **OpenGL
  4.3 compute shader** over a **surfaceless EGL** context (headless — no window; the shader is
  compiled at runtime, so no offline SPIR-V/GLSL toolchain is needed). Built when `ARSTRO_GL_COMPUTE`
  is defined and EGL/GL link (CMake auto-detects; `build.sh` passes it for the cosmo native build).
  - **Ported subset (this increment):** the per-pixel colour/tone point ops — **Exposure**,
    **Contrast**, **White Balance** (gains reused from `color::kelvinToRgbGain`) — plus the exact
    sRGB **encode**. `process()` runs on the GPU only when the edit is entirely within that subset
    (every other stage at its default); anything else (tone curve, mixer, grade, tone regions,
    vibrance/saturation, dehaze, grain, sharpen, noise reduction, lens, geometry, masks) **declines
    → CPU**, so all edits stay correct. The histogram taps are computed on the CPU from the GPU
    result (which equals the final image because the un-ported stages are identity in the fast path).
  - **Threading & availability:** availability is a one-time cached probe; the working GL context is
    created lazily on the RenderService **worker thread** on first use. If context creation or the
    edit is unsupported, it falls back to CPU. Verified on the AMD (Mesa `radeonsi`) GPU — GPU output
    matches CPU ≤ 2/255, on both the direct engine path and the RenderService worker path
    (`EditEngine_gl_backend_matches_cpu`, `RenderService_gpu_worker_matches_cpu`).
- **R-GPU-6 OpenGL ES 3.1 compute backend (Android).**
  `core/ImageProcessing/src/compute/GlesComputeBackend.cpp` is the Android sibling of the desktop GL
  backend, selected by the same factory (`#elif defined(ARSTRO_GLES_COMPUTE)`). It shares the
  accepted-edit predicate and the compute-shader body with the desktop backend
  (`compute/GlComputeShared.h`) so the accepted subset and per-pixel math cannot drift; only the EGL
  context (an **ES 3.1** context over a 1×1 pbuffer, `EGL_OPENGL_ES_API`) and the shader header
  (`#version 310 es` + `precision highp`) differ. ES 3.1 exposes compute / SSBOs / `glMapBufferRange`
  as core, so no proc-address loader is needed. Built when `ARSTRO_GLES_COMPUTE` is defined and
  EGL/GLESv3 link (the Android app CMake sets it; the umbrella `android-app` target links GPU); the
  CPU pipeline stays the reference + guaranteed fallback exactly as R-GPU-2/5. Verified GPU==CPU
  ≤ 2/255 on desktop Mesa GLES (`EditEngine_gles_backend_matches_cpu`, skips cleanly with no ES 3.1
  device) and on-device on an Android GLES 3.2 GPU (Mali). The GLES backend cross-compiles for
  arm64-v8a with the NDK.
  - **Deferred:** the remaining pipeline stages (moving encode + histograms fully onto the GPU too),
    and other APIs (Vulkan/Metal/D3D/WebGPU) — each an incremental add behind the same seam.

## R-HOME — Home screen & projects (item 4) — ✅ IMPLEMENTED

Status: implemented. `widgets/HomeScreen.{h,cpp}` (Figma-faithful launcher) + a screen
state machine in `App` (`Home`/`Loading`/`Editor`, cross-fade scrim on switch) + `apps/cosmo/core/
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
  editor is the current cosmo UI. Clicking the top-left **`cosmo.` wordmark** in the editor
  returns to home; if the project has **unsaved changes** (`EditSession::isDirty()`), a modal
  first asks to **Save** (accent) or **Discard** (red/destructive), with Cancel/click-outside to
  stay (`ConfirmDialog`). Screen transitions cross-fade (R-G-1).
- **R-HOME-1b Project name in the top bar.** The open project's name (the `.cmp` stem) is shown
  centred in the editor top bar; set on New/Open/Import/Recent-open.
- **R-HOME-2 Project file format `.cmp` = catalog/manifest.** A `.cmp` is a JSON catalog that
  **references the original image files on disk** (absolute/relative paths) plus each image's
  edit settings and the group tree — i.e. it reuses cosmo's existing workspace serialization
  (`writeWorkspaceFile`/`readWorkspaceFile`) with a `.cmp` extension and a `name` field.
  Rationale: matches the reference-by-catalogue import model, small/fast, no gigabyte copies.
  Consequence: if an original is moved/renamed the reference is stale (acceptable for now).
  Project "size" shown on cards = sum of the referenced files' sizes. All create/open/save go
  through one `ProjectStore` seam so the format can evolve without touching the UI.
- **R-HOME-3 New Project.** "New Project" (sidebar button, dashed grid card, or the empty-state
  card) opens a popup to enter a project name (and location); confirming creates a new `.cmp`
  and opens the editor on it, where the user adds photos (the current editor screen / import).
- **R-HOME-4 Open Project.** "Open Project…" shows a native file dialog filtered to `.cmp` and
  opens the chosen project in the editor.
- **R-HOME-5 Import Catalog.** "Import Catalog…" shows a native multi-select image dialog, asks
  where to save the new project, and opens the selected images inside it **through the same
  animated open-project transition a recent project uses (R-LOADING)** — the picked images become
  root-level workspace entries and stream in on the background decode thread behind the loading
  screen, with the real progress bar and per-image status line. It must NOT decode inline on the UI
  thread: a catalog of large frames would otherwise freeze the app for the whole import with no
  feedback. The one difference from opening an existing project is that the `.cmp` does not exist
  yet, so it is written once the last image has landed (`LoadJob::saveOnFinish`); the loading-screen
  cover falls back to the first decoded image, since no thumbnail is cached for a never-opened
  catalog.
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

## R-BYPASS — Per-node filter bypass (disable/enable a group's or photo's edits)

A photographer needs to see what an item looks like *without* its own develop settings without
throwing those settings away. Any node of the group tree — an image leaf **or** a group — can
therefore be **bypassed**: its own `EditParams` stop contributing to what is rendered, while the
values themselves stay intact and stay editable, so re-enabling restores the look exactly.

- **R-BYPASS-1 Model (one flag per node).** `EditSession::GNode` carries a `bypass` flag
  (`isBypassed(node)` / `setBypassed(node,on)` / `toggleBypass(node)`). It is a property of the
  **node**, not of the slot, so an image leaf and a group are bypassed the same way.
- **R-BYPASS-2 Semantics — a node's OWN params only.** Bypass removes exactly the bypassed node's
  own contribution from the composition, nothing else:
  - a bypassed **image leaf** contributes `EditParams{}` instead of its own params; its ancestor
    groups still stack onto it;
  - a bypassed **group** contributes `EditParams{}` instead of its own offsets; its members' own
    params and its *other* ancestors are unaffected.
  Bypass therefore composes: each node in a chain can be bypassed independently, and bypassing a
  group does **not** implicitly bypass the images inside it. `effectiveParams(slot)` is the single
  place this is applied for everything that renders — preview, histogram, before/after and export
  all agree by construction.
  `effectiveEditParams()` (which exists only to derive the panel's green "stacked reach" =
  effective − own) honours bypass on the **ancestors** but never zeroes the edit target's *own*
  values, because that reach measures what the ancestors add on top: a bypassed ancestor correctly
  contributes nothing, while the target's own bypass is communicated by the dim scrim (R-BYPASS-4)
  rather than by faking a negative reach on every slider.
- **R-BYPASS-3 Right-click toggle.** The filmstrip/photo context menu (`App::openEditContext`) gains
  a **Disable Filter** / **Enable Filter** item whenever the click landed on a cell (group or image).
  The label reads the target's current state, so the item always names what the click will do.
  Toggling re-renders immediately (R2) and marks the session dirty.
- **R-BYPASS-4 The edit section dims.** While the item currently being edited is bypassed, the
  right column's **edit stack** (the tab strip + the panel body between the histogram and the action
  bar) is covered by a dark scrim and its content is muted, so it reads at a glance as "these values
  will not be applied". A small non-interactive **FILTER DISABLED** pill sits at the top of the
  dimmed area naming the state. The controls stay live — a bypassed item is still editable.
  The scrim's opacity is an `AnimatedProperty` eased in/out (R-G-1); it never pops, and it collapses
  instantly under `reducedMotion()`. The histogram and the pinned action bar are NOT dimmed (they
  are not part of the edit stack).
- **R-BYPASS-5 Filmstrip indicator.** A bypassed cell draws a muted "no entry" badge in its
  top-left corner and its thumbnail ring reads muted rather than accent, so the state is visible
  from the strip without opening the menu. `Filmstrip::Cell` carries a `bypassed` flag fed by
  `App::syncControlsToSlot`.
- **R-BYPASS-6 Persistence.** The flag round-trips through the `.cosmoproj` workspace as a
  `bypass=1` line on the node's `#group` / `#image` section (absent = not bypassed, so older
  projects load unchanged). `WorkspaceEntry::bypass` carries it to the host loader, which passes it
  to `addWorkspaceGroup` / `setSlotBypass`.

## R-EXPORT — In-app Export modal (batch export with a group tree)

File ▸ Export… opens an **in-app modal**, styled exactly like the rest of the editor, that exports
one or many images in one action. It replaces the old "immediately show a native save dialog for the
current image" behaviour of that menu item. The **bare `s` shortcut keeps its quick single-image
PNG/JPEG export** through the native save dialog (R-PERSIST-1's note stands) — the modal is the
deliberate, batch path, `s` is the fast path.

Reference design: `ref/cosmo/File Reader Design(1).zip` → `src/app/App.tsx` `ExportModal`, adapted
to this app's tree model per the deltas called out below.

- **R-EXPORT-1 Chrome.** A modal `ExportDialog` Segment drawn in the overlay pass on the same
  chrome as R-PRESETPICK-3 / `PresetDialog`: dim scrim over the whole window, one centred card on
  `palette::popover()` with a `radius::control()` border, a header band carrying the **download
  icon + the title "Export"** and a close **✕**, a scrollable body, and a pinned footer. It fades +
  rises in and out (R-G-1), and click-outside / **Esc** cancels. Every region hovers (R-G-3).
- **R-EXPORT-2 Images to export — a checkbox TREE (delta from the reference).** The reference's
  group chips + flat thumbnail grid are replaced by the **real group tree**: one row per node,
  indented by depth, groups carrying an expand/collapse chevron + child count, images carrying their
  filename. **Every row has a checkbox**, and the check state propagates:
  - **select a parent → select all its children** (recursively);
  - **deselect a parent → deselect all its children** (recursively);
  - **deselect any child → the parent is unticked**;
  - **when every child is selected → the parent is ticked**.
  This is realised by keeping the **image leaves** authoritative and *deriving* every group's state
  from its descendants — ticked when all descendant images are ticked, **indeterminate** (a dash,
  not a tick) when only some are, unticked when none — which satisfies all four rules by
  construction and cannot drift. A **Select all / Select none** master button sits above the tree,
  and a "`n` of `m` photos selected" line below it. Rows only count toward `m` if they are real
  image leaves (a group is a container, never an export target). The tree scrolls (eased, R-G-1)
  when it is taller than its box.
- **R-EXPORT-3 Destination (delta from the reference).** The destination row carries a **checkbox
  on its right, "Same as source"**, ticked by default:
  - **ticked** → each image is written next to its own original file, so a mixed selection lands
    beside each source. The path field shows `<source folder>/` greyed as a non-editable hint;
  - **unticked** → one explicit output folder is used for everything. It **defaults to the folder of
    the first selected image**, and a **Change…** button asks the host for a native folder chooser
    (`onChooseDestination` → `setDestination(path)`).
  Below it, two optional modifiers, each a checkbox + label + text field (the field is disabled and
  muted while its checkbox is off): **Filename prefix** (prepended to each output name) and
  **Export to subfolder** (a folder created under the resolved destination). The resolved path +
  example filename is previewed live in mono under the destination row, ellipsized to fit (R5).
- **R-EXPORT-4 Format / size / quality.** Format chips: **JPEG · PNG · TIFF** — the three the host
  writer can actually produce (GdkPixbuf); WebP from the reference is dropped rather than shipped
  broken. Size chips: **Original · 2048 px · 1080 px · 720 px** (long edge; downscale only, never
  upscale). **Quality** applies to JPEG only and is laid out as **two rows** (delta from the
  reference, which put the value inline with the slider): a **label row** — `Quality` left, the
  current `NN%` right — and the **slider on its own row** below it, full width. The quality block
  fades out entirely for PNG/TIFF, which have no quality knob.
- **R-EXPORT-5 Metadata.** Three toggles, matching the reference: **Embed EXIF data**,
  **Strip GPS coordinates**, **Embed colour profile (sRGB)**. They are honoured by the host writer
  for the formats that can carry them: EXIF/GPS operate on the JPEG `APP1` segment copied from a
  JPEG source, and the sRGB profile is written as a JPEG `APP2` `ICC_PROFILE` segment / a PNG
  `iCCP` chunk. A toggle that a chosen format cannot carry is simply not applied (no silent
  failure, and the footer summary never claims otherwise).
- **R-EXPORT-6 Footer + progress (R2).** The footer shows a live summary — `n photos · FMT · SIZE`
  (+ `· prefix: …` when one is set) — and **Cancel** / **Export n photos**. Export is disabled while
  nothing is selected. Because a batch full-res render is far slower than a frame, pressing Export
  **does not freeze the UI**: the host feeds `setExportProgress(done, total, name)` one image per
  main-loop idle step. The dialog stays modal (and non-cancellable mid-write) for the duration.
  Pressing Export plays a **three-beat animation**, every beat eased and reduced-motion-safe.
  **The animation plays first and the export starts after it** — pressing Export snapshots the
  request but hands the host nothing until the collapse has finished (`onExport` fires from
  `advance()` when the tween settles), exactly the split R-LOADING-0/1 uses so its intro never
  hitches on I/O. And the batch itself runs on a **worker thread**: `exportFullResSlot()` goes
  through `RenderService::renderFull()`, which *blocks its caller* until the engine finishes, so
  running it on the UI thread would freeze every frame for the length of each image. The UI thread
  only drains finished results (~1 poll per frame), so the dialog animates at full framerate from
  the first frame to the last. While a batch is running `App::render` skips its own full-render
  path (`renderBefore`), because the engine's full-render channel holds one request at a time and
  two callers would steal each other's frame.
  1. **Collapse.** The card *shrinks in place* to a progress card: the header and the
     **Images to Export** section survive; the master button, the selection count, Destination,
     the modifiers, Format/Size/Quality and Metadata all **fade out** and the card height tweens
     down to what is left. The tree's **checkboxes fade away** and each row's chevron/icon/label
     slides left into the space they occupied, so the list reads as a manifest rather than a
     picker. The tree itself is rebuilt to the **participating rows only** — the selected images
     plus the groups that contain them — so what is on screen is exactly what is being written.
  2. **Writing.** A determinate accent **progress bar sits directly under the tree section**, above
     a mono status line naming the file in flight. As each file lands, **that row's background
     highlights** (a success-tinted wash + a small tick, eased in per row, never popping) and a
     group's row highlights once every one of its members is written. The tree auto-scrolls to keep
     the in-flight row visible.
  3. **Done.** On the last file the tree, bar and footer content **fade out together**, the card
     shrinks again to a compact panel showing a **green tick and `Exported n photos`**, and after a
     short hold the dialog closes itself.
- **R-EXPORT-8 The modal is draggable.** The card can be **dragged by its header band** (anywhere
  but the ✕) and stays where it is put, so it can be moved off whatever the photographer wants to
  look at — including mid-export, since the header is the drag handle in every state. The offset is
  clamped so the header always stays reachable on screen, survives a window resize, and resets when
  the dialog is next opened. A drag never fires the click under it.
- **R-EXPORT-7 Bypass is honoured.** Batch export renders each slot through
  `EditSession::exportFullResSlot(slot,…)`, which composes exactly the same `effectiveParams(slot)`
  the preview uses — so a bypassed image or group exports without those edits (R-BYPASS-2), and
  what you see is what is written.

## R-BROWSE — Navigating the photo rack

- **R-BROWSE-1 The filmstrip scrolls on the wheel.** The photo rack is a scroll view like every
  other overflowing surface: a wheel over it scrolls it horizontally to reach photos beyond the
  viewport (it already eased its offset for the sliding selection ring — it simply was never given
  the wheel). `App::wheel` routes to it by hit-testing the strip's world rect, before the
  right-column branch. One notch moves one cell + gap, so a notch is one photo.
- **R-BROWSE-2 Arrow keys walk the rack.** **Left/Right** select the previous/next cell of the
  current group — image or group chip alike, in display order — and the selection ring slides
  (R-G-1) rather than jumping. The strip **auto-scrolls to keep the newly selected cell in view**,
  so holding an arrow walks the whole rack. Navigation is clamped at both ends (no wrap: wrapping
  from the last photo back to the first is disorienting in a cull). Arrow keys are ignored while a
  modal is open or a text field has focus (`App::isTextEditing`), and while the loading transition
  is playing (R-LOADING-4).
- **R-BROWSE-3 Edit-section scroll intensity.** The develop panels scrolled **one pixel per wheel
  notch** (`scrollBy` took the raw wheel delta as pixels), which reads as broken. One notch now
  moves `kEditScrollStep` = **10 px** — ten times the old step — via one named constant, so the
  feel is tunable in one place rather than per panel.

## R-SPLASH — Application open animation (windowless)

Starting the app decoded every recent project's cover thumbnail on the UI thread **before the first
window was even shown** (`refreshHome` → `onDecodeThumbnail`), so cosmo appeared to hang on launch.
It now opens the way a project does: **animation first, work after**.

- **R-SPLASH-1 A small, windowless splash.** On launch cosmo shows a **compact undecorated window**
  (no titlebar, no border, centred, ~`kSplashW`×`kSplashH`) — not a fullscreen screen — rendering an
  Artboard `SplashScreen` through the same `CairoTarget` as the editor. The main window is not
  created until the splash is done, so the first thing on screen is the animation.
- **R-SPLASH-2 The animation** follows the reference design's `LoadingScreen`
  (`ref/cosmo/File Reader Design(1).zip` → `src/app/App.tsx`), scaled to the small window: the
  **`cosmo.` wordmark** (accent dot, R-G-2a spacing) rises 8 px and scales 0.96→1 while fading in;
  the **`PROFESSIONAL PHOTO EDITOR`** tagline (letter-spaced, muted) fades in behind it; **three
  dots** pulse in sequence; and a **2 px accent progress bar is pinned to the bottom edge** of the
  window, filling as startup proceeds. Every part is an eased `AnimatedProperty`; the whole thing
  collapses under `reducedMotion()`.
- **R-SPLASH-2a It says what it is loading.** A launch that only shows a bar tells you *how far*
  but never *what*. The dots therefore **hand off to a status line**: while there is nothing
  specific to report (during the intro) the reference's pulsing dots hold the slot; the moment the
  host reports its first item they **cross-fade out** and a **spinner + status text** cross-fades in
  in the same slot — `Scanning projects…`, then `Loading  <project name>` per cover thumbnail, then
  `Ready`. One activity affordance at a time (never dots *and* a spinner: that is the same
  affordance twice), and the slot never jumps. The spinner is the same rotating arc the filmstrip's
  loading cells use (R-LOADUX-2), so "work is happening" reads identically across the app.
  The **text itself is data, not motion** (R-G-1): the line fades in once and out at the end, while
  the string inside it simply changes — cross-fading every string swap would flicker, since items
  are reported as fast as they complete.
- **R-SPLASH-3 Animate first, load after.** The intro plays against an idle main loop. Only when it
  finishes does the host do the deferred startup work (scanning recents and decoding their cover
  thumbnails), reporting real progress **and the current item's name** into the splash; the splash
  then fades out, is destroyed, and the main window is shown. A launch with images on the command
  line skips straight through.
- **R-SPLASH-4 Non-interactive.** The splash takes no input and cannot be dismissed; it is chrome
  for a fixed, short moment.

## R-LOADUX — A project load says what it is doing

Revealing the editor on the first decoded image (R-LOADPERF-3) made opening fast but **opaque**: the
loading screen flashed "Preparing…", the editor appeared with one photo, and the rest arrived with no
indication that anything was still coming or how much. Fast is not the same as understandable. The
load is therefore made **visible** rather than slower:

- **R-LOADUX-1 The whole rack exists immediately.** The group tree is built from the project's
  entries **up front, on the UI thread, before any decoding** — groups and one **placeholder leaf per
  image** (`EditSession::addPendingImage`, a node with no engine slot, the same shape
  `addWorkspaceMissingImage` already produced). So the filmstrip shows the project's real size from
  the first frame and photos fill in where they belong. As each image decodes it is **attached** to
  its waiting node (`EditSession::attachImage`), which also removes the need to apply results in
  entry order — the parent indices were resolved before any of it started.
- **R-LOADUX-2 Placeholder cells show they are loading.** A `Filmstrip::Cell` that has no image yet
  draws as a dimmed cell with a **spinning indicator** (a rotating arc — the placeholder for the
  supplied loading animation) instead of a thumbnail, so an un-arrived photo reads as *pending*, not
  as *missing*. The spinner is driven from the frame clock and stops as soon as the cell has pixels.
- **R-LOADUX-3 Progress that means something.** The loading screen's bar and status line report
  **`n of N`** against the project's real total, not an unlabelled fraction; and because the editor
  is revealed early, a **slim determinate progress bar remains along the top edge of the filmstrip**
  while images are still arriving, with the count beside it. It fades out when the last one lands.
  A photographer can therefore always tell how much is left, in both phases.
