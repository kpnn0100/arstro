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


### DR-RENDER-6 The monitor renders off the UI thread (D-5, arstro.design.rule R2)
With `Host::asyncPreview` (the GTK host sets it), `renderFrame` (`core/service/ServiceRender.cpp:401`)
plans on the calling thread — resolve, grades, source frames, geometry, copied into a `FramePlan`
(`planFrame`, `:191`) — and hands the plan to one worker that decodes, grades and composites it
(`previewLoop`, `:433`, over `executePlan`, `:348`) with its own decoders and grade engine; the
frame cache is shared. The latest plan wins; the call returns the newest finished frame at once
(false only before the first); `pump` raises `frameSeq` when a frame lands, and the view asks again.
A plan's key covers every input to the pixels, so an unchanged request is never re-rendered.
Renders and export-still plan and execute synchronously, unchanged. Guarded by `the async monitor
answers at once and delivers the synchronous pixels`.

### DR-HOST-2 Thumbnails decode off the UI thread, one decoder per file (D-5, D-6)
`interstellar_host::Thumbnailer` (`host/Thumbnailer.cpp:81`) answers from its cache or queues the
request and returns false; its worker (`:107`) decodes with a persistent `HostFrameSource` per file
(at most six open), box-filters, and raises `epoch()`. The app's optional `AppHooks::thumbnailEpoch`
makes the Grade deck and Home re-ask for what they are missing (`app/App.cpp:520`); late strip
frames and covers fade in. `FrameSourceFFmpeg` decodes forward to targets within ~1.5 s rather than
seeking back to a keyframe (`host/FrameSourceFFmpeg.cpp:211`).

### DR-RACK-4b A group's weight fades its own contribution (R-RACK-4, D-7)
For each source on screen, ancestor groups with weight < 1 are collected; the frame is graded with
them bypassed (Cosmo's bypass rule — `gradeForBypassing`, `core/service/ServiceRender.cpp:158`)
and with them on, mixed by the product of their weights, then mixed with the ungraded frame by the
source's own weight. Weight 0 ≡ the group bypassed, byte for byte.


### DR-RACK-8 Selection, Group Selection and remove (R-RACK-8)
`rack select <node> [--add|--range]` keeps the service's selection (`mSelection`, `#rackobj` ids)
beside the Grade target: plain = one, `--add` toggles (removing a node leaves the target where it
was), `--range` takes every row between the last plain/added click and this one in tree order
(`InterstellarService::rackCommand`, `case CK::RackSelect`). `AppModel::rack[].selected` carries it;
`rack group new` with no `--nodes` groups the selection (no name: one is made up, as Cosmo does);
`rack remove <node>` is Cosmo's `delete`, refused while any clip uses the source, and takes its
`#tlgrade`/`#fx` with it (Cosmo unlinks nodes, so other bindings keep their ids). The rack tree and
the filmstrip send `--range` on Shift-click and `--add` on Ctrl-click; Ctrl+G is Group Selection.
Guarded by `Shift selects a range, Ctrl toggles, Group Selection groups it` and `a source leaves the
rack only when no clip uses it` (L2), and the app's `Shift-click … --range` / `Ctrl-click … --add` /
`Ctrl+G` checks.

### DR-UI-10 Cosmo's context menu on rack rows and filmstrip cells; the weight bar names itself (R-UI-9)
`EditScreen` carries cosmo's `ContextMenu`; a right-click on a rack row (`RackTree`) or a filmstrip
cell (`GradeDeck`, via cosmo `Filmstrip::onContext`) calls `App::openRackContext`
(`app/App.cpp`), which applies cosmo's selection rule and opens Add Footage · Group (Selection) ·
Ungroup · Enable/Disable Filter · Rename (cosmo's inline morph → `rack rename`) · Duplicate as
Variant · Copy Grade · Paste Grade (to Selection) · Remove from Rack — each a command line. The
weight bar's caption ("weight 80%") cross-fades over the bind name on hover. Selected rows carry an
eased wash; the filmstrip rings every selected cell. Guarded by `right-click on a rack row opened
cosmo's context menu`, `its Group item dispatched rack group new`, `hovering the weight bar
cross-fades in its caption` and shots `grade_context_menu`, `grade_multiselect`,
`grade_weight_caption` (both sizes, looked at). Cosmo's `ContextMenu` gained read-only
`itemCount`/`item`/`itemRect`; cosmo's 41 shots byte-identical.

### DR-RACK-3b Any frame of a source, graded — for Grade's monitor and the reference-frame slider (R-RACK-3, R-UI-3)
`renderSourceFrame(bind, t, edge)` (`core/service/ServiceRender.cpp:430`) plans one rack source at
source time `t` (`t < 0` = its reference frame) graded as the open version resolves it, fitted to the
source's own shape (`planSourceFrame`, `:323`), and presents it like the monitor (asynchronous under
`asyncPreview`). `AppModel::rack[].mediaDuration` carries a source's length once opened — selecting
a video opens it (no decode), so the slider can span the whole source without a load opening every
file. Guarded by `a source previews at any time, the monitor captures at full size, the length reaches
the model`.

### DR-UI-11a Capture what the monitor shows, at full size (R-UI-11)
`captureFrame(bind)` (`:439`) renders, synchronously and at full resolution, the source's reference
frame when `bind` names one (Grade) or the current timeline at the playhead (else the Grade target's
reference frame); `capture --out <p.png> [--source <bind>]` saves it; the GTK host's Copy Frame puts
the same pixels on the system clipboard. Same test.

### DR-UI-3c Grade has no transport; its monitor shows the Grade target alone (R-UI-3, amended)
`EditScreen` eases one value, `mTransportAmt` (1 in Cut/Deliver, 0 in Grade): `setTab` records the
intent (`app/widgets/EditScreen.cpp:109`), `advance` starts the 180 ms tween (`:213`), and `layout`
gives the monitor `colH − transportH × amount` and fades/culls the transport from the same live value
(`:146`). In Grade `App::fetchFrame` routes to `App::fetchSource` (`app/App.cpp:588`), which asks the
optional hook `AppHooks::renderSource(bind, t, edge)` — the GTK host binds it to
`renderSourceFrame` (DR-RACK-3b) — for the selected source at its reference frame (`t < 0`), or at the
ref-frame slider's preview time; the caption reads `<bind> · ref <timecode at the source's fps>`
(`rack[].mediaFps`, add-only). A group or no target falls back to the timeline at the playhead.
Guarded by `Grade at rest: no transport`, `the Grade monitor asked renderSource for the target at its
reference frame`, `the transport EASES in leaving Grade`, `…and the monitor's height follows the live
amount`, the layout test's per-tab `Grade has no transport`; mutants (transport snapped; Grade routed
to the timeline) went red. Shots `grade_populated`, `tab_grade_to_cut_mid` (both sizes, looked at).

### DR-UI-11b The capture button and its menu (R-UI-11)
The transport's fourth button, beside ▶▶ (`app/widgets/Transport.cpp:17`), and — Grade having no
transport — an 18 px button left of the monitor caption that fades in with Grade (`Monitor`,
`setCaptureShown`) both raise `EditScreen::onCapture(worldRect)`. `App::openCaptureMenu`
(`app/App.cpp:351`) opens cosmo's `ContextMenu` under the button: **Copy Frame** (only when the host
binds the optional `AppHooks::copyFrame`; the GTK host renders `captureFrame` at full size into a
`GdkPixbuf` on the CLIPBOARD selection, `linux_main.cpp:224`) and **Save Frame…** (the host's .png
dialog → `App::frameSavePicked` → `capture --out <p> [--source <Grade target>]`, `:333`). What is
captured is what the monitor shows: the Grade target alone in Grade, the timeline at the playhead
elsewhere. A successful copy says so in the toast chip without the refusal's red
(`EditScreen::showNotice`); a failed one is a refusal. Guarded by `testCaptureAndGradeMonitor`
(`the capture button sits next to ▶▶`, `clicking it opens Copy Frame / Save Frame…`, `Copy Frame in
Cut copies the timeline`, `the monitor's capture button FADES in on Grade`, `…the answer dispatches
capture --out <path> --source <the Grade target>`, `a failed copy is SAID`) and shots
`grade_capture_menu`, `cut_capture_menu`, `grade_frame_copied` (both sizes, looked at).

### DR-RACK-3c The reference frame is a fast-seek slider that previews, plus frame steps (R-RACK-3, amended)
`GradeDeck`'s frame strip is the track of a slider over the WHOLE source — `rack[].mediaDuration`
once the service has opened it (`app/widgets/GradeDeck.cpp:121`), the cut's extent before — with
cosmo's white knob on the accent marker. Every position snaps to the source's OWN frame grid
(`rack[].mediaFps`, `snapToFrame`, `:201`). Dragging calls `onPreview(bind, t)` per frame crossed
(`:207`); the app stores it as presentation state (`app/App.cpp:70`) and the Grade monitor asks
`renderSource(bind, t)` for it, captioned `seek <tc>` — nothing is dispatched. Release sends
`rack frame <bind> --at <t>` and ends the preview, so the monitor returns to the (now committed)
reference frame. ‹ › beside the timecode step one source frame and commit at once (`step`, `:214`);
the marker eases to the model's new frame (220 ms). Guarded by `testRefFrameSlider` (`the slider
spans the WHOLE source`, `…nothing is committed while dragging`, `the monitor asked renderSource for
the source AT the dragged time`, `release commits rack frame s_day01 --at 9`, `› steps one frame
forward at 24 fps`, `‹ steps one frame back at the SOURCE's 50 fps`, `the marker EASES to the stepped
frame`); mutants (no preview while dragging; stepping at the project's rate) went red. Shots
`grade_ref_seek_drag`, `grade_ref_step_hover` (both sizes, looked at); the live harness over the real
service shows `of 00:00:04:00` for a 4 s source.

### DR-UI-12 Browse groups like Cosmo: members inside a shut group, one level in the strip, a breadcrumb back up (R-UI-12)
**Tree.** `RackTree` shows a row only while every ancestor group is open (`rowVisible`,
`app/widgets/RackTree.cpp:61`); the visible rows are re-keyed through `AnimatedRows`
(`syncRows`, `:85`), so a shut group's members fade out as ghosts and the rows below travel up — and
back on opening. Groups start shut; when the Grade target moves to a node a shut group hides, its
ancestors open once (`:40`), so after `rack group new` (which selects the new group) the members
are INSIDE it. The chevron before the folder toggles open/shut (`:233`) and turns with an eased
per-row amount; a double-click on a group row (`:208`) opens it in the tree and the strip. Every row
reserves the chevron's 12 px column so names align at every depth.
**Strip.** `GradeDeck` shows one level: the top, or the open group's direct members
(`rebuildCells`, `app/widgets/GradeDeck.cpp:109`); cell ↔ rack index is mapped for selection,
context menu and the offline chip. Cosmo's `Filmstrip::onActivate` (a double-click on a folder
chip, `:45`) drills in and opens the group in the tree too (`EditScreen`, `onNavigate`); the strip
follows the Grade target to its level when it moves (`:90`). A level change fades the old cells out
(120 ms), swaps them, and fades the new ones in (180 ms) sliding 18 px from the side they came from
(`:390`). Cosmo's `Breadcrumb` sits in the SOURCES header — `All sources › <group…> [› the selected
source]`, cosmo's path — and a crumb click goes up; two instances cross-fade on a path change.
Cosmo's `Breadcrumb` gained an opt-in `setMeasuredText` (real metrics instead of the width
estimate); cosmo leaves it off and its 41 shots are byte-identical before and after.
Open/shut and the level are presentation state: browsing dispatches nothing. Guarded by
`testGroupBrowsing` (`the new group is SHUT in the tree: its member is inside it`, `the strip shows
the group's chip, not its member`, `double-clicking the folder chip drills the strip into it`,
`…and opens it in the tree`, `the chevron TURNS open`, `the strip FADES out before it swaps`,
`clicking "All sources" asks for the top level`, `double-clicking a group row opens it in the strip
and the tree`, `browsing dispatched nothing`) and `testRackCommands`'s `its chevron opens the group —
presentation, no command`; mutants (groups never shut; an instant level swap) went red. Shots
`grade_populated`, `grade_groups_top`, `grade_groups_swap_mid`, `grade_groups_grouped`,
`grade_groups_open_gr2` (both sizes, looked at).

### DR-RACK-9 A variant shares its file, is its own object, and is offered wherever a source is (R-RACK-5, amended)
`rack duplicate <src>` adds a Cosmo node on the SAME stored path (nothing is copied), copies the
source's own grade in one Cosmo set, binds a new `#rackobj` (`<name>_v`), and now makes it the
selection and Grade target (`core/service/InterstellarService.cpp:1271`) so the tree and strip
follow it into view. The cut opens a file once whatever number of variants use it (the render
contexts key their decoders by resolved path). `rack[].sharesMedia` (`:472`, add-only) counts the
OTHER sources on the file; the rack row shows a muted SHARED badge, eased like OVR
(`app/widgets/RackTree.cpp:432`), the source bin says "shared · N clips". Offered by Ctrl+D
(`app/App.cpp:493`), Edit › Duplicate as Variant, the rack/strip menu and — new — the source bin's
right-click (`app/widgets/SourceBin.cpp:72`). The variant starts at the rack's top: Cosmo has no
"move into a group" (D-8). Guarded by L2 `a variant shares its file, is its own object, and is
selected once made` (the file's open count does not move when the variant renders; red with the
selection step removed) and UI `testVariants`; shots `grade_variant`, `cut_variant_menu`.

### DR-UI-13 The monitor zooms like Cosmo, eased and anchored (R-UI-13)
`Monitor` (`app/widgets/Monitor.cpp:68` `centreFor`, `:93` `zoomAbout`, `:130` the wheel, `:207` the
tween) keeps a target zoom (1×–8×) and an eased live zoom (220 ms). Ctrl + wheel calls
`zoomAbout(1.15^notches, pointer)` (cosmo's notch), which records the picture point under the
pointer; the view centre is RE-DERIVED every frame from the live zoom about that anchor
(`centreFor`) and clamped so the picture covers the frame, so the point stays under the pointer
through the tween. A plain wheel bubbles (cosmo does nothing there). While zoomed a drag pans
(direct manipulation; the anchor lets go); a double-click — or Workspace › Reset Workspace — eases
back to fit about the view's centre. The picture is drawn at `imageRect()` clipped to the 1× frame;
a mono chip names the magnification, its alpha derived from the live zoom. `wantedProxyEdge()`
scales with the TARGET zoom (one request per level, a 7680 bucket added); the service's preview
cap still bounds it (R-SET-3). Guarded by `testMonitorZoom` (`the zoom EASES`, `…the picture point
under the pointer stays under it`, `a drag pans…`, `…clamped`, `…eases back`); mutants (zoom set
instead of eased; anchor ignored) went red. Shots `grade_monitor_zoom`, `grade_monitor_zoom_mid`.

### DR-RENDER-6 The whole output spec, checked before queueing, honoured by the real encoders (R-RENDER-6, R-RENDER-3)
`render` takes `--format h264|h265|prores|dnxhr|png-seq` with `--profile`, `--quality` (CRF),
`--speed`, `--bits`, `--res WxH`, `--fps n|num/den`, `--range a:b`. Every flag is checked against
the codec before a job exists (`core/service/ServiceRender.cpp:546`): a profile only for
ProRes/DNxHR, quality and speed only for H.264/H.265, depth fixed except H.265 8/10 (DNxHR's from
its profile), the container must carry the codec, a size never above the project nor off its aspect
(no upscale, no reframe), even dimensions for video. The job carries an `EncodeSpec`
(`core/FrameSource.h`, passed to `IFrameWriter::begin`), renders AT the output size through the
render path's long edge, and SAMPLES the timeline at the output rate (`:721`) — a pure function of
t still. `renders[]` gains `width`, `height`, `fps` and `spec`, the whole spec in words, which the
queue row prints. `FrameWriterFFmpeg` maps it (`host/FrameWriterFFmpeg.cpp:76`): libx264, libx265
(8/10-bit, `hvc1`-tagged in MP4/MOV), `prores_ks` Proxy…4444, `dnxhd` DNxHR LB…444 — and writes
BT.709 at video range, tagged (`:104`, D-9). Deliver (`app/widgets/OutputSpec.cpp`) builds the line
(`renderLine`, `:146`) from codec, the rows that codec has (`rowWanted`, `:243` — collapsing eased,
opacity leading), Full/½/¼, a rate stepper with exact NTSC fractions, Whole/In–Out with marks at the
playhead, an audio sentence and a two-line summary; the column scrolls (`layout`, `:258`).
Guarded by L2 `a render carries its whole output spec and refuses what the codec cannot honour`
(13 refusals; the writer's spec; frames sampled at the output rate — red when sampled at the
project's), `interstellar_render_codecs` (every codec/profile through real FFmpeg, read back with
ffprobe; an H.264 render decodes to the still's colour within 4 — red at 13 levels with the BT.601
matrix), UI `testTransportAndDeliver` (the whole line, a row EASES open — red when set, the column
scrolls at 1024x640); shots `deliver_spec_h265`, `deliver_spec_codec_mid`, `deliver_populated`.

### DR-TL-6 Copy and paste a clip; a drop places the rest of the source (R-TL-6, R-TL-1)
`clip copy <clip>` keeps the clip as the CURRENT version resolves it (overrides included)
(`core/service/InterstellarService.cpp:2260`); `clip paste [--at] [--track]` adds a clip from it at the
playhead on its track by default and then sets every other EDITABLE field the schema lists —
speed, fit, opacity, blend, geometry, numbers through `canonicalNumber` — so a field added to a clip
later is pasted without this code knowing it (`:2276`); the paste is the selection and one undo
step. `clip add` without `--out` places the rest of the source from `--in` — the source's measured
length, a still 5 s (`:2155`). `hasClipClipboard` / `clipClipboardFrom` in the model (add-only).
Guarded by L2 `a clip is copied and pasted whole; a drop places the rest of the source` (red while
numeric fields were not pasted).

### DR-UI-14 The Cut tab reaches every cut operation (R-UI-14)
**Drop.** `SourceBin` owns a drag that starts on a source row and reports it (`onDragSource`
move/drop); `EditScreen` forwards it to `Timeline::dropHover` / `dropAt`
(`app/widgets/Timeline.cpp:516` `dropLocate`): a ghost clip, eased in, snapped like a move, on the
video lane under the pointer, a "new video track" when there is none, refusing an audio lane. The
app turns a drop into `clip add --track <trk> --src <bind> --in 0 --at <t>`, first
`track add --kind video` when it must (`app/App.cpp:386`). **Roll / slip.** Alt on the edge two
touching clips share rolls it (`:341`; both clips' shared edge moves live; `clip roll`), Alt on a
body slips it (`:415`; right = earlier material, the new in-point shown; `clip slip`); the clip says
what the drag is while it is held. **Keys** (`app/App.cpp:569` and the Ctrl block): Shift+Delete
ripple-deletes, M drops `m<n>` at the playhead, Ctrl+C / Ctrl+X / Ctrl+V copy, cut, paste a clip on
the Cut tab (a grade on the Grade tab, as before). **Menus** (`:480` reports right-clicks): a clip
offers Split at Playhead, Copy, Cut, Paste, Delete, Ripple Delete, Add Dissolve to Next (a touching
neighbour), Speed 50/100/200 %, Show Source in Grade (`app/App.cpp:418`); an empty lane offers
Paste Here, Add Marker Here, Add Video/Audio Track (`:456`). Guarded by `testCutEditing` (drop on
V2, refusal over audio, an empty timeline making its track, roll, slip, the keys, both menus);
mutants (Alt ignored for roll; the ghost set, not eased) went red. Shots `cut_drop_source`,
`cut_roll_mid`, `cut_slip_mid`, `cut_clip_menu` (both sizes, looked at).

### DR-FX-5 A rack node's image-processing stack (R-FX-5)
The `.isp` carries `#effect id=ef_<n> node=<rackobj> type=… order=… enabled=… mix=… <params…>`
(`model/Project.h` `Effect`; the schema knows the fixed fields, the plugin's parameters ride the
open key=value fields the parser already preserves — project-format §5.1). `effect add <node>
--type <t>` / `effect remove` / `effect move --to <i>` (`core/service/InterstellarService.cpp:2267`)
keep `order` dense; `set`/`get <effect>.enabled|mix|<param>` (`:1749`, `:1863`) validate against the
catalog's ranges. `effectChain` (`:2219`) gives a source's plugins after Cosmo — its own enabled
ones in order, then each ancestor group's, inner first — and a cache-key string; the frame path
applies them after the grade and weight, sized in source pixels scaled to the frame
(`core/service/ServiceRender.cpp:404`), and keys the frame cache on them. `rack remove`, `rack
ungroup` drop a node's plugins; `rack duplicate` copies them with fresh ids; add/remove/move are
undoable. The model publishes `effects[]` (with each parameter's catalog definition and value) and
`effectTypes[]`; the registry documents every plugin parameter, generated from the one catalog.
A plugin belongs to the rack like a grade: the same in every version — and a PIN freezes Cosmo's
colour, not the plugins (a limit, said here). Guarded by L2 `a rack node's plugin stack runs after
Cosmo…` (an edge source: blur softens it, off and mix 0 give the input exactly, reorder, a group's
plugin on its member, undo, a variant's own copy, the file) — red with the group walk cut and with
the plugins not applied — and the model round-trip (`#effect` byte-exact, refused on a missing node).

### DR-FX-6 Blur, in kinds (R-FX-6)
`render/Effects.cpp` — the catalog (`:128`) and `applyEffect` (`:150`): Gaussian (three box passes,
σ = radius/2), Box (separable sliding window), Directional (bilinear samples along a line), Zoom
(along the ray to a centre), Spin (along an arc); rows in parallel; sizes × `scale`; mix lerps the
output over the input. Guarded by the render suite: every blur leaves a flat field flat; Gaussian
symmetric, energy kept within 3 %, less spread at half scale; directional along its angle only
(red with the angle ignored); zoom and spin keep their centre; mix 0 = input, 0.5 = half way.

### DR-UI-16 The IMAGE PROCESSING list and the effect panel; the weight leaves the rack row (R-FX-5, R-RACK-4 amended)
`PluginList` (`app/widgets/PluginList.cpp`) under the histogram shows the Grade target's stack:
Cosmo first (switch = `set <bind>.bypass`, its mix % = the weight), then the node's effects in order
(switch = `set <ef>.enabled`, mix %, a × on hover = `effect remove`); its height eases with its row
count; rows travel (AnimatedRows); switches slide. A row click SELECTS it (presentation); a
right-click asks the app for Move Up / Move Down / Enable / Remove (`app/App.cpp`
`openPluginContext`), "+ Add" for the catalog (`openAddEffectMenu`, which selects the new effect).
`GradeInspector` (`app/widgets/GradeInspector.cpp` `layout`) lays out histogram · list · then
Cosmo's **Mix** slider (`set <bind>.weight`) and cosmo's tabs, or the selected effect's
`EffectPanel` (Mix + the catalog's parameters as cosmo `SliderRow`s, `set <ef>.<key>`), cross-faded
through one eased amount. The rack row lost its weight bar, its drag and its caption
(`app/widgets/RackTree.cpp`). Guarded by `testPluginList` (Cosmo's switch and Mix, an effect's
switch, select → the panel CROSS-FADES — red when set —, Radius, + Add → the catalog → `effect
add … --type blur.zoom` selected, × → `effect remove`, right-click → Move Down/Remove), the rack
test's "no weight bar to drag", and the motion test's sliding switch; shots `grade_populated`,
`grade_plugins_effect`, `grade_plugins_swap_mid` (both sizes, looked at).

**Collapsible sections (amended 2026-10-05).** `EffectPanel` (`app/widgets/EffectPanel.cpp`) shows every
effect of the selected effect's node, in stack order, as a section — a header with a disclosure
arrow turning with the section's eased amount (`:224`), the effect's name and id, over its own
SliderRows (built per effect INSTANCE, `:24`). A header click opens or closes it (`:191`), eased
(`:158`): the body's height and the rows' opacity follow the amount, and rows outside it are culled
(no input). Selecting an effect in the list opens its section and scrolls it into view as it will
stand open (`:75`); the body scrolls (`EasedScroll`). Presentation only — nothing is dispatched.
Guarded by the plugin-list UI test (two sections in order; the selected open, the other shut; a
header opens eased; another collapses eased, its rows culled, no line dispatched — red with header
clicks inert). Shot `grade_effects_sections`.
### DR-UI-15 Scopes: histogram, waveform, parade, vectorscope, clipping and levels in words; the clip overlay (R-UI-15)
`scopesOf` (`app/widgets/Scopes.cpp`) measures the frame the monitor shows: a sampled histogram,
a BT.709 luma waveform (256 × 128, log-scaled density), an R|G|B parade, a Cb/Cr vectorscope, and —
over EVERY pixel — the share clipped at 255 and crushed at 0 per channel and the code values used
per channel. `ScopePanel` (`app/widgets/ScopePanel.cpp`) replaces the histogram slot in the Grade
column: mode tabs (Histogram · Waveform · Parade · Vector, cross-faded), graticules (25 % lines; the
vectorscope's 100 % circle, 75 % targets and skin-tone line), a CLIP switch, and the readout line
"▲ clipped % ▼ crushed % · levels n/256 · <bits>-bit source, 8-bit preview" (red at 0.5 % or more).
The source's bit depth comes from the decoder (`IFrameSource::Info::bitDepth`, FFmpeg's pixel-format
descriptor) into `rack[].mediaBitDepth`. The CLIP switch fades `clipMaskOf`'s overlay (red where a
channel is at 255, blue where one is at 0) over the monitor's picture (`app/widgets/Monitor.cpp`),
refreshed with each frame by `App::showScopes`. The histogram is drawn here, not by cosmo's
fixed-height widget (R-UI-5 amended). Guarded by `testScopes` (half white/half black → 50 %/50 %,
2 levels, the mask's colours; a ramp → 256 levels and a rising waveform; red lands upper-left on
the vectorscope; the readout; Waveform CROSS-FADES — red when set; the CLIP switch fades the
overlay) and `interstellar_host` (a 10-bit HEVC source reports 10, the rest 8). Shots
`grade_populated` (histogram), `grade_scope_waveform`, `grade_scope_parade`, `grade_scope_vector`,
`grade_clip_warning` (both sizes, looked at).

**Luma | RGB (amended 2026-10-05).** `scopesOf` also builds `waveformRgb` (`Scopes.cpp:89`): each
channel's per-column density at its own level, painted additively in its own primary, so a neutral
reads white and a cast or a single clipped channel reads as its colour. The waveform's body carries a
Luma | RGB switch in its top-right corner (`ScopePanel.cpp:62`, drawn with the waveform's own fade,
`:267`); the two plots cross-fade (`:188`). Guarded by the UI test (a 200/30/30 frame lights red at
red's level only and cyan where green and blue agree; a mid grey draws white; the switch
cross-fades and returns). Shot `grade_scope_waveform_rgb`.
### DR-ANIM-1 Curves: `#anim` + `#key`, After Effects' bezier, on the footage's clock (R-ANIM-1, -2, -3, -5)
The format (`model/Project.h`, `Schema.h`; project-format §5.3): `#anim id node key` names one
parameter of one #rackobj, #effect or #clip by id; `#key anim t v [in out speedIn inflIn speedOut
inflOut]` are its keyframes, written right under it in time order, a side's speed and influence only
when that side is a bezier. Validation refuses a curve naming nothing, two curves for one parameter,
two keys at one time, an influence outside (0, 100]. The evaluator (`model/Anim.h:57`, `:83`) is
After Effects' model: per side linear | bezier | hold, a bezier side's speed (units/s) and influence
(% of the segment) placing the cubic's control points; two linear sides are an exact lerp; a hold
keeps the value; presets Linear, Ease, Ease In, Ease Out, Hold (`:93`). Header-only, so the graph
editor will draw the very function the render path evaluates.
`core/service/ServiceAnim.cpp`: `animTarget` (`:102`) maps an address to its curve and CLOCK — a rack
node's continuous colour key or an effect's mix/parameter run in SOURCE time, "now" being the
source's reference frame (`sourceNow`, `:88`); a clip's opacity and geometry run on the clip's own
footage clock (in + offset × speed), so a move, a head trim or a split keeps every key on its frame.
`key add|remove|set|clear` (`animCommand`, `:313`) — times to the millisecond, values tidied to
float precision, a speed or influence making its side a bezier, the last key's removal and `key
clear` leaving the value as the parameter's own (`writeStatic`). R-ANIM-3: `set` on an animated
parameter keys it at now (`setAnimated`, `:289`; hooked first in `setAddress`,
`InterstellarService.cpp:1723`). R-ANIM-5: curves are the root's (`curveEditable`, `:211`) — a version
cannot key the rack or an effect, nor a clip it inherits; its `set` is a `#tlgrade` delta against the
curve's value at now (`:1782`) and renders on top of it. The render path: `gradeFor` /
`gradeForBypassing` replace each tree node's own value with its curve at the layer's source time
before the deltas and the fold (`applyColourCurves`, `:477`; `ServiceRender.cpp:173`, `:276`),
`effectChain` evaluates effect curves, `planFrame` the clip's (`ServiceRender.cpp:310`), so plan keys
— and the frame cache, the ring, the preview cache — follow the animation; Grade's panels show the
curves at the reference frame. Pins freeze curves: the commit hashes the .cmp AND the rack's curve
text (`InterstellarService.cpp:1584`), written to `<commit>.anim`, read by `pinCurvesFor` (`:526`).
Curves follow their nodes: dropped with the node (`pruneAnims`, `:444`, after every command), copied
by `rack duplicate` (colour and effect curves), `clip split` and `clip copy`/`paste` (`copyAnims`,
`:455`). Model `anims[]` (`fillAnimModel`, `:562`), event `keys.changed`, undoable. Guarded by model
tests (byte-exact round trip; four refusals; the evaluator: lerp, symmetric ease, the leaving slope
IS the speed — 19.999 for 20, ease-in, hold, influence) and L2 `keyframes: …` (a 0→1 curve renders
exactly the still-0.5 frame at source time 1, and so does another clip of the source elsewhere;
`set` adds a key at the reference frame; a version adds +0.1 and cannot key; a pinned version does
not move when the base's curve does; effect and clip curves; a split keeps both halves animated;
`key clear` renders the same frame; undo; save and reload). Mutants red: curves ignored on the rack,
pins reading live curves, `set` never keying, clip curves not rendered. By hand: exposure −2 → 1.5
eased and scale 1 → 1.6, three stills look right.

### DR-ANIM-2 Animation is authored in the timeline's key lane (R-ANIM-3, R-ANIM-4, amended 2026-10-05)
Grade carries no keyframe control: no diamond on its rows or effect parameters, no curves face in
its deck (both were built and removed the same day at the user's request; cosmo's `SliderRow` and
`ParamPanel` are back to their own code, so cosmo is untouched). Its sliders still edit an animated
value where Grade stands (R-ANIM-3's `set` keying, the service's — editing, not animating).
**The key lane** (`app/widgets/KeyLane.{h,cpp}`), opened by the ◇ toggle in the Cut ruler's header
and revealed from the timeline's bottom (`Timeline.cpp:663`), belongs to the SELECTED clip
(`rebuild`, `KeyLane.cpp:35`): sections CLIP (opacity, position X/Y, scale, rotation — the clip's
curves), GRADE (the 23 colour keys of its source, `ColourKeys.h` — the source's curves, law 1) and
EFFECTS (its source's effects' mix and parameters). Each row's diamond — empty, outline, filled,
eased — keys or un-keys at the PLAYHEAD on the clip's footage clock (`in + (playhead − at) × speed`);
a source's time and the clip's footage time are the same seconds inside one clip, so one mapping
serves every section. A section header folds its rows (eased heights, presentation only); the
column scrolls (`EasedScroll`, measured from the drawn rows) and a chosen row is scrolled into view.
The chosen property's curve is drawn by `KeyGraph` (`bindGraph`, `:95`) with its plot exactly the
clip's span on the timeline and its "now" line under the playhead (`:107`), so keys sit under the
frames they key. In the graph: drag a key (time + value) or a bezier handle (speed + influence),
double-click to add, right-click for the key menu — Linear, Ease, Ease In, Ease Out, Hold, "Speed &
Influence…" (`NamePrompt::showFields`, only changed numbers sent: a speed makes its side a bezier)
and Delete Key (`App::openKeyContext`). Guarded by the UI test `keyframes: authored in the timeline's
key lane, not in Grade` (Grade keys nothing; the lane eases open and lists CLIP, GRADE, EFFECTS; it
keys at the footage time of the playhead — red with the in-point instead; the Contrast diamond keys
the SOURCE's contrast; diamonds fill eased; folding eases and dispatches nothing; a key drag, the
menu, the dialog). Shots `cut_key_lane`, `cut_key_lane_grade`, `cut_key_lane_mid` — looked at at both
sizes (two bugs found that way: the graph's now line stood at Grade's reference frame; a chosen row
was not scrolled into view).
### DR-ANIM-3 Shapes animate: tone curves, colour wheels, the mixer and the crop (R-ANIM-6)
A `#key` may carry `shape=` — the value in the address's own syntax (`"x,y;…"`, `h,s,l`, `x,y,w,h`) —
and a curve is all numbers or all shapes (validated). `model/Anim.h`: a shape blends by the segment's
PROGRESS (`progress`, `:131` — the keys' temporal sides on a 0 → 1 ramp, so presets and influence ease
a shape too; speeds are flat), point by point when two curves have as many points and resampled at 17
x positions otherwise, a wheel's hue the short way round (`blendShape`, `:140`; `evalShape`, `:194`).
The service accepts the registry's Points, Triple and Quad kinds as shape targets
(`ServiceAnim.cpp:118`; switches and whole numbers stay refused), keys them with text values
(`key add|set --value <text>`; a speed on a shape is refused, `:449`; a missing value is the
parameter's own, `staticText`, `:198`), applies them on the render path (`:543`), freezes them in pins
(the pin's curve file gains the shape) and copies them with their node. Model: `anims[].shape`,
`shapeNow`, `keys[].shape`. UI: the key lane's GRADE section lists the source's curves, wheels, mixer
and crop (`ColourKeys.h`); the graph draws a shape as a row of keys moved in time only
(`KeyGraph.cpp:347`); its menu offers presets and "Edit in Grade at This Key" — Grade stands on the
key's source frame, where editing the curve or wheel rewrites that key (R-ANIM-3) — instead of typed
speeds (`App.cpp:446`). Guarded by model tests (round trip with quoting; mixed curves refused; hue
the short way; point-by-point and resampled curves; ease and hold on a crop), L2 `shapes animate…` (a
wheel and a crop half-way render exactly the still of the blended value — the crop on the half-white
"edge" fake, since a flat frame cannot show one; a curve resampled to 17 points; a speed refused; the
file keeps the text; a pinned version stays put when the base's wheel changes; the last key's shape
stays — red with shapes skipped on the render path) and the UI test (the lane lists them; a key row;
a vertical drag says nothing, a sideways one moves time only; the menu's Grade jump).

### DR-ANIM-4 The graph edits like an editor's; the lane sizes itself (R-ANIM-7, R-ANIM-8)
**Several curves.** In the key lane a Ctrl/Shift-click on a property adds it to the graph (again
removes it); the last chosen is in FRONT. `KeyGraph` (rewritten, `app/widgets/KeyGraph.cpp`) draws
every chosen curve, each normalised to its own fitted range (eased per curve), the front one on top
with its value axis and handles, the others quieter; shapes are spread as rows. **Selection** is by
(property, time) and survives a rebind: a click selects a key, Shift adds or removes, a box dragged
on empty plot selects every key inside it across curves (`:364`, `:425`). Dragging a selected key of
a multi-selection moves them all in time (`:378`) and dispatches ONE line — `key shift --keys
"<address>@<t>,…" --by <dt>` (`ServiceAnim.cpp:541`), refused when two keys of a curve would meet
(`:559`), undone in one step. **Copy/paste**: `key copy --keys …` keeps values, shapes and sides with
times relative to the earliest; `key paste [--at t] [--to <address>]` (`:588`) lands the earliest at
`--at` (default each curve's now) on the properties they came from, or onto `--to` when one property
was copied (number vs shape checked). The key menu gains "Copy Key(s)"; a right-click on empty plot
offers "Paste Keys at Playhead", "Paste Keys Here" and "Paste onto <front>" (`App.cpp:494`); Ctrl+C /
Ctrl+V in the Cut tab copy and paste keys first when the lane holds a selection or the clipboard holds
keys (`:754`). Model: `keyClipboardCount`, `keyClipboardCurves`.
**The lane's size** (R-ANIM-8): `settings.keyLaneHeight` (80..600, persisted) is the lane's height,
eased when the model changes it; a 6-px band above the lane resizes it by direct manipulation and
saves the height on release (`Timeline.cpp:410`); `keyLaneH()` (`:57`) never takes more than leaves
one track showing, so at 1024×640 the tracks stay visible and the lane stays usable.
Guarded by L2 `several keys at once…` (shift across two curves; a collision refused — red with the
check removed; undo; copy two keys, paste at 3 s and onto another clip; a shape target refused; one
property required for --to; the setting bounded and persisted) and the UI test (two curves drawn,
the last in front; a box selects across both; a selected key's drag dispatches `key shift`; Ctrl+C /
Ctrl+V; the plot's paste menu; the lane resized and saved; one track still shows when asked for 600
px and at 1024×640). Shot `cut_key_lane_multi`.

### DR-GPU-1 With Use GPU on, the grade runs on Cosmo's GPU pipeline (R-GPU-1)
Measured first: Cosmo's GL backend was available here but ported only exposure, contrast and white
balance, so any real grade declined to the CPU — "Use GPU changed nothing". Cosmo's desktop backend
is now a multi-pass pipeline (cosmo DR-GPU-8) porting every per-pixel stage, Texture, Clarity and
the colour mixer — and then the rest: rotation, lens, noise reduction, dehaze, sharpening, grain
(crop, quarter turns and masks as Cosmo's own CPU code inside the GPU job). No edit declines. In Interstellar every render thread's engine
(the UI thread's, the read-ahead workers', the preview worker's, the cache builder's) takes the
setting at its next frame (`core/service/ServiceRender.cpp:376`, an atomic the settings write);
with the GPU on only two read-ahead workers take jobs (`:548`) — more GL contexts would add memory,
not speed; the frame cache keys on the processor (`:391`, law 7: the GPU may differ by a code value).
Model: `settings.gpuInUse` — the UI thread's last graded frame ran on the GPU (`GradeEngine::
lastAccelerated`). Measured with `interstellar_play_bench` on 4K + exposure/contrast/clarity under a
loaded machine: playing, exact frames 78 → 120 of ~135 due, mean lag 0.44 → 0.01 frames; a 640-px
grade 51 → 34 ms (the rest is CPU-side conversion and transfers); at 1080p a spatial grade (NR,
sharpening, dehaze, lens, grain, rotation) 762 → 94 ms. Guarded by L2 `with Use GPU on…` (the frame
grades afresh on the GPU — `gpuInUse` — within 2/255 of the CPU, the spatial stages included; off
again is CPU) and cosmo's `EditEngine_gl_pipeline_matches_cpu_per_stage` (22 cases).

### DR-COLOR-1 A render to a 10-bit codec runs at 16 bits, decode to encode (R-COLOR-1)
`Raster` carries an optional deep payload, `rgba16`, the same layout at 16 bits full range (v8 × 257
is the same value; `core/Raster.h:35`, widen/narrow at `:48`). Only a render job whose codec keeps
more than 8 bits asks for it (`core/service/ServiceRender.cpp:978` — ProRes, DNxHR HQX/444, H.265
10-bit), and the deep frame never leaves that render: it reads and writes no cache (`:392`), the
monitor and the preview cache stay 8-bit. Each stage:
decode — `IFrameSource::frameAtDeep` (`core/FrameSource.h:41`; the default widens `frameAt`), which
FFmpeg's source fills as RGBA64 from libswscale (`host/FrameSourceFFmpeg.cpp:186`), past the 8-bit
volume (`ServiceRender.cpp:143`; a layer with a temporal effect needs the volume's window, so it stays
8-bit and is widened by the composite); no preview prescale (`:421`);
grade — the same `renderImage` call, fed by Cosmo's new `EditEngine::fromEncodedWords` and read back
from `EditEngine::lastProcessed`, the float picture its RGBA8 bytes are quantised from (`core/
ImageProcessing/src/engine/EditEngine.h:178`, `:186`; `render/GradeEngine.cpp:81`); the grade weight
mixes at 16 bits (`ServiceRender.cpp:80`); effects — the same kernels templated on the pixel type
(`render/Effects.cpp:240`); composite — `runLayer` templated on the pixel type (`render/Composite.cpp:
353`), so a deep frame places every layer on exactly the pixels an 8-bit one does, mixed in double
(`:318`); one deep layer makes the composite deep and widens the rest (`:479`); encode — RGBA64 into
libswscale (`host/FrameWriterFFmpeg.cpp:246`).
Measured end to end through `interstellar-cc` on a true 10-bit 1080p ramp (104 levels on a row, 20-px
steps), graded (exposure 0.6, contrast 20): ProRes out has 149 levels with 20-px steps; H.264 (8-bit)
has 28 with 149-px steps. 24 frames took 2.9 s to ProRes, 1.6 s to H.264 (the codecs differ too).
Guarded by: render `deep composite…` (the same coverage as 8-bit, within a code value, every sampling
kind, blend and dissolve) and `deep: a ramp finer than 8 bits survives grade, effect and composite`
(256 levels vs 4); L2 `a 10-bit delivery carries more than 8 bits…` (47 levels vs 4, through the real
service and its fake 16-bit source); host `10-bit ProRes round trip` (the real writer and decoder:
35 levels / 10-px steps against 18 / 32 from an 8-bit frame); image `Engine_deep_ingest_and_processed_
readout`. Mutants run red: the grade packing from the 8-bit bytes, the composite rounding to 8 bits,
the effects taking an 8-bit round trip, the job never asking for deep, the decode going through the
8-bit volume, the writer narrowing before the encoder.

### DR-COLOR-2 A source says what it is; the project says where it is graded (R-COLOR-2, R-COLOR-3)
`render/ColourTransform.{h,cpp}` — knows no project. A transform is a short list of per-pixel ops
built once per (source space, working space): a camera curve decoded from the vendor's published
formula on 10-bit code values (`render/ColourTransform.cpp:128`; FFmpeg's video-range RGB mapped
back first, Canon Log 3 on its IRE scale), a 3×3 gamut matrix from the primaries with Bradford
adaptation to the ACES white (`:112`), then the working space's encoding (`:311`): Rec.709 —
tone-mapped and sRGB-encoded for Cosmo; ACEScct — AP1 linear, ACEScct-encoded. A display-referred
source entering ACEScct is inverse-tone-mapped, so the Rec.709 output gives it back (within 1/512).
The tone map is ONE function (`:218`): on max(R,G,B), ratio-preserving, the identity to 0.6 and a
rational shoulder above (slope 1 at the knee) — 18 % grey stays 18 %; an exponential shoulder was
tried first and clipped S-Log3's top two stops to white (the test caught it). Each plan layer
carries its source's transform (`core/service/ServiceRender.cpp:96`, `:359`), applied after the
decode and before Cosmo grades (`:471`) — so the grade weight's "ungraded" picture is the
transformed one, never raw log — and the frame cache and the plan key name it (law 7). The address
`<bind>.input` (`core/service/InterstellarService.cpp:1840`, refused for a group and for unknown
spaces) is saved on the `#rackobj` as `input=` (project-format §2); `colour working
<rec709|acescct>` sets the header's `colorspace` (`:292`), undoable. Model: `rack[].input`,
`workingSpace`, `colourInputs`. UI: the rack row's menu says "Input Colour (S-Log3)…" and opens the
list in its place, the current one marked (`app/App.cpp:193`, `:260`); a Colour menu holds the
working space (`:243`). Guarded by render `colour: seven camera curves put 18% grey at ACEScct
0.4136…` (each vendor's published grey code value; Rec.709 red is ACES's AP1 red) and `colour:
Rec.709 is untouched; … round-trips…`; L2 `a source says what it is, …` (the frame is exactly the
transform of the plain one, a cached frame is not served after the change, saved, reloaded, undone;
ACEScct through the Rec.709 view within 2 code values); UI `colour management`. Mutants run red: the
cache key without the input transform, no view transform on the monitor.

### DR-COLOR-3 A render says what it delivers: output transforms and HDR signalling (R-COLOR-4)
`Transform::output` (`render/ColourTransform.cpp:345`): from Rec.709 working, `rec709`/`srgb` are
the graded code values (identity), `rec709-2.4` re-encodes display light with a 2.4 power, `p3d65`
converts to P3 primaries at gamma 2.6, `pq` places SDR white at 203 cd/m² (BT.2408), `hlg` puts
reference white at 75 %; from ACEScct every output decodes to AP1 linear, converts gamut, and tone
maps — SDR through the shared shoulder, PQ through a shoulder to the mastering peak, HLG through a
unit shoulder. The monitor's plan carries the working space's Rec.709 output (`core/service/
ServiceRender.cpp:385`, applied after the composite at `:500`); a render carries its `--output`
(`:884` validates: PQ/HLG need 10 bits — H.265 is moved to 10-bit when `--bits` is not given, H.264
and PNG are refused; `--peak` is PQ's alone, 400–10000; HDR is encoded in software; the job's plan
takes it at `:1067`). The writer tags what it made (`host/FrameWriterFFmpeg.cpp:160`): primaries,
transfer and matrix per output, BT.2020 coefficients for the YUV conversion of HDR; libx265 writes
the colour description and, for PQ, the mastering display (Rec.2020, D65, the peak, 0.005 cd/m²)
and MaxCLL/MaxFALL 0 = unknown (`:190`); Matroska also gets the mastering side data (`:218`).
Deliver's FORMAT gains a COLOUR row (`app/widgets/OutputSpec.cpp:318`): choosing PQ or HLG moves an
8-bit codec to H.265 10-bit (and DNxHR to HQX), choosing 8-bit later moves the colour back (`:80`) —
the Render line is never one the service refuses. D-11 found and fixed on the way (ProRes masters
said "unspecified" in their frame header). Guarded by `interstellar_render_codecs` (ffprobe: PQ
H.265 = yuv420p10le, bt2020/smpte2084/bt2020nc and the mastering display in the stream; ProRes PQ,
HLG, sRGB and P3 tags; an 8-bit HDR render refused), L2 (`--output pq` frames equal the transforms
applied by hand — 0/65535; refusals; HDR off the video unit) and UI (`colour management`). Mutant
run red: the render taking the monitor's view instead of its `--output`.

### DR-PLAY-1 The graded preview cache: one-second H.264 segments, every frame checked by its plan (R-PLAY-1)
`core/service/ServiceCache.cpp`. The cache is the current timeline AS THE MONITOR SHOWS IT, at
`cacheEdge()` (`:76` — 1280, under Preview quality), as `<stem>.cache/<timeline>/seg_<n>_<gen>.mp4`
of one second each plus an `index` naming, per frame, the FNV-1a hash of its PLAN key (`:45`) — the
key the read-ahead ring already trusts: sources, frames, grades, effects, geometry, size. The UI
thread checks (`pumpPreviewCache`, `:179`): it plans each segment's frames, nearest the playhead
first, within 6 ms a pump (`:307`), and compares hashes; a segment whose hashes differ is queued and
handed — re-planned at that moment — to ONE builder thread (`cacheLoop`, `:111`) that grades with its
own decoders and engine (not touching the frame cache) and encodes through the host's writer, H.264
q20, on the video unit when Hardware video is on (R-PLAY-3); odd sizes are padded to even and cropped
back by the reader (`:60`). A rebuilt segment is a NEW generation file (`:353`); the old one is
removed after the index points past it (`:209`), so a playback worker holding it reads a whole file.
It builds when the user has stopped (`:294`: a window, Preview cache on, no command for 1.5 s, not
playing, no render, the rack loaded — a loading rack would cache frames under the wrong plans) and
drops the segment in hand when they start again; `cache build` builds now in any host and keeps the
service busy until done (`wait cache.done`); `cache clear` deletes it (`:417`). Playback reads it:
`scheduleAhead` plans each frame at the cache edge too and, when its hash is the segment's, the
read-ahead worker DECODES it (`ServiceRender.cpp:498`, `:555`) instead of grading; a paused frame, a
render and an export are always graded. The model publishes `previewCacheFrames/Total/Building`,
per-second `previewCacheSegments` and `playbackFromCache`; the ruler draws a 2-px bar per second —
cached, stale, building — each amount eased (`app/widgets/Timeline.cpp:972`); the caption says
"▶ cached"; Engine Settings has the switch (`app/App.cpp:90`). Measured (`interstellar_play_bench`,
4K, exposure + contrast + clarity, a 13-job build running on the machine): graded live, 9 of 121 due
frames exact at 640 px; from the cache, 142 of 143 exact, all decoded, lag 0 frames, at 1280 px.
Building that 8-s timeline took 48 s (4 fps); a second session reuses it in 0.3 s; an edit rebuilt
only its segments. Guarded by L2 `the preview cache holds the graded frames…` (96/96 frames in 4
segments; a cached frame equals the graded frame; nothing rebuilt when nothing changed; an edit to
shotB's source rebuilds exactly its 2 segments — red with the hash compare removed; a new session
rebuilds nothing; `cache clear`) and `playback decodes cached frames…` (≥ 80 % of shown frames from
the cache and equal to the graded frame — red with the lookup removed), the UI test (a finished
second fades in on the ruler; the settings row dispatches) and `interstellar_live` (the real app,
left idle, fills the cache by itself).

### DR-PLAY-2 Playback reads ahead, at a size it keeps up with; a preview prescales large sources (R-PLAY-2)
`AheadPool` (`core/service/ServiceInternal.h`): 2–4 workers (cores ÷ 6), each with its OWN decoders
and grade engine. While playing, `scheduleAhead` (`core/service/ServiceRender.cpp`) plans — on the UI
thread, once per frame per (edge, project revision) — the frames from the playhead plus the measured
latency (smoothed work time), every n-th frame when the pool finishes fewer than the timeline needs
(`:446`), and the workers (`aheadLoop`, `:522`) execute them into a ring keyed by plan. `present`
(`:600`) answers a timeline
frame from the ring — exact, or else the newest ring frame not after the playhead (a dropped frame,
never a stall) — and Grade's source frames never touch it. `playEdge` (`:436`) caps the monitor's edge by
Preview quality and, while playing, by the edge the pool keeps up with: Play starts at 960, steps to
640 when the pool's rate falls under 0.9 × fps, up again over 1.8 × fps; Pause returns to the full
preview edge. Play pre-rolls until the first frames are ready (≤ 500 ms). The model publishes
`playbackEdge`/`playbackRate`; the caption says "▶ 640 px". `render::prescale` (`render/Prescale.cpp`)
box-reduces a PREVIEW of a large source by an integer factor (keeping 2× the edge) in linear light
before the grade's float conversion — 4K at 640 px: 82 → 39 ms; a full-size render never prescales.
Measured (`interstellar_play_bench`, 24 threads, exposure+contrast+clarity): 1080p — 111 of 112 due
frames shown, mean lag 0.09 frames; 4K — 98 of 100, 0.13 frames; both at 640 px while playing.
Guarded by L2 `playback reads ahead…` (ring frames are pixel-identical to a direct render of the same
t; the pool's frames are shown; Pause returns to edge 0 — red with the ring ignored) and the render
suite's prescale test (a half-white 2×2 block averages to 188, not 128; no prescale at full size).

### DR-PLAY-3 Hardware video: a setting, a render flag, and a fallback that says so (R-PLAY-3, R-SET-4)
`EncodeSpec::hardware` (`core/FrameSource.h`) asks the writer for the GPU's video unit. `render`
(`core/service/ServiceRender.cpp:739`) sets it from `settings.hardwareVideo` for H.264/H.265 and lets
`--encoder software|hardware` override it (`:745`); ProRes, DNxHR and PNG are refused a hardware
encoder before anything is queued. `FrameWriterFFmpeg` (`host/FrameWriterFFmpeg.cpp:92`) opens a
VA-API device (`INTERSTELLAR_VAAPI_DEVICE`, else FFmpeg's default render node) and `h264_vaapi` /
`hevc_vaapi` with a GPU frame pool (NV12, or P010 for 10-bit) at constant QP = `--quality` (`:170`);
each frame is converted on the CPU with the same BT.709 matrix and tags as software (D-9) and
uploaded (`:266`). Any failure — no device, no encoder in this FFmpeg, the encoder refusing the
size — closes the GPU path and opens the software encoder with a note (`:102`, `:107`); the service
appends it to the job's spec in words and emits it as info (`ServiceRender.cpp:887`), so the queue
row reads "… · hardware video unavailable (no VA-API device) — encoded in software" and the render
still finishes. `--speed` applies to that fallback; the video unit has no presets. The switch is a
`settings set hardwareVideo=0|1` line (`core/service/ServiceEdit.cpp:401`), persisted, and a row
Interstellar adds to cosmo's Engine Settings through the dialog's opt-in extra rows
(`SettingsDialog::setExtraRows`, `apps/cosmo/widgets/SettingsDialog.h:67`; `app/App.cpp:88`) — cosmo
adds none, and its 41 shots are byte-identical before and after. Guarded by L2 `a render carries its
whole output spec…` (setting on → hardware asked; ProRes → not; `--encoder software` → not; a writer
with no unit → done, said; persisted — red with the setting ignored), the UI test (the On chip
dispatches `settings set hardwareVideo=1` — red with the dialog's callback dropped) and
`interstellar_render_codecs`: on this machine (AMD, radeonsi) the hardware file has no libx264
signature, is tagged BT.709 and decodes to 215,58,28 against the still's 216,59,30; with the device
pointed nowhere it is libx264's, and said.
