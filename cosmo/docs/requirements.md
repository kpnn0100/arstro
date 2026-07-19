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

cosmo is a native desktop, Lightroom-style non-destructive raster photo editor. It manages
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

1. **host** (`cosmo/linux_main.cpp`) — the only layer allowed OS/GTK calls: window, events,
   file dialogs, threads, fonts, logging, PNG export.
2. **app** (`arstro::cosmo_v2`, `cosmo/App.*` + `cosmo/widgets/*`) — the UI, built on Artboard
   `Segment`s; renders through the `IRenderTarget` HAL only.
3. **cosmo_core** (`arstro::cosmo`, `cosmo/core/*`) — UI-free session, group tree, history,
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
`radius::control()=2`), one type ramp, all from `Theme` (`cosmo/Theme.h`). The `cosmo.` wordmark
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
- **Open Project…** → OPEN dialog filtered to `*.cmp` → `startProjectLoad` (the animated
  threaded load) (`linux_main.cpp:627-641`).
- **Import Catalog…** → multi-select image dialog, then a SAVE for a new `.cmp`;
  `resetWorkspace()`, `openImage` each, `saveWorkspaceAs`, remember, `showEditor()`
  (`linux_main.cpp:643-685`).

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

### DR-EDIT-5 Mixer/Curve (`StackPanel` → `MixerPanel` + `CurvePanel`) (R-BUGFIX-3)
- **MixerPanel**: a Hue/Sat/Lum segmented picker over one visible `HueCurveEditor` per channel
  (`EditParams::mixer[3]`). Each editor is a cyclic hue mapper: X = input hue [0,360), Y =
  adjustment [−1,1], wrapping at the 360/0 seam. Points are **bezier control points**
  (`CurvePoint{x,y,ix,iy,ox,oy,smooth}`); Alt-drag pulls tangent handles; double-click adds/removes
  a point. The editor emits control points (handles + smooth flag preserved); the engine flattens
  them with the shared `curve::sample()` for its LUT, so the drawn curve and the render never
  diverge, and a saved project restores the exact editable curve. A Reset button flattens the
  active channel.
- **CurvePanel**: an RGB/R/G/B picker + Reset over a draggable tone-curve plot (Catmull-Rom
  spline). Drag a point, click empty space to add, double-click an interior point to remove;
  endpoints are locked to x=0/1. Each channel has **its own independent curve**: **RGB** is the
  master (`EditParams::curve`, applied to all three channels), and **R/G/B** each edit their own
  `EditParams::curveChannel[0..2]`, applied to that channel after the master
  (`out_c = channel_c(master(x_c))`). The picker switches which curve is shown/edited (its four
  curves are held independently, mirroring MixerPanel's channel swap); the plot's spline + anchors
  are drawn in the active channel's colour (accent for RGB, red/green/blue for R/G/B) for feedback.
  Reset clears only the active channel. All four round-trip through the session/sidecar file.
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
`EditSession` holds a group tree of `GNode{group,name,parent,slot,offset,kids}` rooted at "All
Photos" (`EditSession.cpp:27`). Image leaves reference a slot index into the per-slot vectors
(`mSlotParams/History/Names/Paths/Sessions/Thumbs`). The filmstrip renders `currentGroupCells()`;
the breadcrumb renders `breadcrumbPath()`.

### DR-TREE-2 Selection & navigation
`selectNode(cell, shift, ctrl)` implements range (shift, from anchor), toggle (ctrl, keeps ≥1),
and replace (plain) selection. Selecting a group sets `mEditGroup`; selecting an image sets the
current slot, resets preview resolution, and submits. `navigateToGroup` drills in/out;
`createGroupFromSelection`/`ungroupSelected` restructure the tree; `deleteSelected` releases
engine slots for the subtree and re-anchors selection so the next image slides in (root protected).

### DR-TREE-3 Group offsets
A group carries a `LocalAdjust` **offset**; `effectiveParams(slot)` = the slot's params plus the
summed offsets of all ancestor groups (temp shifts as `d.temp/100·3500` K)
(`EditSession.cpp:284-296`). Group edits are applied through the group node while
image selection edits the slot.

### DR-TREE-4 Clipboard
Copy Settings stashes the current params; Paste to Selected / to All Images applies them to the
target slots as discrete history steps (`copyCurrent`/`pasteTo`).

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
offset=<12 comma-separated LocalAdjust scalars>
#image
parent=<id>
path=<source path>
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
