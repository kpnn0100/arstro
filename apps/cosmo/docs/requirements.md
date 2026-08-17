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
  (`kLoadingBg={0x14,0x14,0x14}`), the project name grows (`sz=17+6·intro`), stars fade in. The
  decode has **not** started. At `!mIntro.isAnimating()` the phase advances to Loading, the
  progress bar fades in, and `onLoadingReady()` fires **once** (`App.cpp:797-803`).
- **Loading** (progress only): the host starts the background decode only now; `setLoadProgress`
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
  (`effectiveEditParams`, i.e. `item(x) + group(x) − x` per axis) — faint (`#4cb573`) **behind** the
  editable one, so you see the final result after group stacking. Fed via
  `RightColumn::refreshCurveReferences` (`MixerPanel::setReference` / `CurvePanel::setReferenceCurves`),
  called on sync **and on every curve/mixer edit** so the "final" tracks the drag live; when it
  equals the own curve (no group curve) it is not drawn.
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

### DR-SVC-9 Two front ends, one state (R-SVC-9)
`two_services_dump_the_same_state` runs the same command sequence twice — once pumped 120 extra
frames, standing in for a GUI that has animated longer — and asserts the `stable` dumps are
byte-identical, while the non-stable dumps do differ (or the exclusion would prove nothing).
`ModelDumpOptions::stable` is therefore the comparison artifact, and `--json` is asserted to be
well-formed enough to start with `{` and contain the `nodes` array.

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
