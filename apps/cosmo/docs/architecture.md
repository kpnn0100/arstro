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
│           EditSession (sessions, group tree, history, presets,         │
│           persistence) + native image decoder. No Artboard dependency. │
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
  clicked card, name grows, stars fade in); at the intro boundary `onLoadingReady` fires and the
  host starts the background decode; Loading advances the progress bar; when decode completes and a
  minimum has elapsed, `beginReveal` dissolves the loading elements while the editor materializes
  on the same dark backdrop with a wordmark cross-fade.
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
│   ├─ PhotoCanvas ── ImageView, split before-view + seam, MaskOverlay, Before/Split/After pill
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
```
Home card click → App.onOpenRecent captures the card rect → host.onOpenRecentRequested(path)
  → startProjectLoad: readWorkspaceFile(.cmp) → beginOpenTransition + resetWorkspace
  → (intro plays; at its end) onLoadingReady → spawn decodeWorker thread + pollLoad timer
  → decodeWorker decodes each image off-thread; pollLoad applies results in order
       (addWorkspaceGroup / openImageInto + applyParamsToSlot(params, history))
  → all consumed → finishWorkspaceLoad + rememberProject + finishOpenTransition
  → beginReveal → Editor
```

### 6.3 Selection / navigation
Filmstrip/breadcrumb callbacks call `EditSession::selectNode`/`navigateToGroup`; the App then
`syncControlsToSlot()` — refreshing TopBar filename, breadcrumb, filmstrip cells, and pushing the
new slot's `EditParams` into every right-column panel via `RightColumn::syncToSlot()`.

## 7. Threading model

Two worker threads exist, both owned at the host/engine boundary and both feeding the GTK thread
through a poll:

- **Decode worker** (`LoadJob::worker`, host): decodes a project's images off the UI thread and
  pushes results into a mutex-guarded queue; `pollLoad` (15 ms GTK timeout) drains it in order.
- **Render worker** (`RenderService`, engine, `ARSTRO_ENABLE_THREADS`): owns the `EditEngine`
  exclusively; `render()` requests are coalesced to the latest; the UI polls `tryAcquire()`. With
  threads disabled the service degrades to synchronous.

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
| `apps/cosmo/linux_main.cpp` | host | GTK app, events, dialogs, threaded loader (`startEntriesLoad`, shared by Open and Import Catalog), threaded batch exporter, fonts, logging |
| `apps/cosmo/App.{h,cpp}` | app | screen state machine, transitions, Segment tree, host-callback seam |
| `apps/cosmo/Theme.{h,cpp}` | app | palette, radii, fonts, type ramp |
| `apps/cosmo/Log.{h,cpp}` | app | file log + crash backtrace |
| `apps/cosmo/ExportWriter.{h,cpp}` | app (host) | batch export encoder: path resolution, JPEG/PNG/TIFF via GdkPixbuf, EXIF/GPS/sRGB metadata (R-EXPORT-3/4/5) |
| `apps/cosmo/widgets/*` | app | ~40 `Segment` widgets (chrome, panels, controls, overlays, dialogs) — plus `SplashScreen`, which the host renders in its OWN borderless window before the main one exists (R-SPLASH) |
| `apps/cosmo/core/EditSession.{h,cpp}` | core | sessions, group tree, params, history, presets, persistence, render seam |
| `apps/cosmo/core/OrderedParallelLoad.h` | core | pooled produce → strictly-ordered consume, with bounded work in flight (R-LOADPERF-1) |
| `apps/cosmo/core/History.{h,cpp}` | core | branching undo tree |
| `apps/cosmo/core/ProjectStore.{h,cpp}` | core | recents index (config dir) |
| `apps/cosmo/core/AppSettings.{h,cpp}` | core | engine preferences persisted across launches (R-SETTINGS-4) |
| `apps/cosmo/core/PresetLibrary.{h,cpp}` | core | `.apf` preset tree scan |
| `apps/cosmo/core/decode/*` | core | `IImageDecoder` + GdkPixbuf/LibRaw `NativeImageDecoder` |
| `core/ImageProcessing/src/engine/*` | engine | `EditEngine`, `RenderService`, `EditParams`, serialization |
| `core/Artboard/*` | framework | `Segment`, `IRenderTarget`, gestures, `AnimatedProperty`, controls |
