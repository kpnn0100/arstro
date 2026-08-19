# cosmo — Detailed Requirements (as-built)

This is the **detailed, source-derived** requirements specification for the `cosmo` photo
editor. It documents what the shipping implementation actually does, subsystem by subsystem,
with `file:line` anchors into the source so each requirement is traceable and verifiable.

Relationship to the other docs:

- [`../REQUIREMENTS.md`](../REQUIREMENTS.md) is the **numbered-requirement / decision ledger**
  (`R-<area>-<n>`) — the running record of what was asked for and its status. It stays the
  authority for *intent and history*.
- **This file** is the authority for *as-built behavior* — the complete contract the code
  honors today. Where a behavior traces to a numbered requirement, the tag is cited (e.g.
  R-LOADING-0).
- [`architecture.md`](architecture.md), [`detailed_design.md`](detailed_design.md),
  [`design.md`](design.md), and [`architecture.puml`](architecture.puml) cover structure and
  rationale.

Requirements are grouped `DR-<AREA>-<n>` (Detailed Requirement).

---

## 1. Product scope

cosmo is a native desktop, professional non-destructive raster photo editor. It manages
**projects** (catalogs that reference image files on disk), organizes images in a **group
tree**, and edits each image through a fixed non-destructive engine pipeline with a **branching
undo history**. It is a GTK3 + Cairo application built on the platform-free `artboard` 2D UI
framework, sharing all UI-free logic with `cosmo_core` (`arstro::cosmo`) and delegating pixel
work to the `ImageProcessing` engine (`arstro`).

- **App shell namespace:** `arstro::cosmo_v2` (kept internal; the product, binary, and build
  target are all `cosmo`).
- **Base artboard size:** 1440×900 logical px (`linux_main.cpp:34`); window default 1440×900,
  freely resizable (`linux_main.cpp:888-890`).
- **Frame tick:** ~60 fps (`g_timeout_add(16, onTick, …)`, `linux_main.cpp:910`).

---

## 2. Non-functional requirements

### DR-NFR-1 Layering & platform-independence
The codebase is four layers, each depending only downward:

1. **host** (`apps/cosmo/linux_main.cpp`) — the only layer allowed OS/GTK calls: window, events,
   file dialogs, threads, fonts, logging, PNG export.
2. **app** (`arstro::cosmo_v2`, `apps/cosmo/App.*` + `apps/cosmo/widgets/*`) — the UI, built on Artboard
   `Segment`s; renders through the `IRenderTarget` HAL only.
3. **cosmo_core** (`arstro::cosmo`, `apps/cosmo/core/*`) — UI-free session, group tree, history,
   presets, persistence, and the native decoder. No Artboard dependency (`EditSession.h:1-14`).
4. **ImageProcessing** (`arstro`) — the headless edit engine; no UI, no threads of its own
   beyond the RenderService worker.

The app never calls a concrete render backend; it emits primitives to `artboard::IRenderTarget`
and the host binds a `CairoTarget` (`linux_main.cpp:717`). cosmo_core never includes Artboard.

### DR-NFR-2 Everything animates, nothing snaps (R-G-1)
Every visible change — position, size, show/hide (fade), color, corner radius, scroll/zoom
offset, panel/tab/dialog open-close, list ring — is driven through an `artboard::AnimatedProperty`
(or `Spring`), never a single-frame jump. Motion eases (`Easing::EaseOutCubic`, ~120–520 ms
depending on scope) and collapses to the final state under `artboard::reducedMotion()`. Known
framework limit: true per-sibling opacity cross-fade is unavailable, so a few in-place widget
swaps (e.g. mixer channel editor) are instant; scroll, panel/dialog open-close, tab swaps, and
screen transitions all animate.

### DR-NFR-3 Responsiveness under load (R-LOADING)
Opening a multi-image project must never stall the UI. Image decode runs on a **background
thread** (`decodeWorker`, `linux_main.cpp:466-493`); the GTK thread consumes finished results in
entry order on a 15 ms poll (`pollLoad`, `linux_main.cpp:498-549`) so the loading animation holds
full frame rate and the progress bar reflects real decode progress. Preview rendering likewise
runs on the `RenderService` worker thread (`ARSTRO_ENABLE_THREADS`); the UI polls completed
frames with a non-blocking `tryAcquire` (`App.cpp:502`).

### DR-NFR-4 Non-destructive editing
No edit ever mutates source pixels. All edits are `EditParams` (data); the engine renders from
the decoded source + params on demand. Export re-renders at full resolution
(`EditSession::exportFullRes` → `RenderService::renderFull`, `EditSession.cpp:741-747`).

### DR-NFR-5 Logging & crash diagnostics (R-LOG)
The app writes a timestamped, levelled log to `<configDir>/cosmo.log`
(`ProjectStore::configDir()`), appended across sessions; every `g_print`/`g_printerr` is routed
through it via GLib print handlers (`linux_main.cpp:152-159, 836-841`). A fatal-signal handler
(SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL) appends an async-signal-safe backtrace and re-raises
(`log::installCrashHandler`). Logging is host-layer only (it does OS I/O).

### DR-NFR-6 Consistency locks
One accent (`palette::primary()` = `#4F7EF7`), one radius scale (`radius::hairline()=1`,
`radius::control()=2`), one type ramp, all from `Theme` (`apps/cosmo/Theme.h`). The `cosmo.` wordmark
is drawn identically everywhere (letter-spacing `-0.03·size`, accent dot after the measured
"cosmo" width) — home sidebar, top bar, and both transitions all call `App::drawWordmark`
(R-G-2a).

---

## 3. Home screen & projects (R-HOME)

### DR-HOME-1 Launcher
On start with no file arguments the app shows the **Home** screen (`Screen::Home` default,
`App.h:190`; `linux_main.cpp:881-886`). Home is `widgets/HomeScreen` — a 300 px sidebar
(wordmark, tagline, New Project / Open Project… / Import Catalog… actions, reserved
Settings/What's New/Help links, version) and a right pane with a "Recent Projects" header +
count + search box and an auto-fill `minmax(220px,1fr)` grid of project cards plus a dashed
New-Project card. HomeScreen is **not** part of the editor Segment tree; the App advances and
renders it separately (`App.cpp:463-476`).

### DR-HOME-2 Project file = catalog (`.cmp`) (R-HOME-2)
A project (`.cmp`) IS a workspace catalog that **references image files on disk** plus each
image's edit params, history, and the group tree. New/Open/Import all reuse the workspace
machinery (`saveWorkspaceAs`/`readWorkspaceFile`); `.cmp`, `.cosmoproj` are the same format.
Project "size" on a card is the sum of the referenced files' byte sizes
(`rememberProject`, `linux_main.cpp:439-461`).

### DR-HOME-3 New / Open / Import
- **New Project** → SAVE dialog for a `.cmp` name, `ensureCmp` appends the extension,
  `resetWorkspace()`, `saveWorkspaceAs`, remember, `showEditor()` (`linux_main.cpp:602-625`).
- **Open Project…** → OPEN dialog filtered to `*.cmp` → `startProjectLoad`, which reads the
  entries and hands them to `startEntriesLoad(…, saveOnFinish=false)`.
- **Import Catalog…** → multi-select image dialog, then a SAVE for a new `.cmp`; the picked paths
  become root-level `WorkspaceEntry`s and go through the SAME `startEntriesLoad(…,
  saveOnFinish=true)`, so an import plays the full R-LOADING transition and decodes on the
  background thread instead of freezing the UI inline. `saveOnFinish` makes `pollLoad` write the
  `.cmp` after the last image lands, since the file does not exist yet; the loading cover falls back
  to the first decoded image (no cached thumbnail for a never-opened catalog).

### DR-HOME-4 Recent projects (persisted) (R-HOME-6)
Recents persist in a tab-separated `recent.tsv` in the config dir, newest first, capped at 24
(`ProjectStore`, `kMaxRecents=24`). Each `RecentEntry` stores name, `.cmp` path (identity key),
first-image path, photo count, total byte size, last-opened epoch. `recents()` drops entries
whose `.cmp` no longer exists. Creating/opening a project calls `remember()` (de-dupe by path,
push front, rewrite). Each card's thumbnail is the project's first image, decoded and cached by
the host (`onDecodeThumbnail` → `downscaleCover(…, 480)` → `host.thumbs`, `linux_main.cpp:865-877`).

### DR-HOME-5 Search
The search box is a focusable `artboard::TextBox`; typing filters recents by
case-insensitive substring of the name; no matches shows the empty-state placeholder
(`HomeScreen.cpp:259-264`).

### DR-HOME-6 Reserved affordances
Settings / What's New / Help & Documentation links and the version string are drawn with hover
feedback but are inert (R-HOME-8).

---

## 4. Screen state machine & transitions (R-LOADING)

### DR-SCREEN-1 Three screens
`enum class Screen { Home, Loading, Editor }` (`App.h:159`). `render()` dispatches: Home →
render HomeScreen + `mScreenFade` scrim; Loading → `renderTransition`; Editor → `renderEditor`
(`App.cpp:456-480`). Input during Loading is fully swallowed (`App.cpp:418`, `key()` 1021,
`wheel()` 418).

### DR-SCREEN-2 Open transition — 3 phases (R-LOADING-0/1/2/3)
`beginOpenTransition(name)` sets `Screen::Loading`, `Phase::Intro`, animates `mIntro`→1 over
`kIntroMs=460 ms` (`App.cpp:710-729`). Phases (`enum class Phase`, `App.h:197`):

- **Intro** (pure animation, no I/O): the home `cosmo.` wordmark flies to the top-bar slot
  (46 px→13 px via `drawWordmark`), the clicked project's cover lifts from its card rect
  (`mCoverFrom`, from `HomeScreen::lastOpenCardRect`) toward center over a near-black star-sky
  (`kLoadingBg={0x0A,0x0A,0x0A}`), the project name grows (`sz=17+6·intro`), stars fade in.
  **The decode is already running behind it** — R-LOADPERF amended that, and the `onLoadingReady`
  hook this entry used to describe no longer exists in the code (it contradicted DR-LOADUX-4 in
  this same file; D-1). The decode starts when `CosmoService` dispatches `ProjectOpen`, whose
  `ProjectOpening` event is what calls `beginOpenTransition` in the first place, so the pool is
  already at work as the first intro frame draws. At `!mIntro.isAnimating()` the phase advances to
  Loading and the progress bar fades in.
- **Loading** (progress only): the decode continues and its results stream in; `setLoadProgress`
  eases `mProgress` toward `done/total`; the cover thumbnail fades in when supplied
  (`setLoadingCover` → `mCoverFade`). A minimum of `kMinLoadingMs=260 ms` keeps the bar from
  merely flashing (`App.cpp:806`).
- **Reveal**: when the load completes (`mLoadComplete`) and the minimum has elapsed,
  `beginReveal()` animates `mReveal`→1 over `kRevealMs=520 ms`. The editor materializes on the
  same dark backdrop (`fadeIn = clamp((reveal-0.40)/0.60)`) while the loading cover/stars/name/bar
  fade out in place (`fadeOut = clamp(reveal/0.45)`); the wordmark cross-fades
  (`drawWordmark(target,1.0,1.0-fadeIn)`) so it never doubles. At `!mReveal.isAnimating()`,
  `Screen::Editor`, `Phase::None` (`App.cpp:808-887`).

### DR-SCREEN-3 Return transition — 3 phases (R-LOADING-5/6)
`showHome()` from the editor runs `renderReturn` in three phases (`App.cpp:937-1004`):
**ReturnEnter** — the editor fades to the star-sky (`mEnterFade`→1 over `kEnterMs=300 ms`), shown
immediately on click; **ReturnLoad** — a `kReturnHoldMs=200 ms` star-sky beat during which
`refreshHome()` runs behind the shown loading screen (recents + thumbnails from cache, no
re-decode); **ReturnExit** — home fades in from the star-sky (`mExitFade`→1 over `kExitMs=320 ms`).
The wordmark flies from top-bar back to home (`mReturn`→0 over `kReturnMs=480 ms`); the sidebar
wordmark stays hidden (`HomeScreen::setWordmarkHidden`) until it lands.

### DR-SCREEN-4 Reset clears editor state (R-BUGFIX-2)
`resetWorkspace()` resets the session **and** the editor's visible state — `mLastAfterFrame={}`,
both photo `ImageView`s `clearImage()`, filmstrip `clearThumbs()`, breadcrumb `setPath({})` — so
the next project reveals from scratch, not through the previous project's leftovers
(`App.cpp:219-231`). It runs immediately after `beginOpenTransition`, before the new decode.

---

## 5. Editor shell

### DR-SHELL-1 Layout
The editor Segment tree under `mRoot`: `TopBar` (full width, `kHeight=29.25`), `LeftRail`
(collapsible preset dock, `kOpenWidth=196`, below the top bar), `RightColumn` (fixed
`kWidth=324`, right edge), and `CenterStage` filling the space between rail and column
(`App.cpp:168-199`). Modals (HistoryView, ContextMenu, PresetDialog, SettingsDialog,
ConfirmDialog) are full-window children drawn in the overlay pass. `layout()` re-runs every
frame so children reflow while the rail width animates (`App.cpp:489`).

### DR-SHELL-2 Top bar (`widgets/TopBar`)
Left: the `cosmo.` wordmark (click → `requestHome`, i.e. save-or-discard confirm then home) and
the `MenuStrip` menu bar. Center: the open project's name. Right: the current image filename +
`(i/n)` index, and the left-rail toggle button (`icon::panelLeft`). `setRailOpen` reflects rail
state on the toggle.

### DR-SHELL-3 Menus (`widgets/MenuStrip`) (`App.cpp:263-301`)
- **File**: Home; Open…; Save (Ctrl+S) → `saveWorkspace()`; Save As… (Ctrl+Shift+S) → workspace
  save dialog.
- **Settings**: Engine Settings… (modal); Reset Workspace.
- **Develop**: Copy Settings; Paste to Selected; Paste to All Images; Group Selection; Ungroup
  Selection.
- **History**: Undo (Ctrl+Z); Redo (Ctrl+Y); Show History Tree….
- **Preset**: Save Preset…; Import Preset….
At most one dropdown open; opening raises the TopBar so dropdowns win hit-testing
(`onOpenChanged`, `App.cpp:273`).

### DR-SHELL-4 Keyboard shortcuts (`linux_main.cpp:760-829`)
Text input is handled first (BackSpace + printable unicode to a focused field). On Home or during
a transition, all editor keys are swallowed. Editor shortcuts: **Ctrl+Z** undo (Ctrl+Shift+Z
redo); **Ctrl+Y** redo; **Ctrl+S** save project (Ctrl+Shift+S = Save As); plain **o** open;
plain **s** export PNG; **Delete** delete selected (R-PERSIST-2).

### DR-SHELL-5 Left rail & preset tree
`LeftRail` shows a "PRESETS" header over a scrollable `PresetTree`. The tree flattens
`cosmo::PresetNode` roots into rows; single-click toggles a folder, double-click applies a
preset (`onApply` → `applyPreset` + re-select), right-click opens a context menu. Rail open/close
animates its width over `kRailAnimMs=200 ms` driven by an `Observable<bool> mRailOpen`.

---

## 6. Center stage (`widgets/CenterStage`)

Vertical stack: `PhotoCanvas` (fills), `Breadcrumb` (`kHeight=22.75`), `Filmstrip`
(`kHeight=86`) (`CenterStage.cpp:21-35`).

### DR-PHOTO-1 Photo canvas
A `#0A0A0A` backdrop behind an `ImageView` (Contain fit). The main image shows the edited
("After") frame; Before shows the geometry-only baseline (`renderBefore`); Split shows the before
image clipped to the left half with a 1.5 px seam (`refreshPhotoForMode`, `App.cpp:233-252`;
`PhotoCanvas.cpp:75-105`). A Before/Split/After `SegmentedControl` pill sits bottom-center.

### DR-PHOTO-2 Zoom & pan (R-ZOOM)
Ctrl+wheel over the photo zooms about the cursor in 1.15× notches, clamped 1×–8×
(`App.cpp:423-438`, `PhotoCanvas::zoomAbout`); plain wheel does not zoom. While zoomed, press-drag
pans; before + after views share one zoom/pan so the split seam stays aligned
(`PhotoCanvas.cpp:133-137`). Each zoom step scales preview render resolution
(`EditSession::setPreviewZoom`) and resets to base at 1× or on image change. R-ZOOM-5 (eased zoom)
is deferred to an Artboard `ImageView` change; today zoom applies per notch.

### DR-PHOTO-3 Mask overlay (R-MASK)
When a local-adjustment mask is selected (Mask tab), `MaskOverlay` draws interactive geometry over
the photo, mapped to the ImageView's fitted rect including zoom/pan (`setFittedRect`). Radial:
drag center to move, edge handles to resize; Linear: drag the two endpoints; Brush: drag to paint
coverage dabs. It edits `MaskParams` geometry only, never pixels; `onChange` bridges to
`RightColumn::writeSelectedMask`. With no mask selected the overlay is fully click-through
(`hitTestSelf` returns false), so it never intercepts zoom/pan or the pill.

### DR-FILMSTRIP-1 Filmstrip
Horizontal strip of 86×62 photo cells and 78×62 dashed folder chips. A sliding accent ring marks
the primary cell (eased over 200 ms); per-cell hover cross-fades via `HoverFade`; horizontal
scroll eases over 180 ms. Click selects (`onSelect(cell, shift, ctrl)`), double-click activates a
group (drill in) via `onActivate`, right-click opens a context menu anywhere in the strip
(`onContext`, even on empty space). Thumbnails are a reused `ImageView` child pool
(`mThumbCount`); `clearThumbs()` restarts it in lockstep with the engine's reset slot ids
(R-BUGFIX-2).

### DR-BREADCRUMB-1 Breadcrumb
Shows the current group path (plus the edited image's filename). Every crumb but the last is
muted and clickable; `onCrumbClick(index)` navigates to that group. Per-crumb hover cross-fades.

---

## 7. Right column — the edit stack (`widgets/RightColumn`, R-EDITSTACK)

### DR-EDIT-1 Structure
Fixed 324 px column: `HistogramWidget` on top, then `EditStackTabs` with five merged pages, then
a pinned `ActionBar` (Save/Import/Export) at the bottom. The whole column paints `palette::card()`
so the active tab welds into the body. Every control's change callback funnels through
`mSession.curParams()` mutation + `mSession.submit()`; `syncToSlot()` pushes the current slot's
`EditParams` back into every panel (`RightColumn.cpp`).

### DR-EDIT-2 Tabs (R-EDITSTACK-1)
Five tabs, each label-sized (+even padding to fill the strip, never clipped): **Basic/Detail**
(index 0), **Mask** (1), **Mixer/Curve** (2), **Grade** (3), **Xform** (4). A 1.5 px accent bar
slides under the active tab (200 ms); page swaps cross-fade via an overlay scrim (180 ms). The
mask-overlay bridge keys off `maskTabActive()`, not a hard index.

### DR-EDIT-3 Histogram (`HistogramWidget`)
Draws the engine's real RGB + luminance histogram, always log-scaled, as three translucent area
plots plus a white luminance outline (`kHeight=94.325`). Fed each frame from the acquired
`Frame::hist` (`App.cpp:506`).

### DR-EDIT-4 Basic/Detail (`ParamPanel`)
A scrollable list of `SliderRow`s in titled sections: TONE (Exposure, Contrast, Highlights,
Shadows, Whites, Blacks), COLOUR (Temperature, Tint, Vibrance, Saturation), PRESENCE (Texture,
Clarity), EFFECTS (Dehaze, Grain Amount/Size), SHARPENING (Amount, Radius, Masking), NOISE
REDUCTION (Luminance, Colour), LENS (Distortion, CA, Vignette). Unit conversions happen in the
`RightColumn` callbacks, not the row: Exposure `v/80` (−400..400 → −5..+5 EV), Temperature
`6500 + v/100·3500` K, Tint `v·1.5`, sharpen Radius `v/10`; the reverse on `syncToSlot`.
Temperature/Tint rows carry colour-ramp tracks.
**Stacked-value reach (green #4cb573).** Each row's thumb marks the item's *own* value; when the
edit target sits inside groups, the slider also shows the **effective** value after all recursive
ancestor-group settings stack on top — a green reach from the thumb to `own + Σ(ancestor deltas)`
plus a thin end tick (Artboard `Slider::setSubValueOffset`; negative reaches left). `syncToSlot`
computes each row's offset as `effectiveEditParams − own` in slider units and pushes it via
`ParamPanel::setSubValues`; with no groups the offset is 0 and no green shows.

### DR-EDIT-5 Mixer/Curve (`StackPanel` → `MixerPanel` + `CurvePanel`) (R-BUGFIX-3)
- **Effective ("final") curve behind (both editors).** Like the sliders' green reach, each curve
  editor draws the **effective** curve — the item's own curve **summed** with its ancestor groups'
  (`effectiveEditParams`, i.e. `item(x) + group(x) − x` per axis) — `#4cb573` **behind** the
  editable one, so you see the final result after group stacking. Fed via
  `RightColumn::refreshCurveReferences` (`MixerPanel::setReference` / `CurvePanel::setReferenceCurves`),
  called on sync **and on every curve/mixer edit** so the "final" tracks the drag live; when it
  equals the own curve (no group curve) it is not drawn.
- **It is a readout, and it is drawn as one (D-28).** Originally it was a solid 1.5 px line — the
  same weight as the tone curve it sits behind — with no caption. Two lines of equal weight in one
  interactive plot is one line too many: a user reads the second as a curve to grab, and grabbing
  it does nothing, because the reference carries no nodes and is not in the input path at all. So
  it is now **dashed** (`widgets/DashedLine.h`, arc-length stepped so the rhythm survives tight
  bends), **1.0 px against the editable 1.5**, at **α 0.34 rather than 0.5**, and **captioned**
  `— final, with group` in muted text: the tone curve puts the caption in the strip below its plot,
  the hue editor in the free strip above its own. Dashing is what says "readout" without a legend;
  the caption is what stops the only way to find out being to try to drag it.
- **The reference never reaches input, and it leaves while you work (D-31).** `pointAt` /
  `handleAt` scan the active curve only, in both editors — but that was never the whole problem.
  Being drawn in space the plot treats as empty made it a **target**: a double-click on the green
  line adds a corner exactly on it (18 of 21 sampled points), so the edited curve snaps to touch the
  reference and the user has apparently grabbed and dragged it; and at either end the two curves
  share an endpoint, so a press there grabs the user's own node through the 13 px pick radius.
  So the readout now **fades out on a press inside the plot** and back in once the gesture is over —
  `mPressed`, plus a 220 ms `mRevealAtMs` hold so a double-click's two presses read as one gesture
  rather than flashing the line between them; out in 110 ms, back in 180 ms. A line that is not on
  screen cannot be aimed at, and it is present whenever the user is not editing, which is when they
  are reading it. The cost is that the "final" no longer tracks a drag *visibly* while the drag is
  happening — it is still recomputed throughout, so it is correct the instant it returns.
  Unchanged on purpose: a double-click still adds a corner where you click (the editing model), and
  a press within the pick radius of your own node still grabs it.
  Swept, not spot-checked: `curveReferenceIsNotEditable` walks 21 points along the reference and
  requires it absent at every one during a press, present at rest, absent through the hold, back
  afterwards, and adding no node anywhere. The first version of this guard asserted one point, and
  that point was one of the three where nothing happens — which is how D-31 shipped.
- **The line fades (R-G-1).** It appears and disappears through a 180 ms `AnimatedProperty` rather
  than blinking on the frame a group's curve starts or stops differing; each editor keeps the last
  drawable copy so the fade-out has something to fade. Note for tests: a `render()` with no
  `advance()` now draws no reference at all, which is why `greenRefStrokes` ticks the clock first.
- **MixerPanel**: a Hue/Sat/Lum segmented picker over one visible `HueCurveEditor` per channel
  (`EditParams::mixer[3]`). Each editor is a cyclic hue mapper: X = input hue [0,360), Y =
  adjustment [−1,1], wrapping at the 360/0 seam. Points are **bezier control points**
  (`CurvePoint{x,y,ix,iy,ox,oy,smooth}`); Alt-drag pulls tangent handles; double-click adds/removes
  a point. The editor emits control points (handles + smooth flag preserved); the engine flattens
  them with the shared `curve::sample()` for its LUT, so the drawn curve and the render never
  diverge, and a saved project restores the exact editable curve. A Reset button flattens the
  active channel.
- **CurvePanel**: an RGB/R/G/B picker + Reset over a tone-curve plot that edits with the **same
  UX as the mixer's HueCurveEditor** — points are **bezier `CurvePoint`s** (`EditParams::curve` /
  `curveChannel`, same model + `curve::sample` sampler): **corners** (straight segments) by
  default, no auto-easing; **Alt-drag a node** pulls symmetric tangent handles (a smooth spline),
  and dragging a handle shapes it (Alt breaks in/out symmetry). Double-click adds a corner on empty
  space / removes an interior node; endpoints are locked in x (0/1). Each channel has **its own
  independent curve**: **RGB** is the master (applied to all three channels), **R/G/B** each edit
  their own `curveChannel[0..2]`, applied after the master (`out_c = channel_c(master(x_c))`). The
  picker switches which curve is shown/edited; the plot's curve + nodes are drawn in the active
  channel's colour (accent for RGB, red/green/blue for R/G/B). Reset clears only the active channel.
  All four round-trip (bezier control points) through the session/sidecar file; legacy projects with
  plain `x,y` points load as corners.
- **Anchor/handle pick radius (both editors).** A drawn anchor dot is only ~4 px, which is
  fiddly to click and drag. The clickable target is a **forgiving radius, larger than the dot**,
  taken from ONE shared token (`metrics::anchorHitRadius`, 13 px) so the mixer hue curves and the
  tone curve feel identical. When two anchors' targets overlap, the **nearest** anchor within the
  radius wins (not the first found), so a click resolves to the point the user aimed at. The token
  also governs the bezier tangent-handle grab. Visual dot size is unchanged — only the hit area
  grew.

### DR-EDIT-6 Grade (`GradePanel`)
A Shadows/Midtones/Highlights region picker → hue (0..360) / saturation (0..100) / luminance
(−100..100) triplet per region, a Balance slider (−100..100), and a Hue-Remap sub-section
(Enable toggle + Source 0..360 / Range 0..180 / Target 0..360 / Strength 0..100). Writes
`EditParams::grade[region]`, `balance`, `remap*` (strength stored as 0..1).

### DR-EDIT-7 Xform (`XformPanel`)
A scrubbable rotation readout (drag = ±0.15°/px, clamped −45..45), −90°/+90° quarter-turn
buttons and a reset, and an aspect-ratio chip row (Free/1:1/4:3/16:9/3:2/5:4 → centered
normalized crop). Flip and Auto rows are drawn but inert (the engine has no flip/auto API).
Writes `rotation`, `quarterTurns`, `cropX/Y/W/H`.

### DR-EDIT-8 Mask (`MaskPanel`)
Three add-mask chips (Radial/Linear/Brush), a ComboBox to select the active mask, Invert and
Delete controls, a Feather slider (0..1), then Basic-style tone/colour/presence sliders scoped to
the selected mask's `LocalAdjust`. Per-mask controls are hidden until a mask is selected. The
overlay bridge (`selectedMaskParams`/`writeSelectedMask`) round-trips dragged geometry.

### DR-EDIT-9 Action bar (`ActionBar`)
A pinned Save / Import / Export row, always visible at the column's bottom (`kHeight=39`). In the
app these are wired to Save Preset, Import Preset, and Export Preset respectively
(`App.cpp:95-97`).

---

## 8. Group tree, selection & clipboard

### DR-TREE-1 Model
`EditSession` holds a group tree of `GNode{group,name,parent,slot,params,history,kids}` rooted at
"All Photos" (`EditSession.cpp:27`); a group node carries its own `EditParams`+`History` (its
settings), an image leaf its `slot`. Image leaves reference a slot index into the per-slot vectors
(`mSlotParams/History/Names/Paths/Sessions/Thumbs`). The filmstrip renders `currentGroupCells()`;
the breadcrumb renders `breadcrumbPath()`.

### DR-TREE-2 Selection & navigation
`selectNode(cell, shift, ctrl)` implements range (shift, from anchor), toggle (ctrl, keeps ≥1),
and replace (plain) selection. Selecting a group sets `mEditGroup`; selecting an image sets the
current slot, resets preview resolution, and submits. `navigateToGroup` drills in/out;
`createGroupFromSelection`/`ungroupSelected` restructure the tree; `deleteSelected` releases
engine slots for the subtree and re-anchors selection so the next image slides in (root protected).

### DR-TREE-3 Group settings (a group is an editable item; its edits stack onto members)
A group is itself a selectable, editable item: every `GNode` carries **its own full
`EditParams`** (not just a scalar offset) and its own branching `History`. Single-clicking a
group selects it for editing (`mEditGroup = node`); the develop panels then edit the **group's**
params (`curParams()` returns the group's params while `editGroup() >= 0`), and the canvas
previews a **representative member** — the group's first descendant image — rendered with the
stacked result, live, so the effect is visible as the group's sliders move.

`effectiveParams(slot)` = the image's own params **composed** with every ancestor group's params,
walking child→root (recursive; a nested group's settings stack on top of its parent's). The
composition (`arstro::composeParams(base, over)`, ImageProcessing) is defined per field so that
"a group with exposure +1 adds 1 to every member's exposure":
- **Additive** (the bulk): exposure, contrast, highlights/shadows/whites/blacks, tint, vibrance,
  saturation, texture, clarity, dehaze, grain amount/size, sharpen amount/masking, NR luma/colour,
  lens distortion/CA/vignette, grade hue/sat/lum per region, balance, rotation, quarter-turns —
  `result += over` (each field's neutral is 0). **Temperature** adds the group's Kelvin *offset*
  from neutral (`over.temp − 6500`); **sharpen radius** its offset from 1.
- **Tone curves** (RGB master + R/G/B) and the **mixer** curves stack **additively in Y**: at each
  sampled input the group's deviation-from-identity is added to the member's curve (an identity
  group curve is a no-op). Resampled for render only — the member's authored control points are
  untouched.
- **Masks** concatenate: a group's masks apply to every member (after the member's own).
- **Crop** does **not** stack (framing is inherently per-image); a group's crop is ignored. This
  is the one deliberate exception to "all develop settings".

Composition is used only to build the render/export params; each item's stored params stay its
own. Group params + history persist in the workspace (`#group` block, same params/`#hnode`
serialization as images); legacy projects with the old 12-scalar `offset=` line still load (mapped
into the group's `EditParams`).

### DR-TREE-4 Clipboard
Copy Settings stashes the current params; Paste to Selected / to All Images applies them to the
target slots as discrete history steps (`copyCurrent`/`pasteTo`).

### DR-TREE-5 Rename a group (in-app, animated)
A group is renamed inside the app (no native dialog), through **one** rename affordance — the
`ContextMenu`'s **rename mode** — reachable two ways:
- **Right-click a group** → the menu includes **"Rename Group"**; choosing it morphs the menu in
  place: the other items collapse away so only a **"Rename"** header remains, then an inline
  **textbox expands below it** (R1: one eased `AnimatedProperty`, ≈260 ms EaseInOut — items fade
  out in the first half, the box grows in the second; collapses instantly under `reducedMotion()`).
- **Click the group's name** in the top-right of the top bar (shown while a group is the edit
  target, DR-TOPBAR) → opens the same rename mode at that spot.

The textbox seeds with the current group name (e.g. "Group 1"), takes **keyboard focus with the
text selected** (first keystroke replaces it; Backspace clears the selection), and is styled as an
**elegant light field** — near-white background, near-black text (`palette::inputLight` /
`inputLightText`), one accent caret + selection wash. **Enter** commits (`onRename` →
`EditSession::renameGroup(node, name)`, then re-sync), **Escape** or click-away cancels. While a
rename textbox is focused the app's single-key shortcuts are suppressed (`App::isTextEditing`).

### DR-TOPBAR Group name in the top bar
When a group is the edit target, the top bar's right slot shows **"Group: <name>"** (in place of a
photo filename + counter), as a **clickable, hovered** affordance (R-G-3) that opens rename mode
(DR-TREE-5). For an image it shows the filename + "(i/n)" as before.

---

## 9. History (R-PERSIST-1)

### DR-HIST-1 Branching model
Each slot has a `History` — a branching DAG of `HistoryNode{params,parent,kids,label,seq}`
(`History.h`). `record(p, nowMs)`: identical params = no-op (navigation); within the coalesce
window (`coalesceMs=450 ms`) on a fresh childless leaf = overwrite in place (a whole drag is one
step); otherwise a new child (branching if the node already has children), labelled by
`describeChange` (the single changed preset category, "Adjustments" for many, "Edit" for none),
then `prune()` caps the tree at `maxSteps=100` keeping the current node + nearest ancestors +
most-recent branches.

### DR-HIST-2 Navigation
`undo()` → parent; `redo()` → the highest-`seq` child (newest branch); `jumpToHistory(node)` →
any node. The History modal (`widgets/HistoryView`, Show History Tree…) draws the tree git-log
style, click-to-jump, drag-to-pan, painted in the overlay pass while modal.

### DR-HIST-3 History persists with the project
`saveWorkspaceAs` writes each image's full history after its current-params block; `readWorkspaceFile`
parses it back into `WorkspaceEntry::history`; `applyParamsToSlot(slot, params, history)` restores
it via `History::restore()`, which rebuilds `kids` from `parent`, restores `current`, and resumes
`seq` past the highest loaded value. Old projects with no history load with a single root
(`EditSession.cpp:531-650`, `History.cpp:123-140`).

---

## 10. Presets

### DR-PRESET-1 Format & library
Presets are `.apf` documents produced by `editParamsToApf(params, categories, name)` and applied
by `applyApfToEditParams` (the engine's category-masked param serialization). `PresetLibrary::scan`
builds a folder/leaf tree of `.apf` files under the preset dir (folders-first, alphabetical).

### DR-PRESET-2 Save / export / import / apply
`savePreset(name)` writes `<presetDir>/name.apf` (restricted to `mPendingCategories` if a picker
chose a subset). `exportPresetTo(path)` writes anywhere. `importPresetFrom(path)` parses, rejects
a mismatched engine id, and returns the present categories for a picker;
`applyImport(categories)` applies the chosen categories to every selected slot as discrete
history steps. `applyPreset(name)` quick-applies to the current slot. A category-picker modal
(`PresetDialog`) backs save/export/import (R-PRESETPICK).

---

## 11. Persistence formats

### DR-FMT-1 Session (`.cosmo`, single image)
`image=<path>\n` followed by `serializeParams(effectiveParams(current))`. `readSessionFile`
extracts the `image=` line and `deserializeParams` (`EditSession.cpp:505-527`).

### DR-FMT-2 Workspace / project (`.cmp` = `.cosmoproj`)
Line-based, `key=value`, section markers:
```
cosmoworkspace=1
#group
parent=<id>
name=<name>
bypass=1                                ; optional (R-BYPASS-6); absent = filter enabled
offset=<12 comma-separated LocalAdjust scalars>
#image
parent=<id>
path=<source path>
bypass=1                                ; optional (R-BYPASS-6)
<serializeParams(params) …>            ; current params
hcurrent=<i>  hmax=<n>  hcoalesce=<ms>  ; history header (optional)
#hnode                                  ; one per history node, vector-index order
hparent=<i>  hseq=<n>  hlabel=<label>
<serializeParams(node.params) …>
```
Nodes are written in vector-index order so parent indices stay valid; the reader routes lines to
the current-params buffer until the first `#hnode`, then to each node
(`EditSession.cpp:531-650`). Backward compatible: files without `#hnode` load with a single-root
history; the mixer curve reads a 2-field token as a corner point and 6 fields as a smooth point
(R-BUGFIX-3).

### DR-FMT-3 Reset invariant (R-BUGFIX)
`resetWorkspace()` calls `RenderService::reset()` (which restarts engine slot ids from 0) and
clears all per-slot vectors before rebuilding the root group, preserving the "slot id == index,
grow-only" invariant that a prior monotonic counter violated (the reset-then-open segfault).

---

## 12. Rendering engine seam

### DR-ENGINE-1 RenderService
`EditSession` owns a `RenderService` and never touches `EditEngine` directly. `submit()` computes
`effectiveParams` and calls `render(slot, params)` (coalesced to the latest request); the UI polls
`tryAcquire(Frame&)` each frame. `renderFull` (export) and `renderPreviewSync` (before/after
baseline) block. The engine lives entirely on the RenderService worker thread under
`ARSTRO_ENABLE_THREADS`, degrading to synchronous otherwise. A `Frame` carries `rgba`, `width`,
`height`, the histogram, and the pre-curve / pre-mixer taps.

### DR-ENGINE-2 EditParams & pipeline
`EditParams` is the complete non-destructive description: tone (exposure/contrast/highlights/
shadows/whites/blacks), colour/presence (temp/tint/vibrance/saturation/texture/clarity),
effects (dehaze/grain), detail (sharpen/NR), lens, a tone `curve` + log flag, a 3-channel `mixer`
of `CurvePoint` bezier curves, a 3-region `grade` + balance + hue remap, transform (crop/rotation/
quarterTurns), and a list of `MaskParams` local adjustments. The engine applies a fixed pipeline
order (Crop → Rotate → Lens → NR → Exposure → Contrast → ToneRegions → WB → ToneCurve → Texture →
Clarity → Vibrance → ColorMixer → ColorGrading → Dehaze → Sharpen → Grain), then masks.

### DR-ENGINE-3 Decode
`NativeImageDecoder` decodes to top-down RGBA8 via GdkPixbuf (JPEG/PNG/TIFF, channels expanded to
RGBA) and, when built with `COSMO_HAVE_LIBRAW`, RAW formats (rw2/arw/cr2/cr3/nef/dng/orf/raf/pef/
srw/rwl/raw) via LibRaw. `openPaths` prefers a RAW over a same-stem JPEG when both are selected.

---

## 13. Known gaps & deferred (from the source)

- **R-ZOOM-5** eased zoom — deferred to an Artboard `ImageView` change; zoom currently steps per
  notch.
- **R-PARITY-CROP** interactive crop box on the photo — open (Xform exposes numeric crop only).
- **R-PARITY-SPLIT** draggable split divider — open (seam is fixed at 50%).
- **Import category picker** — import is "quick apply all present categories" plus a picker; a
  standalone import-only picker is a follow-up (`App.h:102-105`).
- **`onSaveRequested`** (`App.h:89`) is a declared-but-unwired seam; `App::renameGroup` is
  currently a no-op seam even though the dialog exists.
- **CurvePanel R/G/B** buttons all edit one shared curve; **XformPanel Flip/Auto** are inert;
  **HistogramWidget** has no Log/Linear toggle (always log). These are intentional per the source.

## 14. Filter bypass (R-BYPASS)

### DR-BYPASS-1 Model
`EditSession::GNode` carries `bypass`. `isBypassed(node)` / `setBypassed(node,on)` /
`toggleBypass(node)` read and write it; `editTargetNode()` / `editTargetBypassed()` answer for
whatever the develop panels are currently editing; `setSlotBypass(slot,on)` restores it on load.
Setting it marks the session dirty and re-renders the current slot immediately.

### DR-BYPASS-2 Composition
`effectiveParams(slot)` is the ONLY place bypass is applied for rendering: a bypassed leaf
contributes `EditParams{}` instead of its own params, and a bypassed ancestor group is skipped in
the fold. Preview, histogram, before/after and export therefore agree by construction.
`effectiveEditParams()` (panel "stacked reach" only) honours bypass on the ancestors but keeps the
edit target's own values — see `design.md`.

### DR-BYPASS-3 Context menu
`App::openEditContext` adds **Disable Filter** / **Enable Filter** for any cell (group or image);
the label names the current state's inverse. The action calls `toggleBypass` then
`syncControlsToSlot()`.

### DR-BYPASS-4 Edit-stack scrim
`RightColumn::setBypassed(bool)` drives an eased `AnimatedProperty` (180 ms, `EaseInOutCubic`,
`reducedMotion()`-safe). `RightColumn::onOverlay` washes `editStackRect()` — histogram bottom to
ActionBar top — at 62 % black and draws a `FILTER DISABLED` pill (ban icon + label) centred just
below the tab strip. Controls stay enabled: a bypassed item is still editable.

### DR-BYPASS-5 Filmstrip badge
`Filmstrip::Cell::bypassed` drives a per-cell eased amount (`advanceBypassFades`, 160 ms,
smoothstep). A bypassed cell gets a 52 % dark wash, a ban badge in its top-left, and its selection /
primary ring lerps from accent toward neutral grey.

### DR-BYPASS-6 Persistence
`bypass=1` is written on a node's `#group` / `#image` section in the `.cosmoproj`; absent means
enabled, so older projects load unchanged. `WorkspaceEntry::bypass` carries it to the host, which
passes it to `addWorkspaceGroup(..., bypass)` / `setSlotBypass(slot, bypass)` on both the
synchronous and the threaded load paths.

## 15. Export modal (R-EXPORT)

### DR-EXPORT-1 Chrome
`widgets/ExportDialog` — a focusable, self-drawn modal `Segment` on the PresetDialog chrome: scrim,
centred 560 px card, header (download icon + "Export" + ✕), clipped scrolling body, pinned footer.
Opens with an eased fade + 8 px rise folded into `cardRect()` itself, so layout, hit-testing and
paint can never disagree. Click-outside / ✕ / Cancel / **Esc** close it; it takes keyboard focus on
`show()` because `dispatchKey` only reaches the focused Segment. `App::isTextEditing()` reports true
while it is open so the host's plain-key shortcuts stay suppressed.

### DR-EXPORT-2 Tree
`show(nodes, preselect)` takes the group tree in pre-order (`App::openExportDialog` walks
`EditSession::nodes()` from the root's children). State lives on the image leaves
(`mLeafChecked`); `checkState(node)` derives a group's 0/1/2. The public model surface —
`rowCount()`, `rowIsGroup()`, `rowState()`, `toggleRow()`, `toggleExpand()`, `setAllChecked()`,
`selectedSlots()` — is what `handleGesture` calls after mapping a pixel to a row, and is what the
unit tests drive. The box shows exactly `kTreeRows`(8) whole rows and scrolls (eased) beyond that.

### DR-EXPORT-3 Destination
A **Same as source** checkbox sits at the right of the destination row (default on). On: each image
is written beside its own original and the path field is an inert hint. Off: one folder is used,
seeded from the first selected image's folder, changed via `onChooseDestination` →
host folder chooser → `setDestination()`. Two optional modifiers below — **Filename prefix** and
**Export to subfolder** — each a checkbox + label + hand-rolled text field (focus + `handleKey`, as
in `ContextMenu`'s rename box); the field is drawn muted and inert while its checkbox is off. A mono
preview line under the row shows the resolved path + example filename, front-ellipsized.

### DR-EXPORT-4 Format / size / quality
Format chips **JPEG · PNG · TIFF** (exactly what the GdkPixbuf writer produces). Size chips
**Original · 2048 · 1080 · 720 px** cap the long edge, downscale only. Quality is JPEG-only and is
laid out as **two rows**: a label row (`Quality` left, `NN%` right) and the slider on its own row.
The whole block disappears for PNG/TIFF.

### DR-EXPORT-5 Metadata
Three toggles with eased knobs, honoured by `ExportWriter` where the container can carry them:
EXIF copies the source JPEG's APP1 into a JPEG output; Strip GPS removes IFD0's `0x8825` entry from
that copy; sRGB writes an APP2 `ICC_PROFILE` (JPEG) / an ICC tag (TIFF) / `sRGB`+`gAMA` chunks
(PNG, whose GdkPixbuf saver rejects `icc-profile`).

### DR-EXPORT-6 The three export beats
**Animate first, export after.** `beginExport()` snapshots the `Request` and starts the collapse;
`advance()` fires `onExport` only once `mPhase` has stopped animating, so beat 1 is pure animation
with no I/O behind it (the split R-LOADING-0/1 uses). The host then runs the batch on a **worker
thread** (`ExportJob::worker`) — `exportFullResSlot()` blocks its caller inside
`RenderService::renderFull()`, so on the UI thread it would freeze a frame per image — and the UI
thread drains finished results via `pollExport` (`g_timeout_add(15)`), calling
`setExportProgress(done,total,name)`. `App::render` skips its own `tryAcquire`/`refreshPhotoForMode`
path while `App::exportInProgress()`, since `renderBefore()` re-enters the engine's single-slot
full-render channel that the worker is using. The dialog is modal and non-cancellable
(click-outside and Escape are both swallowed) for the duration. Three faces, tweened
through in order by `mPhase` then `mCompleteAmt`; `cardRect()` lerps the body height across all
three, so the whole sequence is one continuous shrink:

1. **Collapse** (`beginExport`, 260 ms `EaseInOutCubic`). The section header + tree survive; the
   master button leaves fast (`1 - clamp01(phase/0.4)`) and everything below it fades with the form.
   `progressTreeRect()` interpolates the tree's box from its form position/size to the manifest's, so
   exactly ONE tree is on screen — `drawForm` stops drawing the pair the moment `mExporting` is set,
   and `drawProgress` owns it. In `drawTree`, `progress` fades the checkboxes out and closes their
   slot so the icon + label slide left; the indent and chevron keep their place (shifting those too
   would push a depth-0 chevron out through the box's left edge).
   `buildExportRows()` rebuilds the row list as the **manifest** — selected images plus the groups
   that contain them — which is what makes the box genuinely shrink.
2. **Writing.** `progressBarRect()` sits under the tree, with a mono status line (file in flight,
   left; `done / total`, right) below it. Both appear only in the BACK half of the collapse
   (`clamp01((phase-0.55)/0.45)`), and the footer's buttons leave in the FRONT half — two strings
   sharing one slot must not cross-fade through each other. `advanceRowFades` eases a per-row
   "written" amount (a file row once the batch passes it, a group row once every member has landed);
   a written row gets a `successAlpha` wash, a green tick and a green-tinted name, the row in flight
   gets a `primaryAlpha` wash, and `followActiveRow()` scrolls the manifest to keep it visible.
3. **Done** (`beginComplete`, 260 ms). The tree, bar and footer content fade out, the card shrinks to
   `kCompleteBodyH`, and a green `checkCircle` + `Exported n photos` fades in. After
   `kCompleteHoldMs` (950 ms) `advance()` calls `beginClose()`.

`cancelExport()` returns to the form (a write that failed outright), leaving the picker intact.

### DR-EXPORT-8 Draggable card
`headerRect()` is the drag handle in every state. A `Down` there (outside `closeRect()`) starts a
drag; subsequent `Move`/`Drag` accumulate into `mCardOffset`, which `cardX()`/`cardRect()` apply so
layout, hit-testing and paint all move together. `cardX()` clamps so ≥120 px of the card stays on
screen and `cardRect()` clamps y so the header band is always reachable, which also keeps a dragged
card usable after a window resize. `mSuppressClick` swallows the `Click` a drag terminates with, so
releasing over the ✕ (or outside the card) never dismisses it. The offset resets on `show()`.

### DR-EXPORT-7 Bypass honoured
`EditSession::exportFullResSlot(slot,…)` composes the same `effectiveParams(slot)` the preview uses,
so a bypassed image or group exports without those edits.

## 16. Project-load performance (R-LOADPERF)

Measured on this machine (24 cores) with a 12 x 24 MP JPEG catalog: **2512 ms → 478 ms total**
(5.3x), **830 ms → ~0 ms of UI-thread work**, and — because the editor is now revealed on the first
image rather than the last — **2512 ms → ~225 ms before the project is usable**. First-image latency
alone rose slightly (181 → 225 ms): eight decoders share memory bandwidth, so image 0 no longer has
the machine to itself. That is the deliberate trade.

### DR-LOADPERF-1 Ordered parallel pipeline
`apps/cosmo/core/OrderedParallelLoad.h` — a UI-free, header-only template: `start(count, workers, window,
maxInFlightBytes, produce, weigh)` runs `produce(i)` on a pool, and `tryConsume(out)` hands items to
a single consumer **strictly in index order**. Ordering is mandatory because a `.cosmoproj` names a
node's parent by entry index. Back-pressure is two-fold — a window of entries ahead of the consumer
AND a cap on decoded-but-unapplied bytes — because a decoded 24 MP frame is ~100 MB.

**Deadlock-freedom:** workers claim indices with a monotonic counter, so a worker holding `i`
implies every index below `i` is already claimed; the index the consumer needs next is therefore
always either produced or held by a worker, and that worker is **exempt** from both limits. Verified
by `cosmo_core_tests` over 60 randomised (workers × window × byte-cap) combinations — with caps
deliberately smaller than a single item — and separately under ThreadSanitizer.

### DR-CPU-1 The budget, and how a percentage becomes a thread count (R-CPU-1, R-SVC-10)
**As of S1 the conversion happens in `ThreadBudget`, once.** `ThreadBudget::total()`
(`core/ThreadBudget.cpp:39-43`) is `clamp((cores * percent + 50) / 100, 1, cores)` — integer
round-to-nearest, never zero, never more than the machine — with `hardware_concurrency()` falling
back to 4 when it reports 0 (`ThreadBudget.cpp:16-20`). Percent is the user-facing unit and a count
is what is enforceable, because no portable per-process CPU-time cap exists across the platforms
cosmo targets and a sleep-based throttle would occupy the cores it is trying to spare. The default is
50, in `AppSettings::cpuPercent` (`core/AppSettings.h:29`), persisted as `cpuPercent=` in
`settings.txt`; a value outside 1..100 falls back to 50 rather than clamping up, in both
`AppSettings::load()` (`AppSettings.cpp:52`) and `ThreadBudget::setPercent()`
(`ThreadBudget.cpp:27-31`), since an out-of-range budget means a corrupt file, not a request for the
whole machine.

`AppSettings::workersFor(percent, cap)` (`core/AppSettings.cpp:14-24`) still exists and still has its
test (`cpu_budget_scales_with_percent`), but it is **legacy**: a pure function that each consumer
called for itself is exactly how the budget came to be applied twice (D-11). It has no callers in the
app any more.

### DR-CPU-2 Where the budget is enforced, and how it is divided (R-CPU-2, R-SVC-10)
**One total, divided — not converted per consumer.** `Host::budget` (`linux_main.cpp:140`) is told
the percentage and the explicit thread count once at startup (`linux_main.cpp:1280-1296`) and again
on every settings change, and it owns the engine's width from then on.

1. **Decode pool** — `ProjectLoader::start` (`core/ProjectLoader.cpp:16-39`) takes its size from
   `ThreadBudget::beginLoad()`, which **reserves** that share for the load's duration;
   `startEntriesLoad` (`linux_main.cpp:642-690`) logs
   `load: <n> entries on <w> decode workers, engine <e> threads (cpu budget <p>% = <t> of <c> cores;
   decode cap 8)`.
2. **Engine threads** — `ThreadBudget::apply()` (`ThreadBudget.cpp:75`) is the only call to
   `par::setThreads` for the budget: `engineThreads()` is the whole total when idle and
   `total - reserved` while a load runs, floored at 1 so an arriving photo can still render a preview
   (R-LOADPERF-3 shows the editor during the load). An explicit CPU-threads choice (2/4/8) wins for
   the engine per R-CPU-2b, and may exceed the budget — a deliberate override, logged rather than
   clamped. `App::applyThreadBudget` is **gone** (S1b): `App::applySettings` (`App.cpp:851-861`) sets
   the preview edge and the GPU preference and deliberately does not touch the engine's width, and the
   Settings dialog's thread controls only report the choice through `onSettingsChanged` — notified
   *before* `mSession.submit()` (`App.cpp:168-183`), so the re-render uses the new width rather than
   the old one. One owner in code as well as in principle.
3. **Nested library parallelism (LibRaw's OpenMP)** — `cosmo_v2::pinNestedOpenMPForThisThread()`
   (`OmpPin.cpp:47-50`), passed to `ProjectLoader` as the per-worker start hook
   (`linux_main.cpp:680-685`) and therefore called **on each decode worker**. It resolves
   `omp_set_num_threads` with `dlsym(RTLD_DEFAULT, …)` / `GetProcAddress` over the loaded OpenMP DLLs
   (`OmpPin.cpp:19-45`), so nothing links OpenMP and the pin is a no-op where LibRaw has none.
   `ompPinStatus()` is logged at startup, because the previous mechanism was assumed rather than
   checked: **`g_setenv("OMP_NUM_THREADS", "1", FALSE)` in `main()` never worked at all** — libgomp
   parses the environment in a load-time constructor that runs before `main`, so the assignment was
   read by nobody (D-12, proven by `core/tests/fixtures/omp_env_order.c`). The thread count is a
   per-thread ICV, which is why it must be set on the worker rather than once at startup.

**Measured, not asserted (R-CPU-4 as amended).** `ThreadBudget::producerEnter/Exit` keep
`peakDecode()`, a lock-free high-water mark of producers inside their work at once, and the load's
completion logs it next to the allotment (`linux_main.cpp:624-628`).

### DR-SVC-10 What the division actually measures (R-SVC-10, R-CPU-4)
Two tests in `cosmo_core_tests`. `one_budget_is_divided_not_duplicated` asserts, for cores ∈
{2,4,8,12,16,24,32} × percent ∈ {1,25,50,75,100}: an idle engine gets the whole budget; a load's pool
is ≥1 and ≤8; the engine keeps ≥1 during the load; **decode + engine ≤ total** (the two floors are
the only slack, and only when the whole budget is one thread); the engine is restored afterwards; an
explicit count overrides Auto. It fails on the pre-S1 arithmetic at exactly that assertion.
`project_load_peak_never_exceeds_its_pool` runs an 18-entry load — the reported case — through a
sleeping fake decoder at 25% of 24 cores and asserts results arrive in entry order, thumbnails were
built on the workers, the per-worker hook ran once per worker, `peakDecode() <= pool`, and
`peakDecode() + engineDuringLoad <= total`. Measured on this host (24 cores, real RAF files, real
LibRaw): 25% → pool 5, peak 5, 4.51 cores mean = **19% of the machine**; 100% → pool 8 (the cap
binds), 6.05 cores mean = 25%.

### DR-SVC-1 The service, and what a front end may do (R-SVC-1/2/3/4)
`CosmoService` (`core/service/CosmoService.{h,cpp}`) owns the application: the edit session
(borrowed in S2, see §2.4e of `detailed_design.md`), the project load, the export pass,
settings, recents and the group tree. Its whole interface is `dispatch(Command)` /
`dispatchText(line, err)` / `pump(nowMs)` / `model()` / `subscribe(sink)`. The GTK host no
longer contains a load: `onServiceEvent` (`linux_main.cpp:548-644`) translates the event stream
into App's animation calls and decides nothing, and `startProjectLoad`/`startImportLoad`
(`linux_main.cpp:646-664`) build a `Command` and dispatch it. `onTick` pumps the service once
per frame, unconditionally (`linux_main.cpp:1042-1052`) — the old code added and removed a
dedicated 15 ms GTK source per load, and a service that is always pumped cannot forget to be.

Two App entry points exist for the adapter and are deliberately narrow: `registerThumb(slot)`
(the filmstrip's thumb pool is indexed by slot, so it must be fed in lockstep with attachment)
and `syncFromSession()` (re-push the panels when a *command* changed the edit state from
outside the widgets — a script, or an agent on the control socket). Screen ownership stays with
App in S2 because it drives the transition phases, which are presentation; S4 makes
`AppModel::screen` authoritative.

### DR-SVC-2 One grammar, generated from one struct (R-SVC-2/5)
`parseCommand`/`formatCommand` (`core/service/Command.cpp:92-330`) are the only parser and the
only formatter, and `formatEvent` (`core/service/Event.cpp:35-70`) is the only event
serializer. Verified by `command_text_roundtrips`, which asserts `format(parse(x))` re-parses to
the same kind AND is a fixed point for all 23 documented lines, that a blank/`#` line is a
successful no-op while garbage names its error, and that `"quoted names"` survive.

### DR-SVC-3 A project opens with no UI at all (R-SVC-1/2/3)
`service_opens_a_project_with_no_ui` builds a 7-entry `.cmp` (one group, five decodable images,
one missing), dispatches `project open`, pumps at a fixed 16 ms tick, and asserts: the loading
surface appears, the editor is reached, five images attached, the group is the parent of the
images, the missing one reads `failed` and not `pending`, the budget resolved to 2 of 8 cores at
25%, and the event stream contains `project.opening`, `entry.decoded`, `entry.failed`,
`load.finished decoded=5 total=7`, `project.opened` and `screen.changed editor`. No window, no
Artboard, no click — which is what D-6 made impossible and what let D-11/D-12 ship unmeasured.
`commands_drive_the_session` covers select/set/undo/redo/next/prev/bypass/group/settings/screen/
save plus rejection, each asserted against the model.

### DR-SVC-8 The control socket (R-SVC-8)
`cosmo --control <path>` opens a `cosmo_v2::ControlChannel` (`apps/cosmo/ControlChannel.{h,cpp}`)
— a non-blocking `AF_UNIX` listener polled from the frame tick. `--control` is pulled out of argv
before `openPaths()`, or the socket path would be treated as a photo to open
(`linux_main.cpp:1329-1344`). The channel moves lines and nothing else: no parsing, no dispatch,
no service knowledge, which is what keeps R-SVC-5's single codec true. Events reach clients through
a second `subscribe()` sink that broadcasts `formatEvent(e)` — the identical line the log gets.

`pollControl` (`linux_main.cpp:1052-1085`) parses each line with the one parser and dispatches it
into the SAME `CosmoService::dispatch` a click produces, on the UI thread between frames, so there
is no locking and an agent-driven change is indistinguishable from a clicked one. Two kinds are
answered by the host rather than the service, because only the caller knows where to print and who
owns the loop: `state print` (framed by `[evt] state.begin`/`state.end`, since `formatModel` is
multi-line) and `wait`, which is the client's own loop.

Liveness is time-based, not size-based: a client is dropped after 120 consecutive frames (~2 s) in
which its pending output did not shrink by a byte, plus a 16 MB hard cap as a memory bound. A
byte-cap alone drops healthy clients, because 6000 events in 4 ms outruns any peer momentarily.
Reads are capped at 256 KB per client per frame and accepts at 8 per frame — "non-blocking" is not
"bounded", and an undrained read loop would freeze the window without one blocking call.

`cosmo-cc attach <socket> [--script f] [--watch]` is the client (`cli/main.cpp`, `cmdAttach`) and
the only subcommand that builds **no** service — everything else in `cosmo-cc` *is* an application,
this one is a terminal on somebody else's, which is the asymmetry that keeps pixels in the process
that owns them. `wait <event>` is handled client-side because a wait is a property of the caller's
loop. It exits non-zero if any command was rejected or a `wait` timed out, so a script that
half-worked fails a build instead of looking fine. It drains for 600 ms of stream silence after the
last command: a command's events arrive *after* the send, so ending the loop when the script ends
drops every response to the final commands — including the `state print` that was the point of the
run.

**Verified live** on the reported 18-RAF project: the GUI was driven entirely over the socket
through `settings set` → `project open` → `wait load.finished` → `select next` → `set exposure=0.8`
→ `state print`, returning `load.finished decoded=18 total=18`,
`info load.peak decode=5 engine=6 budget=6`, `selection.changed node=2 slot=1`,
`params.changed exposure`, and a model dump reading `imageCount=18 currentSlot=1 canUndo=1`. A
window capture confirmed the UI followed: `DSCF5194.RAF (2/18)` in the top bar, the Exposure slider
moved, 18 thumbnails in the filmstrip. Repeated through `cosmo-cc attach` with a longer script —
`select next` twice, `set exposure=1.4 temp=7800`, `undo`, `state print --stable` — returning
`selection.changed node=2 slot=1` then `node=3 slot=2`, `params.changed exposure,temp`,
`history.changed current=0 nodes=2 label=Open`, and a dump reading `currentSlot=2 canUndo=0
canRedo=1`. The window showed `DSCF5288.RAF (3/18)` with Exposure back at 0, the undo having
landed.

**Windows is a deliberate stub**: `open()` returns false with a reason, so `--control` reports
itself unavailable at startup rather than hanging later. An overlapped named pipe is a few hundred
lines whose failure mode is a hung UI thread; `PIPE_NOWAIT` is not a shortcut because it silently
drops data when the buffer fills. The header records both routes, and notes that Winsock `AF_UNIX`
(Win10 1803+) would make the POSIX branch nearly portable as-is.

### DR-SVC-2a The view's outbound channel, and what still bypasses it (R-SVC-2)
`App::onCommand` (`App.h`) is the seam a widget or shortcut uses to ask for a behaviour the
service owns; `emitCommand()` returns false when no service is wired, which is how a bare `App`
in `cosmo_widget_tests` keeps working. Four methods route through it today — `undo`, `redo`,
`deleteSelected`, `renameGroup` — each falling back to the direct session call only when
unwired. `linux_main.cpp` wires it to `CosmoService::dispatch`, so a menu item, a keyboard
shortcut and a line on the control socket are one path.

**What still bypasses it, stated as a number because "the UI contains no logic" is otherwise
unfalsifiable:** `widgets/RightColumn.cpp` makes 60 direct `EditSession`/`EditParams` accesses
and the host makes 2 `svc->session()` calls. Both must reach zero for S to be done
(`service-architecture-proposal.md` §5). Note that counting `mSession.` in `App.cpp` measures
nothing, since a converted method keeps its fallback — see `PROGRESS.md` S4b for why that metric
was replaced.

### DR-SVC-2b What needs a command, and what `set` already reaches (R-SVC-2)
Worth stating because it was got wrong once from reading: **`set` already addresses every scalar,
every curve, the colour mixer and colour grading**, because `EditParamsIO` names them —
`set curve=0,0;0.5,0.62;1,1`, `set mixer0=…`, `set grade0=210,18,-4`,
`set rotation=1.5 balance=12 remapEnable=1`, `set mask=<blob>` (which **appends**). A new
adjustment is therefore shell-reachable the moment it round-trips through the params codec, with
no command work at all.

The one thing the codec cannot express is addressing an existing mask **by index**, so
`Command::MaskSet` / `MaskDelete` exist for exactly that (`CosmoService.cpp`, the `MaskSet` case):
`mask set <i> feather=0.42 inverted=1 adjust.exposure=0.75`, `mask delete <i>`, with geometry
(`cx/cy/rx/ry/x0/y0/x1/y1`), `type`, and the twelve `adjust.*` fields. An out-of-range index is
rejected by reason — `mask: no mask 9 (have 1)` — rather than clamped, because silently editing a
different mask than the caller named is worse than refusing.

### DR-SVC-9a Every command kind has a grammar (R-SVC-9)
`every_command_kind_has_a_grammar` pairs each of the 22 `Command::Kind` values with a
documented line, asserts the line parses to that kind, that no kind is listed twice, that all
22 are covered, and that `commandNames()` has exactly 22 entries. `kKindCount` is asserted
against the table's size, so **adding a Kind fails this test until its parser rule and its
documented line exist** — without which the struct path would work while the CLI and the
control socket could not reach the behaviour, which is the divergence the architecture exists
to prevent.

### DR-HOME-11a The recents header row shares one line (R5 / R-SCALE-3)
The header holds a title block (clock glyph + `Recent Projects` + the visible count) and a search
field on one row. Both used to be positioned independently — the field pinned right at a fixed
168 px, the title drawn from the left knowing nothing about it — so at the minimum window width the
title ran straight under the box. Two things cannot both be fixed on one row.

The title is the more important, so it is measured first and the field takes what is left:
`searchRect()` starts at `max(titleEnd + 12, W - pad - 168)` and keeps `max(72, W - pad - x)`. When
even that floor does not fit, `headerTitle()` gives up the long form and returns `Recent` rather than
the field disappearing. `titleBlockW()` measures with the same `estimateTextWidth` the paint draws
with — a box measured one way and drawn another is how the overlap got in. `minContentWidth()` now
sums the header's own floor as well as the grid's and takes the larger, so a future sidebar or title
change moves the minimum instead of breaking the row.

Found by rendering the launcher at `App::minPhysical*` for each screen scale. Guarded by
`homeHeaderTitleNeverRunsUnderTheSearchField`, which **sweeps** every width from the published
minimum to 2200 px (the failure is a threshold, so a spot check at the minimum proves nothing about
the widths either side) and asserts one verdict per property rather than one per width; it also
requires that all three responses — long title, short title, shrunken field — actually occur
somewhere in the range, so the test cannot pass because a branch is unreachable.

### DR-SCALE-1 `AppSettings::uiScale` — the persisted screen scale (R-SCALE-1)
`int uiScale = 100`, percent, in `core/AppSettings.h` beside the four engine preferences. Percent
rather than a double because `settings.txt` is plain `key=value` text a person may edit and "90"
cannot round-trip wrong the way "0.8999999" can. `AppSettings::uiScales()` is the one list of legal
values — `{75, 90, 100, 125, 150, 175, 200}` — and `clampUiScale(p)` snaps to the nearest, applied on
**load**, so a hand-edited or corrupt value becomes a scale the app has actually been laid out and
rendered at rather than being range-checked into something arbitrary.

The range runs both ways because "small screen" is two different problems. 75 / 90 are for a screen
small in **pixels** (a 1366x768 panel cannot show the editor's three columns and a usable canvas at
the Figma sizes, so the shell is drawn smaller to fit). 125 through 200 are for a screen small in
**inches** but dense (a 7-inch 1920x1200 panel has pixels to spare and controls too small to hit, so
the shell is drawn bigger). 200 is the top because it is what makes such a panel usable and its
window minimum, 1168x932, still fits one.

Reachable from a script as `settings set uiScale=N` (the existing `SettingsSet` grammar needed no
new command) and readable in a dump as `settingsUiScale=`. `CosmoService::applySettingsFields`
stores and echoes it and **acts on nothing** — the only settings key with no engine call beside it,
because what it scales is the view's layout and R-SVC-3 forbids the service to know that exists.

Guarded by `settings_roundtrip_and_survive_a_bad_file`: the value survives a restart, a file that
predates the setting renders at 100, `uiScale=83` loads as 90, `uiScale=1000` loads as 125 (snapped
to the largest, not through it), and every offered scale is a fixed point of the snap.

### DR-SCALE-2 One transform, one coordinate system (R-SCALE-2)
`App` owns `mUiScale` (percent) and keeps **two** sizes: `mPhysW/mPhysH`, what the window last
reported, and `mW/mH`, the **logical** box every widget lays out in — `physical / scale`, floored at
`minLogical*`. Three members carry the whole feature:

- `rootTransform()` = `Transform::scaling(s, s)`, installed **everywhere** the render path used to
  install `Transform::identity()` (ten sites in `App.cpp`) and passed as the parent transform to
  every `render` / `renderOverlay` call. Both halves are required: `Segment::renderContent` computes
  `parent.mul(localTransform())` and calls `setTransform(world)`, and `setTransform` is **absolute**
  — which is also the reason the scale cannot live in the GTK layer as a `cairo_scale`, since the
  first identity reset inside `App` would wipe it.
- `toLogical(x, y)` = the inverse, applied once at the top of `pointer()` and `wheel()`. No widget
  hit-tests in physical pixels, because none of them ever sees one.
- `setUiScale(percent, animate = true)` sets the **target** and eases the **drawn** scale to it.

**The scale eases (R-SCALE-2a / R-G-1, D-29).** `mUiScale` is the target — a preference is a number,
not a motion, and `uiScale()` reports it — while `mScaleAnim` (an `AnimatedProperty`, `kScaleAnimMs`
= 260, `EaseOutCubic`) carries what is drawn. `scale()`, and therefore `rootTransform()` and
`toLogical()`, read the eased value; `App::render` ticks it and, **while it is moving**, calls
`applyLogicalSize()` every frame so the logical box and every widget's layout are re-derived from
the eased scale. Deriving them once at the start would animate the transform and leave the panels at
the old size — the shell would zoom and its contents would not, which is worse than a snap.
`minPhysical*` deliberately uses the **target** scale, so the window's minimum does not wobble
mid-tween. `applySettings` passes `animate = false`: the startup apply has no previous scale to
travel from and a tween there is an entrance nobody asked for. `drawnUiScale()` is public because the
difference between it and `uiScale()` mid-tween is the only thing that can tell an eased
implementation from a snapping one, which is what `cosmo_ui_tests` asserts.

No widget reads the scale and no constant is multiplied at its use site — a per-widget factor is how
a layout acquires two truths. `App::applySettings` sets it before the first render. The splash is
scaled the same way in `linux_main.cpp` (its Segment keeps the 420x260 design size; the window is
sized `x scale`).

`linux_main.cpp` bridges the control socket to the view: on `Event::Kind::SettingsChanged` it
compares `svc.model().settings.uiScale` with `app.uiScale()` and applies the difference. Needed
because the service deliberately does nothing with the value (DR-SCALE-1) — without the bridge
`settings set uiScale=125` would change the stored number, print it in a dump and leave the window
untouched, which is worse than not having the command.

### DR-SCALE-3 The minimum window, and the rail that folds before the canvas (R-SCALE-3)
`App::minLogicalWidth/Height()` are the shell's floor, and they take the **larger** of the launcher's
and the editor's needs. The launcher's has been summed from its own anchored blocks since D-26; the
editor's never existed, which is how the photo canvas could be dragged to 64 px wide with the rail
still open. The editor's floor is `RightColumn::kWidth + kMinCanvasW` wide (324 + 260) and
`TopBar::kHeight + kMinCanvasH + Breadcrumb::kHeight + Filmstrip::kHeight` tall — the fixed rows that
cannot be given up. `App::kMinCanvasW/H = 260x220` is the one place the canvas's floor is written.

Enforcement is the **window**, not the layout code: `applyWindowMinimum()` in `linux_main.cpp` asks
GTK for `minPhysical* = minLogical* x scale`, and re-asks on every scale change, so a bigger scale
requires a bigger window instead of quietly breaking. Across the seven offered scales that is
438x350 / 526x420 / 584x466 / 730x583 / 876x699 / 1022x816 / 1168x932.

**A scale the display cannot honour is offered disabled.** 1168x932 does not fit a 1366x768 panel,
so with the range reaching 200% it is no longer true that every scale fits every screen. The host
reports the monitor **work area** (`gdk_monitor_get_workarea` — a dock or a title bar is height the
window will never get) via `App::setDisplaySize`; `App::scaleFitsDisplay(pct)` is the rule, and
`openSettingsDialog` turns it into the single `maxScale` the dialog needs — one number suffices
because the constraint is monotonic, the logical minimum being fixed. Chips above it draw at 0.4
alpha and ignore clicks, exactly as the GPU row's "On" does with no backend, and the row label gains
`, up to N% here` only when there is a cap. A display size of 0 means "unknown" and disables nothing,
so a headless harness sees the full range; the host logs which of the two happened, because "every
scale is offered" and "we could not find out" look identical in the dialog and only one of them means
a 200% chip is safe to click. `setSize` still clamps, for the cases the host cannot hold (a tiling WM, an offscreen
harness, `--size` below the minimum): a shell drawn slightly cropped is usable, one that refuses to
size is not.

**The rail folds before the canvas does.** Two flags, and the distinction is the point:
`mRailWanted` is the user's intent and only `toggleRail` changes it; `mRailOpen` is what the window
can afford and is **derived** every `layout()` as `mRailWanted && roomForRail()`. It eases both ways
through the observable the toolbar toggle already drives (R-G-1), and `Observable::set` early-returns
on an unchanged value so deriving it every frame starts no animation. Keeping only the effective
flag — the first version — meant a window dragged narrow and then wide again had thrown the user's
choice away and they had to hunt for the toggle.

Shots: `scale-{75,90,100,125}-{home-min,editor-min,settings-min}` render each screen at exactly
`minPhysical*` for its scale — the smallest window that scale permits, and therefore the frame where
three fixed columns and a floor-bound canvas have the least room to disagree — plus
`-editor-1280x800` to show what the setting is for. All fixture-free, so they run in
`cosmo_shots_headless`. `settings-min` in particular proves the dialog that sets the scale never puts
its own Done button off the bottom edge.

### DR-SETTINGS-1a The dialog's chip rows wrap (R5/R6, D-30)
Seven scale chips at ~52 px do not fit a 324 px card interior, and the old `chipRects` laid chips on
one unbounded line — so they would have run off the card's right edge, silently. Chips now **wrap**
at the card's inner width and each row's height follows: `rowBlockH(row) = kLabelH + 6 + lines *
kChipH + (lines - 1) * kChipRowGap`, `blockTop` sums the previous rows rather than multiplying a
constant, and `cardRect` sums all of them — so the card grows only as much as it needs (366 -> 430 px
for five rows, one of them wrapped 5 + 2) and cannot clip its own Done button. Generic, so the next
long row is already handled.

One walk serves both questions: `layoutChips(row, left, top, out)` returns the line count and, given
an origin and a vector, also fills the boxes. That split is load-bearing — see **D-30**: the first
version had the measure path (`rowLines` → `chipRects`) ask `cardRect()` for its wrap bound, and
`cardRect()` needed `rowBlockH` → `rowLines`, so "how big is the card" and "where do the chips go"
each waited on the other and the dialog crashed the moment it was drawn. The wrap bound is `kCardW`,
a constant; only the chips' absolute positions need the card's position.

### DR-UITEST-1 `cosmo_ui_tests` — assertions over the assembled app
A ctest target (`apps/cosmo/tests/ui/uiTests.cpp`) that builds the real `App` over a real
`CosmoService` from `COSMO_APP_NOMAIN`, with no display and no window, and drives its clock through
`render()` into an `artboard::RecordingTarget`. It exists because of **D-29**: R-G-1's compliance
clause says a snap is ruled out by comparing frames, not by reading code, and `cosmo_widget_tests`
cannot do that — it builds widgets in isolation, with no App, no service and no clock. Same
libraries as `cosmo_shots`, for the ODR reason that target's comment records.

Covers today: `scaleChangeIsAnimatedNotSnapped` (the drawn scale passes through several values
strictly between old and new, the logical size changes on those same frames, and it arrives exactly
on target), `theStartupScaleDoesNotAnimate`, and `everyScaleLaysOutAtItsOwnMinimum` (at each offered
scale, size the window to `minPhysical*` and require the logical box to meet the published floor and
`CenterStage` to keep `kMinCanvasW`). The last uses `findSegmentByType` from `UiDump.h` to name the
widget rather than indexing a child list whose order is a layout detail.

### DR-SVC-11 `ui dump` — the view as text (R-SVC-11)
`apps/cosmo/UiDump.{h,cpp}` walks an `artboard::Segment` and prints one line per node: demangled
type (RTTI, so a widget cannot forget to declare a name it then lets rot), **world** rect (a local
x/y says nothing once a parent has moved), size, `opacity`, `visible`, an explicit `SHOWN=no` when
`isFadedOut()`, plus hover / disabled / clip and the child count. `--json` emits the same fields as
an array, `--visible` prunes faded subtrees (off by default — *why is it not showing* is the
commonest question and a filter that hides the answer is worse than a longer dump), `--depth N`
truncates.

`App::uiRoot(name)` / `App::uiRootNames()` expose **two** roots, because there really are two: the
editor tree and the home screen are drawn by different branches of `render()` and neither contains
the other, so a single "the root" accessor would silently return half the UI. The **splash** is
dumped by the host, which owns it — it lives in its own borderless window and is in neither App
root. `--root` selects one; the default dumps all three; a root that does not exist right now
answers `ui-root <name> (absent)`, which is itself the answer to "has the splash gone yet".

`UiInspectable` (`apps/cosmo/UiInspectable.h`) is the opt-in escape hatch for widgets that paint
themselves: one virtual returning a line of `key=value`, found by `dynamic_cast` (affordable — the
dumper already uses RTTI). Cosmo-side rather than a virtual on `artboard::Segment` because
debugging cosmo is not a general UI engine's job, and `core/Artboard` is a submodule whose every
change costs a second commit. `SplashScreen::uiDetail` is the first implementor and is what turned
DR-SPLASH-5's invisible progress bar from a suspicion into a timeline.

Host-side in `pollControl`, framed `[evt] ui.begin` / `[evt] ui.end` for line-oriented clients,
exactly like `state print` and for the same reason (R-SVC-3). `cosmo-cc` headless answers
`(no view attached)` and exits 0, so one acceptance script runs through both front ends without
branching on which it is.

### DR-SVC-9 Two front ends, one state (R-SVC-9)
`two_services_dump_the_same_state` runs the same command sequence twice — once pumped 120 extra
frames, standing in for a GUI that has animated longer — and asserts the `stable` dumps are
byte-identical, while the non-stable dumps do differ (or the exclusion would prove nothing).
`ModelDumpOptions::stable` is therefore the comparison artifact, and `--json` is asserted to be
well-formed enough to start with `{` and contain the `nodes` array.

### DR-MASK-5 Mask geometry is not bounded by the framed image (R-MASK-5)
`MaskOverlay::localToNorm` (`widgets/MaskOverlay.cpp`) no longer clamps to 0..1 — that clamp was
the entire defect. Normalised framed-image coordinates are a coordinate *space*, not a boundary:
a radial mask larger than the frame, a vignette centred off-frame, and a linear gradient entering
from off-canvas are ordinary tools, and R-MASK-1's "inside the photo" described where the
*interaction* happens rather than where the geometry may go.

What bounds a drag now is what the pointer can reach: `hitTestSelf` accepts the whole overlay,
which is the photo *stage*, so the letterbox around a fitted image is draggable. `clampGeom`
(±8 in normalised units) and `positiveRadius` (≥1e-4) are arithmetic hygiene only — they keep a
degenerate `mFitted` mid-transition from turning a stray pixel into a huge or non-finite value
that would then be written into a project file. A radius of zero is not a small mask; it is the
division `maskCoverage` guards with an epsilon.

**The engine never had the limit.** `maskCoverage` (`engine/MaskStack.cpp`) clamps the coverage it
computes and `feather`, never `cx/cy/rx/ry` — which is why the symptom was "the handle stops at
the border" and not "the mask renders wrong", and why no engine change was needed.

Verified end to end: `set mask=` with `cx=-0.3 rx=1.8`, then `mask set 0 rx=2.2 cy=-0.4`, saved
and reloaded — `mask=0,0,0.5,-0.3,-0.4,2.2,1.4,…` round-trips byte-identical, so out-of-frame
geometry survives persistence and is not silently pulled back in on load. Guarded by
`maskRadiusCanGrowPastTheImageEdge`, `maskCentreCanLeaveTheImage` and `maskRadiusStaysPositive`
in `cosmo_widget_tests`, which required adding `MaskOverlay.cpp` to that target and making the
coordinate mapping `protected` — the property under test is what happens *outside* 0..1, which is
unassertable from outside the mapping.

### DR-LOADPERF-2 Off-thread apply
`EditSession::makeThumb` is public and re-entrant so the loader builds the filmstrip thumbnail on its
own thread, and `EditSession::openImageInto(int, vector<uint8_t>&&, …, Thumb&&)` +
`RenderService::addImage(vector<uint8_t>&&, …)` **move** the decoded buffer into the engine instead
of copying it. What is left on the UI thread per image is bookkeeping.

### DR-LOADPERF-3 Progressive reveal
`pollLoad` calls `selectImage` + `finishOpenTransition` on the **first** decoded image; the rest
stream in behind the revealed editor, each calling `App::refreshLibrary()` (browse chrome only — the
develop panels are not re-pushed under the user's hands). `EditSession::finishWorkspaceLoad` only
auto-selects image 0 when nothing is selected yet, so finishing a load cannot yank a photographer
who started working during the stream. A project whose images all fail to decode still reveals, via
the completion branch.

## 17. Browse navigation (R-BROWSE)

### DR-BROWSE-1 The rack scrolls
`App::wheel` hit-tests the filmstrip's world rect **before** the right-column branch and calls
`Filmstrip::scrollBy(delta * Filmstrip::kWheelStep)`, where `kWheelStep` is one photo cell + its gap
— so a notch is one photo. The offset was already eased for the sliding selection ring; it simply
had no wheel route.

### DR-BROWSE-2 Arrow keys
The host maps Left/Right to key codes 37/39 (matching the 8/13/27 it already sends) and offers them
to `App::key` before its own single-key shortcuts. `App::stepSelection(dir)` moves to the adjacent
cell of the current group and calls `Filmstrip::scrollCellIntoView`, which scrolls the minimum
needed. It steps from the **selection**, not the edit target: arrowing onto a still-loading photo
moves the selection without changing the edit target (DR-LOADUX-2), and stepping from the edit
target would then stick on the last ready photo. Clamped at both ends; ignored while a modal or a
text field has the keyboard (`App::isTextEditing`).

### DR-BROWSE-3 Scroll intensity
The develop panels' `scrollBy` treats its argument as pixels and was handed the raw wheel delta —
**one pixel per notch**. `App::kEditScrollStep` (10 px) now scales it, in one named place.

## 18. Application-open splash (R-SPLASH)

`SplashScreen` (`widgets/SplashScreen.{h,cpp}`) is a self-drawn, `inputTransparent` Segment sized
`kWidth`×`kHeight` (420×260). `begin(nowMs)` starts three staggered eased properties — wordmark
rise+scale, tagline, dot row — plus `setProgress()` for the 2 px bottom bar and `beginExit()` /
`isGone()` for the fade-out. The wordmark uses the R-G-2a spacing formula and `IRenderTarget::
measureText` for centring (mixing a measured width with an estimated one is what put it off-centre
first time round).

### DR-SPLASH-2a The activity slot
`setStatus(text)` shares ONE slot with the pulsing dots: the first non-empty call eases `mStatusMix`
0→1, cross-fading the dots out and a **spinner + status line** in; afterwards only the string
changes, because the text is data rather than motion (cross-fading each swap would flicker at the
rate items land). The spinner is the same rotating quarter-arc the filmstrip's loading cells use, so
one activity affordance reads identically app-wide — and never two at once. The line is centred as a
unit (spinner + gap + text) and ellipsizes at `kStatusMaxFrac` of the window. **Ellipsize against
the room available, not against the string's own measured width** — the latter truncates *every*
string, since appending `…` always makes it wider.

### DR-SPLASH-5 The progress bar, and why it waits (R-SPLASH / R-LOADUX-3)
An inset rounded bar pinned above the version line: `kBarInset = 44` (the tagline's optical
margin), `kBarBottom = 34`, `kBarH = 4`, a track at 10% white with the fill in the accent colour,
never narrower than the cap radius (a small fraction drawn thinner than its own rounding reads as a
smudge). Beside it, the R-LOADUX-3 count — `n of N` in the mono face at 9.5 px — because a fraction
with no denominator says how far but never how much; the bar gives up exactly the width the text
needs, and drops the count entirely rather than shrink below 40 px.

Two timing rules, both **measured** through `ui dump --root splash` sampled at 80 ms rather than
reasoned about:

- The **track fades in with the dots** during the intro, not on the first real work. Its first
  version faded in on the first `setProgress`, so that an empty track would not sit through the
  intro looking stuck. But a cover is now the embedded preview (~7 ms, DR-SPLASH-5a) rather than a
  full decode, so on a machine with a few recents the first and last `setProgress` land in the same
  frame the splash starts leaving: the 260 ms fade-in got one 80 ms window at **23% opacity**,
  inside the exit fade. The bar was, in practice, never visible.
- **`beginExit()` only requests the exit.** The fade starts once the eased fill reaches ≥ 0.995,
  because the 220 ms ease and the 260 ms fade run concurrently otherwise and the bar's last drawn
  state was 96% full at 0.1% alpha — the one frame that says *finished* was the one frame nobody
  saw. It costs ~200 ms of launch (measured 1.2 s → 1.55 s) to make completion legible, the same
  trade R-SPLASH already makes for the intro.

Measured after the fix: the bar holds full opacity from ~900 ms to ~1280 ms, filling 4 → 332 px
with `1 of 1` beside it, and only then fades.

### DR-SPLASH-5a A cover is the camera's own preview, not a decode
`ImageDecoder::decodeThumb(path, maxEdge)` — default implementation forwards to `decodeFile`, so
every decoder keeps working — is overridden by `NativeImageDecoder` to pull the **embedded**
preview: LibRaw `unpack_thumb()` + `dcraw_make_mem_thumb()` for RAW, then a GdkPixbufLoader with
`size-prepared` wired up so the JPEG is scaled **during** decode rather than after. Non-RAW files
take the same scaled-loader path. It falls back to the full `decodeRaw` when a file carries no
preview.

Measured on a Fujifilm X-Trans `.RAF`: **8072 ms → 6.6 ms**, and a probe put the returned preview
at 480x320 in 32.1 ms end to end. A cover is drawn at 480 px, so the full demosaic was decoding
~1200x the pixels it would ever show — and it ran **inline in the GTK splash tick**, which is why
the splash froze mid-animation and the status text set two lines above it was never painted before
the freeze began. The home screen's covers come from the same call.

Host: `startSplash` creates a **borderless, non-resizable, centred** `GTK_WINDOW_TOPLEVEL`
(`gtk_window_set_decorated(FALSE)`, `GDK_WINDOW_TYPE_HINT_SPLASHSCREEN`) with its own drawing area
and 16 ms tick. The main window is constructed but **not shown**. `onSplashTick` plays the intro;
only once `introDone()` does it call `showHome()`, whose `onDecodeThumbnail` requests are **queued**
rather than decoded because `Host::startupPhase` is set — the tick then decodes ONE cover per frame
(each is a full-resolution decode; a loop here would stall the very animation this exists to
protect), feeding `setProgress`. When the queue drains it calls `beginExit()`, and on `isGone()`
destroys the splash window and calls `showMainWindow`. Along the way it names the work —
`Scanning projects…`, `Loading  <project name>` (via `App::recentName`, the launcher's unit is a
project, not a file path), then `Ready`. A launch with image paths on the command line goes straight
to the editor with no splash.

## 19. Load legibility (R-LOADUX)

### DR-LOADUX-1 The tree before the pixels
`App::buildPendingTree(entries)` runs on the UI thread **before any decoding**, creating groups
(`addWorkspaceGroup`) and one `EditSession::addPendingImage` leaf per image, and returns the node
index per entry. `GNode::pending` marks "still coming" — distinct from a slotless leaf that is
genuinely missing. `EditSession::attachImage(node, rgba&&, w, h, path, thumb&&)` later gives a
pending leaf its pixels; `markImageFailed(node)` stops it spinning. Because parents were resolved up
front, arrival order can no longer reparent anything.

### DR-LOADUX-2 Spinner cells
`Filmstrip::Cell::loading` draws a dim plate plus a rotating quarter-arc (900 ms per turn, driven
from the frame clock) instead of a thumbnail. `EditSession::selectNode` on a pending leaf moves the
selection but leaves `mCurrentSlot` alone, so the stage keeps what it was showing; `attachImage`
points the editor at the photo if it is the one selected.

### DR-LOADUX-3 Meaningful progress
`Filmstrip::setLoadProgress(done,total)` draws a 2 px determinate accent bar along the strip's top
edge and `Loading n of N` bottom-right — the one band the cells and their selection rings never
reach — eased in while streaming and out when the last photo lands. The host feeds it alongside the
loading screen's own `setLoadProgress`, so both phases report the project's real total.

## 20. Follow-up fixes (browse animation, load legibility, settings)

### DR-BROWSE-2a Why arrow navigation was broken
`Filmstrip::setCells` reset the scroll to 0 unconditionally — and the same cell list is re-pushed on
**every** selection change (and once per photo while a project streams in), so each arrow press
snapped the rack back to the start. It now compares the incoming list's node ids with the current
one and only treats a genuinely different list as a new view. `scrollCellIntoView` is a **no-op when
the cell is already fully visible** (the selector just moves) and otherwise scrolls the MINIMUM,
which lands the cell flush against the edge it came in from rather than yanking it to the middle;
`cellFullyVisible` is the shared predicate. The click path calls the same thing, so a click on a
half-hidden cell brings it in and a click on a visible one scrolls nothing. The rack's viewport is
its own `width.value()`: it is laid out inside the centre stage, so the right column never covers
it.

### DR-LOADUX-4 Why the loading screen said nothing
Two compounding causes: the decode was deferred until the intro finished (R-LOADING-0's original
"pure animation, no I/O"), so the bar sat at zero reading "Preparing…" for the whole intro; and the
editor was then revealed on the FIRST image, so the bar jumped from empty straight to gone.

Both are fixed by the same reasoning. The deferral existed to stop a UI-thread decode hitching the
intro — but decoding moved to a worker pool and the per-image apply moved off the UI thread with it
(R-LOADPERF-1/2), so there is nothing left to hitch on. `beginOpenTransition` therefore starts the
decode immediately (the host starts the pipeline right after it returns; the `onLoadingReady` hook
is gone), and `App` reveals when the load **completes** — or, past `kMaxLoadingMs` with at least one
image usable (`setLoadUsable`), for a catalog too big to sit through, after which the rest stream in
behind the editor with the rack's spinners and progress bar. `kMinLoadingMs` is measured from
`mLoadStartMs` (decode start) rather than the end of the intro, since the overlap already gave the
bar its time on screen.

Effect on a 12 × 24 MP project: reveal at **~1093 ms → ~460 ms**, and the floor is now the intro
animation itself rather than anything I/O-bound. What remains for a much bigger or RAW-heavy catalog
is decode itself; the next lever there is a two-tier decode (preview-resolution first, full
resolution on demand for the photo being edited or exported).

### DR-SETTINGS-5 One dialog, reachable from both screens (R-SETTINGS-5, R-HOME-8 amended)
The home sidebar's "Settings" link fires `HomeScreen::onSettings`
(`widgets/HomeScreen.cpp:322-331`), wired to `App::openSettingsDialog()` — the same entry point the
editor's Settings menu uses, so there is one dialog, one set of callbacks and one persisted record.
What's New and Help stay reserved but still consume their click, so it cannot fall through to the
grid behind them.

The dialog is a child of `mRoot`, which only the Editor screen renders, so `App::render`'s Home
branch sizes it, `advance()`s it and calls `renderOverlay()` on it directly (the dialog paints
entirely in the overlay pass). That is done **every** Home frame rather than only while open,
because `isOpen()` is already false during the closing fade — gating on it would freeze the close
mid-fade (R-G-1) — and because `show()` needs a current `nowMs` to start its open tween.
`SettingsDialog::advance`/`onOverlay` are public for exactly this reason, as `HomeScreen::advance`
already was. The gesture sink routes Home gestures to the dialog while `isOpen()`, else to `mHome`.

Verified by `cosmo_widget_tests`: `homeSettingsLinkOpensSettings`,
`homeReservedLinksStaySilentButSwallowTheClick`, `homeSettingsLinkIsReachableAtASmallWindow`
(1024×640, since the link block is pinned to the sidebar bottom).

### DR-SETTINGS-4 Persisted preferences
`cosmo::AppSettings` (`core/AppSettings.{h,cpp}`) — `previewEdge` / `threads` / `useGpu` /
`cpuPercent` (R-CPU-3) in a plain
`key=value` file at `ProjectStore::configDir()/settings.txt`. `AppSettings::load()` falls back per
field, so a truncated or garbled file cannot stop the app starting. The host applies it via
`App::applySettings` **before the first render**, and each `SettingsDialog` callback updates the
in-force copy and fires `App::onSettingsChanged`, which the host saves.
