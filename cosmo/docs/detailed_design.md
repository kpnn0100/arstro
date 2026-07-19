# cosmo — Detailed Design

Per-module, per-class design derived from the source. Signatures, constants, and behaviors are
quoted from the implementation; `file:line` anchors are given where they aid navigation. Types
resolve in `arstro::cosmo_v2` (app), `arstro::cosmo` (core), `arstro` (engine), `artboard`
(framework) unless noted.

Contents: [1. App](#1-app-shell) · [2. cosmo_core](#2-cosmo_core) · [3. Engine seam](#3-engine-seam)
· [4. Widgets — chrome & layout](#4-widgets--chrome--layout) · [5. Widgets — edit panels](#5-widgets--edit-panels)
· [6. Widgets — controls](#6-widgets--reusable-controls) · [7. Widgets — overlays & modals](#7-widgets--overlays--modals)
· [8. Helpers](#8-helpers) · [9. Cross-cutting patterns](#9-cross-cutting-patterns)

---

## 1. App shell

`cosmo/App.{h,cpp}` — `class App` (`arstro::cosmo_v2`). Owns the screen state machine, the editor
Segment tree, the `EditSession`, and the host-callback seam.

### 1.1 State
- `enum class Screen { Home, Loading, Editor }` (`App.h:159`), default `Home` (`App.h:190`).
- `enum class Phase { None, Intro, Loading, Reveal, ReturnEnter, ReturnLoad, ReturnExit }`
  (`App.h:197`), `mPhase=None`, `mPhaseT0` (phase start time).
- `cosmo::EditSession mSession` — the model.
- `RenderService::Frame mLastAfterFrame` — last acquired edited frame, pushed to the photo view.
- Transition members: `mStars` (`Starfield`, 220 specks), `mCover` (`ImageView`, Contain fit),
  `mCoverReady`, `mLoadComplete`, `mLoadDone/mLoadTotal`, `mOpenFromRect`/`mCoverFrom` (fly-in
  rects), `mLoadingStarted`, `mReturning`, `mLoadName`.
- Animated properties: `mScreenFade`, `mIntro`, `mReveal`, `mProgress`, `mCoverFade`, `mBarFade`,
  `mReturn`, `mEnterFade`, `mExitFade`.

### 1.2 Transition durations (`App.cpp:16-34`, anonymous namespace)
`kRailAnimMs=200`, `kIntroMs=460`, `kRevealMs=520`, `kProgressMs=200`, `kReturnMs=480`,
`kEnterMs=300`, `kReturnHoldMs=200`, `kExitMs=320`, `kMinLoadingMs=260`,
`kLoadingBg={0x14,0x14,0x14}`. Helper `lerpRect(a,b,t)`.

### 1.3 Render dispatch
- `render(target, nowMs)` (`App.cpp:456-480`): sets `mNowMs`, updates `mScreenFade`; Home → advance
  + render `mHome` + scrim; Loading → `renderTransition`; Editor → `renderEditor`.
- `renderEditor` (`App.cpp:482-527`): `mSession.tick`, `mRoot->advance`, re-`layout()`; pushes the
  mask-overlay active state from `RightColumn`; `if (renderService().tryAcquire(f) && f.width>0)` →
  cache `mLastAfterFrame`, `refreshPhotoForMode()`, feed histogram; fill background; `mRoot->render`
  + `renderOverlay`; screen-switch scrim.
- `renderTransition` (`App.cpp:788-935`): the open transition (delegates to `renderReturn` if
  `mReturning`). Phase advance Intro→Loading (fires `onLoadingReady` once), Loading→Reveal
  (`mLoadComplete && elapsed≥kMinLoadingMs`), Reveal→Editor. Draws stars, flying cover, growing
  name, wordmark, progress bar; in Reveal, dissolves loading elements (`fadeOut=clamp(reveal/0.45)`)
  while the editor materializes (`fadeIn=clamp((reveal-0.40)/0.60)`) with a wordmark cross-fade.
- `renderReturn` (`App.cpp:937-1004`): ReturnEnter (editor→star-sky, then `refreshHome()`),
  ReturnLoad (`kReturnHoldMs` beat), ReturnExit (home fades in); wordmark flies back via `mReturn`.
- `drawWordmark(target, p, alpha=1)` (`App.cpp:772-786`): p=0 home (46 px @ x32/baseline96), p=1
  top-bar (13 px @ x9.75/baseline19.2); "cosmo" in foreground, "." in primary, spacing `-0.03·sz`.

### 1.4 Transition entry points
`beginOpenTransition(name)`, `setLoadingCover(rgba,w,h)`, `setLoadProgress(done,total)`,
`finishOpenTransition()`, `beginReveal()`, `showHome()`, `showEditor()`, `resetWorkspace()`,
and the `std::function<void()> onLoadingReady` seam fired at the intro→loading boundary.

### 1.5 Public API (grouped)
- **Rendering/input**: `App(w,h)`, `render`, `pointer(kind,x,y,button,timeMs,alt,shift,ctrl)`,
  `wheel(x,y,delta,ctrl)`, `bool key(KeyEvent)`, `setSize`.
- **Home/projects**: `showHome`, `showEditor`, `bool onHomeScreen()`.
- **Open transition**: the entry points above + `bool inOpenTransition()`, `bool hasLoadingCover()`,
  `setHomeThumbnail(recentIndex,rgba,w,h)`.
- **Images/slots**: `int openImage(rgba,w,h,name,path="")`, `selectImage(slot)`, `deleteSelected`,
  `int imageCount()`, `const uint8_t* exportFullRes(w&,h&)`.
- **Presets**: `setPresetDir`, `savePreset`, `exportPresetTo`, `importPresetFrom`, `applyPreset`,
  `deletePresetFile`.
- **Undo/redo**: `undo`, `redo`, `bool canUndo/canRedo`.
- **Params/session**: `currentSourcePath`, `applyParams`, `saveSession`, `saveSessionAs`, static
  `readSessionFile`.
- **Workspace**: `readWorkspaceFile` (static), `saveWorkspaceAs`, `saveWorkspace`,
  `currentWorkspacePath`, `resetWorkspace`, `addWorkspaceGroup`, `openImageInto`,
  `addWorkspaceMissingImage`, `applyParamsToSlot(slot,params[,history])`, `finishWorkspaceLoad`,
  `renameGroup`.

### 1.6 Host-callback seam (`App.h`) → host wiring (`linux_main.cpp:849-877`)
`onOpenRequested→openDialog`, `onSaveAsRequested→saveSessionDialog`,
`onSavePresetRequested→savePresetDialog`, `onExportPresetRequested→exportPresetDialog`,
`onImportPresetRequested→importPresetDialog`, `onRenameGroupRequested→renameGroupDialog`,
`onSaveWorkspaceRequested→saveWorkspaceDialog`, `onLoadWorkspaceRequested→loadWorkspaceDialog`,
`onNewProjectRequested→newProjectDialog`, `onOpenProjectRequested→openProjectDialog`,
`onImportCatalogRequested→importCatalogDialog`, `onOpenRecentRequested→startProjectLoad`,
`onDecodeThumbnail→` decode+cache+`setHomeThumbnail`. `onLoadingReady` is set inside
`startProjectLoad`. Note: `onSaveRequested` (`App.h:89`) is a declared-but-unwired seam.

### 1.7 Construction & layout
Ctor (`App.cpp:36-166`) builds `mRoot` and adds children in draw order: `TopBar` (→ `toggleRail`,
`requestHome`, `buildMenus`), `LeftRail` (preset `onApply`→`applyPreset`+sync), `CenterStage`
(photo `onModeChange`, filmstrip `onSelect`/`onActivate`, breadcrumb `onCrumbClick`),
`RightColumn(mSession)` (actionBar → preset save/import/export; mask overlay
`onChange→writeSelectedMask`), `HistoryView` (`onSelect→jumpToHistory`), `ContextMenu`,
`PresetDialog(mAccent)`, `SettingsDialog` (`onPreviewEdge→setPreviewEdge`,
`onThreads→par::setThreads+submit`), `ConfirmDialog`. `mHome` is standalone (not in `mRoot`);
its `onOpenRecent` captures `lastOpenCardRect()`→`mOpenFromRect` then calls
`onOpenRecentRequested`. `layout()` (`App.cpp:168-199`) sizes the tree from the widget constants
and animates the rail width via the `Observable<bool> mRailOpen`.

### 1.8 Helpers
`buildMenus` (§ menus above), `syncControlsToSlot` (TopBar filename, breadcrumb, filmstrip cells,
selection, `RightColumn::syncToSlot`), `refreshPhotoForMode` (Before→`renderBefore`, else
`mLastAfterFrame`, Split→both), `wheel` (Ctrl+wheel over the photo zooms; else rail/right-column
scroll), `pointer`/`key` (menu dismissal, gesture feed, Home/Loading key swallowing).

---

## 2. cosmo_core

`arstro::cosmo`, `cosmo/core/*` — UI-free; owns no Artboard types.

### 2.1 EditSession (`EditSession.{h,cpp}`)
The model: group tree + per-slot state + the owned `RenderService`.

- **Group tree**: `struct GNode { bool group; std::string name; int parent; int slot=-1;
  EditParams params; History history; std::vector<int> kids; }` rooted at
  `{true,"All Photos",0,-1,{},{},{}}`. A group node carries its **own full `EditParams`+`History`**
  (its settings + timeline); an image leaf uses `slot`.
  `struct Cell { bool group; int node; int slot; std::string name; int count; }` (a filmstrip row);
  `struct Thumb { std::vector<uint8_t> rgba; int w,h; }`.
- **Per-slot vectors** (parallel, indexed by engine slot id): `mSlotParams`, `mSlotHistory`,
  `mSlotNames`, `mSlotPaths`, `mSlotSessions`, `mSlotThumbs`.
- **State**: `mNodes`, `mCurGroup=0`, `mSel`, `mSelAnchor=-1`, `mEditGroup=-1`, `mCurrentSlot=-1`,
  `mClipboard/mHasClip`, preset `mPresetDir/mPendingCategories/mPendingApf`, limits
  `mHistorySteps=100`/`mHistoryCoalesceMs=450`, `mPreviewEdge=1600`, `mDirty`, before/export caches,
  and `arstro::RenderService mService`.

**Public API (by concern):** clock `tick(nowMs)`; image I/O `openImage`, `openImageInto`,
`addWorkspaceMissingImage`, `imageCount`, `deleteSelected`; tree/selection `nodes`,
`currentGroup`, `selection`, `editGroup`, `currentSlot`, `nodeForSlot`, `currentGroupCells`,
`breadcrumbPath`, `thumbForSlot`, `navigateToGroup`, `selectNode(cell,shift,ctrl)`, `selectImage`,
`createGroupFromSelection`, `ungroupSelected`, `renameGroup`, `collectSubtree`,
`selectedImageSlots`; params `curParams`, `effectiveParams(slot)`, `applyParams`,
`applyParamsToSlot(slot,params[,history])`, `submit`, `isDirty`/`markClean`, `setCropPreviewMode`;
clipboard `copyCurrent`, `pasteTo`, `hasClipboard`; history `currentHistory`, `undo`, `redo`,
`jumpToHistory`, `recordSlotEdit`, `canUndo`/`canRedo`, `setHistoryLimits`; presets `setPresetDir`,
`savePreset`, `exportPresetTo`, `importPresetFrom`, `applyImport`, `applyPreset`,
`deletePresetFile`; session `currentSourcePath`, `saveSessionAs`, static `readSessionFile`,
`sessionPath`; workspace `WorkspaceEntry`, static `readWorkspaceFile`, `saveWorkspaceAs`,
`workspacePath`, `resetWorkspace`, `addWorkspaceGroup`, `finishWorkspaceLoad`; engine
`renderService`, `resetPreviewResolution`, `previewEdge`/`setPreviewEdge`, `setPreviewZoom`,
`exportFullRes`, `renderBefore`.

**Key behaviors:**
- `effectiveParams(slot)` = the slot's params **composed** with every ancestor group's params,
  folded child→root via `arstro::composeParams` (recursive stacking; `EditSession.cpp`).
- `curParams()` returns the **group's** params while `mEditGroup>=0` (so every develop panel edits
  the group), else the current slot's — the single seam that makes a group editable.
- `editHistory()`/`recordHistory`/`undo`/`redo`/`applyHistoryParams`/`canUndo`/`canRedo` route to
  the group's `History` while a group is the edit target, else the slot's.
- `submit()` = `mDirty=true`, `recordHistory()` (group or slot), compute `effectiveParams`,
  crop-preview override, `mService.render(currentSlot, params)`.
- `selectNode` = shift range / ctrl toggle / plain replace; **group** → `mEditGroup=node` +
  `mCurrentSlot=firstImageSlotUnder(node)` (a representative member) + `submit` so the group's
  stacked effect previews live; **image** → current slot + `resetPreviewResolution` + `submit`.
- `resetWorkspace` = `mService.reset()` (restart slot ids), clear per-slot vectors + `mNodes`,
  re-push root, reset selection/current/clipboard/dirty (the reset-then-open segfault guard).
- `openImageInto` = `mService.addImage`→slot, push params + a fresh `History.init`, name/path/
  session + `makeThumb(…,110)`; add the leaf GNode.

**Serialization** — session `.cosmo`: `image=<path>\n` + `serializeParams(effectiveParams)`.
Workspace `.cmp`: `cosmoworkspace=1`, a pre-order `walk` emitting `#group`/`#image` blocks and,
per image with history, `hcurrent/hmax/hcoalesce` + `#hnode` blocks (each `hparent/hseq/hlabel` +
`serializeParams(node.params)`) in vector-index order. Reader routes lines to the current-params
buffer until the first `#hnode`, then to each node (`EditSession.cpp:531-650`).

### 2.2 History (`History.{h,cpp}`)
Branching "time machine". `struct HistoryNode { EditParams params; int parent=-1;
std::vector<int> kids; std::string label; int seq; }`. `class History` public data: `nodes`,
`current=-1`, `maxSteps=100`, `coalesceMs=450`. `empty/canUndo/canRedo`.
- `init(p)` — single root, label "Open".
- `record(p,nowMs)` — no-op if equal; coalesce (overwrite) within the window on a childless leaf;
  else new child (branch if the node has kids), `label=describeChange(from,to)`, `seq=mNextSeq++`,
  `prune()`.
- `undo`→parent, `redo`→highest-seq child, `jumpTo`→any; all break coalescing.
- `restore(nodes,cur,steps,coalMs)` — adopt a loaded tree, rebuild `kids` from `parent`, clamp
  `current`, `mNextSeq=maxSeq+1`, break coalescing.
- `prune()` — keep, up to `maxSteps`: current, nearest→farthest ancestors, then most-recent by seq;
  re-attach kept nodes to their nearest kept ancestor; remap indices/kids/current.
- `describeChange(from,to)` — diff two `apf::Document`s per category; single changed category
  title-cased, "Adjustments" for many, "Edit" for none. `paramsEqual` = deep string compare of
  `serializeParams`.

### 2.3 ProjectStore (`ProjectStore.{h,cpp}`) — static
`struct RecentEntry { name; path; firstImagePath; int photoCount; long long sizeBytes; long long
lastOpened; }`. `configDir()` = `$XDG_CONFIG_HOME/cosmo_v2` or `$HOME/.config/cosmo_v2` (created if
missing); `indexPath()` = `configDir()/recent.tsv`; `recents()` parses the TSV and drops entries
whose `.cmp` is gone; `remember(entry)` de-dupes by path, pushes front, caps at `kMaxRecents=24`,
rewrites. Storage: tab-separated, newest first, six fields.

### 2.4 PresetLibrary (`PresetLibrary.{h,cpp}`) — static
`struct PresetNode { bool folder; std::string name; std::string relPath; std::vector<PresetNode>
kids; }`. `scan(dir)` recursively builds folder/leaf nodes (folders-first, alphabetical);
`flatNames(dir)` lists top-level `.apf` stems; `categoryLabel(key)` maps apf category keys to UI
labels.

### 2.5 NativeImageDecoder (`decode/*`)
`struct DecodedImage { std::vector<uint8_t> rgba; int width,height; std::string name; bool ok(); }`
(top-down RGBA8). `struct IImageDecoder { virtual DecodedImage decodeFile(path)=0; }`.
`NativeImageDecoder` implements it: `decodePixbuf` via GdkPixbuf (channels expanded to RGBA);
`decodeRaw` via LibRaw under `COSMO_HAVE_LIBRAW`; `isRawExtension` matches rw2/arw/cr2/cr3/nef/dng/
orf/raf/pef/srw/rwl/raw; `decodeFile` routes RAW→raw else pixbuf and sets `name=baseName(path)`.

---

## 3. Engine seam

`arstro`, `ImageProcessing/src/engine/*`. cosmo drives it **only** through `RenderService`.

### 3.1 RenderService (`RenderService.h`)
`struct Frame { std::vector<uint8_t> rgba; int width,height; HistogramData hist; HistogramData
preCurveHist; HueHistogram preMixerHue; }`. API cosmo uses: `addImage(rgba,w,h,channels=4)`,
`releaseImage(slot)`, `reset()` (drop all, restart slot ids from 0), `setPreviewSize(maxEdge)`,
`render(slot, EditParams)` (coalesced to the latest), `tryAcquire(Frame&)` (move out the most-recent
completed preview), `renderFull(slot,params,Frame&)` (blocks, export), `renderPreviewSync` (blocks,
before/after baseline), `threaded()`. Under `ARSTRO_ENABLE_THREADS` a worker thread owns the engine
exclusively; without it, synchronous. `mPreviewMaxEdge=1600` default.

### 3.2 EditEngine (`EditEngine.h`)
The flat pipeline behind the service (cosmo pushes whole `EditParams`, not individual setters).
Fixed order: Crop → Rotate → LensCorrection → NoiseReduction → Exposure → Contrast → ToneRegions →
WhiteBalance → ToneCurve → Texture → Clarity → Vibrance → ColorMixer → ColorGrading → Dehaze →
Sharpen → Grain, then masks. Whole-params seam: `applyParams`, `currentParams`/`setCurrentParams`,
`renderPreview`/`renderFull` → `PreviewBuffer{rgba,width,height}`, `histogram`/`preCurveHistogram`/
`preMixerHue`. Per-slot `Slot{Image source; EditParams params; Image proxy; int proxyEdge}` with an
LRU proxy cache (`kMaxProxies=6`).

### 3.3 EditParams (`EditParams.h`)
The complete non-destructive description:
- **tone**: `exposure, contrast, highlights, shadows, whites, blacks`.
- **colour/presence**: `temp=6500, tint=0, vibrance=0, saturation=0, texture=0, clarity=0`.
- **effects**: `dehaze=0, grainAmount=0, grainSize=0`.
- **detail**: `sharpenAmount=0, sharpenRadius=1, sharpenMasking=0, nrLuminance=0, nrColor=0`.
- **lens**: `lensDistortion=0, lensCA=0, lensVignette=0`.
- **curve**: `std::vector<std::pair<float,float>> curve{{0,0},{1,1}}; bool curveLog=true`.
- **mixer**: `std::array<std::vector<CurvePoint>,3> mixer{}` (hue/sat/lum cyclic bezier curves).
- **grade**: `std::array<GradeWheel,3> grade{}; float balance=0; bool remapEnable; remapSrc=0,
  remapRange=30, remapDst=0, remapStrength=0`. `GradeWheel{hue,sat,lum}`.
- **transform**: `cropX=0,cropY=0,cropW=1,cropH=1; rotation=0; int quarterTurns=0`.
- **masks**: `std::vector<MaskParams> masks`. `MaskParams{ enum Type{Radial,Linear,Brush};
  int type; bool inverted; float feather=0.5; radial cx/cy/rx/ry; linear x0/y0/x1/y1;
  std::vector<BrushDab> dabs; LocalAdjust adjust; }`. `BrushDab{x,y,radius=0.05,flow=1}`.
  `LocalAdjust` = 12 relative scalars (temp/tint −100..100 relative, not Kelvin).
- **CurvePoint** (`base/CurvePoint.h`): `{float x,y; float ix,iy; float ox,oy; bool smooth;}`;
  the shared `curve::sample()` flattens control points to the LUT the mixer engine consumes.

---

## 4. Widgets — chrome & layout

All widgets subclass `artboard::Segment` (animated `x/y/width/height`, `visible`, `advance`,
`onPaint`/`onOverlay`/`handleGesture`/`hitTestSelf`, `addChild`/`raise`).

### 4.1 TopBar
h `kHeight=29.25`. Owns a `MenuStrip` + an `IconButton` rail toggle (`icon::panelLeft`). Draws the
`cosmo.` wordmark (13 px, spacing −0.39), a centered project name, and a right-aligned filename +
`(i/n)`. `setFilename(name,index,total)` (total≤0 hides), `setProjectName`, `setRailOpen`. Callbacks
`onRailToggle`, `onHome` (Click in `wordmarkRect`).
While a group is the edit target the right slot instead shows **"Group: <name>"** (`setGroupName`,
ellipsized to fit, DR-TOPBAR) as a **clickable, eased-hover** affordance (`groupNameRect`,
`mNameHover`); `onNameClick` → opens rename (DR-TREE-5).

### 4.2 MenuStrip
The File/Settings/Develop/History/Preset bar. `struct Item{label,action}`, `struct Menu{title,
items}`; `addMenu`, `openIndex`, `close`, `pointInActiveArea`, `contentWidth`; `onOpenChanged(int)`.
At most one dropdown open, drawn in `onOverlay`. Animated highlight (`mHiCenter/mHiW`, 190 ms) and
dropdown reveal (`mDropReveal`, 160 ms); `HoverFade` for titles + items. `dropdownRect` min-width
152, positioned below the title.

### 4.3 LeftRail
The collapsible 196 px preset dock (`kOpenWidth=196`, `kHeaderH=24.7`). A "PRESETS" header over a
scrollable `PresetTree` (`tree()`, `scrollBy`). Returns early in `onPaint` when collapsed (App
animates width→0).

### 4.4 CenterStage
Pure container: `PhotoCanvas` (fills) + `Breadcrumb` + `Filmstrip`, stacked in `layout()`.
Accessors `photo()`, `breadcrumb()`, `filmstrip()`.

### 4.5 PhotoCanvas
`enum Mode{Before,Split,After}`. A `#0A0A0A` backdrop, an `ImageView` (Contain), a split
before-view clipped to the left half + a 1.5 px divider, a `MaskOverlay`, and a Before/Split/After
`SegmentedControl` pill (bottom-center, added last so it stays clickable). Owns shared zoom/pan:
`zoom()`, `zoomAbout(factor, localInCanvas)`, `resetZoom()`; drag-pans both views while zoomed.
Callbacks `onModeChange(int)`, `onContext(worldX,worldY)` (right-click).

### 4.6 Filmstrip
`kHeight=86`; 86×62 photo cells, 78×62 dashed folder chips. `struct Cell{group,node,thumbSlot,
name,count}`. `addThumb` (pooled `ImageView`, reused via `mThumbCount`), `clearThumbs`, `setCells`,
`setSelection(cells, primary)`, `scrollBy`. Callbacks `onSelect(cell,shift,ctrl)`,
`onActivate(cell)` (double-click), `onContext(cell,x,y)` (right-click, fires even on empty space).
Selection rings + name bars draw in `onOverlay` (above thumbnails). Animated: `mScrollX` (180 ms),
`mRingPos` (200 ms sliding primary ring), `HoverFade mHover`.

### 4.7 Breadcrumb
`kHeight=22.75`. `setPath(crumbs)`, `onCrumbClick(index)` (never the last). Each non-last crumb
muted+clickable with a chevron; per-crumb `HoverFade`.

### 4.8 RightColumn
`kWidth=324`. Owns `EditSession&`, a `HistogramWidget`, an `EditStackTabs` (5 pages), and an
`ActionBar`. Tab indices `kTabBasicDetail=0, kTabMask=1, kTabColor=2, kTabGrade=3, kTabXform=4`.
Builds and wires every panel: a `set(setter)` helper wraps a `void(EditParams&,double)` into a
`void(double)` that mutates `curParams()` + `submit()`. `syncToSlot()` pushes params into every
panel; `scrollActivePanel(delta)` routes wheel to the active tab; mask bridge `activeTab`,
`maskTabActive`, `selectedMaskParams`, `writeSelectedMask`. Paints `palette::card()` so the active
tab welds into the body.

### 4.9 EditStackTabs
The tab strip + swappable pages (a local TabView so labels center). `tabHeight=27`. `addPage`,
`selectedIndex`/`setSelectedIndex`, `onChange(int)`, `layoutPages`. `tabW(i)` sizes each tab to its
label + even padding to fill the strip (floor `kMinTabPad=6`). A 1.5 px accent bar slides
(`mIndX/mIndW`, 200 ms); page swap cross-fades via an `onOverlay` card-color scrim eased by `mFade`
(180 ms); per-tab `HoverFade`.

### 4.10 HomeScreen
The launcher (not in the editor tree). `struct CardInfo{name,photos,size,date,bool edited,int
recentIndex}`. Callbacks `onNewProject`, `onOpenProject`, `onImportCatalog`,
`onOpenRecent(recentIndex)`. `setRecents`, `setThumbnail(recentIndex,rgba,w,h)`, `scrollBy`,
`lastOpenCardRect()` (the clicked card's 16:9 cover rect, for the open-transition fly), and
`setWordmarkHidden(bool)` (hidden until the return wordmark lands). 300 px sidebar (wordmark,
tagline, 3 actions, reserved links, version) + a scrollable auto-fill card grid + a focusable
`TextBox` search + empty-state placeholder. `HoverFade` with flat region ids; `mScrollYAnim` eased
(180 ms).

---

## 5. Widgets — edit panels

Common scroll model: an `AnimatedProperty mScroll` eased toward `mScrollTarget` (180 ms
EaseOutCubic), re-`layout()` while animating; `clipToBounds=true`. Section headers are the
stateless `drawSectionHeader` (`kSectionHeaderHeight=27.95`); the `SliderRow`s are real child
Segments positioned by `layout()`.

### 5.1 ParamPanel
`struct Spec{label,min=-100,max=100,onChange,bool hasGradient,Color gradLeft,gradRight}`;
`struct Section{title, rows}`. Ctor takes `std::vector<Section>`, flattens into child `SliderRow`s.
`setValues(vector<double>)` (flattened order, no callbacks), `setSubValues(vector<double>)` (per-row
green stacked-reach offsets, DR-EDIT-4), `scrollBy`. In `RightColumn` it hosts
Basic (TONE/COLOUR/PRESENCE/EFFECTS) + Detail (SHARPENING/NOISE REDUCTION/LENS); Temperature/Tint
carry colour-ramp tracks. Unit conversion is in the `RightColumn` callbacks, not the row.
`syncToSlot` sets each row's offset = `flat(effectiveEditParams) − flat(own)` so groups' recursive
contribution shows as the green reach; 0 (no groups) hides it.

### 5.2 MixerPanel
Hue/Sat/Lum `SegmentedControl` + one visible `HueCurveEditor` per channel + a Reset `IconButton`
(`icon::refreshCw`). `setMixer(array<vector<CurvePoint>,3>)`, `setReference(array<...,3>)` (the
effective/group-stacked curves, per channel), `onCurveChange(channel, vector<CurvePoint>)`. Editor 0
has `setMappedHue(true)`.

### 5.3 HueCurveEditor
Cyclic hue mapper: X = input hue [0,360), Y = adjustment [−1,1], wrapping at the 360/0 seam.
`onChange(const vector<CurvePoint>&)`, `setPoints` (restores handles + smooth verbatim),
`setReference` (effective/group-stacked curve, drawn faint `#4cb573` behind), `reset`,
`setMappedHue`. Gestures (pick radius `metrics::anchorHitRadius`=13, nearest-within-radius wins):
double-click adds/removes a point (>2 pts to remove); Down hit-tests smooth-point handles then the
node (Alt = make smooth + symmetric pull); Drag moves the anchor or sets in/out handles (Alt =
independent, else mirror); each drag `emit()`s the control
points. Paint: rounded plot bg, zero line, 60° gridlines, a 48-swatch hue strip, the dense curve
via `curve::sample(pts, cyclic=true, 360)` (broken at wrap folds), tangent handle lines + nodes.
`setMappedHue` colors the line by output hue.

### 5.4 CurvePanel
RGB/R/G/B picker + Reset over a tone-curve plot (`kPlotH=164`) — **same bezier UX as HueCurveEditor**.
Holds **four** independent `CurvePoint` sets `mCurves[4]` (0=RGB master, 1=R, 2=G, 3=B), default
identity corners; the picker's `onChange` swaps `mChannel` (instant). `setCurves(master, channels[3])`
restores all four; `setReferenceCurves(...)` sets the faint effective (group-stacked) overlay;
`onCurveChange(int channel, vector<CurvePoint>)` emits the edited channel (0=master→`EditParams::curve`,
1..3→`curveChannel[ch-1]`). Gestures (pick radius `metrics::anchorHitRadius`=13, nearest wins):
`Down` grabs a smooth node's in/out handle (`handleAt`) else the node (`pointAt`); **Alt on a node**
makes it smooth + `mDragKind=3` (symmetric handle pull); `Drag` moves the node (endpoints locked
x=0/1, interior clamped between neighbours) or shapes a handle (Alt breaks symmetry); double-click
adds a corner on empty space / removes an interior node. Reset clears the active channel. Paint:
rounded plot bg, quarter gridlines + identity diagonal, the faint reference curve (`#4cb573`),
then `curve::sample(active, cyclic=false)` polyline + tangent-handle lines/dots + node circles, in
the active channel's colour (`channelColor`).

### 5.5 GradePanel
`struct State{ array<GradeWheel,3> grade; float balance; bool remapEnable; float remapSrc,
remapRange, remapDst, remapStrength; }`. Region `SegmentedControl` → Hue (0..360)/Saturation
(0..100)/Luminance (−100..100) per region; Balance (−100..100); a `ToggleSwitch` remap enable;
remap Source (0..360)/Range (0..180)/Target (0..360)/Strength (0..100). Callbacks
`onRegionChange(region,h,s,l)`, `onBalanceChange`, `onRemapEnableChange`, `onRemapChange`
(strength 0..100; stored as 0..1).

### 5.6 XformPanel
`struct State{ float rotation; int quarterTurns; float cropX,cropY,cropW,cropH; }`. A scrubbable
rotation readout (drag = start + dx·0.15, clamped −45..45), −90°/+90° `PillButton`s, a reset
`IconButton` (`icon::rotateCcw`), and 6 aspect chips {Free,1:1,4:3,16:9,3:2,5:4} → centered
normalized crop. Callbacks `onRotationChange`, `onQuarterTurn(±1)`, `onResetRotation`,
`onCropChange(x,y,w,h)`. Flip/Auto rows are drawn but inert.

### 5.7 MaskPanel
Three add chips {Radial,Linear,Brush} → `onAddMask(type)`; a `ComboBox` → `onSelectMask(index)`;
`Inv` `PillButton` → `onToggleInvert`; trash `IconButton` (`icon::trash2`) → `onDeleteMask`;
Feather `SliderRow` (0..100 → 0..1) → `onFeatherChange`; then Basic-style Tone/Colour/Presence rows
writing a working `LocalAdjust` → `onAdjustChange(LocalAdjust)`. Per-mask controls hidden until a
mask is selected. `setMasks(masks, selected)`.

---

## 6. Widgets — reusable controls

### 6.1 SliderRow
Label (`kLabelWidth=86`) + bipolar slider + numeric readout (`kValueWidth=22.75`), row height
`kRowHeight=20`. Wraps `artboard::Slider` (its zero-crossing range-fill gives the bipolar look).
Ctor `SliderRow(label,min,max,initial)`; `setValue` (no callback), `value`, `setSubValueOffset`
(green stacked reach, forwards to `Slider::setSubValueOffset`; #4cb573 is the Slider default),
`setTrackGradient`,
`layout`; `onChange(double)`. Slider config: `setClickJumps(false)` (drag-to-set only), double-click
resets to `initial`, arrows step by `(max-min)/20`. Readout is a signed integer (`+%d`/`%d`); unit
conversion is done by the owner. **No fine-drag modifier exists.**

### 6.2 SegmentedControl
N labeled segments, one selected, with a single sliding highlight. Ctor takes labels (builds a
child `PillButton` each). `setSelected(index)` (animates + fires `onChange`),
`setSelectedImmediate`, `selected`, `onChange(int)`. Style members `containerBox`, `idleSegBox`,
`activeSegBox`, `idleText`, `activeText`, `padding=2`, `gap=2`, `edgeRadius`. The highlight
(`mHiPos`, 220 ms EaseOutCubic) blends per-corner radii across the split point so rounding eases
rather than snaps.

### 6.3 PillButton
A labeled clickable with idle/active style pairs. `setLabel`, `active`, `idleBox/activeBox`,
`idleText/activeText`, `cornerRadius=2`, `hoverEmphasis`, `onClick`. Only `Click` fires; hover
cross-fades via the framework `hoverAmount()` (`interaction::kHoverMs=120`).

### 6.4 IconButton
A square button drawing a vector glyph via `Painter = void(IRenderTarget&, const Rect&, const
Color&)`. `active`, `activeColor`, `idleColor`, `hoverBg`, `onClick`. Rounded hover/pressed bg eases
in; glyph brightens toward white on hover.

### 6.5 Icons (`namespace icon`)
Stroke-only lucide-style glyphs from path primitives, authored in a 0..1 box:
`chevronRight/chevronDown`, `panelLeft`, `save`, `upload`, `download`, `refreshCw`, `trash2`,
`rotateCcw`. Each is `(IRenderTarget&, const Rect& box, const Color&, double strokeWidth=default)`.

---

## 7. Widgets — overlays & modals

Shared modal skeleton: an `AnimatedProperty mAppear`; `show()` animates to 1 over 150 ms
EaseOutCubic + `raise()`; `beginClose()` to 0 over 120 ms; a screen-dim scrim; a local `fade(c,a)`
helper; click-outside-card cancels; drawn in `onOverlay`; modal `hitTestSelf`.

### 7.1 MaskOverlay
The interactive layer over the photo (R-MASK). `MaskOverlay(accent)`; `onChange(const MaskParams&)`;
`setFittedRect(localFitted)` (photo rect incl. zoom/pan), `setMask(m,active)`, `updateMask(m)`,
`setBrushRadius`, `active()`. Click-through when inactive (`hitTestSelf` false). `normToLocal`/
`localToNorm` map against the fitted rect. Draws handles as accent dots outlined in the canvas bg;
Radial = ellipse + center + edge handles; Linear = two boundary lines + endpoints; Brush = dab
ellipses. `pickHandle`/`applyDrag` write geometry then `onChange`. Geometry-only (no pixels).

### 7.2 HistoryView
The "Show History Tree…" modal — a git-log branching layout of `struct Node{parent,label}`.
`show(nodes,current)`, `setCurrent`, `scrollBy`, `hide`, `isOpen`, `onSelect(int)`. DFS lane
layout (first child keeps the lane; others open new lanes); click a node to jump, drag to pan,
close-X or click-outside to close; eased pan (`mPanXAnim/mPanYAnim`, per-interaction durations),
`mAppear` open 150/close 110 ms + a 12 px rise.

### 7.3 ContextMenu
A right-click popup of `struct Item{label,action}` at the cursor. `open(items,x,y)` (nudged to stay
on-screen), `close`, `isOpen`. `mAppear` open 130/close 100 ms; per-item `HoverFade`. Fed by the
photo/filmstrip/preset-tree `onContext` callbacks via the App.
**Rename mode (DR-TREE-5):** `enterRenameMode(name)` (from the "Rename Group" item — morphs an open
menu) or `openRename(name,x,y)` (top-bar name click — opens straight to it). One eased
`AnimatedProperty mRename` (260 ms EaseInOut): the first half collapses the items to a "Rename"
header (`renameContentH`), the second grows the light textbox (`fieldRect`, `palette::inputLight` /
`inputLightText`, accent caret + select-all wash). `focusable`; `handleKey` does type/replace-on-
select-all/Backspace/Enter=commit(`onRename`)/Escape=cancel; click-away cancels. `isRenaming()`
drives `App::isTextEditing()` (host suppresses single-key shortcuts).

### 7.4 PresetDialog
The category-picker modal (R-PRESETPICK). `PresetDialog(accent)`; `struct Row{key,label,bool
checked}`; `show(title, confirmLabel, rows, onConfirm(vector<string>))`. A "Select all" master
toggle + one checkbox per category + Cancel/Confirm; Confirm gathers ticked keys. `kCardW=340`.

### 7.5 SettingsDialog
The engine-settings modal (R-SETTINGS). `SettingsDialog(accent)`; `onPreviewEdge(int)`,
`onThreads(int)` (0=auto); `show(previewEdge, threads)`. Two chip rows — Preview quality
(`kEdges{1000,1600,2400}` → Draft/Standard/High) and CPU threads (`kThreads{0,2,4,8}` → Auto/n) —
plus Done. Values map straight onto `RenderService`/`par::setThreads`.

### 7.6 ConfirmDialog
A confirm/choose prompt (save-or-discard on leaving an edited project). `ConfirmDialog(accent)`;
`struct Button{label,bool destructive,bool primary,onClick}`; `show(title,message,buttons)`;
`close()` (snap shut). Right-aligned button row; destructive=red, primary=accent, else outline.

### 7.7 ActionBar
The pinned Save/Import/Export row (`kHeight=39`). `onSave`, `onImport`, `onExport`. Save is the
accent button; the others outline. Per-button `HoverFade` + `hoverBox`. Icons `save`/`upload`/
`download`. (Wired in the app to Save/Import/Export **Preset**.)

### 7.8 PresetTree
The left-rail browser (Artboard has no tree control, so it is built from primitives). `kRowH=20`.
`setRoots(vector<PresetNode>)` (preserves expanded folders by relPath), `setSelected(relPath)`,
`scrollBy`; `onApply(relPath)` (double-click leaf), `onContext(relPath,x,y)` (right-click).
DFS-flattened rows; single-click toggles a folder; eased scroll (180 ms) + selection fade (140 ms);
per-row `HoverFade`.

---

## 8. Helpers

- **HoverFade** (`HoverFade.h`) — per-item eased 0..1 hover for multi-region self-drawn widgets.
  `setHovered(id)`/`clear`/`hovered`, `advance(nowMs, durMs=interaction::kHoverMs=120)`,
  `amount(id)` smoothstep (`t·t·(3-2t)`); honors `reducedMotion()`. Idiom in `advance`: `if
  (!isHovered()) mHover.clear(); mHover.advance(nowMs);`.
- **Starfield** (`Starfield.h`) — not a Segment; the loading backdrop. `init(count)` lays out
  deterministic specks (fixed seed); `draw(t, rect, alpha, nowMs)` draws each as a twinkling
  rounded-rect speck. Platform-free (rounded-rect fills only).
- **StackPanel** (`StackPanel.h`) — a `Segment` stacking fixed-height child panels with eased
  scroll; `addItem(seg, height, relayout={})`, `scrollBy`, `layout`. Hosts Mixer+Curve as one
  scrollable tab (`kMixerStackH=250`, `kCurveStackH=235`).
- **SectionHeader** (`SectionHeader.h`) — `drawSectionHeader(t,x,y,w,label)` →
  `kSectionHeaderHeight=27.95`; a tracked label + a hairline divider.
- **TextMetrics** (`TextMetrics.h`) — `estimateTextWidth(text,sizePx)=text.size()·sizePx·0.6`
  (the HAL has no text measurement).
- **UnitConversions** (`UnitConversions.h`) — `toEv/fromEv` (÷/×80), `toKelvin/fromKelvin`
  (6500 ± ·/3500), `toTint/fromTint` (×/÷1.5), `toRadiusPx/fromRadiusPx` (÷/×10).
- **RoundedRectExt** (`RoundedRectExt.h`) — `drawRoundedRectCorners(t, rect, tl,tr,br,bl, paint)`
  (per-corner radii; used by SegmentedControl).

---

## 9. Cross-cutting patterns

- **Two hover mechanisms.** Child-`Segment` controls use the framework `hoverAmount()` +
  `artboard::hoverBox()`; multi-region self-drawn widgets track a hovered region id and use
  `HoverFade` (per-region cross-fade). `palette::hoverWash(t)=whiteAlpha(0.07·t)` is the shared
  wash.
- **Overlay pass.** Popups/dropdowns/modals override `onOverlay` (a second, unclipped, top-of-
  everything pass) and `raise()` to sort last / hit-test first: MenuStrip dropdown, Filmstrip rings,
  EditStackTabs page scrim, ContextMenu, HistoryView, all three dialogs.
- **Eased scroll.** A plain `mScrollTarget` moved by `scrollBy`, an `AnimatedProperty` eased toward
  it in `advance` (~180 ms EaseOutCubic) then re-`layout()`. Shared by StackPanel, Filmstrip,
  PresetTree, HomeScreen, the param panels, and HistoryView's pan.
- **Upward change reporting.** Every control exposes `std::function` callbacks; `RightColumn` wires
  each to a `curParams()` mutation + `submit()`; `syncToSlot()` is the reverse (params → panels).
- **Modal fade skeleton.** `mAppear` open/close animation + scrim + `fade(c,a)` + click-outside
  cancel, shared verbatim by PresetDialog/SettingsDialog/ConfirmDialog (and, with a rise, ContextMenu
  and HistoryView).
