# cosmo — Software Architecture

## 1. Architectural overview

cosmo is a native desktop photo editor assembled from four layers, each depending only on the
layers below it. The two lower layers are reusable, platform-free libraries; the two upper layers
are the application.

```
┌───────────────────────────────────────────────────────────────────────┐
│ host      cosmo/linux_main.cpp            (the ONLY layer with OS code) │
│           GTK3 window/events · file dialogs · threads · fonts · logging │
│           · PNG export · binds a Cairo IRenderTarget                    │
├───────────────────────────────────────────────────────────────────────┤
│ app       arstro::cosmo_v2   cosmo/App.* + cosmo/widgets/*              │
│           the Segment tree (screens, panels, controls); renders through │
│           artboard::IRenderTarget only; drives EditSession              │
├───────────────────────────────────────────────────────────────────────┤
│ cosmo_core  arstro::cosmo    cosmo/core/*                (UI-free)      │
│           CosmoService (Command in, AppModel + Event out) over          │
│           EditSession (sessions, group tree, history, presets,         │
│           persistence), ProjectLoader, ThreadBudget + the decoder seam. │
│           No Artboard dependency — the CLI is a front end of THIS.      │
├───────────────────────────────────────────────────────────────────────┤
│ engine    arstro   core/ImageProcessing/*               (headless)     │
│           EditEngine pipeline + RenderService worker + EditParams       │
└───────────────────────────────────────────────────────────────────────┘
        Artboard  (artboard)  — the platform-free 2D UI framework the app builds on
```

The app is built on the **Artboard** framework: a UI is a tree of `artboard::Segment`s that emit
primitive drawing ops to an abstract `IRenderTarget`; a thin adapter renders those ops to a real
device. cosmo supplies the Cairo adapter binding at the host layer and never calls a concrete
backend itself.

See [`architecture.puml`](architecture.puml) for the class/component diagram and
[`detailed_design.md`](detailed_design.md) for per-class detail.

## 2. Key architectural decisions

### 2.1 Strict downward layering
The host is the only layer permitted OS calls (GTK, threads, filesystem dialogs, fonts, logging).
The app depends on Artboard + cosmo_core; cosmo_core depends only on the engine (no Artboard, so
it stays reusable by a non-Artboard front end); the engine depends on nothing UI. This is what
lets the same session/engine be driven headlessly (tests, batch, a future front end).

### 2.2 Two authoring seams to the outside world
cosmo touches exactly two external contracts:
- **Artboard HAL** (`artboard::IRenderTarget` + the input router) — how the UI draws and receives
  gestures. The app emits primitives; the host binds `CairoTarget`.
- **Engine seam** (`cosmo::EditSession` → `arstro::RenderService`) — how edits become pixels. The
  app mutates `EditParams` and calls `submit()`; the engine renders on a worker thread and the app
  polls finished frames.

### 2.2b The core is a service; every front end is a view (R-SVC)
The layer above is not "the app" any more — it is *a* view of the app. `CosmoService` owns
the state and every operation on it; a front end sends a `Command` and reads an `AppModel`
plus an `Event` stream, and holds no logic of its own. The GTK window, `cosmo-cc` and the
shot renderer are three views of one service, and a control socket lets one drive a window
another is watching (R-SVC-8).

This was already the claim in §2.1/§2.3 — and it was not true: `startEntriesLoad` /
`decodeEntry` / `pollLoad` lived in `linux_main.cpp`, so opening a project needed a mouse
(D-6). The cost came due when the CPU budget's two defects (D-11, D-12) had to be found by
*reading*, because the load could not be run from a shell at all. The service is that claim
made structural rather than aspirational.

Presentation stays a real category (R-SVC-4). Animation, easing, hover, scroll offsets and
the transition phases are the view's business and R-G-1 is untouched: the service says a
load is 6 of 18, and the bar decides how to get there. `linux_main.cpp`'s `onServiceEvent`
is exactly that boundary, and it is worth reading as the shape all four front ends take.

### 2.3 App shell separated from UI-free logic
Everything that is not drawing or gesture handling — the group tree, selection, undo history,
presets, and all persistence — lives in `cosmo_core::EditSession`, with no Artboard types. The app
is a thin presentation + wiring layer over it. `RightColumn` is the main bridge: it owns the
`EditSession&`, and every control callback funnels through `curParams()` mutation + `submit()`.

### 2.4 Non-destructive, params-in / pixels-out
The editable state of an image is an `EditParams` value; source pixels are never mutated. The
engine renders `decoded source + params → Frame` on demand. This makes undo a matter of swapping
params (History), export a matter of re-rendering at full resolution, and presets a matter of
copying a subset of param fields.

### 2.5 Everything animates (R-G-1)
Every visible change is an eased `AnimatedProperty`, never a single-frame jump — a design
constraint inherited from Artboard's taste rules. `App::render` re-runs `layout()` and
`advance(nowMs)` every frame so animated positions/sizes reflow continuously. Reduced-motion
collapses each animation to its end state.

## 3. The four seams in detail

### 3.1 Output HAL — `artboard::IRenderTarget`
Every widget's `onPaint`/`onOverlay` emits path/paint/text/image primitives. The host owns a
single persistent `artboard::CairoTarget` and, per GTK `draw` signal, calls
`app.render(target, nowMs)` (`linux_main.cpp:716-722`). Because images are registered with the
target (id-per-target), the render target must persist across frames.

### 3.2 Input HAL — gestures
The host feeds raw pointer/scroll/key events to `App::pointer/wheel/key`, which drive an
`artboard::GestureRecognizer` (`App.cpp:406-412`); the recognizer derives click/double/right/
drag/drop and routes to the Segment under the pointer. The App installs a screen-aware sink: on
Home the gesture goes to `HomeScreen`, in the editor to `mRoot`, and during Loading it is
swallowed (`App.cpp:43-47`).

### 3.3 Engine seam — `RenderService`
`EditSession` owns an `arstro::RenderService` and never calls `EditEngine` directly. `submit()`
computes `effectiveParams(currentSlot)` and calls `render(slot, params)` (coalesced to the latest
request). The UI polls `tryAcquire(Frame&)` each frame in `renderEditor`; a completed `Frame` is
cached as `mLastAfterFrame`, pushed into the photo `ImageView`, and its histogram fed to the
`HistogramWidget`. Export (`renderFull`) and the before/after baseline (`renderPreviewSync`) block.

**Resident pixels are capped, and a cold slot is re-decoded (R-MEM).** `EditEngine` used to hold
every slot's source as a full-resolution LINEAR FLOAT image for the life of the project — 24 MP is
387 MB — so memory was a function of how many photos were opened rather than of how many were being
looked at (465 MB per photo measured; ~54 GB for a 120-RAW catalog). It now keeps two **byte-capped
LRU pools**, sources and proxies, and never evicts the slot it is rendering. A slot whose pixels are
both gone is *cold*, not broken: `RenderService::ensureSource` re-decodes the original file through
the `SourceLoader` seam — a `std::function` the host fills with its own budgeted decoder, so no codec
enters `arstro_image` and there is no second, unbudgeted decode path. The loader takes a **path**,
not a slot id, because it runs on the render worker and must never read session state; `addImage`
carries the path alongside the slot for exactly that reason.

### 3.4 Decode seam — `IImageDecoder`
`cosmo_core` defines `IImageDecoder`/`DecodedImage`; `NativeImageDecoder` implements it with
GdkPixbuf (JPEG/PNG/TIFF) + optional LibRaw (RAW). The host uses its own decoder instances (one
per worker thread) so decoding is thread-safe.

## 4. Screen state machine

The App is a three-screen state machine (`enum class Screen { Home, Loading, Editor }`) plus a
transition phase (`enum class Phase { None, Intro, Loading, Reveal, ReturnEnter, ReturnLoad,
ReturnExit }`). `render()` dispatches by screen:

```
Home    → render HomeScreen (+ cross-fade scrim)
Loading → renderTransition()  (open: Intro→Loading→Reveal;  return: ReturnEnter→ReturnLoad→ReturnExit)
Editor  → renderEditor()
```

- **Open** (`beginOpenTransition`): Intro is pure animation (wordmark flies, cover lifts from the
  clicked card, name grows, stars fade in) — but the decode is **already running behind it**, because
  `CosmoService::startProjectLoad` emits `ProjectOpening` (whose host handler begins the transition)
  and then starts `ProjectLoader`'s pool, in one call. There is no `onLoadingReady` hook any more:
  R-LOADPERF overlapped the decode with the intro, so the intro boundary only fades the progress bar
  in. Loading advances that bar toward the real fraction, and its `kMinLoadingMs=260 ms` floor is
  measured from `mLoadStartMs` — decode start, set in `beginOpenTransition` — rather than the end of
  the intro, since the two now overlap. When the load completes and that minimum has elapsed (or
  `kMaxLoadingMs=2500 ms` passes with at least one image usable, R-LOADPERF-3), `beginReveal`
  dissolves the loading elements while the editor materializes on the same dark backdrop with a
  wordmark cross-fade.
- **Return** (`showHome` from the editor): the editor fades to the star-sky (ReturnEnter), a brief
  beat rebuilds the launcher behind it (ReturnLoad), then home fades in (ReturnExit); the wordmark
  flies back to the home slot.

Animated properties: `mScreenFade` (screen switch), `mIntro`, `mReveal`, `mProgress`, `mCoverFade`,
`mBarFade` (open), `mEnterFade`, `mExitFade`, `mReturn` (return). The `Starfield` helper draws the
twinkling backdrop. Full timeline in [`requirements.md`](requirements.md) §4 and
[`detailed_design.md`](detailed_design.md).

## 5. The editor Segment tree

`App` builds one root Segment and adds children in draw/hit-test order (back to front):

```
mRoot
├─ TopBar ─── MenuStrip, wordmark, project name, filename, rail toggle
├─ LeftRail ─ PresetTree (scrollable), collapsible
├─ CenterStage
│   ├─ PhotoCanvas ── two cross-dissolving ImageViews (R-VIEW-1), split before-view + seam,
│   │                 MaskOverlay, Before/Split/After pill
│   ├─ Breadcrumb
│   └─ Filmstrip ──── pooled thumbnail ImageViews, sliding selection ring
├─ RightColumn
│   ├─ HistogramWidget
│   ├─ EditStackTabs
│   │   ├─ ParamPanel        (Basic/Detail)
│   │   ├─ MaskPanel         (Mask)
│   │   ├─ StackPanel        (Mixer/Curve) ── MixerPanel(×3 HueCurveEditor) + CurvePanel
│   │   ├─ GradePanel        (Grade)
│   │   └─ XformPanel        (Xform)
│   └─ ActionBar             (Save / Import / Export)
├─ HistoryView   (modal, overlay pass)
├─ ContextMenu   (modal, overlay pass)
├─ PresetDialog / SettingsDialog / ExportDialog / ConfirmDialog   (modals, overlay pass)
```

`RightColumn` also paints a **bypass scrim** in the overlay pass (R-BYPASS-4): a dark wash over
the tab strip + panel body (the histogram and the pinned ActionBar stay clear) plus a
`FILTER DISABLED` pill under the tabs, whose opacity is an eased `AnimatedProperty`. It is drawn in
the overlay pass rather than `onPaint` because `onPaint` runs BEFORE a Segment's children — a scrim
there would land under the very controls it has to mute.

`HomeScreen` is **not** in this tree — the App advances/renders it separately depending on the
screen. Modals draw in Artboard's second `renderOverlay` pass so they escape parent clipping, and
`raise()` themselves so they are hit-tested first.

`layout()` re-runs every frame so children reflow while the rail width and other properties
animate. Sizes come from widget constants (`TopBar::kHeight=29.25`, `LeftRail::kOpenWidth=196`,
`RightColumn::kWidth=324`, `Filmstrip::kHeight=86`, `Breadcrumb::kHeight=22.75`).

## 6. Data flow

### 6.1 An edit
```
control drag → widget callback → RightColumn: mutate mSession.curParams() field → mSession.submit()
  → EditSession.recordHistory() (coalesce / branch)
  → effectiveParams(slot) = slot params + ancestor group offsets
  → RenderService.render(slot, params)         [worker thread, coalesced]
        ── EditEngine applies the fixed pipeline on a downscaled proxy ──
  → RenderService publishes a Frame
App::renderEditor (next frame): tryAcquire(Frame) → cache mLastAfterFrame
  → refreshPhotoForMode() pushes it into the photo ImageView
  → HistogramWidget.setHistogram(Frame.hist)
```

### 6.2 Opening a project
Since S1a/S2 (R-SVC-1) none of this lives in `linux_main.cpp`: the host names the path and the
event stream animates the rest, so the same load runs with no window at all.
```
Home card click → App.onOpenRecent captures the card rect → host.onOpenRecentRequested(path)
  → host dispatches Command::ProjectOpen   (identically to `cosmo-cc`, a script, or the control
    socket — the GUI has no privileged path, R-SVC-2)
  → CosmoService::startProjectLoad: readWorkspaceFile(.cmp), then, in this order (D-13):
       emit ScreenChanged + ProjectOpening   [host handler runs synchronously here:
            App::beginOpenTransition + resetWorkspace + setLoadProgress(0,n)]
       EditSession::resetWorkspace, then build the WHOLE pending node tree (R-LOADUX-1)
       ProjectLoader::start(entries, ThreadBudget, decoderFactory, workerInit)
            — the decode pool is now running while the intro animates (R-LOADPERF)
  → CosmoService::pump(), once per frame, drains the loader strictly in entry order:
       attachImage + applyParamsToSlot(params, history) + setSlotBypass
       → emit EntryDecoded / EntryFailed, then LoadProgress   [host: cover, thumbs, bar, status]
  → all consumed → finishWorkspaceLoad + ProjectStore::remember
       → emit ScreenChanged + LoadFinished + ProjectOpened + Info "load.peak …"
       [host, on LoadFinished: App::finishOpenTransition] → beginReveal → Editor
```

### 6.3 Selection / navigation
Filmstrip/breadcrumb callbacks call `EditSession::selectNode`/`navigateToGroup`; the App then
`syncControlsToSlot()` — refreshing TopBar filename, breadcrumb, filmstrip cells, and pushing the
new slot's `EditParams` into every right-column panel via `RightColumn::syncToSlot()`.

## 7. Threading model

Two kinds of worker exist, and **one object decides how many of each** — `cosmo::ThreadBudget`
(R-SVC-10). Both feed the GTK thread through a poll:

- **Decode pool** (`cosmo::ProjectLoader`, core): N workers decode a project's images and build
  their filmstrip thumbnails off the UI thread; results land at their entry index and `pollLoad`
  (15 ms GTK timeout) drains them strictly in order. N is `ThreadBudget::beginLoad()`, which
  *reserves* that share for the load's duration. Each worker runs a start hook — the host uses it to
  pin nested OpenMP to one thread, which must happen on the worker because the OpenMP thread count
  is a per-thread ICV (D-12).
- **Render worker** (`RenderService`, engine, `ARSTRO_ENABLE_THREADS`): owns the `EditEngine`
  exclusively; `render()` requests are coalesced to the latest; the UI polls `tryAcquire()`. Its
  internal `par::parallelFor` width is `ThreadBudget::engineThreads()` — the whole budget when idle,
  and only what the load left while one is running. With threads disabled the service degrades to
  synchronous.

**Why one owner.** These two overlap for most of a load, because R-LOADPERF-3 reveals the editor
while images are still arriving. When each of them converted the user's percentage independently,
the concurrent total was about twice what was asked for — 13 of 24 cores at a 25% budget, and 17 of
16 on a 16-core box at the shipped 50% default (D-11). A budget nobody owns is not a budget.

All UI state lives on the GTK thread; the workers touch only their own job/engine, so there is no
shared UI mutation. `resetWorkspace` resets the render service (dropping engine slots and
restarting slot ids) before clearing session vectors, preserving the slot-id invariant.

## 8. Persistence

- **Session** `.cosmo` — a single image + its params.
- **Project / workspace** `.cmp` = `.cosmoproj` — a catalog referencing images on disk, the group
  tree (with per-group `LocalAdjust` offsets), and, per image, current params + the full branching
  history (`#hnode` blocks). Line-based `key=value` with `#group`/`#image`/`#hnode` markers;
  forward/backward tolerant (unknown keys ignored; missing history → single root).
- **Recents** — a tab-separated `recent.tsv` in the config dir (`ProjectStore`), newest first,
  capped at 24, self-pruning of missing files.
- **Presets** `.apf` — category-masked param documents, scanned into a tree by `PresetLibrary`.
- **Log** — `cosmo.log` in the config dir.

## 9. Module map

| Path | Layer | Responsibility |
|------|-------|----------------|
| `apps/cosmo/linux_main.cpp` | host | GTK app, events, dialogs, the load's UI-side consumer (`startEntriesLoad`/`pollLoad` — the decode itself moved down, R-SVC-1), threaded batch exporter, fonts, logging |
| `apps/cosmo/OmpPin.{h,cpp}` | host | sizes the nested OpenMP team of the calling thread, resolved by `dlsym`/`GetProcAddress` rather than linked; counts the threads it bound, so R-CPU-4's honesty clause is a number (R-CPU-2c, fixes D-12 + D-41) |
| `apps/cosmo/PixelBudget.h` | host | how big the engine's pixel caches may be on THIS machine — physical RAM queried here because it is the only layer allowed a platform call (R-MEM-1/5, fixes D-44) |
| `apps/cosmo/PinnedDecoder.h` | host | the ONE decoder the host constructs: wraps `NativeImageDecoder` and pins the decoding thread first, so the budget covers every decode and not only the load pool's (R-CPU-2c, fixes D-41) |
| `apps/cosmo/App.{h,cpp}` | app | screen state machine, transitions, Segment tree, host-callback seam |
| `apps/cosmo/Theme.{h,cpp}` | app | palette, radii, font family names, type ramp |
| `apps/cosmo/touch/*` + `apps/cosmo/TouchViewport.h` | app | the TOUCH shell (`arstro::cosmo_touch`), built into the desktop binary too: `linux_main.cpp` holds one beside `App` and cross-fades to it when `touchUi` is set (R-TOUCH-6). Same service, so the switch keeps the project open |
| `apps/cosmo/EditCommands.{h,cpp}` | app | the one place a moved control becomes a `Command`, shared by BOTH shells (R-TOUCH-1) |
| `apps/cosmo/EmbeddedFonts.{h,cpp}` + `cmake/embed_fonts.cmake` | app (host) | the typeface, compiled into the binary: the script generates a C++ array from the vendored TTFs and `registerEmbeddedFonts()` hands them to `CairoTarget::registerFontMemory` — no font file, no Fontconfig, no system font (R-FONT-1, Artboard FR-22a) |
| `apps/cosmo/Log.{h,cpp}` | app | file log + crash backtrace |
| `apps/cosmo/ExportWriter.{h,cpp}` | app (host) | batch export encoder: path resolution, JPEG/PNG/TIFF via GdkPixbuf, EXIF/GPS/sRGB metadata (R-EXPORT-3/4/5) |
| `apps/cosmo/widgets/*` | app | ~40 `Segment` widgets (chrome, panels, controls, overlays, dialogs) — plus `SplashScreen`, which the host renders in its OWN borderless window before the main one exists (R-SPLASH) |
| `apps/cosmo/core/service/CosmoService.{h,cpp}` | core | **the application** — `dispatch(Command)` / `pump(nowMs)` / `model()`, owning the load, the export queue, settings and recents (R-SVC-1) |
| `apps/cosmo/core/service/Command.{h,cpp}` | core | the one way in, plus the single parser/formatter for its text form (R-SVC-2/5) |
| `apps/cosmo/core/service/Event.{h,cpp}` | core | the one way out; `formatEvent()` output **is** the log line (R-SVC-3/5) |
| `apps/cosmo/core/service/AppModel.h` | core | the whole observable state as plain data — no pixels, no Artboard types, no presentation (R-SVC-3/4) |
| `apps/cosmo/core/service/AppModelCodec.{h,cpp}` | core | the model as deterministic text/JSON; the `stable` form is what proves two front ends agree (R-SVC-9) |
| `apps/cosmo/core/EditSession.{h,cpp}` | core | sessions, group tree, params, history, presets, persistence, render seam. `treeRows()` and `selectNodeById()` are the id-addressed projections a front end needs |
| `apps/cosmo/core/OrderedParallelLoad.h` | core | pooled produce → strictly-ordered consume, with bounded work in flight (R-LOADPERF-1); reusable, calls a per-worker start hook, and every predicate change — `stop()`'s included — is made under the mutex (D-42) |
| `apps/cosmo/core/ProjectLoader.{h,cpp}` | core | **the project load** — decode pool, per-worker decode + thumbnail, in-order delivery. Was three functions in the GTK host; moving it down is what made a load runnable and measurable with no window (R-SVC-1) |
| `apps/cosmo/core/ThreadBudget.{h,cpp}` | core | the ONE owner of the CPU budget: one total, divided between the decode pool and the engine, with the measured peak (R-SVC-10, fixes D-11) |
| `apps/cosmo/core/History.{h,cpp}` | core | branching undo tree |
| `apps/cosmo/core/ProjectStore.{h,cpp}` | core | recents index (config dir) |
| `apps/cosmo/core/AppSettings.{h,cpp}` | core | engine preferences persisted across launches (R-SETTINGS-4). It still holds `cpuPercent`, but no longer converts it — `workersFor()` survives only for the legacy test; `ThreadBudget` owns the conversion (R-SVC-10) |
| `apps/cosmo/core/PresetLibrary.{h,cpp}` | core | `.apf` preset tree scan |
| `apps/cosmo/core/decode/*` | core | `IImageDecoder` + GdkPixbuf/LibRaw `NativeImageDecoder` |
| `core/ImageProcessing/src/engine/*` | engine | `EditEngine`, `RenderService`, `EditParams`, serialization |
| `core/Artboard/*` | framework | `Segment`, `IRenderTarget`, gestures, `AnimatedProperty`, controls |
