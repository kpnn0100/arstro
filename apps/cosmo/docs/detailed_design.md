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

`apps/cosmo/App.{h,cpp}` — `class App` (`arstro::cosmo_v2`). Owns the screen state machine, the editor
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

### 1.2 Transition durations (`App.cpp:23-35`, anonymous namespace)
`kRailAnimMs=200`, `kIntroMs=460`, `kRevealMs=520`, `kProgressMs=200`, `kReturnMs=480`,
`kEnterMs=300`, `kReturnHoldMs=200`, `kExitMs=320`, `kMinLoadingMs=260`, `kMaxLoadingMs=2500`
(R-LOADPERF-3: a catalog too big to sit through gets in anyway and streams the rest behind the
editor), `kLoadingBg={0x0A,0x0A,0x0A}`. Helper `lerpRect(a,b,t)`.

### 1.3 Render dispatch
- `render(target, nowMs)`: sets `mNowMs`, updates `mScreenFade`; Home → advance + render `mHome`,
  then size/advance/`renderOverlay` the **`mSettingsDialog`** (which lives in the editor tree but is
  openable from Home — R-SETTINGS-5; advanced unconditionally, since `isOpen()` is false during its
  closing fade), then the scrim; Loading → `renderTransition`; Editor → `renderEditor`. The gesture
  sink routes Home gestures to the dialog while `isOpen()`, else to `mHome`.
- `renderEditor` (`App.cpp:482-527`): `mSession.tick`, `mRoot->advance`, re-`layout()`; pushes the
  mask-overlay active state from `RightColumn`; `if (renderService().tryAcquire(f) && f.width>0)` →
  cache `mLastAfterFrame`, `refreshPhotoForMode()`, feed histogram; fill background; `mRoot->render`
  + `renderOverlay`; screen-switch scrim.
- `renderTransition` (`App.cpp:1092-1249`): the open transition (delegates to `renderReturn` if
  `mReturning`). Phase advance Intro→Loading — which now only fades the progress bar in, because the
  decode started back in `beginOpenTransition`'s call and there is no `onLoadingReady` left to fire
  (R-LOADPERF) — then Loading→Reveal (`mLoadComplete && nowMs-mLoadStartMs ≥ kMinLoadingMs`, **or**
  `mLoadUsable && nowMs-mLoadStartMs ≥ kMaxLoadingMs`; the elapsed time runs from decode start, not
  from the end of the intro, since they overlap), then Reveal→Editor. Draws stars, flying cover, growing
  name, wordmark, progress bar; in Reveal, dissolves loading elements (`fadeOut=clamp(reveal/0.45)`)
  while the editor materializes (`fadeIn=clamp((reveal-0.40)/0.60)`) with a wordmark cross-fade.
- `renderReturn` (`App.cpp:937-1004`): ReturnEnter (editor→star-sky, then `refreshHome()`),
  ReturnLoad (`kReturnHoldMs` beat), ReturnExit (home fades in); wordmark flies back via `mReturn`.
- `drawWordmark(target, p, alpha=1)` (`App.cpp:772-786`): p=0 home (46 px @ x32/baseline96), p=1
  top-bar (13 px @ x9.75/baseline19.2); "cosmo" in foreground, "." in primary, spacing `-0.03·sz`.

### 1.4 Transition entry points
`beginOpenTransition(name)`, `setLoadingCover(rgba,w,h)`, `setLoadProgress(done,total)`,
`setLoadStatus(text)`, `setLoadUsable()`, `setStreamProgress(done,total)`, `finishOpenTransition()`,
`beginReveal()`, `showHome()`, `showEditor()`, `resetWorkspace()`. There is **no** `onLoadingReady`
seam any more (R-LOADPERF): the decode no longer waits for the intro to end, so there is nothing to
call the host back about at the intro→loading boundary. These are driven by `CosmoService`'s events
instead — `ProjectOpening`→`beginOpenTransition`+`resetWorkspace`, `EntryDecoded`→`setLoadingCover`
+`setLoadUsable`, `LoadProgress`→`setLoadStatus`+`setLoadProgress`+`setStreamProgress`,
`LoadFinished`→`finishOpenTransition` (`linux_main.cpp:564-640`).

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

### 1.6 Host-callback seam (`App.h`) → host wiring (`linux_main.cpp:1265-1290`)
`onOpenRequested→openDialog`, `onSaveAsRequested→saveSessionDialog`,
`onSavePresetRequested→savePresetDialog`, `onExportPresetRequested→exportPresetDialog`,
`onImportPresetRequested→importPresetDialog`, `onRenameGroupRequested→renameGroupDialog`,
`onSaveWorkspaceRequested→saveWorkspaceDialog`, `onLoadWorkspaceRequested→loadWorkspaceDialog`,
`onNewProjectRequested→newProjectDialog`, `onOpenProjectRequested→openProjectDialog`,
`onImportCatalogRequested→importCatalogDialog`, `onOpenRecentRequested→startProjectLoad`,
`onDecodeThumbnail→` decode+cache+`setHomeThumbnail`, `onCommand→CosmoService::dispatch` (S4a).
The host's `startProjectLoad` is now three lines — it fills in a `Command::ProjectOpen` and
dispatches it (`linux_main.cpp:662-668`) — so it installs no `onLoadingReady` callback; that seam is
gone (R-LOADPERF) and the load is animated by the service's events (§1.4), not by a callback the
host owns. Note: `onSaveRequested` (`App.h:129`) is a declared-but-unwired seam.

### 1.7 Construction & layout
Ctor (`App.cpp:36-166`) builds `mRoot` and adds children in draw order: `TopBar` (→ `toggleRail`,
`requestHome`, `buildMenus`), `LeftRail` (preset `onApply`→`applyPreset`+sync), `CenterStage`
(photo `onModeChange`, filmstrip `onSelect`/`onActivate`, breadcrumb `onCrumbClick`),
`RightColumn(mSession)` (actionBar → preset save/import/export; mask overlay
`onChange→writeSelectedMask`), `HistoryView` (`onSelect→jumpToHistory`), `ContextMenu`,
`PresetDialog(mAccent)`, `SettingsDialog` (`onPreviewEdge→setPreviewEdge`;
`onThreads` and `onCpuPercent` update `mSettings`, fire `onSettingsChanged` **and then**
`submit()` — notified before the re-render, or the frame the user is waiting on is the one
rendered at the old width), `ConfirmDialog`. `mHome` is standalone (not in `mRoot`);
its `onOpenRecent` captures `lastOpenCardRect()`→`mOpenFromRect` then calls
`onOpenRecentRequested`, and its `onSettings`→`openSettingsDialog()` (R-SETTINGS-5).
App no longer sets the engine's thread count at all: `cosmo::ThreadBudget` owns it, because the
width is a slice of a budget the decode pool draws from at the same time and App cannot see the
other side (R-SVC-10, D-11). `layout()` (`App.cpp:168-199`) sizes the tree from the widget constants
and animates the rail width via the `Observable<bool> mRailOpen`.

### 1.8 Helpers
`buildMenus` (§ menus above), `syncControlsToSlot` (TopBar filename, breadcrumb, filmstrip cells,
selection, `RightColumn::syncToSlot`), `refreshPhotoForMode` (Before→`renderBefore`, else
`mLastAfterFrame`, Split→both), `wheel` (Ctrl+wheel over the photo zooms; else rail/right-column
scroll), `pointer`/`key` (menu dismissal, gesture feed, Home/Loading key swallowing).

---

## 2. cosmo_core

`arstro::cosmo`, `apps/cosmo/core/*` — UI-free; owns no Artboard types.

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

### 2.4b AppSettings (`AppSettings.{h,cpp}`)
`struct AppSettings { int previewEdge=1600; int threads=0; bool useGpu=false; int cpuPercent=50; }`
— the preferences that must survive a restart (R-SETTINGS-4, R-CPU-3). `path()` =
`ProjectStore::configDir()/settings.txt`; `load()` parses `key=value` and falls back **per field**
(a `try/catch` per value, then range checks: `previewEdge<64`→1600, `threads<0`→0, `cpuPercent`
outside 1..100→50, because an out-of-range budget is a corrupt file rather than a request for the
whole machine); `save()` truncates and writes `cosmosettings=1` plus the four keys.

`static int workersFor(int percent, int cap = 0)` turned the budget into a thread count:
`round(hardware_concurrency × percent/100)` — integer `(cores*percent + 50)/100` — floored at **1**
and capped at `cap` when `cap > 0`. **It is legacy as of S1** and kept only for its existing test:
having two callers each convert the same percentage for themselves is precisely D-11, and
`ThreadBudget` (§2.4c) now owns the conversion. Do not add a third caller.

### 2.4c ThreadBudget (`ThreadBudget.{h,cpp}`) — R-SVC-10

The one owner of the CPU budget. `ThreadBudget(int percent = 50, int cores = 0)`; `cores <= 0` asks
`hardware_concurrency()` and assumes 4 if it cannot say. `setPercent()` applies the same
corrupt-value rule as `AppSettings::load()` (outside 1..100 → 50, not clamped up).

- `total()` = `clamp((cores*percent + 50)/100, 1, cores)` — **the** number; everything else is a
  slice of it.
- `decodeWorkers()` = `clamp(total() - kEngineFloor, 1, kMaxDecodeWorkers)`, with
  `kMaxDecodeWorkers = 8` (R-CPU-5: the cap binds before the budget above 16 cores) and
  `kEngineFloor = 1`.
- `engineThreads()` = `mExplicit > 0 ? mExplicit : clamp(total() - reserved, 1, cores)`. So the
  engine gets the **whole** budget when idle and only the remainder during a load. An explicit user
  CPU-threads choice (2/4/8) wins and is allowed to exceed the budget — R-CPU-2b calls it a
  deliberate override, so it is logged rather than silently clamped.
- `beginLoad()` reserves `decodeWorkers()`, calls `apply()`, resets the peak, and returns the pool
  size. `endLoad()` releases and re-applies. Not nestable: one load at a time (R-CPU-3, a pool is
  sized when it starts).
- `apply()` is `par::setThreads(engineThreads())` — **the only** place the budget touches the
  engine's width.
- `producerEnter()/producerExit()` maintain `peakDecode()`, a lock-free monotonic high-water mark
  (CAS retry loop). This is R-CPU-4 as amended: the previous version of the feature was verified by
  a log line nobody had ever run, so the peak is now a measured number a test asserts against.

The two floors are the only way the sum can exceed the total, and only by one thread on a machine
whose whole budget is a single thread. Asserted in `one_budget_is_divided_not_duplicated` across
cores ∈ {2,4,8,12,16,24,32} × percent ∈ {1,25,50,75,100}.

**The UI's thread comes off the top (R-CPU-2d).** `schedulable()` is
`max(1, total() - kUiReserve)` with `kUiReserve = 1`, and it — not `total()` — is what
`decodeWorkers()` and `engineThreads()` divide. The thread that draws the window is a thread cosmo
starts and it was in nobody's share: at 100% on 16 cores the two consumers took 8 and 8, the whole
machine, leaving the UI to contend for every core it got. One thread rather than a percentage,
because the UI is single-threaded and a share would scale the reservation exactly where it is least
needed. The floor of 1 keeps a one-thread budget working rather than reserving away the only thread
it had. `cosmo-cc backends` prints `schedulable=` and `ui reserve` alongside the rest.

### 2.4d ProjectLoader (`ProjectLoader.{h,cpp}`) — R-SVC-1

The project load, moved out of the GTK host (`startEntriesLoad`/`decodeEntry`/`pollLoad`) so it can
run — and be measured — with no window. `Result` carries what the applier needs: `index`, `group`,
`name`, `imagePath`, `parent`, `params`, `history`, `rgba`, `thumb`, `w/h`, `decoded`, `bypass`.

- `start(entries, budget, makeDecoder, onWorkerStart)` calls `budget.beginLoad()` for the pool size
  and hands `OrderedParallelLoad` a `produce` that instantiates a decoder **per call** (stateless,
  and provably per-thread without a `thread_local`), brackets the work in
  `producerEnter/Exit`, and weighs the result by `rgba.size()`. `kDecodeWindow = 8` entries ahead of
  the applier and `kMaxInFlightBytes = 512 MB` bound how far producers may run (R-LOADPERF-1a).
- `produce(i, decoder)` is the old `decodeEntry`: groups return metadata only; a leaf decodes, then
  builds its filmstrip thumbnail **on the worker** (R-LOADPERF-2) and moves the pixels into the
  result. A failed decode leaves `decoded == false`, which reads as *missing*, never as a stall.
- `poll(Result&)` is `tryConsume` plus one thing: when it drains the last result it calls
  `endLoad()`, so the engine gets its threads back at the end of the load rather than whenever the
  host remembers to tear the job down.
- `onWorkerStart` runs once per worker, on that worker. The host passes
  `cosmo_v2::pinNestedOpenMPForThisThread()`: the OpenMP thread count is a per-thread ICV, which is
  why the old `OMP_NUM_THREADS` pin in `main()` could never work (D-12). It is **no longer the only
  place the pin happens** — see 2.4f: wiring it here and nowhere else left every decode off the pool
  outside the budget (D-41). The hook stays because the failure was silent, and a silent failure is
  worth covering at both ends.

### 2.4f PinnedDecoder (`apps/cosmo/PinnedDecoder.h`) — R-CPU-2c

`NativeImageDecoder` decodes; this pins the thread it decodes on first, and it is the only decoder
the host constructs. `decodeFile` and `decodeThumb` call
`pinNestedOpenMPForThisThread(teamSize())` and delegate; `setBudget(const ThreadBudget *)` attaches
a budget and `teamSize()` is `mBudget ? mBudget->total() : 1`, re-read per decode so a budget change
lands on the next one (R-CPU-3). `makePinnedDecoder()` is the factory every `setDecoderFactory` in
the host hands the service.

Two decisions, both measured on 16 cores with a 24 MP ARW:

- **Why a decoder and not a hook.** The pin was `CosmoService::setWorkerInit`, called per pool
  worker, so it covered the project load and none of the six other decodes — `openImageFile`, the
  `.cosmo` open and the synchronous workspace load (all on the GTK main thread), `cosmo-cc info`,
  `bench`, and `renderShots`. Those took 4.6 cores and 20 OS threads at `cpuPercent=25` and 4.5 at
  100%: identical, so the setting did nothing to them. D-11's lesson a second time — the answer to
  "the limit does not limit" was a single owner, not a better clamp — so the pin became a property
  of decoding that no caller can forget, because there is no other decoder in the host to construct.
- **Why the team size is not simply 1.** 1 is the *pool's* answer: `decodeWorkers()` workers each
  opening a team of one is exactly the budget. A decode running alone has no outer parallelism to
  oversubscribe against, and pinning it to 1 removed the violation while making a single open take
  1.85 s against 0.79 s — at 100% as much as at 25%, which ignores the setting in the other
  direction. Sized from `total()` instead, `info` measures 3.26 cores / 8 threads at 25% and 4.71 /
  20 at 100%. A user's own `OMP_NUM_THREADS` outranks both (`ompPinUserOverride()`); the previous
  pin called `omp_set_num_threads(1)` unconditionally and overrode them, against R-CPU-2(c)'s own
  "an explicit user value still wins".

Host layer, and necessarily: `OmpPin` needs `dlsym`/`GetProcAddress` and `getenv`, none of which
`cosmo_core` may carry — which is also why this is a wrapper rather than a change inside
`NativeImageDecoder`. `cosmo_core_tests` compiles `OmpPin.cpp` in for the same reason `cosmo-cc`
does, so `every_decoding_thread_is_pinned` can assert the count.

`stop()` joins and releases the budget; `start()` calls it first, which is why
`OrderedParallelLoad::start` now re-initialises `mStop`/`mClaimed`/`mConsumed`/`mInFlight` — the
class had never been restarted before (each load built a fresh `LoadJob`), so a reused pipeline
started with `mStop` still latched and every worker returned immediately.

`OrderedParallelLoad::stop()` sets `mStop` **under `mMu`** and only then notifies
(`OrderedParallelLoad.h:114-135`). Signalling it outside the lock let the flag land in the gap
between a worker evaluating the wait predicate (which it does *holding* the lock) and actually
blocking, so `notify_all` reached no waiter, the worker slept forever and `join()` never returned —
D-42, deterministic on winpthreads, unseen on glibc since the class was written. This is the
UI-thread path: `CosmoService` calls `mLoader.stop()` on `ProjectClose`, and `ProjectLoader::start`
calls it before every load, so the two user actions that hung the app were going Home mid-load and
opening a second project while one was still loading.

### 2.4g Resident pixel memory (`EditEngine` + `RenderService`) — R-MEM

**The defect.** `EditEngine::Slot::source` is the decoded image in *linear float RGBA* — 24 MP x 4
channels x 4 bytes = **387 MB** — and every slot held one for as long as the project stayed open.
Measured with `cosmo-cc project --print` on 24 MP ARWs: **465 MB resident per photo**, exactly
linear — 4 photos 1.6 GB, 8 photos 3.7 GB, so the reported 120-photo catalog wanted **~54 GB on a
27.7 GB machine**. Not a leak, and not something a smart pointer addresses: the pixels were
correctly owned and correctly freed, there were simply always N of them.

**The caps.** Two byte-capped LRU pools on the engine, `mSourceLRU` and `mProxyLRU`, evicting from
the cold end until each is under `setMemoryCaps(sourceBytes, proxyBytes)`; defaults 800 MB of
sources (~2 full-resolution frames) and 1 GB of proxies (~37 previews at 1600 px). **Bytes, not a
count of images:** the previous `kMaxProxies = 6` silently meant six times whatever the camera
produced, and an image is not a unit of memory. Proxies get the larger share because browsing is
what must stay instant and a proxy is ~14x smaller than its source (R-MEM-5).

Two rules the loops encode, both learned by getting them wrong first:
- **The slot being rendered is skipped, never evicted** — a render reading an image dropped to
  satisfy a number is a crash, not a saving.
- **Skipping means leaving it in the list.** Popping the current slot while declining to free it
  stopped tracking it, so it could never be evicted once the selection moved on — a slow leak
  wearing the fix's clothes.

**Cold, not broken (R-MEM-2).** A slot with neither source nor usable proxy reports
`slotNeedsSource()`; `renderPreview` returns an empty buffer rather than drawing an empty image.
`RenderService::ensureSource` re-decodes the original file first, through
`setSourceLoader(fn)` — a `std::function` seam, exactly like `setComputeAccelerator`, so no codec
enters `arstro_image`. `CosmoService::setDecoderFactory` installs it from the same `IImageDecoder`
the load uses, which is deliberate: D-41 was a decode path nobody remembered to budget, and a
second one installed separately would be that mistake with a new name.

The loader is handed a **path, not a slot id**, because it runs on the render worker and must never
read session state; `RenderService::addImage(..., sourcePath)` carries the path alongside the slot
for exactly that reason. A slot added with no path (a paste, a test fixture) is simply never
re-decoded.

**`released` vs evicted.** `releaseImage` now sets `Slot::released`, and `selectImage` refuses only
*that*. Before the caps existed the engine could read "no source" as "removed from the session",
because `releaseImage` was the only way a source went away; now eviction is routine, and conflating
them made every cached-out photo unselectable rather than merely slow. `Engine_release_image_frees_
and_keeps_indices_stable` is what caught it.

**Measured (R-MEM-4).** `residentSourceBytes()`/`residentProxyBytes()`/`rehydrations()` are
published by the worker into `AppModel::budget.residentBytes`/`.rehydrations` and printed by
`state print` as `engineResidentMB` / `engineRehydrations` (non-stable fields — what a cache holds
at an instant is real state but never part of an R-SVC-9 comparison).

Result, on the reported case: **120 RAW photos open in 50 s with `engineResidentMB=765` and zero
re-decodes**, and engine residency is flat — 739 MB at 8 images, 739 at 16, 765 at 24, 765 at 120.
Guarded by `engine_memory_is_capped_and_a_cold_slot_is_re_decoded` (20 images resident in 2 MB under
a 2-source cap) and `a_cold_slot_is_re_decoded_through_the_service` (browsing past the cap
re-decodes and still produces frames), both checked to fail with eviction disabled.

### 2.4h Filling the browse cache, and sizing it (`EditEngine` + `PixelBudget.h`) — R-MEM-5

**The defect (D-44).** Capping resident pixels (2.4g) fixed the memory but left the cache *empty*:
the proxy was built lazily by `ensurePreviewProxy`, so it existed only for a photo already visited.
After a load nothing was cached, and the first hop to each photo paid a fresh ~1 s LibRaw decode —
8 of 10 hops on a 120-photo project, ~1.8 s each against ~0.65 s warm. That is the user's "sometime
changing photo take too long", and it was the un-measured half of the memory trade.

**Two changes.**

- `EditEngine::addImagePreviewOnly` builds the proxy **at ingest** and keeps no source, via
  `downscaleEncodedToLinear` — `downscaleLinear`'s box filter with `fromEncodedBytes`' sRGB LUT
  folded into the accumulation. Same arithmetic, one pass, and the 387 MB linear-float intermediate
  is never allocated. Conversion is per SAMPLE, not per output pixel, because averaging must happen
  in linear light. `RenderService`'s worker uses it whenever the slot has a source path (`AddCmd`
  now carries its slot so the worker can ask); an image with no file behind it cannot be re-decoded
  and so keeps its source.
- `apps/cosmo/PixelBudget.h` sizes both caps from physical RAM (~1/12 sources, ~1/5 proxies, floored
  and capped), applied by both hosts through `setMemoryCaps`. The literals it replaced were chosen
  blind: a 26 MB proxy against a 1 GB default held 37 of 120 photos while 27 GB of RAM sat unused.
  Host layer because it is a platform call.

**The trade, stated because it is real.** Resident goes 765 MB → 3.1 GB (120 proxies under a 5.6 GB
cap) and a 120-photo load goes 50 s → 65 s, in exchange for 0 of 10 re-decodes instead of 8 and a
uniform ~0.62 s hop. The load cost is the fused downscale on the render worker, which holds **one**
engine thread while a load runs (R-CPU-2); moving it onto the decode pool, where the thumbnail is
already built (R-LOADPERF-2), is the recorded follow-up.

**`frame.ready` reports its own cost** now — `ms=`, plus `rehydrated` when a cold slot was
re-decoded. `Event::ms` and `formatEvent`'s printing of it both already existed; nothing set it, so
a 250 ms hop and a 2537 ms one were indistinguishable in the log.

Guarded by `walking_a_rack_that_fits_the_cap_never_re_decodes`.

### 2.4e The service layer (`core/service/*`) — R-SVC-1…10

**`AppModel`** (`service/AppModel.h`) is the whole observable state as plain data: `revision`,
`screen`, `projectPath/Name`, `dirty`, `recents[]`, `nodes[]` (a `NodeModel` per tree node —
`node`, `parent`, `depth`, `group`, `name`, `slot`, `pending`, `failed`, `bypass`, `selected`),
`selectedNode`, `currentSlot`, `editGroup`, `imageCount`, `params`, and the `HistoryModel` /
`LoadModel` / `ExportModel` / `AppSettings` / `BudgetModel` blocks. Three rules: **no pixels**
(a frame is 5-20 MB preview or ~100 MB full, so frames are `frameSlot`/`frameWidth`/`frameSeq`
metadata and the view fetches bytes through `RenderService` as before); **no Artboard types and
no presentation** (positions, easing, hover, scroll and the transition phase belong to the view,
R-SVC-4); and `revision` rises on every change.

**`Command`** (`service/Command.{h,cpp}`) is a tagged struct with 23 kinds and one
`parseCommand`/`formatCommand` pair. The struct is authoritative and the text is generated
(R-SVC-5) — the CLI, `--script`, the control socket and the journal all use these two
functions, so the grammar cannot fork. `tokenize()` honours `"double quotes"` and no escapes,
matching every other cosmo text format. A blank or `#`-commented line parses to `Kind::None`
with an empty `err`, which is how a script file distinguishes a comment from a syntax error.
`commandNames()` exists for `--help` and for the R-SVC-9 coverage check.

**`Event`** (`service/Event.{h,cpp}`) is a kind plus `text`/`a`/`b`/`ms`. `eventName()` gives
the stable dotted name (`load.progress`, `frame.ready`) and `formatEvent()` the canonical line
`[evt] <name> <fields>`. **That line IS the log line** — the host's `onServiceEvent` does
`LOGI("%s", formatEvent(e))` and nothing else, which is why P0's separate log-level/category/
UI-logging tasks collapsed into this.

**`formatModel`** (`service/AppModelCodec.{h,cpp}`) renders the model as deterministic
`key=value` text or JSON. `ModelDumpOptions::stable` drops `revision`, `frameSeq` and
`budgetPeakDecode` — fields that legitimately differ between two front ends showing the same
state — and that stable form is the artifact R-SVC-9 compares.

**`CosmoService`** (`service/CosmoService.{h,cpp}`). `dispatch(Command)` returns false and
emits `CommandRejected` (which also lands in `model().lastError`) rather than throwing.
`pump(nowMs)` drains the loader, attaches each arrival, emits events, and calls
`endLoad()`-equivalent bookkeeping when the last result lands; it never blocks (R-SVC-6), so a
GTK timeout, a CLI loop and a fixed-tick test all drive it identically. `refreshModel()`
rebuilds the snapshot wholesale from the session rather than patching it per command — the tree
is small, and a derived snapshot cannot drift the way incrementally-maintained mirror state
does. Three seams are injected because cosmo_core may not contain them (R-SVC-7):
`setDecoderFactory` (a decoder **per produce call** — stateless, and per-call is provably
per-thread without a `thread_local`), `setWorkerInit` (the per-thread OpenMP pin, D-12), and
`setImageWriter` (the encoder, which would break Android/WASM if it lived here).

It **borrows** the session (`CosmoService(EditSession&, ThreadBudget&)`) rather than owning it.
App has owned `mSession` since long before the service existed, and reparenting that ownership
in the same step as introducing the service would mean rewriting both at once with nothing
working in between; S4 moves it and App becomes a view holding a `CosmoService&`. `session()`
is the transitional accessor and every use of it is a line S4 deletes.

`set` reuses `deserializeParams` instead of growing a second key→field table, so it accepts
exactly the fields `.cosmo`/`.cmp`/`.apf` do and can never fall behind them — a new adjustment
becomes settable from a shell for free.

### 2.5 NativeImageDecoder (`decode/*`)
`struct DecodedImage { std::vector<uint8_t> rgba; int width,height; std::string name; bool ok(); }`
(top-down RGBA8). `struct IImageDecoder { virtual DecodedImage decodeFile(path)=0; }`.
`NativeImageDecoder` implements it: `decodePixbuf` via GdkPixbuf (channels expanded to RGBA);
`decodeRaw` via LibRaw under `COSMO_HAVE_LIBRAW`; `isRawExtension` matches rw2/arw/cr2/cr3/nef/dng/
orf/raf/pef/srw/rwl/raw; `decodeFile` routes RAW→raw else pixbuf and sets `name=baseName(path)`.

---

## 3. Engine seam

`arstro`, `core/ImageProcessing/src/engine/*`. cosmo drives it **only** through `RenderService`.

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
`enum Mode{Before,Split,After}`. A `#0A0A0A` backdrop, **two stacked `ImageView`s** (Contain) that
cross-dissolve (below), a split before-view clipped to the left half + a 1.5 px divider, a
`MaskOverlay`, and a Before/Split/After `SegmentedControl` pill (bottom-center, added last so it
stays clickable). Owns shared zoom/pan: `zoom()`, `zoomAbout(factor, localInCanvas)`, `resetZoom()`;
drag-pans **every** view while zoomed (R-ZOOM-3 as amended). Callbacks `onModeChange(int)`,
`onContext(worldX,worldY)` (right-click).

**The dissolve (R-VIEW-1).** The public seam is `setPhoto(rgba, w, h, nowMs)` / `clearPhoto()` — the
canvas is told *these pixels*, not handed a view to write into, which is what keeps the pair an
implementation detail. `mPhotoBase` (bottom) and `mPhotoTop` (top) are fixed in z-order; only
`mPhotoTop->opacity` animates (`kPhotoFadeMs = 120`, `EaseOutCubic`), so the composite is
`a·top + (1−a)·bottom` and the covered layer stays opaque — no dip through the canvas, and nothing
is ever copied between the two. `mTopIsCurrent` says which view holds the newest pixels: a render
goes into the *other* one and the flag flips, so successive renders dissolve 0→1, 1→0, 0→1…
Interruption is the same code path (**R-VIEW-1a**): `animateTo` retargets from the current eased
value, so opacity never jumps. `setPhoto` sets instead of dissolving when the current view has no
image (**R-VIEW-1b**) or when the incoming pixel size differs (**R-VIEW-1c**) — then *both* views
take the pixels, so nothing stale can show around a differently-shaped frame.

**Mode (R-VIEW-2).** `applyMode()` records a wanted split state; `advance(nowMs)` starts the tween
(a setter with no `nowMs` cannot), fading `mSplitClip` and `mDivider` opacity between 0 and 1 rather
than flipping `visible` — the constructor sets them immediately, since the first frame has nothing
to fade from. `visibleView()` (the one with `a > 0.5`) is what `layout()` reads for the mask
overlay's fitted rect, so the overlay tracks what is actually on screen.

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
The engine-settings modal (R-SETTINGS). `SettingsDialog(accent)`; `onUiScale(int)`,
`onPreviewEdge(int)`, `onThreads(int)` (0=auto), `onCpuPercent(int)`, `onUseGpu(bool)`;
`show(uiScale, previewEdge, threads, cpuPercent, useGpu, gpuAvailable)`. **Five** chip rows, named
by `enum Row { kRowScale, kRowQuality, kRowThreads, kRowCpu, kRowGpu, kRows }` —

- `kRowScale` **Screen scale** (`kScales`, a reference to `AppSettings::uiScales()` =
  {75,90,100,125,150,175,200} → "n%", R-SCALE-1), labelled "Screen scale · smaller fits more",
  plus ", up to N% here" when the display caps it (R-SCALE-3). Seven chips wrap onto two lines
  (DR-SETTINGS-1a); chips above `mMaxScale` draw at 0.4 alpha and ignore clicks. **First**, because it changes the
  window every other row is read in: a user who cannot read the dialog should not have to find the
  fix at the bottom of it. It is the one row that maps onto the **view** (`App::setUiScale`) rather
  than onto the engine, and the only one bound to a core list instead of owning its own — two lists
  would drift into a chip that loads back as something else.
- `kRowQuality` Preview quality (`kEdges{1000,1600,2400}` → Draft/Standard/High).
- `kRowThreads` CPU threads (`kThreads{0,2,4,8}` → Auto/n).
- `kRowCpu` **CPU limit** (`kCpuPercents{25,50,75,100}` → "n%", R-CPU-3, labelled
  "CPU limit · Auto uses this" because the row above's Auto resolves to it).
- `kRowGpu` GPU acceleration (Off/On, "· unavailable" and the On chip at 0.4 alpha with no backend).

Plus Done. The `enum` replaced bare `0..3` literals in five places (`rowChipCount`, `chipLabel`, the
click dispatch, the row labels, the selected-chip test) — inserting a row was otherwise five
coordinated edits to five sets of magic numbers. `chipLabel(row, i)` remains the single label
mapping, read both by `chipRects` (which sizes each chip to its text) and by the paint. Card height is summed from
`rowBlockH(row)`, which follows each row's wrapped line count, so the row addition and the wrap both
resized it with no constant to update: 366 → 430 px, which still clears the 466 px logical minimum
height at every scale (shot `scale-*-settings-min`). `layoutChips` is the single chip walk behind
both `rowLines` and `chipRects` — keeping them apart is what stops `cardRect` and `chipRects` calling
each other (D-30).

`advance()` and `onOverlay()` are **public** (unlike the other Segment overrides): the home screen
drives this same instance directly, since Home is not part of the editor tree that would otherwise
tick it (R-SETTINGS-5). It is advanced every Home frame, not only while open — `isOpen()` is already
false during the closing fade, so gating on it would freeze the close mid-fade.

### 7.6 ConfirmDialog
A confirm/choose prompt (save-or-discard on leaving an edited project). `ConfirmDialog(accent)`;
`struct Button{label,bool destructive,bool primary,onClick}`; `show(title,message,buttons)`;
`close()` (snap shut). Right-aligned button row; destructive=red, primary=accent, else outline.

### 7.6b InfoDialog (R-INFO)
The image-information modal. `show(title, rows)` where `rows` is `vector<pair<label,value>>` exactly
as `AppModel::metadata` produced them — the widget interprets nothing, which is why it works
unchanged for a JPEG's eleven rows and a RAW's sixteen. ConfirmDialog's chrome and timings
(`kCardW=400`, `kPad=20`, 150 ms appear / 120 ms close, scrim `rgba(0,0,0,0.55)`), plus:

- `kRowH=19.5` (6 spacing units), `kMaxBodyH=292.5` (15 rows), `kLabelW=110.5`, `kThumbW=3`.
  `bodyHeight()` is `min(content, kMaxBodyH, room in the window)` so the card stays on a 720p screen.
- **Scrolls** through the standard pattern: `scrollBy(delta)` sets a target, `advance()` eases it in
  180 ms `EaseOutCubic`, `onOverlay` clips the body and culls rows outside it. A thumb is drawn only
  when the content overflows. `App::wheel` routes the wheel here first while it is open.
- Value column takes `font::mono()` when `looksNumeric(value)` (leading digit / sign, or `f/`,
  `ISO `, `R ` prefixes) or the label is `Path` — decided from the VALUE, because a per-row font flag
  on a service-produced list is where R-SVC-4 leaks. Labels and values are elided to fit; paths
  elide from the LEFT so the filename survives.
- Closes on Escape/Enter (`handleKey`, routed before the tree in `App::key`), the X, the Close
  button, or a click outside. `rowCount()`, `valueOf(label)` and `scrollOffset()` exist for the
  headless assertions.

### 7.7 ActionBar
The pinned Save/Import/Export row (`kHeight=39`). `onSave`, `onImport`, `onExport`. Save is the
accent button; the others outline. Per-button `HoverFade` + `hoverBox`. Icons `save`/`upload`/
`download`. (Wired in the app to Save/Import/Export **Preset**.)

### 7.8b ExportDialog
The batch-export modal (R-EXPORT). `ExportDialog(accent)`; `struct Node{parent,group,slot,name,
sourcePath}` (pre-order, `parent` indexes the vector) and `struct Request{slots, sameAsSource,
destination, usePrefix/prefix, useSubfolder/subfolder, format, quality, longEdge, embedExif,
stripGps, embedProfile}`; `show(nodes, preselect)`, `setDestination`, `setExportProgress(done,
total,name)`, `cancelExport`, `scrollBy(delta,x,y)`; seams `onChooseDestination`, `onExport(Request)`.
`kCardW=560`, `kTreeRows=8 × kTreeRowH=22`.

Tree model (public, and what the tests drive): `rowCount`, `rowIsGroup`, `rowState` (0/1/2),
`toggleRow`, `toggleExpand`, `setAllChecked`, `selectedSlots`. Ticks live on the image leaves;
`checkState(node)` derives a group's state from `collectLeaves`, which is what makes the four
propagation rules hold without a second copy of the truth (see `design.md`).

Progress state: `mPhase` (form → progress), `mCompleteAmt` (progress → confirmation),
`mExportRows`/`mExportSeq`/`mRowFade` (the manifest, each row's place in the write order, and its
eased "written" amount), `mPendingRequest`/`mFirePending` (the snapshot handed to the host only once
the collapse has played out), `mCardOffset`/`mDraggingCard`/`mSuppressClick` (the drag). See
`docs/requirements.md` DR-EXPORT-6/8 for the beat-by-beat contract.

Geometry: one `Layout layoutForm(top)` walks a single running cursor so no row can overlap its
neighbour, and returns `contentH` for the scroll extent. `cardX()` is deliberately split out of
`cardRect()` — the card's HEIGHT depends on the form's content height, which depends on the layout,
which only needs the card's X; routing the layout through `cardRect()` makes the two mutually
recursive. `cardRect()` height = `kHeaderH + lerp(min(formH,avail), progressH, mPhase) + kFooterH`,
clamped to the window with a 24 px margin (R4), and folds the fade-in rise in so hit-testing and
paint share one rect. The body is clipped and scrolls, with a gradient fade at each overflowing
edge. Two hand-rolled text fields take focus via `mFocusField` + `handleKey`. Hover ids are a flat
enum with tree rows at `kIdTree + row`.

### 7.8c ExportWriter (`apps/cosmo/ExportWriter.{h,cpp}`, host layer)
`resolvePath(req, sourcePath, fallbackName)` → destination dir (created with
`g_mkdir_with_parents`) + prefix + stem + the format's extension. `write(req, rgba, w, h, path,
sourcePath, error)` packs RGB for JPEG (no alpha) or hands RGBA straight to PNG/TIFF, applies the
long-edge cap with `gdk_pixbuf_scale_simple` (downscale only), saves via `gdk_pixbuf_savev`
(`quality` for JPEG, `icc-profile` with a 468-byte embedded sRGB v2 profile for JPEG/TIFF), then
post-processes: `addPngSrgbChunks` splices `sRGB`+`gAMA` after IHDR for PNG (whose GdkPixbuf saver
rejects `icc-profile`), and `readJpegApp1`/`stripGps`/`injectApp1` copy the source JPEG's Exif APP1
into a JPEG output, optionally dropping IFD0's `0x8825` GPS pointer (a fixed 12-byte entry, so
removing it and decrementing the count keeps every other offset valid).

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
