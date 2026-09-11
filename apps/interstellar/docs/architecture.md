# Interstellar — Software Architecture

> **Partly as-built as of 2026-09-11.** `interstellar_core`, `interstellar-cc`, `gene_core` and the
> UI shell exist; the hosted `CosmoService`, the FFmpeg seams, the control socket and Nebula do not.
> The module map in §9 marks each row **[built]** or **[planned]**, and
> [`requirements.md`](requirements.md) carries the `DR-` entries with live `file:line` anchors.
>
> **One amendment to §3.5, made while implementing it:** the rack reaches the core through the
> `RackAccess` seam rather than by `AppModel` carrying `cosmo::AppModel` by value. Carrying it would
> have put GTK3 on every file in the library and every test. The seam is narrower, the core suite
> runs in 0.03 s against a three-line fake, and R-COSMO-4 is amended in place with the reason.

## 1. Architectural overview

Five layers, each depending only on those below it. The two lowest are reusable platform-free
libraries; the two highest are the application; the middle one is **another application, hosted**.

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│ host      interstellar/linux_main.cpp        (the ONLY layer with OS code)       │
│           GTK3 window/events · file dialogs · fonts · logging · sockets ·        │
│           FFmpeg IFrameSource/IFrameWriter · audio out · PixelBudget ·           │
│           binds a Cairo IRenderTarget                                            │
├──────────────────────────────────────────────────────────────────────────────────┤
│ app       arstro::interstellar_v1   App.* + widgets/* + workspaces/*             │
│           the Segment tree (4 workspaces, monitor, timeline, mixer, rack) —      │
│           renders through artboard::IRenderTarget only; dispatches Commands      │
├──────────────────────────────────────────────────────────────────────────────────┤
│ interstellar_core  arstro::interstellar   core/*                     (UI-free)   │
│           InterstellarService (Command in, AppModel + Event out) over            │
│           Project · Timeline · ParamRegistry · Automation · BindingGraph ·       │
│           Evaluator · Composite · FrameCache · RackEmbed · RenderJob             │
│           No Artboard dependency. The CLI is a front end of THIS.                │
├──────────────────────────────────────────────────────────────────────────────────┤
│ cosmo_core  arstro::cosmo    apps/cosmo/core/*     — HOSTED, NOT COPIED          │
│           a real CosmoService instance: THE colour authority (R-COSMO-3).        │
│           Interstellar dispatches cosmo::Commands to it and reads its AppModel.  │
├──────────────────────────────────────────────────────────────────────────────────┤
│ engines   arstro   core/ImageProcessing/*      EditEngine · RenderService ·      │
│                                                EditParams · composeParams        │
│           gene     core/Gene/*                 the expression language            │
│           nebula   core/Nebula/*               text project · VCS · embeds · pool │
└──────────────────────────────────────────────────────────────────────────────────┘
        Artboard (artboard) — the platform-free 2D UI framework the app builds on
```

The unusual line is the fourth. **`cosmo_core` is a dependency of `interstellar_core`** — not a
sibling app, not a vendored copy, not a shared header. Interstellar constructs a `CosmoService`,
dispatches `cosmo::Command`s into it, and reads its `cosmo::AppModel`. Everything the user means by
*"project in project"* falls out of that one relationship, and §3.5 is where it is specified.

## 2. Key architectural decisions

### 2.1 Strict downward layering
The host is the only layer permitted OS calls. The app depends on Artboard + `interstellar_core`;
`interstellar_core` depends on `cosmo_core` + the engines and on **no** UI framework;
`cosmo_core` depends only on ImageProcessing. A codec, a socket, an `argv` or a `getenv` below the
host is a layering bug. This is what lets a whole master render — including the colour — run with no
display, which is the only way `R-RENDER` is testable.

### 2.2 The core is a service, from the first commit
Not a migration. Cosmo's `R-SVC` is a project it is still finishing — its `App.cpp` holds 105 direct
session calls against 7 dispatches, admitted in its own comments. Interstellar has no such history,
so `dispatch(Command)` / `pump(nowMs)` / `model()` is the *only* seam the app layer gets, and there
is no transitional accessor to reach past it. (R-SVC-1, R-G-4.)

### 2.3 One authority per fact
The design's spine (R-G-3). Colour lives once, in the rack. A parameter's value at *t* has one
producer. A clip's position lives in the clip. Where a second reader needs a value it derives it —
including Interstellar's `AppModel`, which carries Cosmo's model **by value as a member** rather
than copying fields out of it (R-COSMO-4).

### 2.4 Non-destructive, params-in / pixels-out, extended by time
Cosmo's model is `source + EditParams → frame`. Interstellar's is
`sources + EditParams(t) + geometry(t) + composite(t) → frame(t)`. Nothing is baked: automation and
bindings are evaluated per frame, and `auto flatten` is the only command that ever writes a computed
value back — and it says what it destroyed.

### 2.5 Everything animates (R-G-1, by reference)
The design law is `arstro.design.rule`, invoked, not restated. What is specific to Interstellar is
that **the playhead is a value the service owns and the view eases toward** (R-SVC-4): the service
says 4.271 s and frame 102 is ready; the view knows how to travel there. A view that sets the
playhead directly from a drag is correct — direct manipulation is the one motion exemption — but
every value *derived* from it still eases.

### 2.6 Determinism is a structural property, not a discipline
`render(project, branch, range, spec)` is a pure function (R-NFR-1). That is why the evaluator has
no cache that survives a parameter change, why grain is seeded from `(source id, frame index)`, and
why the frame-cache key hashes every resolved value that fed the layer. Without it there are no
golden-frame tests, and without those there is no evidence about a video editor at all.

## 3. The seams in detail

Interstellar has **seven** external contracts. Cosmo has four; the three new ones are the frame
source, the frame writer, and the hosted Cosmo service.

### 3.1 Output HAL — `artboard::IRenderTarget`
As Cosmo: widgets emit path/paint/text/image primitives; the host owns one persistent
`artboard::CairoTarget` and calls `app.render(target, nowMs)` per draw signal. Images are registered
per target, so the target must persist across frames — and the monitor registers a *new* image every
frame, which makes its registration lifetime the one thing to get right (see
[detailed_design.md](detailed_design.md) §5.1).

### 3.2 Input HAL — gestures
As Cosmo: the host feeds pointer/scroll/key to `App::pointer/wheel/key`, which drive an
`artboard::GestureRecognizer` routing to the Segment under the pointer. Interstellar adds a
workspace-aware sink (Grade / Cut / Mix / Deliver) the way Cosmo has a screen-aware one.

### 3.3 Frame source — `IFrameSource` *(new)*
```cpp
struct SourceInfo { int width, height; double fps; long long frames; double duration; };
class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    virtual bool open(const std::string &path, SourceInfo &out) = 0;
    /** Straight RGBA8 for `frame`. Decoders are sequential: `frame` is expected to
     *  advance by 1 in the common case, and a seek is the exception, not the rule. */
    virtual bool frameAt(long long frame, std::vector<uint8_t> &rgba, int &w, int &h) = 0;
};
```
The host implements it over FFmpeg (`libavformat` / `libavcodec` / `libswscale`); the core is
codec-free, exactly as `cosmo_core` is with `IImageDecoder`. Two properties are requirements rather
than implementation details: **one source object per worker thread** (a decoder is not
thread-safe), and **every worker runs the init hook** that pins nested OpenMP — Cosmo's D-12 is a
per-thread lesson and it cost a whole investigation.

A still image is a `IFrameSource` with one frame, so the timeline needs no second code path for
stills. The behind-the-scenes photographs in the rack are the same objects as the footage.

### 3.4 Frame writer — `IFrameWriter` *(new)*
```cpp
struct OutputSpec { int width, height; double fps; std::string codec, container; int quality; };
class IFrameWriter {
public:
    virtual bool begin(const std::string &path, const OutputSpec &spec) = 0;
    virtual bool write(const uint8_t *rgba, int w, int h) = 0;
    virtual bool end() = 0;
};
```
Host-side, FFmpeg again — plus a **PNG-sequence writer**, which is not a convenience: it is the only
output a golden-frame test can compare byte-for-byte, so it is the first one built (R-RENDER-3).

### 3.5 The rack seam — a hosted `cosmo::CosmoService` *(new, and the important one)*

```
             InterstellarService
                    │
   RackEmbed ───────┤  owns:  cosmo::CosmoService   (constructed with OUR budget,
        │           │                                OUR caps, OUR decoder — R-COSMO-8)
        │           │
        │  dispatch(cosmo::Command)          ── a colour edit from any Interstellar front end
        │  onEvent(cosmo::Event)             ── re-published into OUR event stream, prefixed
        │  model()  → cosmo::AppModel        ── carried BY VALUE in our AppModel (R-COSMO-4)
        │  renderService()                   ── the per-frame colour render (R-EVAL-2 step 5)
        ▼
   japan18.cmp    the file BOTH apps read and write
```

Five rules make this a seam rather than a tangle:

1. **Commands only.** `RackEmbed` never calls `EditSession`, never mutates an `EditParams` and never
   reads Cosmo's private state. `set gr1.basic.exposure=0.2` becomes
   `cosmo::Command{Kind::Set, fields{{"exposure","0.2"}}}` preceded by a `Select` of the rack node —
   which is exactly what `cosmo-cc` does, so the two paths are one path.
2. **Cosmo's events are re-published, not swallowed.** A `cosmo::Event` becomes an
   `interstellar::Event` with a `rack.` prefix, so `--watch` shows the colour edit that a render
   depended on. An edit visible in one channel and invisible in another is the failure R-SVC-5
   exists to prevent.
3. **Cosmo's model is a member, not a mirror.** `AppModel::rack` *is* a `cosmo::AppModel`.
   Interstellar adds only its own projections — bind names, and which rack nodes are referenced by
   clips.
4. **One budget, one decoder, one memory cap** (R-COSMO-8). Interstellar constructs the service with
   its own `ThreadBudget`, its own `PixelBudget`-derived caps and its own `PinnedDecoder`. Two
   services each converting the user's CPU percentage independently is D-11 with a new name — and
   Interstellar has *three* consumers of the budget where Cosmo had two.
5. **No Cosmo UI.** Not its `App`, not its window, not its screens. Where Interstellar's Grade
   workspace looks like Cosmo it is because it *links Cosmo's widget classes* and drives them itself
   (R-UI-6) — the pattern `arstrobench` already uses for `cosmo/Theme.cpp`.

### 3.6 Engine seam — `RenderService`, per layer
Each active clip's frame is `EditEngine`'s output for its rack node's effective params. Reuse, not
re-implementation: the pyramid levels, the interactive budget, the LRU pixel pools, the source
re-decode on eviction and the histogram all already exist and all already have requirements
(Cosmo's R-PREVIEW, R-MEM). What Interstellar adds is that the **`SourceLoader`** seam is fed by the
`IFrameSource` at a *frame index*, so an evicted video frame re-decodes the same way an evicted
still re-decodes.

### 3.7 Project seam — `nebula`
The text model, the canonical serializer, the commit store, the semantic merge, the auto-rebase and
the resource pool ([shared-core.md](../../../docs/shared-core.md)). **None of it exists.**
Interstellar needs the same minimal subset Solaris does
([../../solaris/docs/prerequisites.md](../../solaris/docs/prerequisites.md) §B1), it must be built as its
own library rather than inside either app, and P1 may legitimately ship with only the serializer +
branch pointers while merge and embeds land in P9.

## 4. Workspaces and the state machine

Four workspaces over one project, plus the screens either side of it:

```
enum class Screen    { Splash, Home, Loading, Editor }
enum class Workspace { Grade, Cut, Mix, Deliver }
```

- **Screen** is Cosmo's, near enough to reuse its transition machinery: a splash before the window,
  a home grid of recent projects, an animated open transition overlapped with the load, then the
  editor.
- **Workspace** is Interstellar's, and switching one is **not** a screen change: the project stays
  loaded, the playhead does not move, the monitor does not reload (R-UI-1). The switch is a
  cross-fade of the panel columns around a monitor that never leaves.

Loading a project is a two-stage load, and the order matters:

```
project open cut.isp
  → parse the .isp                                    (fast, text)
  → resolve the rack embed → CosmoService::ProjectOpen (SLOW: n images decode)
       … Cosmo's own LoadProgress events, re-published as rack.load.progress
  → resolve source media: open an IFrameSource per distinct file, read SourceInfo
  → build the timeline model; compile every expression; topologically sort the graph
  → seek the playhead to 0 and render one frame
```

**Announce before you mutate** (design-rule gotcha 14): `ProjectOpening` is emitted *before*
`resetWorkspace`, so a view is entitled to clear itself when it hears that a project is opening.
Cosmo learned this by attaching 18 decoded images to nothing (D-13).

## 5. The editor Segment tree

```
mRoot
├─ TopBar ───────── MenuStrip, wordmark, project name, workspace switcher, playhead readout
├─ WorkspaceHost ── cross-fades the four column sets; the Monitor is NOT inside it
│   ├─ GradeColumns ── RackTree (Cosmo's group tree) │ Cosmo's ParamPanel / MixerPanel /
│   │                  CurvePanel / GradePanel / XformPanel / HistogramWidget  (R-UI-6)
│   ├─ CutColumns ──── SourceBin │ TimelineView (tracks, clips, transitions, markers)
│   ├─ MixColumns ──── ObjectList │ LaneStack (automation lanes) │ BindingInspector
│   └─ DeliverColumns  RenderQueue │ OutputSpecPanel │ LintReport
├─ Monitor ──────── ONE widget, every workspace, the composited frame (R-UI-2)
├─ Transport ────── playhead scrubber, play/pause, in/out, level + dropped-frame readout
└─ modals ───────── (overlay pass, raised) ConfirmDialog, ExpressionEditor, RenderDialog,
                    SettingsDialog, ProjectDialog
```

`Monitor` sits outside `WorkspaceHost` deliberately: it is the same widget showing the same frame in
every workspace, and a frame that looks different in two workspaces is a defect (R-UI-2). Putting it
inside the cross-fading host would make it two widgets with two states.

Layout follows Cosmo's convention — a non-virtual `void layout()` per container, called every frame
after `advance()` so geometry stays correct mid-tween, pure arithmetic, no allocation. The choice is
deliberate rather than inherited: Artboard's `Row`/`Column` exist and pulsar uses them, but the
timeline and the lane stack both place one flexible child against several animated siblings, which
is the case hand-written arithmetic is better at (`arstro.design.rule` §4).

## 6. Data flow

### 6.1 A colour edit (the two-service path)
```
SliderRow drag → widget callback → EditCommands: build Command{Set, "gr1.basic.exposure=0.2"}
  → InterstellarService::dispatch
      → ParamRegistry resolves the address → owner = cosmo, node = cn_41
      → RackEmbed: cosmo::Command{Select cn_41} then cosmo::Command{Set exposure=0.2}
          → CosmoService → EditSession::curParams() → submit() → history + RenderService
      → cosmo::Event{ParamsChanged} → re-published as [evt] rack.params.changed exposure
  → InterstellarService invalidates every frame-cache entry whose parameter hash covered cn_41
  → next pump(): the frame for the playhead is re-rendered → Event{FrameReady}
Monitor (next frame): takeFrame() → push into the ImageView → HistogramWidget.setHistogram
```
Note what the widget does **not** do: it does not know the edit went to Cosmo. The address space is
the abstraction, and the registry is what routes it (R-PARAM-3).

### 6.2 A frame
Exactly the eight steps of [binding.md](binding.md) §6 — resolve time, automation, bindings, rack
composition, per-layer colour, per-layer geometry, composite, output. That order is a contract, and
a golden-frame test guards it.

### 6.3 A cut
```
TimelineView drag → Command{ClipMove, clip, at, track} → dispatch
  → Timeline mutates the clip node → recompute the active-clip index for every frame in range
  → invalidate the cache for the affected time range only  (not the whole cache)
  → Event{TimelineChanged} + Event{FrameReady} for the playhead
```

## 7. Threading model

Four kinds of worker, and **one object decides how many of each** — `ThreadBudget`, shared with the
hosted Cosmo service (R-NFR-3, R-COSMO-8):

| worker | owns | fed to the UI by |
|---|---|---|
| **rack decode pool** | Cosmo's `ProjectLoader` — the rack's stills and reference frames | `CosmoService::pump()`, drained in entry order |
| **colour render worker** | `RenderService`'s `EditEngine`, exclusively; requests coalesce to the latest | `tryAcquire()` polled from `pump()` |
| **video decode workers** | one `IFrameSource` each; read-ahead for playback | a bounded ordered queue, `OrderedParallelLoad`'s pattern |
| **render job** | an offline master render: decode → evaluate → composite → write | progress events; cancellable at a frame boundary |

Rules that are requirements, not implementation notes:

- **All UI state stays on the UI thread.** Workers touch only their own job.
- **The service polls, not the view.** `tryAcquire` *moves* the frame out, so exactly one owner may
  call it; while that owner was the view, no headless front end could tell a frame had arrived
  (Cosmo's D-21). `pump()` acquires; `takeFrame()` hands it on.
- **Playback read-ahead is bounded**, and bounded by *bytes*, not by frame count — a 4K frame is
  33 MB and a 20-frame lookahead is 660 MB.
- **A render job may not starve playback**, and playback may not starve a render job: the budget
  divides, and the division is published in the model so "why is it slow" is answerable
  (R-CPU-4's lesson).

## 8. Persistence

| artefact | what |
|---|---|
| **`.isp`** | the Interstellar project — Nebula text (R-FMT). Contains no colour. |
| **`.cmp`** | the rack's Cosmo project, referenced by the embed and written by Cosmo's own writer. **Interstellar never writes `.cmp` bytes itself** — it dispatches `cosmo::Command{ProjectSave}`. |
| **`.slp`** | an embedded Solaris project (audio), read-only in v1 |
| **resource pool** | `res:<hash>` → media, relinkable, offline-flagged rather than fatal |
| **recents** | a TSV in the config dir, as Cosmo's `ProjectStore` |
| **settings** | machine-scoped preferences in the config dir; project-scoped ones in `#settings` |
| **log** | `interstellar.log` in the config dir — and it is the event stream (R-SVC-5) |
| **cache** | proxy frames on disk, content-addressed by the same key as the memory cache, prunable, never required for correctness |

## 9. Module map

Every planned file, its layer, and the requirement that justifies it. A file with no row is a file
nothing justifies (`arstro.rule` §3.1).

**[built]** rows exist and are covered by a `DR-` entry; **[planned]** rows do not exist yet. A
doc that cannot tell you which is which is worse than no doc — four instructions in cosmo's own
skills named tools that had never existed.

| Path | Layer | Responsibility |
|------|-------|----------------|
| `core/Gene/*` | engine | **[built]** `gene::` promoted out of `genesis_core`, extended with dotted paths (any depth, digit-led segments allowed) and a time scope; `genesis` aliases it via a four-line shim (R-BIND-2) |
| `apps/interstellar/core/Project.{h,cpp}` | core | **[built]** the `.isp`: ten node types, canonical text, the byte-exact fixed point, the colour-field refusal, the `nan` repair, bind names and renaming (R-FMT) |
| `apps/interstellar/core/Timeline.{h,cpp}` | core | **[built]** the cut operations and the derived cut points (R-CUT) |
| `apps/interstellar/core/ParamRegistry.{h,cpp}` | core | **[built]** the generated address space and the owner-based router for every `set` (R-PARAM) |
| `apps/interstellar/core/Automation.{h,cpp}` | core | **[built]** shapes, links, the overlap refusal, lanes and the boundary lint (R-AUTO) |
| `apps/interstellar/core/BindingGraph.{h,cpp}` | core | **[built]** compiled Gene expressions, derived deps, cycle refusal with rollback, topological order (R-BIND) |
| `apps/interstellar/core/Evaluator.{h,cpp}` | core | **[built]** steps 1–4, pure; the grade-weight fold; the parameter hash (R-EVAL) |
| `apps/interstellar/core/Composite.{h,cpp}` | core | **[built]** steps 6–8: inverse-mapped geometry, seven blend modes, the linear mix (R-COMP) |
| `apps/interstellar/core/RackAccess.h` | core | **[built]** the rack seam — four methods, and the R-COSMO-4 amendment as built |
| `apps/interstellar/core/service/*` | core | **[built]** `InterstellarService` (46 command kinds), `Command` + its generated codec and specs, `Event`, `AppModel`, `AppModelCodec`, the generated API document (R-SVC) |
| `apps/interstellar/cli/main.cpp` | front end | **[built]** `interstellar-cc`: argv, stdout, a PPM writer, no behaviour (R-CLI) |
| `apps/interstellar/Theme.h` | app | **[built]** aliases cosmo's token namespaces; adds `surface::` and `time::` (R-G-2) |
| `apps/interstellar/App.{h,cpp}` | app | **[built]** the shell: four workspaces, the monitor outside the deck, the gesture→Command seam (R-UI-1) |
| `apps/interstellar/widgets/*` | app | **[built]** `Monitor`, `TimelineView`, `LaneStack`, `Transport`, `WorkspaceBar` (R-UI-2/3/4) |
| `apps/interstellar/tests/*` | tests | **[built]** `interstellar_core_tests` (29), `interstellar_ui_tests` (8), `interstellar_shots` with `--size`/`--script`/`--tree`/`--check` |
| `apps/interstellar/core/RackEmbed.{h,cpp}` | core | **[planned, P3]** the hosted `cosmo::CosmoService` behind `RackAccess` |
| `apps/interstellar/FrameSourceFFmpeg.*` / `FrameWriterFFmpeg.*` | host | **[planned, P2/P8]** the codec seams; `IFrameSource`/`IFrameWriter` are declared today and the CLI implements a PPM writer |
| `apps/interstellar/linux_main.cpp` | host | **[planned, P10]** the GTK window, the control socket, the audio bed |
| `core/Nebula/*` | engine | **[planned, P9]** the text project store, VCS, merge, embeds, resource pool |
|  |  | *the rows below were the original plan and are superseded or deferred:* |
| `apps/interstellar/linux_main.cpp` | host | GTK app, events, dialogs, fonts, the control socket, the FFmpeg source/writer construction, the budgeted decoder, the audio device (R-SCOPE-7, R-PLAY-1, R-RENDER-2) |
| `apps/interstellar/FrameSourceFFmpeg.{h,cpp}` | host | `IFrameSource` over libav*: open, seek, decode to RGBA8, one object per worker (R-PLAY-1) |
| `apps/interstellar/FrameWriterFFmpeg.{h,cpp}` | host | `IFrameWriter`: ProRes / H.264 / H.265 (R-RENDER-2) |
| `apps/interstellar/FrameWriterPngSeq.{h,cpp}` | host | `IFrameWriter`: a PNG sequence — the only byte-comparable output (R-RENDER-3) |
| `apps/interstellar/AudioOut.{h,cpp}` | host | the audio bed, clocked to the video playhead (R-PLAY-6) |
| `apps/interstellar/App.{h,cpp}` | app | screen + workspace state machine, transitions, the Segment tree, the host-callback seam (R-UI-1) |
| `apps/interstellar/EditCommands.{h,cpp}` | app | the ONE place a moved control becomes a `Command` — shared by every workspace (Cosmo's R-TOUCH-1 pattern) |
| `apps/interstellar/Theme.{h,cpp}` | app | **aliases** `arstro::cosmo_v2::palette/radius/font` and compiles `cosmo/Theme.cpp`; adds only Interstellar's own surface literals (R-G-2, `arstro.design.rule` §2) |
| `apps/interstellar/widgets/Monitor.{h,cpp}` | app | the composited-frame view, shared by every workspace (R-UI-2) |
| `apps/interstellar/widgets/TimelineView.{h,cpp}` | app | tracks, clips, transitions, markers, playhead, snapping — cuts only (R-UI-3) |
| `apps/interstellar/widgets/LaneStack.{h,cpp}` | app | automation lanes: one row per automated address, links drawn as clips (R-UI-4) |
| `apps/interstellar/widgets/ObjectList.{h,cpp}` | app | the mixer's object column, expandable to its parameters (R-UI-4) |
| `apps/interstellar/widgets/BindingInspector.{h,cpp}` | app | a binding shown on the parameter it drives, with its resolved value (R-UI-4) |
| `apps/interstellar/widgets/ExpressionEditor.{h,cpp}` | app | the expression field: completion over the registry, inline validation, live value (R-UI-5) |
| `apps/interstellar/widgets/Transport.{h,cpp}` | app | scrubber, transport buttons, level + dropped-frame readout (R-PLAY-2/4) |
| `apps/interstellar/widgets/RackTree.{h,cpp}` | app | the rack's group tree — Cosmo's tree, Interstellar's chrome (R-RACK-1) |
| `apps/interstellar/widgets/SourceBin.{h,cpp}` | app | rack sources as draggable cells; Cosmo's `Filmstrip` where it fits (R-UI-6) |
| `apps/interstellar/core/service/InterstellarService.{h,cpp}` | core | **the application** — `dispatch` / `pump` / `model` (R-SVC-1) |
| `apps/interstellar/core/service/Command.{h,cpp}` | core | the one way in + the single generated parser/formatter (R-SVC-2) |
| `apps/interstellar/core/service/Event.{h,cpp}` | core | the one way out; `formatEvent()` **is** the log line (R-SVC-3/5) |
| `apps/interstellar/core/service/AppModel.h` | core | the whole observable state as plain data, carrying `cosmo::AppModel` by value (R-SVC-3, R-COSMO-4) |
| `apps/interstellar/core/service/AppModelCodec.{h,cpp}` | core | the model as deterministic text/JSON; the `stable` form is what proves two front ends agree (R-SVC-9) |
| `apps/interstellar/core/service/ApiDoc.{h,cpp}` | core | generates `api.json` / `API.md` from the command table, the event table, the model codec and the registry (R-SVC-10) |
| `apps/interstellar/core/Project.{h,cpp}` | core | the `.isp` document: nodes, ids, bind names, load/save through nebula (R-FMT) |
| `apps/interstellar/core/Timeline.{h,cpp}` | core | tracks, clips, transitions; the cut operations; the active-clip index per frame (R-CUT) |
| `apps/interstellar/core/ParamRegistry.{h,cpp}` | core | every address with type/unit/range/default/flags/owner — **generated**, and the router that decides whether a `set` goes to Cosmo or to us (R-PARAM-3) |
| `apps/interstellar/core/Automation.{h,cpp}` | core | `AutoClip` (shapes) + `AutoLink` (placement, mapping, mode, scope) + the lane projection (R-AUTO) |
| `apps/interstellar/core/BindingGraph.{h,cpp}` | core | compiled expressions, dependency collection, cycle refusal, topological order (R-BIND) |
| `apps/interstellar/core/Evaluator.{h,cpp}` | core | steps 1–4 of the frame pipeline: one pure function, `(project, t) → resolved values` (R-EVAL) |
| `apps/interstellar/core/Composite.{h,cpp}` | core | steps 6–8: geometry, blend modes, transitions, the output raster (R-COMP) |
| `apps/interstellar/core/FrameCache.{h,cpp}` | core | byte-capped LRU keyed by `(layer, source frame, param hash, level)` (R-PLAY-3) |
| `apps/interstellar/core/RackEmbed.{h,cpp}` | core | owns the hosted `CosmoService`; translates addresses to `cosmo::Command`s; re-publishes its events; honours the pin (R-COSMO) |
| `apps/interstellar/core/RenderJob.{h,cpp}` | core | the offline master render: frame walk, progress, cancel, resume, lint (R-RENDER-1) |
| `apps/interstellar/core/Playback.{h,cpp}` | core | the playback clock, read-ahead, frame dropping, level selection (R-PLAY-4/5) |
| `apps/interstellar/core/ThreadBudget.{h,cpp}` | core | **or** a reuse of `cosmo::ThreadBudget` promoted to a shared header — one budget, three consumers (R-NFR-3) |
| `apps/interstellar/cli/main.cpp` | front end | `interstellar-cc`: argv, stdout, the clock, the codecs, the filesystem — and no behaviour (R-CLI-1) |
| `apps/interstellar/tests/*` | tests | round-trip, resolved-value tables, golden frames, timeline ops, equivalence (R-TEST-4) |
| `core/Gene/*` | engine | `gene::` promoted out of `genesis_core`, extended with dotted paths + a time scope; `genesis` aliases it (R-BIND-2) |
| `core/Nebula/*` | engine | text project model, canonical serializer, commit store, semantic merge, auto-rebase, resource pool, embed resolver (R-FMT, R-VCS) |
| `core/ImageProcessing/*` | engine | unchanged, reused: `EditEngine`, `RenderService`, `EditParams`, `composeParams` |
| `apps/cosmo/core/*` | engine (hosted) | unchanged, reused as the colour authority. Interstellar's needs on it are R-COSMO-7 (video sources) and R-COSMO-8 (injected budget), landed by `arstro.cosmo.core.implement` |

## 10. What this architecture is betting on

Stated plainly, so that if one of these turns out false the design is revisited rather than patched:

1. **That a hosted `CosmoService` is cheap enough to be the colour authority.** It is a service
   over an `EditSession` over a `RenderService` — no window, no GTK, no fonts. If it turns out to
   drag in a host dependency, the fix is to cut that dependency in Cosmo, not to copy `EditParams`.
2. **That per-frame `EditEngine` renders are fast enough for a proxy-resolution scrub.** The engine
   already drops every stage at its default value (17 of 17 at defaults) and already picks a pyramid
   level against a measured budget. If a 1280-edge graded frame cannot be produced inside a frame
   interval, playback falls to a lower level and says so — which is R-NFR-4, not a surprise.
3. **That the address space is the right abstraction for the whole app.** Every `set`, every lane,
   every expression, every API-document row and every UI panel reads the same registry. If that
   turns out to be a bottleneck it is a bottleneck in one file.
4. **That Nebula can be built as a shared library rather than twice.** Solaris needs the same
   subset. If Nebula slips, P1 ships with a serializer and branch pointers only, and merge/embeds
   wait for P9 — which is why the plan sequences them that way.
