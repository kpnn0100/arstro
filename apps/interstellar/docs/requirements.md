# Interstellar — Requirements (as-built tier)

**What the code contractually does today.** `DR-<AREA>-<n>`, descriptive present, `file:line`
anchors, each citing the `R-` tag it implements. An entry with a dead anchor is a defect
(cosmo's D-1), and a behaviour with no entry does not ship.

The first build's as-built tier is withdrawn with the build it described (git history, `69b91eb`);
carrying it forward would be a document describing a seam that no longer exists. Every entry below
is the second build's.

## Conformance

Rung **4** of `arstro.rule` §5. The ladder:

| rung | means | lands with |
|---|---|---|
| 0 | spec only | done |
| 1 | core split out; `Command`/`Event`/model with one text codec | done — DR-SVC-1..3 |
| 2 | registered with the root `ctest`; L2 headless service tests | done — `interstellar_service_l2` |
| 3 | a real CLI that is the whole app without a window | done — `interstellar-cc`, DR-SVC-3 |
| 4 | **generated API document, committed, drift-tested** | ← **here** — DR-API-1; the first app in the suite to reach it |
| 5 | control socket + the GUI/headless equivalence test | post-P7 |

## Entries

### DR-RACK-1 The rack is a real hosted CosmoService (R-RACK-1)
`Rack` (`core/Rack.h:1`) owns a `cosmo::CosmoService` constructed with Interstellar's
`ThreadBudget` by reference, and reaches it **only** through `cosmo::Command`s. A colour write is
the `Select` + `Set` pair `cosmo-cc` sends (`Rack::setParam`, `core/Rack.cpp`), so the CLI path and
any future GUI path are one path. `interstellar_core` links `cosmo_core` deliberately — a real
authority is worth a link dependency, and avoiding one is what let colour be a fake in two builds.

### DR-RACK-2 A colour edit reaches a real `.cmp` (R-RACK-2) — THE GATE
Guarded by `test_a_colour_edit_reaches_a_real_cmp`, which reads the saved file back with code that
shares **nothing** with the writer, and by `test_a_second_service_sees_the_edit_the_first_one_made`,
which opens the saved file in a second `CosmoService` and reads `exposure 0.450, temp 5200` back.
Through the service (`a colour edit on a root timeline writes THROUGH`, in `interstellar_service_l2`)
the same holds for an ADDRESS: `set a.basic.exposure=0.35 a.basic.temp=5600` on a root timeline →
the `.cmp`'s `#image` carries both, read back by a parser that shares no code with the writer, and
by a second service. From a shell:
```
interstellar-cc project new mv.isp : rack add footage/a.mp4 : set a.basic.exposure=0.5 : project save
grep -A3 '^#image' mv.cmp   →   exposure=0.5
```

### DR-RACK-3 A video source is graded on an extracted frame, with no change to Cosmo (R-RACK-3)
`VideoFrameDecoder` (`host/VideoFrameDecoder.cpp:1`) is an `IImageDecoder` installed through
`CosmoService::setDecoderFactory`: a video path — `…/clip.mp4#t=2.0` — becomes one extracted frame
via `FrameSourceFFmpeg`; anything else delegates to Cosmo's own `NativeImageDecoder`. Cosmo then
holds an ordinary image slot. `rack add a.png b.png clip.mp4#t=2.0` → `3 images in the rack`.
**Limit, filed as D-1:** Cosmo opened *by itself* has no such decoder and shows the video node as
`kind=failed` — its grade is preserved in the file, its pixels are not displayable there.

### DR-RACK-4 Grouping stacks through Cosmo's own composeParams — one fold for every colour source (R-RACK-4)
Cosmo's model exposes params for the SELECTED node only, and a frame needs every node's. So `Rack`
keeps each node's OWN params as a read-through cache, filled by selecting each node once after a
load and refreshed by every write made through it, and **one** fold — `foldRender`
(`core/Colour.cpp:23`), `EditSession::effectiveParams(slot)` step for step: a bypassed node
contributes nothing of its own, every non-bypassed ancestor stacks through Cosmo's `composeParams` —
serves the live rack, a pin snapshot and a version's overrides alike. Guarded by
`test_render_params_equal_cosmos_own_for_every_node`, which holds the fold equal to Cosmo's own
`params` for every node, group bypass included, and `test_a_group_offset_stacks_onto_its_members`.
Found while writing it: `cosmo::NodeModel::parent` is the parent's NODE ID, not an index as
cosmo's `AppModel.h` comment says — walking it as an index hung the suite; `Rack::colourTree`
converts it.

### DR-RACK-5 The load is pumped against the wall clock (R-RACK-1)
`Rack::pumpUntilLoaded` (`core/Rack.cpp`) sleeps 1 ms per 16 ms tick against a wall-clock deadline
and keeps its simulated clock **monotonic** as a member — cosmo's own `pumpUntilIdle` pattern, and
D-56's lesson. The first version spun simulated time and reported `0 images` for a project that was
loading fine, because 7 500 ticks elapsed before a worker opened the first file. A timeout is now
an error naming the path, never a silently empty rack.

### DR-VOL-1 Video is a lazy volume with a WINDOW accessor and no point accessor (R-VOL-1, R-VOL-2)
`Volume::window(t0, t1, VolumeView&)` (`core/ImageProcessing/src/volume/Volume.h:121`) materialises
source frames **[t0, t1] inclusive** and is strict: it refuses t0 < 0, t1 ≥ frames, an inverted
range, more than `VolumeView::kMaxWindow` (33) frames, or a frame the provider could not produce —
and a refused call leaves the view empty. There is no `at(x, y, t)`. A clip-end window is the
driver's job (`temporalWindow`, `TemporalOps.cpp:242`), which pads by pointing the extra slots at
the edge frame. Guarded by `test_window_refuses_what_it_cannot_serve` and
`test_driver_clamps_at_both_clip_ends` (`core/ImageProcessing/unittest/volumeTests.cpp`).

### DR-VOL-2 A view is raw memory: frame pointers into the cache, rows hoisted (R-VOL-3)
`VolumeView` (`Volume.h:93`) is plain data — `const uint8_t *frame[33]`, straight RGBA8, a row
stride and an inline `row(dt, y)` — so an op resolves a row once and walks pointers. The window is
**not** copied into a contiguous block. Measured (`volume_bench`, Release, 24 threads, op alone):
temporal denoise r2 **1.35 ms** at 1080p / **5.69 ms** at 4K; frame blend r2 **0.54 ms** / 4.58 ms.

### DR-VOL-3 Residency is bounded by the footprint, never by the clip, and is invisible (R-VOL-4, R-VOL-7)
`CachedVolume` (`Volume.h:152`, `window` at `Volume.cpp:52`) keeps **exactly** the current window
resident, recycles the departing frame's buffer for the entering one, decodes in ascending order (a
forward walk decodes each source frame once), and **refuses** a window that would exceed its byte
cap rather than overshooting. Guarded by `test_residency_bounded_by_footprint_not_clip_length`,
`test_forward_walk_decodes_each_frame_once`, `test_cache_never_evicts_inside_the_window`, and
`test_residency_is_invisible` (same t, same bytes, any access order). Limit: stepping BACKWARDS one
frame re-decodes one frame (a seek) — the cost of a bound that is a number.

### DR-FX-2 Three temporal effects, each proving a different part of R-VOL (R-FX-2)
`TemporalDenoise` (`TemporalOps.cpp:94`) — a per-pixel gate against the centre frame,
`w = max(0, 1 − (d/h)²)²` with `d = (|ΔR| + 2|ΔG| + |ΔB|)/4`, `h = 48·strength`; **not**
motion-compensated. `FrameBlend` (`TemporalOps.cpp:190`) — the mean of the window, in 8-bit code
values (not linear light, so not a physically exact shutter). `freezeRemap` (`TemporalOps.h:146`) —
`min(t, freezeAt)`, a remap of t at radius 0. Guarded by: static noise σ 7.999 → 3.605 at r2
(×2.22, ceiling √5 = ×2.236) and 2.700 at r4; a moving square leaves **no** ghost (a plain average
measures 54.0 on the same probe, so the probe can fail); blend equals the analytic mean. Each test
was confirmed to fail against a deliberately broken op (cache off; denoise returning the centre;
denoise ungated). **Not yet wired to clips** — the `#fx` node and the render pipeline land with the
service (P3).

### DR-TL-4 A transition holds the outgoing clip, and the pair mixes against one base (R-TL-4, R-TL-5)
`render::activeAt(clips, transitions, t, fps)` (`render/ActiveSet.cpp:37`) returns the clips live at
`t`, bottom track first, each with its source frame `floor(localTime·fps + 1e-6)` — the epsilon
because a bare floor reads frame k−1 at t = k/fps (checked for k < 5000 at 24 and 29.97 fps). Through
a transition the OUTGOING clip is **held past its out-point** (`held = true`) with weights 1→0 / 0→1
that sum to 1, and the incoming entry carries `dissolveWithPrevious`, which `render::compose`
(`render/Composite.cpp:417`) honours by mixing the pair against the SAME base —
`base + (A−base)·wA + (B−base)·wB` — because stacking them gives `A(1−f)² + Bf`, a 75 % dip at the
midpoint: the very darkening R-TL-4 forbids. Guarded by
`activeAt: a transition holds the outgoing clip` (confirmed to fail with the hold removed) and
`a dissolve group mixes against one base: constant brightness` (`render/tests/renderTests.cpp`).

### DR-FX-3 Geometry and composite per clip (R-FX-3)
`render::Layer` (`render/Composite.h:63`) carries crop, `Fit` (Contain | Cover | Stretch | None),
scale, rotation, anchor, translation, opacity and `Blend` (Normal, Multiply, Screen, Overlay, Add,
Subtract, Difference). `placeLayer` computes the inverse affine once per layer, scans only its
bounding box, solves each row's covered span, samples bilinear in 8-bit fixed point clamped to the
crop, and runs rows in parallel; serial and parallel output are byte-identical. Measured
(`interstellar_render_bench`): two layers into 1080p, one rotated and Screen-blended,
**0.85 ms** at 24 threads (7.9 ms on one). Limits: no edge anti-aliasing; >2× downscale aliases.

### DR-RENDER-2 The grade step is EditEngine, unchanged, and a frame cache keys on the params (R-RENDER-2, R-FX-1)
`render::GradeEngine::render` (`render/GradeEngine.cpp:36`) runs Cosmo's own `EditEngine` via
`renderImage` — byte-identical to the clear/add/render slot sequence at full and proxy size, which a
test checks on every run — and passes an identity grade through untouched. `render::FrameCache` is a
byte-capped, thread-safe LRU keyed on (media, source frame, `hashParams`, proxy level); `hashParams`
never returns 0, so 0 means an ungraded source frame (R-VOL-5). Measured, 1080p, a non-identity
grade: **33–35 ms** at 24 threads (145 ms on one), 12 ms at a 1280 proxy. Most of a full-res frame
is page faults from glibc returning each 33 MB float buffer to the OS; pinning the malloc
thresholds in the host takes it to 23.4 ms (recommended to the host, not yet done).


### DR-RACK-6 Rack identity is Interstellar's, and survives a reopen (R-RACK-6)
Each `.cmp` entry binds to a `#rackobj` (`InterstellarService::bindFromCmp`,
`core/service/InterstellarService.cpp:865`) by `node=cn_<i>` — the entry's index in the file Cosmo
last wrote, kept current by `syncRackObjNodes` (`:948`) on every save — validated by media for
sources. If the `.cmp` changed in Cosmo meanwhile, sources re-bind by media (by occurrence, so two
variants of one file keep their order) and groups by order; an unclaimed entry becomes a new
`#rackobj` with a legal bind name derived from Cosmo's name; an unclaimed `#rackobj` stays OFFLINE,
never deleted. Guarded by `a reopened project binds every #rackobj to its Cosmo node again`.

### DR-RACK-3a A new reference frame changes no parameter, no Cosmo node — and reloads nothing (R-RACK-3)
`FrameSelector` (`core/FrameSelector.h`) maps a path as Cosmo stores it (`clip.mp4#t=2.000`) to the
`.isp`'s `#rackobj frame=`; the host's `VideoFrameDecoder` consults it whenever the rack loads.
`rack frame <node> --at <t>` snaps to a frame of the SOURCE's rate, updates the `.isp` and the
selector, and reloads nothing: every pixel Interstellar shows of a source (Grade monitor, filmstrip,
renders) is decoded by its own frame source at that time, and Cosmo's slot takes the new frame at
the next rack load (D-4). Guarded by `choosing a reference frame reloads nothing and the Grade
monitor shows it`.

### DR-HOST-1 Source frame N is frame N, in any container (R-VOL-1, R-TL-5)
`FrameSourceFFmpeg` counts frames from the stream's FIRST timestamp, not from 0
(`host/FrameSourceFFmpeg.cpp:70`, `:131`), and measures the length of a stream whose header carries
no frame count (`probeEndPts`, `:89`). Guarded by `interstellar_host` over mp4, mov, mkv (H.264,
HEVC 10-bit, VP9), 29.97 fps and two streams with a nonzero start (D-3).

### DR-RACK-7 An offline source reads as missing, and Cosmo is not allowed to delete it (R-RACK-7, D-2)
A `#rackobj` the rack does not have, or whose Cosmo node failed to decode, is listed with
`failed=true` and its clips `offline`; the project opens, renders (skipping it) and lints (`offline
<bind> <media>`). Because Cosmo's save deletes a failed node with its grade (D-2, Cosmo D-66), every
command that would make Cosmo save — `project save` (which still saves the `.isp`), `rack add`,
`rack duplicate`, `rack frame`, `timeline pin`, `timeline rebase` — is refused while one is offline,
naming it (`rackSaveBlocked`, `:965`). Guarded by `offline media leaves the project openable` and
`a save is refused while a source is offline`.

### DR-FMT-1 The `.isp` is a byte-exact fixed point, with unknown keys and nodes preserved (R-FMT)
`Project::parse` / `serialize` (`model/Project.cpp:709`, `:1406`) — typed nodes for everything
Interstellar implements, `RawNode` for the audio forms and any node type it does not (kept verbatim,
listed by `unrenderable()`); field tables in `model/Schema.h` shared by parse, serialize, `#tlset`
application and `setField`. `roundTripsExactly` is strict. A colour key on a `#clip` is refused
naming the key (R-TL-2); a structural error refuses; a non-finite number is repaired and counted
(a `nan` `in`/`out`/`speed` becomes a one-frame clip, not a refusal). Guarded by
`interstellar_model_tests` (431 checks, 19 groups: `roundTripEveryNode`, `messyInputCanonicalises`,
`colourOnClipRefused`, `nanRepairedAndCounted`, `structuralRefusals`, …; clean under ASan/UBSan).
Timeline names follow the bind-name rules, so `social-30s` (R-RENDER-1's example) is not legal —
`social30` is.

### DR-VER-1 A version is its base resolved live, plus its deltas (R-VER-1, R-VER-2, R-G-3)
`resolve` (`model/Versions.cpp:325`) walks the base chain unless the cut is frozen, applying
`#tldrop` / `#tlset` and adding local nodes; every edit is derived-aware, so editing an inherited
clip records a `#tlset` and never copies it (`derivedEditRecordsDeltaNotCopy`, confirmed to fail
against a copy-on-edit mutant). Arrangement ops (`model/Arrange.cpp`: trim, split, roll, …) go
through the same path; splitting an inherited clip records `#tlset out` on the original and a local
right half (`splitInheritedClip`).

### DR-VER-2 A version's colour is an OVERRIDE: a scalar delta on the colour source (R-VER-2)
On a derived timeline, `set <bind>.<filter>.<key>=v` (`setAddress`, `:1335`) stores
`#tlgrade key=(v − source own value)`, rounded to float precision; `gradeDeltas` resolves nearest
timeline first per key, and `gradeFor` (`:1256`) applies the deltas to each node's OWN params before
the fold, so a group override stacks onto its members. A base regrade therefore still arrives under
the override; `revert <address>` drops it. A curve, wheel or crop override is **refused**, pointing
at the base or `rack duplicate` (R-RACK-5): the format stores numbers. Guarded by `a version's colour
is an OVERRIDE` (0.5 → override 0.8; base to 0.6 → version 0.9; revert → 0.6; `.cmp` untouched) and
`a non-scalar override … is refused`. Both mutants (absolute value instead of delta; pins ignored)
fail the suite.

### DR-VER-3 A pin is a content-addressed byte copy of the `.cmp` (R-VER-3, R-VER-4)
`timeline pin` (`takePin`, `:1229`) saves the rack, hashes the `.cmp` bytes (FNV-1a, 12 hex) and
copies them to `<stem>.pins/<commit>.cmp`, with a `<commit>.map` of which `#rackobj` is which entry;
the base chain's overrides are copied into the pinned version's own, because the pinned walk stops
there. A pinned version's colour source is that snapshot, read through Cosmo's static
`readWorkspaceFile` and the same fold (`colourTreeFor`, `:1181`); a colour `set` on it is refused
naming the commit. `timeline rebase` reports and prunes dangling deltas (the model's `rebase`,
`model/Versions.cpp:1043`) and advances a pin to a fresh snapshot. A pin saves the rack first, so
pinning is refused while a source is offline (D-2). Guarded by `a pin freezes the base's colour`.

### DR-SVC-1 One service, one way in, one way out (R-SVC-1, R-SVC-2, R-G-4)
`InterstellarService` (`core/service/InterstellarService.h`) — `dispatch(Command)`,
`dispatchText(line)` (`:134`), `pump(nowMs)`, `model()`, `renderFrame(t, proxyEdge)` and an `Event`
sink; `formatEvent()` is the log line. The core holds no codec and no OS path: the host injects the
rack decoder, the timeline frame source, the encoder, the PNG writer and the recents path. A rack
open or add runs asynchronously (`Rack::beginOpen`/`beginAdd`, finished in `pump`) so a window never
blocks on a decode; a CLI pumps until `busy()` is false. Guarded by the 14 `interstellar_service_l2`
tests, all of which drive the service through text lines, and by `the same script on two services
dumps the same stable state`.

### DR-SVC-2 The grammar is a table, and an unknown input is refused with candidates (R-SVC-2, R-SVC-3)
`commandSpecs()` (`core/service/Command.cpp:29`) is the parser's input (`parseCommand`, `:240`),
the formatter's, `api`'s and the committed document's. An unknown verb or flag is refused naming the
nearest candidates; a switch given a value, a missing positional, and a flag the command does not
take are refused with the usage line; an unknown address, filter or key is refused by the router
with the nearest names. `format → parse` is a fixed point for every row. Guarded by
`interstellar_service_tests` (13) and `an unknown address, key or filter is refused`.

### DR-SVC-3 `interstellar-cc` is the whole app without a window (R-SVC-1, R-API-2)
`cli/main.cpp` holds argv, stdout, the clock and the codecs and no behaviour: every line goes
through `dispatchText`, then `pumpUntilIdle`, then `output()` to stdout; refusals to stderr with exit
3; `--watch` streams events; `--script` runs a file; `:` chains commands. Verified end to end on
real media (FFmpeg-generated 640×360 clips): new project → rack add (two videos, a still) → grade →
track → clips → versions → pin → rebase → transition → render H.264 (96 frames) and a PNG sequence
→ export-still; the mid-dissolve frame shows both clips at 50 %, the outgoing one held.

### DR-API-1 The API document is generated, committed and drift-tested — rung 4 (R-API-1)
`apiJson` / `apiMarkdown` (`core/service/ApiDoc.cpp`) print four tables and no hand prose: commands
(`commandSpecs`), events (`eventSpecs`), model fields (`appModelFields`) and the address space with
owners, kinds, engine units, ranges and neutrals (`paramDefs`, `core/ParamRegistry.cpp`).
`docs/api.json` and `docs/API.md` are committed; `ctest -R interstellar_api_current` regenerates and
diffs both (confirmed red on a stale doc). Two drift guards back the tables themselves: every key
`modelToJson` writes is documented and every documented key is written; every key Cosmo's
`serializeParams` writes is an address under exactly one filter.

### DR-RENDER-1 A render names its timeline, and runs a frame per pump (R-RENDER-1, R-RENDER-4)
`render --timeline <tl> --out <p>` (`renderCommand`, `core/service/ServiceRender.cpp:331`) refuses
without `--timeline`, naming R-RENDER-1; the queue row carries the timeline's id and name. Format
from `--format` or the extension (`.mp4` h264, `.mov` prores, else a PNG sequence). `pumpJobs`
(`:397`) renders ONE frame per pump, so a window stays live and `render cancel` lands on the next
frame. Limit: a frame is ~35–60 ms of grade + decode at 1080p, so a window renders at that rate
while a job runs. **Audio is not yet muxed** (P6).

### DR-RENDER-2a The frame path is pure (R-RENDER-2, R-VOL-5, R-VOL-7)
`renderTimelineFrame` (`:163`): resolve → `activeAt` → per clip, the source frame at the SOURCE's own
rate from its lazy volume (a still is frame 0; a clip's `freeze` remaps t; the first temporal `#fx`
on its rack node runs through `renderTemporal`) → the version's grade (`gradeFor`) through
`GradeEngine`, cached on (media + fx, frame, param hash, proxy) → the grade weight as a per-pixel mix
→ a `Layer` with the clip's geometry in FRAME units (so proxy and full place it identically) →
`compose`. Guarded by `a render is a pure function: same timeline, same bytes, any order` (two full
renders byte-identical; a frame identical after reading another source in between) and `the grade
reaches the pixels, and weight 0 is the ungraded frame`. When nothing is cut at t, `renderFrame`
shows the Grade target's reference frame graded, so grading works before the first cut.

### DR-RENDER-5 An Interstellar still equals Cosmo's export of the same frame (R-RENDER-5)
`tests/still_equals_cosmo.sh`, registered as `interstellar_still_equals_cosmo`: a photo graded in
Interstellar (exposure, contrast, temperature, vibrance, a tone curve, a grade wheel), cut onto a
timeline and exported with `export-still`, decodes to the same RGBA as `cosmo-cc export` of the same
`.cmp` — measured `111819bb5cc3145c0f1e54812d8f4c63` both sides on the 640×360 run; confirmed red when
Interstellar's weight is 0.5. The cheapest proof that the rack really is Cosmo.

### DR-AUD-1 The suite audio schema parses and round-trips; Interstellar places clips (R-AUD-2, R-AUD-4)
`#atrack` / `#aclip` are typed (`model/Project.h`); `#note`, `#arack`, `#aeffect`, `#aauto`,
`#asend` are kept as `RawNode` and preserved byte-exact. `audio track add` and `audio clip add`
(`--track --src --at --out [--in --gain --fade]`, references never embedded) create local nodes in the
current timeline; the model lists them as tracks/clips with `audio=true`. **Not built: the master
sum** — R-AUD-5's "sums to a master so a cut can be watched and delivered with its bed" is P6, and
a render today is picture only.


### DR-UI-1 The accent is the one forked token, installed at runtime (R-UI-2, R-G-2)
`app/Theme.h` aliases cosmo's palette, radius, font and metrics namespaces and `sharedTheme()`, and
adds only named Interstellar surface tokens; `installInterstellarAccent()` sets cosmo's runtime
accent slot (cosmo R-G-5) to `#CF5AED` first thing in `App::App` (`app/App.cpp:46`). Guarded by
`interstellar_app_ui_tests`, which reads `palette::primary()` and a reused cosmo slider fill and gets
exactly `#CF5AED`.

### DR-UI-2 Home, Loading and Edit follow the model's screen, cross-faded (R-UI-1, R-UI-3)
`App` (`app/App.h`) shows `AppModel::screen` and cross-fades between screens (260 ms); Edit is three
tabs (Grade / Cut / Deliver, keys 1–3) over ONE monitor that lives outside the tab host — a test
asserts the monitor does not move across a tab switch. The version switcher is chrome in the top bar
(`EditTopBar`), with pin/freeze/rebase/new actions.

### DR-UI-3 Cosmo's panels are reused as libraries (R-UI-5)
`EditStackTabs`, `ParamPanel`, `SliderRow`, `MixerPanel`, `CurvePanel`, `GradePanel`, `XformPanel`,
`HistogramWidget`, `Filmstrip`, `SegmentedControl`, `PillButton`, `IconButton`, `ConfirmDialog` are
compiled from `apps/cosmo/widgets/*.cpp`. Cosmo's `RightColumn` is not (it is built on cosmo_core's
service); `GradeInspector` replicates its wiring so each panel callback becomes
`set <bind>.<filter>.<key>=<v>` in cosmo's keys and unit conversions.

### DR-UI-4 The GUI dispatches text, and the host binds it to the service (R-G-4, R-SVC-1)
The app reports intent only through `AppHooks::dispatch` (text lines in the service grammar; the
exact line per control is tabled in `app/NOTES.md`); the GTK host (`linux_main.cpp`) binds
`model`/`dispatch`/`renderFrame` to `InterstellarService` and the optional `thumbnail` hook to a
host-side cache (`host/Thumbnailer`), pumps the service on every frame tick, and repaints only while
`needsRedraw`. Guarded end to end by `interstellar_live` (real app, real service, real media).
Verb check at integration: every verb the app emits parses in the grammar; `clip trim --in/--out`
are source points on both sides (`clip trim takes SOURCE points`, L2).

### DR-UI-5 Version overrides are visible and revertible in one click (R-VER-2, ui-brief §3)
A rack row carries an eased OVR badge when the current version has a `#tlgrade` on it; a click sends
`revert <bind>`. Timeline clips draw by provenance (inherited dimmed, overridden with the accent
edge, dangling in destructive with its reason). Guarded by `clicking the OVR badge dispatched
revert s_day02`.

### DR-UI-6 Every state is drawn, shot and looked at, at two sizes (R-UI-6, arstro.design.rule)
`interstellar_app_shots --check` renders 31 named states × {1440×900, 1024×640} (idle, hover, empty,
loading, populated, mid-transition) and fails a uniform PNG; `interstellar_app_ui_tests` (137
checks) asserts exact dispatched lines, layout containment at both sizes, clamped scrolling, image
release, and — pumping one 16 ms frame at a time — that every tween's first moved frame lies
strictly between start and target. Known gaps (app/NOTES.md): no drag of a source onto the timeline,
no regroup by drag, a reused cosmo slider still steps when the model pushes a new value for the same
node.

### DR-EDIT-1 One undo history across the rack and the project (R-EDIT-1)
Around every undoable command (`undoable`, `core/service/ServiceEdit.cpp:82`) the service captures
the `.isp` text and each bound rack node's own params and bypass (`captureState`, `:109`, read from
the rack's cache of Cosmo's values); `recordEdit` (`:158`) pushes the pair, coalescing writes to the
same addresses within 500 ms of wall clock (a slider drag). `undo`/`redo` (`:231`) restore a state:
the project by re-parsing its text (keeping the OPEN timeline — that is presentation), the rack by
writing params back THROUGH Cosmo with one `set` per node (`applyState`, `:129`) — masks excluded,
since `set mask=` appends and Interstellar never edits masks. A change to the rack's node set
(`structural`, `:103`) clears history, as Cosmo's per-node history would. Model: `canUndo`,
`canRedo`, `undoLabel`, `redoLabel`; event `history.changed`. Guarded by `undo and redo span the
rack and the project, and a drag is one step` — red with the rack restore disabled.

### DR-EDIT-2 Copy and paste a grade; ungroup (R-EDIT-2, R-RACK-4)
`grade copy <node>` copies the node's own params as the open version resolves them; `grade paste
[node…] [--all]` (`:271`) writes them through to Cosmo on each target — root timeline only, refused
on a version or a pin with the way forward. `rack ungroup <group>` (`:308`) is Cosmo's `group
ungroup`; the group's `#rackobj` goes, its members keep their own grades. Guarded by `grade copy /
paste, ungroup and presets work like Cosmo's Develop and Preset menus`.

### DR-EDIT-3 Presets are Cosmo's library (R-EDIT-3)
`preset save|apply <name> [--node <bind>]` are Cosmo's own `preset save` / `preset apply` on the
node (apply refused on a group — Cosmo applies to an image); `preset import <path.apf>` copies into
the library; the library is listed with Cosmo's `PresetLibrary::scan` (`rescanPresets`, `:478`) into
`AppModel::presets`. The folder is the host's (`Host::presetDir`; the GUI and CLI default to
`$XDG_DATA_HOME/interstellar/presets`, `INTERSTELLAR_PRESETS` overrides), set into Cosmo the way
`cosmo-cc --presets` sets it.

### DR-SET-1 Engine settings are commands, validated, applied and persisted (R-SET-1, R-SET-2)
`settings set cpuPercent=… threads=… previewEdge=… useGpu=… uiScale=…` (`settingsCommand`, `:370`)
validates every key (unknown keys refused with the nearest), then `applySettingsNow` (`:452`) sets
the ONE `ThreadBudget` (percent, explicit threads, `apply()` → `par::setThreads` — the engine
threads Interstellar's composite, grade and temporal ops run on), hands Cosmo the same values
(`CosmoService::applySettings`), sets the grade step's GPU opt-in (`GradeEngine::setPreferGpu`), and
writes the host's settings file, which the next service reads at construction. Model:
`settings.{cpuPercent,threads,previewEdge,useGpu,uiScale}` plus measured `gpuAvailable`, `cores`,
`engineThreads`, `decodeWorkers`. Guarded by `engine settings: one CPU budget, persisted, and preview
quality caps the monitor`.

### DR-SET-3 Preview quality caps the monitor only (R-SET-3)
`renderFrame` clamps its edge to `settings.previewEdge` (`core/service/ServiceRender.cpp:303`);
`renderTimelineFrame` — renders, export-still — is untouched; a cap never upscales. Measured in the
same test: a 640×360 project at `previewEdge=256` → monitor 256×144, render 640×360; `0` → full.

### DR-UI-7 Cosmo's menu bar and accelerators (R-UI-7)
`EditTopBar` carries cosmo's `MenuStrip` after the wordmark; `App::buildMenus` (`app/App.cpp:144`)
fills File · Edit · Settings · Workspace · Preset, every item a command line or a host picker
(Save As, Import Preset, Export Still added to the GTK host); the Preset menu is rebuilt from
`AppModel::presets` (`refreshPresetMenu`, `:190`, via cosmo's new `MenuStrip::setItems`).
Accelerators in `App::editKey` (`:404`). Engine Settings opens COSMO's `SettingsDialog`
(`openSettings`, `:126`) with its Input row hidden (cosmo's new `setInputRowShown`) — one instance
for Home and Edit; Interstellar's own Reduce-motion dialog is retired (the OS setting governs, as in
cosmo). Guarded by `interstellar_app_ui_tests` (menu clicks dispatch `project save`, `undo`,
`grade paste --all`, `preset apply Film/Warm --node …`; Workspace switches the tab; the
accelerators) and shots `edit_menu_file`, `edit_menu_file_mid`, `edit_menu_edit`,
`edit_menu_preset`, `edit_settings`.

### DR-UI-8 Screen scale zooms, and input maps through it (R-UI-8)
`App::setUiScale` (`:103`) follows `settings.uiScale` from the model (whoever changed it), eases the
DRAWN scale over 260 ms, re-derives the logical size and layout from it every frame while it moves,
renders the tree through a scaling root transform, and divides pointer and wheel positions by it;
the GTK host sizes the window minimum from `minPhysicalWidth/Height` and tells the dialog the
largest scale the display can hold. Guarded by `a new screen scale ZOOMS (first moved frame strictly
between 100% and 125%)`, `at 125% the layout is 1152 logical units wide`, `a click in pixels lands
on the logical File title at 125%`, and shot `edit_scale_125`.

### DR-UI-9 With nothing cut at the playhead, the monitor shows the Grade target's reference frame (R-RACK-3)
`App::fetchFrame` asks the service for a frame whenever there is a Grade target, not only when a clip
is under the playhead (`app/App.cpp:477`); the service answers with the target's reference frame,
graded. Before this the monitor said "no clip at the playhead" and a chosen reference frame was
never seen. Guarded by `with no clip at the playhead the monitor still asks for a frame`.
