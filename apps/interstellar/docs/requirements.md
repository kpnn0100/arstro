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

### DR-DLV-5 DCP and IMF packages, unvalidated and saying so (R-DLV-4)
`render --format dcp|imf --out <folder>` (`core/service/ServiceRender.cpp:1066`) writes a folder through
the host's package writer (`Host::packageWriter`, `:1341`). A host without one refuses.

**DCP (SMPTE).**
- Picture: the render's `dcdm` output (`render/ColourTransform.cpp:352`), DCI X'Y'Z' per ST 428-1. The
  display light becomes CIE XYZ without adaptation, white sits at 48 of 52.37 cd/m², and gamma is 2.6.
  D65 white comes out as the published 3884/3960/4092. From ACEScct it is the same picture the Rec.709
  output shows.
- Container: `--container 2k-flat|2k-scope|2k-full|4k-flat|4k-scope|4k-full` (`ServiceRender.cpp:1174`).
  The default is scope from 2:1, and 4K when the picture fills it. The picture is fitted inside, centred,
  and never enlarged.
- Rates: 24/25/30/48 at 2K and 24 at 4K (`:1231`), because OpenJPEG's DCI modes cap only 24 and 48.
- A reel lasts at least a second. `--output`, `--res`, `--bits` and `--quality` are refused with the
  way out.

**IMF App 2E.**
- The project's size (at most 4096×3112) and its `--output` colour, signalled in the descriptor.
- Rates: 23.976 to 60.

**Sound.** Both carry the master at 48 kHz: on L/R of 5.1 in a DCP, as stereo in an IMF. Captions are
burned in only; timed-text track files are not built.

**Host.**
- `host/PackageWriter.cpp:123` encodes JPEG 2000 with OpenJPEG through libavcodec: DCI 2K/4K profile
  for the DCP; for the IMF, a 9/7 at 10:1 in CPRL as RGB 12-bit. A DCP picture is centred in its
  container (`:240`).
- `host/MxfWriter.cpp` writes the track files to the layout of the reference implementation (asdcplib's
  asdcp-wrap and as-02-wrap), set for set. That means a 16 KiB header, a body partition, the index
  table, the footer and the RIP. The header is rewritten in place at the end (`:629`).
  - DCP: OP-Atom, with an RGBA descriptor plus a JPEG 2000 sub-descriptor parsed from the first
    codestream (`:53`); frame-wrapped 24-bit sound with 5.1 MCA labels.
  - IMF: OP1a with the index in its own partition; clip-wrapped stereo with the MCA "ST" group,
    carrying MCATitle, PRM and FCMP; J2CLayout and VideoLineMap.
  FFmpeg 4.4's own MXF muxer writes neither descriptor and refuses multichannel OP-Atom sound.
- The XML (`:346`):
  - DCP: CPL (ST 429-7), PKL (ST 429-8, SHA-1 base64 hashes and sizes), ASSETMAP and VOLINDEX
    (ST 429-9).
  - IMF: CPL (ST 2067-3:2016, Core Constraints 2020, App 2E 2020 identification) whose
    EssenceDescriptorList repeats the track files' descriptors in RegXML as Netflix Photon writes them,
    plus a PKL and an ASSETMAP.
- The writer's note says "NOT validated here" and names a checker (`:197`). The render's row and its
  Info event carry it. The IMF note also names the known gap: its JPEG 2000 is ISO 15444-1 (signalled
  as such, `060e2b34.04010107.04010202.03010100`), not an IMF profile, because OpenJPEG 2.4 cannot
  write one.

**UI.** Deliver's format picker gains DCP and IMF. For a DCP, the Container row takes Size's place and
the colour control becomes the words "DCI X'Y'Z' 12-bit", both cross-faded so the column keeps its
height (`app/widgets/OutputSpec.cpp:470`, `:281`). Each package has its own eased note row saying it is
not validated. The default path is a folder (`<timeline>_DCP` or `_IMF`).

**Checked during development, outside this build:**
- asdcplib's asdcp-info parses both DCP track files (descriptor, JPEG 2000 header, index, footer), and
  asdcp-unwrap extracts the frames and the WAV.
- ClairMeta 1.6.2 runs its 78 checks on a CLI-rendered 2K Flat DCP: Success, with ISDCF naming advice
  only.
- Netflix Photon 5.0.1 passes the IMF's ASSETMAP, PKL and both track files. The CPL's single error is
  the JPEG 2000 profile above. Getting there took fixes Photon pointed at: the MCA title and kinds, the
  J2CLayout, the VideoLineMap and the 2020 namespaces.

**Guarded by:**
- Render `dcdm` values.
- Host packages (`host/tests/hostTests.cpp:285`): a 2K Flat DCP and an App 2E IMF, each with three
  frames that FFmpeg reads back. The DCP frame is decoded with its picture centred (the last column at
  x 1958, black beyond). The index points at an essence key for every frame, the header holds the
  duration, the CPL holds the durations, and every PKL size is true.
- L2 `packages…`: 2048×1152 fitted to 1920×1080 in 1998×1080; X'Y'Z' pictures equal the dcdm transform
  of an ordinary render; 24 frames and 48000 samples; the IMF at 2048×1152; the twelve refusals; a host
  with no writer.
- UI `testPackagesUi`; shots `deliver_dcp`, `deliver_dcp_mid`, `deliver_imf`.
- E2E: the CLI rendered both. Frames from each were looked at (the DCP's pillarboxed).

Mutants run red:
- index offsets after the frame, the header not rewritten, the picture not centred, the sound not in
  the PKL;
- the DCP not dcdm, the fit by max, the reel and rate rules gone, the frame writer used;
- the 48/52.37 white gone, the container left out of the line, the container not following the
  project.

### DR-DLV-4 Captions (R-DLV-1)
A caption is a `#caption` node: `at`, `dur` and `text` in timeline seconds (`model/Project.h:211`, schema
`model/Schema.h:283`). It is an arrangement node like a marker, so `resolve` inherits it, a `#tlset`
overrides `text`/`at`/`dur` on a version, a `#tldrop` hides it, and a freeze copies it. Invariants: `at`
is never negative and `dur` is more than zero.

Commands (`core/service/ServiceCaptions.cpp:157`):
- `caption import <file.srt> [--offset s] [--replace]` reads an SRT (`:76`: a BOM, CRLF, `,` or `.`
  milliseconds and a position after the end time are all accepted; HTML/ASS styling is dropped, `:63`)
  onto the open timeline as `cue1`, `cue2`, … in one undo step. `--replace` drops the timeline's own
  captions first.
- `caption add --at --dur --text [--name]` and `caption remove`.
- `caption export <file.srt> [--timeline]`.
- `view captions on|off` controls only what the monitor shows.
- Addresses `<caption>.at/.dur/.text/.name` go through `setField`, so a version's edit is a delta.

A render carries captions only when asked: `render --captions burn,track,sidecar`
(`core/service/ServiceRender.cpp:1244`). `captionCues` (`ServiceCaptions.cpp:128`) cuts the timeline's
captions to the render's range and shifts them into its own seconds. A stream shows one cue at a time,
so cues that start together are joined and an earlier cue that overlaps the next is shortened (`:140`).
- **burn**: the caption on screen at each frame is drawn by the host, through the burn-in path. It sits
  bottom centre inside title safe, a plate to each line with the last line lowest, sized 4.5 % of the
  frame (`core/service/ServiceBurnIn.cpp:91`), at graphics white in HDR.
- **track**: `EncodeSpec::subtitles` (`core/FrameSource.h`) is muxed by the writer
  (`host/FrameWriterFFmpeg.cpp:246`, `:423`) as 3GPP timed text (mov_text) in MP4/MOV or SubRip in MKV.
  The encoder takes ASS events with a minimal ASS header, and the cues are written right after the
  container header so the muxer interleaves them by time. A PNG sequence or another container is
  refused, and a PNG sequence is pointed at `sidecar`.
- **sidecar**: `<out>.srt` beside the file (beside a PNG sequence's folder), written when the render
  finishes (`ServiceRender.cpp:1416`).

A timeline with no captions in the range is refused ("`caption import`"). The queue row's spec says
"· captions burned in + track + .srt".

Model: `captions[]` (the current timeline's), `captionsShown`, `timelines[].captions`.

UI:
- The monitor draws the caption under the playhead the way the burn does, cross-faded on change and
  lifted above the multicam bar (`app/widgets/Monitor.cpp:536`, fed by `app/App.cpp:1174`).
- File › Import Captions… / Export Captions… and Workspace › Show Captions (`App.cpp:332`, `:412`).
- Deliver's RANGE header line has a captions pill (`app/widgets/OutputSpec.cpp:447`), dimmed (eased)
  while the chosen timeline has none. Its menu toggles the three ways (`App.cpp:85`); the render line
  gains `--captions` (`OutputSpec.cpp:216`), never in a preset's flags, and a PNG sequence leaves out the
  track.

Guarded by:
- Model `captions()` (`model/tests/modelTests.cpp:1093`): the round trip with a line break, a version's
  text delta, the invariants, freeze, drop.
- Host `subtitles`: mov_text in MP4 and MOV and SubRip in MKV read back cue by cue at 0.25 s and 1.0 s.
- L2 `captions…`:
  - the SRT with BOM/CRLF/tags/position imports as "Hello\nworld" at 0.5 s for 0.75 s; undo and redo;
  - a version's `cue2.text` is a delta, and the export round trip;
  - a 0.25–3 s render burns "Hello" over "world" at render 0.5 s and nothing at 0 s or 1.25 s; the track
    reads 0.25–1.0 and 1.75–2.75; the .srt says the same;
  - the join and the shortening;
  - the refusals: PNG + track, an unknown way, an empty range, a zero span, a backwards cue.
- UI `testCaptionsUi`: fade-in, the cross-fade, off, the menus, the pill eased, the line, PNG leaving out
  the track, no captions meaning none.
- Shots `cut_caption`, `deliver_captions`.
- E2E: the CLI rendered MP4 (h264 + mov_text, frame 24 looked at: two lines burned in), MKV (h264 +
  subrip, extracted back to the same SRT) and the sidecar.

Mutants run red: no range shift, no shortening, the tags kept, the frame time not the render's, no
sidecar, a version not inheriting, the leaving caption snapped, the monitor ignoring Show Captions, the
flag without captions, the pill's dimming snapped.

### DR-DLV-3 Burn-ins (R-DLV-2)
`render --burnin "tc@bl,srctc@br,clip@tl,source@tr,text=DRAFT@tc"` (`core/service/ServiceRender.cpp:1214`)
names what goes over every frame of that render, each item at one of six places (`tl tc tr bl bc br`):
`tc`, the record timecode, 01:00:00:00 at the timeline's start (the broadcast habit the interchange
formats share, drop-frame and 23.976 alike, `core/service/ServiceBurnIn.cpp:55`); `srctc`, the top
clip's source timecode, i.e. its media's own start plus the source time it shows (`:68`); `clip` and
`source`, the top clip's name and its source's (a nested timeline's name); and `text=…`. Where nothing is
cut, the item reads "—". Sizes come from the frame (3.2 % of its height, a 2.5 % margin) on a translucent
black plate. The core NAMES the text and its place (`core/Overlay.h`). The host DRAWS it
(`Host::drawText`) after the grade and the output transform (`ServiceRender.cpp:1342`), so a burn-in is
never graded. In a PQ or HLG render its white is graphics white, not the peak: BT.2408's 203 cd/m², which
is 0.58 PQ and 0.75 HLG (`ServiceBurnIn.cpp:46`). The drawing is the app's own typeface:
`app/BurnText.cpp:16` uses the app library's CairoTarget and the embedded JetBrains Mono / Roboto,
composited premultiplied into an 8- or 16-bit frame. `linux_main.cpp` and `interstellar-cc` install it;
the CLI links the app library for it (`CMakeLists.txt:22`). A host without it refuses `--burnin` ("this
build cannot draw text over a frame") rather than rendering without the burn-ins. Renders only: the
monitor, a still and the preview cache never carry one, and the render's spec says "· burn-ins".

UI: the Deliver tab's OUTPUT header line carries "No burn-ins" / "n burn-ins"
(`app/widgets/OutputSpec.cpp:442`). It opens a menu of the five, each with its place, the ones on marked
(`app/App.cpp:68`); Text… asks for its words. The render line gains `--burnin` (`OutputSpec.cpp:188`). The
burn-ins belong to one render, like its range, so a saved preset never holds them.

Guarded by:
- L2 `burn-ins…`: frame 0 reads 01:00:00:00 · 01:00:10:00 · shotA · a · DRAFT, and frame 60 reads
  01:00:02:12 · 01:00:11:12 · shotB. Also checked: the places, the size, the frame graded underneath,
  PQ 0.5807 and HLG 0.75, a still carrying none, and a host without text refusing.
- UI `testBurnInsUi`: the menu's five, the count, the render line, Text… and its words, none in a
  preset's flags, off again.
- Shot `deliver_burnins` at both sizes. A chip row was tried first and pushed Render out of reach at
  1440×900, which the Deliver test caught, so the control rides the header line as the preset does.
- E2E: a CLI PNG-sequence render's frame was looked at.

Mutants run red: no burn-in call, no source TC start, no record start, no refusal without text, the
white ignored, a menu item only turning on, the count never shown, and the header pill not placed.

### DR-DLV-2 Render presets (R-DLV-3)
A preset is a render's output spec as the flags `render` takes, less the timeline, the path and the
range, by name. Three are built in (`core/service/ServicePresets.cpp:31`): YouTube 1080p (H.264, a
1920×1080 frame, q18, slow), ProRes HQ master (ProRes HQ at the project's size) and Review H.264 (a
1280×720 frame, q26, fast). `render preset save <name> --format … [spec flags]` keeps one beside the engine
settings (`render-presets`, one `name<TAB>flags` per line, `:77`) — every project has it — refusing a
built-in name or a preset without a format (`:158`); `render preset delete`, `render preset list`.
`render --preset <name>` (`core/service/ServiceRender.cpp:1051`) lays the preset's flags first and each flag
given replaces its value (`ServicePresets.cpp:124`); a preset's `--res` is a frame to fit in — the project's
aspect, inside it, even, never above the project (`:130`). Model: `renderPresets[]` (name, built in,
flags, the spec in words). UI: the Deliver tab's FORMAT line carries "Preset: …" — the list of presets
and Custom — and Save Preset… (`app/widgets/OutputSpec.cpp:352`, `app/App.cpp:56`); with a preset chosen the
render line is `--preset` and the range only (`OutputSpec.cpp:197`), the controls dim (eased) and touching
one returns to Custom (`:174`); Save Preset… asks a name and keeps the controls' flags (`:181`). Guarded by
L2 `render presets…` (YouTube 1080p on a 2880×2160 project renders 1440×1080 at q18 slow; a given quality
wins; ProRes HQ at the project's size; a preset saved, listed, applied — H.265 10-bit q20, a 960×540 frame
on 4:3 = 720×540 — and kept for a new session; the refusals; delete), UI (the list, the chosen preset in
the line, the controls dimming, Custom again on a touch, Save Preset… dispatching the controls' flags),
shot `deliver_preset` (the preset row first moved the Render button out of reach at a small size —
caught by the Deliver test — so it rides the FORMAT header's line). Mutants run red: the given flag not
replacing the preset's, the size taken literally (refused on 4:3), presets not written, a built-in
overwritten.

### DR-DLV-1 Autosave and crash recovery (R-DLV-5, R-DLV-6)
While the project has unsaved changes (`core/service/ServiceSafety.cpp:108`), the first change starts a
clock and every `settings.autosave` seconds (60; 0 = off; 10..3600, `core/service/ServiceEdit.cpp:422`) the
service writes `<stem>.autosave.grades` — every rack node's own params and bypass as Cosmo holds them —
then `<stem>.autosave.isp`, the .isp as it is in memory (`:65`), each beside its name and renamed over it
(`:36`), so a crash mid-write leaves the last autosave whole; `project autosave` writes now. A save removes
them (`core/service/InterstellarService.cpp:1196`), and so does a deliberate `project close` (`:1209`);
opening another project does not. Opening a project checks for an autosave newer than its file
(`checkRecovery`, `ServiceSafety.cpp:90`, `InterstellarService.cpp:1085` — file times compared only with each
other, the file clock's epoch being the library's) and offers it (`recoveryAvailable`, `recoveryTime`).
`project recover` (`:120`) parses the autosave .isp, reads the grades and restores both as an undo
restore does — the .isp, and every grade written THROUGH Cosmo — but taking the autosave's own
proxies, stills and relinked files (`applyState(…, keepMedia=false)`, `:159`); the history clears and the
project is left unsaved. `--discard` removes it. UI: the recovery is offered once per autosave in Cosmo's
confirm dialog — Discard or Recover (`app/App.cpp:1119`); Settings has Autosave Every Minute / Every 5
Minutes / Off, marked (`:310`). Guarded by L2 `autosave…` (no write before the interval, one after; the
grades file holds the edit; a "crash" — a new session — is offered the autosave; recover restores two
grades, a clip move and a still, unsaved; save removes it; discard; a deliberate close leaves nothing; an
autosave older than the file is not offered; the setting's range), UI (the menu, the offer once, its
primary answer), shot `recovery_offer` (a message that overran Cosmo's one-line dialog, shortened).
Mutants run red: no interval, no removal on save, none on close, any autosave offered, the grades not
restored, the current media kept in a recovery.

### DR-CLR-4 The node graph (R-CLR-3)
The rack is the graph: a source's own grade, then each group it is in — a group grades its members'
results, the SERIAL chain. `node serial <node>` (`core/service/ServiceNodes.cpp:28`) wraps the node in a new
group, made by Cosmo as `rack group new` makes one. `node parallel <source>` (`:37`) makes a variant (`rack
duplicate`) marked `#rackobj parallelOf=<source> parallelMix=` and starts it EMPTY — Cosmo's identity grade,
none of the plugins or curves the duplicate copied (`:51`) — so adding it changes nothing. The plan gives
each source its non-empty parallel nodes (`planParallel`, `core/service/ServiceRender.cpp:198`), their params
and mix in the cache and plan keys; the layer grades each from the SAME input and adds its difference from
the input by its mix to the source's result (`:657`; `render::addDifference` `render/Matte.cpp:159`) — Resolve's
parallel mixer — before the groups' partial weights and the source's weight. `<bind>.parallelMix` (0..1)
sets the share (`core/service/InterstellarService.cpp:2029`). `node remove` (`ServiceNodes.cpp:77`) ungroups a
serial node or removes a parallel node; a source is refused. Model: `rack[].parallelOf`,
`rack[].parallelMix`. UI: the Grade deck's third view, NODES (tabs SOURCES · STILLS · NODES with a
travelling underline, the views cross-fading, `app/widgets/GradeDeck.cpp:460`, `:603`), draws the Grade
target's chain left to right — input, source, its parallel nodes in the source's column off its line, a
"+" mixer, its groups, output (`app/widgets/NodeGraph.cpp:21`, `:70`, `:195`), each node at its own eased
place, new ones fading in; a click makes a node the Grade target; a right-click asks for its menu — add a
serial or a parallel node, a parallel node's mix (100/75/50/25 %), remove (`app/App.cpp:764`). Guarded by L2
`the node graph…` (an empty parallel node changes nothing; graded, its difference is added; half at 0.5,
none at 0; refusals; a serial node is a group around the source, graded after it; removal of both kinds;
a source refused), UI (the tab's cross-fade, the nodes' order and places, each kind's menu and its line,
a click selecting, a node added sliding the rest along), shot `grade_node_graph` looked at (a parallel
label measured without its share, then the mixer placed before the wider parallel node — both fixed).
Mutants run red: no parallel planning, no parallel key (a stale frame), a parallel node not starting
empty, the mix ignored, the nodes' slide snapped, the views' cross-fade snapped.

### DR-CLR-3 The stills gallery and the split-screen wipe (R-CLR-4, R-CLR-5)
`still grab [<source>] [--name]` (`core/service/ServiceStills.cpp:141`) keeps what the monitor shows — a
named source's graded reference frame (Grade), else the timeline at the playhead with the grade of the
clip on top there — as `<project>.stills/<id>.png`, and the node's own grade as the open version folds
it, overrides included (`:175`, as `grade copy` takes it) as `<id>.grade` (EditParamsIO text): a snapshot
beside the project, never an authority. The gallery is `#still` nodes in the .isp (id, name, node, at,
file, grade; `model/Schema.h`). `still apply <still> [<node>…]` (`:105`) writes the grade through Cosmo
onto the nodes (else the Grade target), on the root timeline only, as Paste Grade does — one undo step;
`still delete` removes it and its files. Stills are references: undo leaves the gallery as it is
(`core/service/ServiceEdit.cpp:145`). `view wipe [<still|timeline|off>] [--split vertical|horizontal]
[--at 0..1]` (`:59`) splits the MONITOR: `applyWipe` (`:209`) fits the reference into the picture's frame
and paints it past the split (`:238`) — a still's picture (this session's grab, else its file decoded
once, `stillPicture` `:41`), or another version: in Cut its frame at the time shown, in Grade the same
source as that version grades it (`core/service/ServiceRender.cpp:875`, `:888`). Renders, export-still and
capture never wipe. Model: `stills[]`, `wipeRef`, `wipeLabel`, `wipeVertical`, `wipeAt`. UI: the Grade
deck's STILLS chip cross-fades the sources' strip to the stills' (Cosmo's filmstrip, thumbnails through
`AppHooks::stillPicture`; `app/widgets/GradeDeck.cpp:117`, `:441`); a still's menu applies its grade, wipes
against it (or stops), deletes it; a double-click applies it (`app/App.cpp:770`); Colour › Grab Still and
Ctrl+Alt+G grab (`:761`). The monitor draws the divider with a grip and the A / B labels; dragging it is
direct and asks `view wipe --at`; a split the model moves eases (`app/widgets/Monitor.cpp:128`, `:463`).
Guarded by L2 `stills…` (the picture and grade written; apply to b and its one-step undo; a grab from
the playhead takes b's grade at its source time; wipes both ways against a still pixel for pixel, the
split moved alone; export-still unwiped; a wipe against a version in Cut and in Grade; a still grabbed
on a version keeps the version's grade; off; the gallery outside undo; apply refused on a version;
delete removes the files; the .isp carries them and a new session applies them), UI (the gallery's
cross-fade, its menu, Grab Still, the divider shown, dragged, eased, faded), shot `grade_stills_wipe`,
by hand (a real PNG and grade written; applied in a new session). Mutants run red: the split's axis
swapped, no wipe in renderFrame, Grade's version wipe on the open version, undo dropping stills,
overrides missing from a grab, apply on a version, `#still` not parsed (found by the new-session check —
the first draft lost the gallery on reopen), the gallery's fade and the divider's ease snapped.

### DR-CLR-2 Tracking a window (R-CLR-2)
`track window <effect> [--to <source seconds>] [--back]` (`core/service/ServiceTrack.cpp:69`) queues a job
for a window on a SOURCE (a group's is refused: it has no one source to follow). It starts at the
playhead's frame of that source when a clip of it is under the playhead, else at its reference frame
(`:100`), and runs to `--to`, else the source's end (or start). The job (`:132`) decodes the source frame by
frame at an analysis size (a long edge of 320 px at most, Rec.709 luma by area average, `:36`), takes the
patch the window covers (its size, at least 8 px a side) and searches each next frame within a reach
around the last position for the least sum of absolute differences — a candidate abandoned only once
its partial sum is worse than the best (`:205`), the nearest of equals kept — re-takes the patch where it
was found (`:226`) and keys the window's centre there (`:228`, the start keyed too). One frame per pump,
progress in `trackJobs[]`; the whole track is one undo step from the state it began in (`:143`); `track
cancel` stops it, keeping what was keyed. UI: a window's plugin row has Track Forward, Track Backward and,
while one runs, Cancel Tracking (`app/App.cpp:546`); the caption says "tracking N%" (`:1134`). Guarded by L2
`a tracked window…` (a 7×7 square drifting right one pixel and down half a pixel a frame: forward from
source 0 to 1 s, 25 keys, the window on the square within a pixel at frames 0, 7, 20 and 24; undo removes
the whole track and redo restores it; backward from 1 s to 0.5 s on it within a pixel; a direction with
nothing to track stops saying so; a group's window refused), UI (the row's items, the caption, Cancel
Tracking). Found by the test: an early exit that read `sad < best` before summing made every candidate
after a perfect match score 0, so the tie-break kept the window where it was — the mutant restoring it
runs red, as do: the direction ignored, no undo entry, the start from the reference frame not the
playhead, the start not keyed, no reach.

### DR-CLR-1 Qualifiers, windows and the matte view (R-CLR-1, R-CLR-2 windows)
Two plugins in a node's stack grade nothing: they make a KEY — 0..1 per pixel, where the node's
grade reaches (`render/Matte.h`, catalog `render/Effects.cpp:227`). `qualifier.hsl` (`render/Matte.cpp:37`)
keys hue (a centre, a half-width, a softness; 180° = every hue, and a grey has no hue), saturation
((max − min) / max) and Rec.709 luma ranges, each with its softness, inverted on request, on the input's
code values. `window.shape` (`:72`) keys a circle (an ellipse in the picture) or a rectangle by centre,
width and height (shares of the picture), feathered outward over a share of the width (round on any
aspect), inverted on request. A matte's mix fades its limit (`1 − mix·(1 − k)`); several intersect. The
plan splits a chain's mattes from its plugins by owner (`core/service/ServiceRender.cpp:198`, `:486`): a
source's own key limits its whole look — its grade, its groups', its plugins — mixed back to its input
by the key (`:662`, `mixByKey` `render/Matte.cpp:144`); a group's key limits that group's contribution,
computed apart as a partial group weight is (D-7) and read from the picture it grades (`:627`). A
matted layer grades at source size so key, input and grade line up. Effects being animatable, a
window's centre and size animate by keys (R-ANIM). The matte view (`view matte on|off`,
`core/service/InterstellarService.cpp:353`) turns Grade's monitor into the selected source's key as grey
(`ServiceRender.cpp:560`, `:671`; `keyPicture` `render/Matte.cpp:159`), never cached. UI: the plugin menu
lists Qualifier (HSL) and Window; their sliders are the effect panel's; Colour › Show Matte (Shift+H in
Grade, `app/App.cpp:333`, `:920`) and the caption says "matte". Guarded by L2 `qualifiers and windows…`
(the edge source darkened by a grade: a luma key selecting the white, then the black only, mix 0 and 0.5,
inverted; a hue key on a's blue graded at its hue and not at another; a hard window left and right; a
feathered circle rising 188 → 204 → 250 across its edge; the matte view 255 inside and 0 outside; a
window keyed from left to right over a second; a group's window limiting the group's grade and not the
member's), UI (the menu, the toggle both ways, the caption, the plugin menu), shot `grade_window`.
Mutants run red: group keys ignored, source keys ignored, a group's matte taken as the source's, no
feather, no matte view, invert ignored, mix ignored, the hue band ignored.

### DR-MEDIA-3 Relink: offline media in one list, found by hand or by name in a folder (R-MEDIA-3)
`media offline` (`core/service/ServiceMedia.cpp:60`) lists every offline source with its file and why
(missing, or the vendor SDK it needs). `media relink <source> <file>` points one source at its file
where it is now (a folder becomes its CinemaDNG pattern; a still for a video, or the reverse, is
refused); `media relink --search <folder>` indexes the folder once (recursively, `:42`) and relinks every
missing source found there by its file name — a sequence by its folder. A source's file is the `.isp`'s;
Cosmo's slot keeps the path it was added with, so the `.isp` remembers it (`cosmoPath`, `:174`) and binds
the `.cmp` entry by it (`slotFile`, `core/service/InterstellarService.cpp:1270`), and the decoder seam
decodes a stored path from the file the `.isp` names now (`FrameSelector::fileFor`, `core/FrameSelector.h:82`;
`host/VideoFrameDecoder.cpp:43`; set at binding, `InterstellarService.cpp:1360`). Cosmo decodes only on a
load, so the rack is reloaded from its `.cmp` — as old as its last save while a source was offline (D-2)
— and what was only in memory is put back after: every node's own params and bypass, captured first
(`ServiceMedia.cpp:189`) and written THROUGH Cosmo as an undo restore writes them
(`InterstellarService.cpp:1104`). A structural change since the save cannot be put back that way, so the
relink is refused naming it (`ServiceMedia.cpp:164`). A relink is media management, outside undo
(`core/service/ServiceEdit.cpp:141`). UI: File › Relink Media... lists every offline source — "Locate
<name> (<file>)..." each (the host asks for the file by its old name), "Search a Folder for All..." —
and a decoder-missing source with its reason (`app/App.cpp:733`); an offline source's menu has
Relink... (`:197`); the GTK host's file and folder pickers (`linux_main.cpp:520`). Guarded by L2
`relink…` (listed missing; an unsaved edit while offline; refusals; found by search two folders down;
plays graded as before; grade and unsaved edit kept; saved and found in a new session; refused after a
regroup, accepted after ungrouping; an undo of an earlier edit keeps the relink; a CinemaDNG folder
found by name), host (a relinked stored path decodes from its new file, missing unmapped), UI (the
list, Locate, Search, the row's Relink...), shot `relink_media`, and by hand with the real Cosmo (a graded
source moved, listed missing, found by search: its grade and another source's unsaved edit kept, the
exported still identical to before the move, max difference 0). Mutants run red: no restore after the
reload, no structure check, `cosmoPath` not kept, the decoder's map not set, undo dropping the relink,
the sequence search, the seam ignoring the map.

### DR-MEDIA-2 Proxies, offline/online (R-MEDIA-2)
`proxy make [<source>…] [--codec prores|h264] [--edge <px>]` (`core/service/ServiceProxy.cpp:76`) queues
one job per video source (none named: every one without a proxy; a still is refused — it needs none);
a job (`:178`) decodes the original frame by frame, shrinks it to the long edge by area average (never
up, `:37`), drops an odd row or column for the encoder's chroma, and writes ProRes Proxy 10-bit
(default) or H.264 to `<project>.proxies/<bind>_<edge>.mov|mp4`, one frame per pump like a render,
its progress in `proxyJobs[]` every frame. Only when the last frame is written does the source
reference it (`#rackobj proxy= proxyScale=`, `:231`), so a half-made proxy is never shown. `proxy use
on|off` is the project's switch (`proxies = true` in the header, `model/Schema.h:140`). Planning takes
a source's proxy when the switch is on, the source has one and it opens (`proxyFor`,
`core/service/ServiceRender.cpp:197`) — for the timeline, Grade's source frame, the viewer, playback, the
preview cache and capture — and plugins keep their size in ORIGINAL pixels through `proxyScale`
(`srcWidthOf`, `:208`). A render job and export-still plan under `OriginalsOnly` (`:1208`, `:1268`): they
decode the originals whatever the switch. `proxy remove` forgets a proxy (or cancels one being made);
the file stays. Undo restores the `.isp` but keeps the proxy fields as they are
(`core/service/ServiceEdit.cpp:141`): a proxy finished after an edit survives undoing it. Model:
`useProxies`, `proxyJobs[]`, `rack[].proxy`. UI: a video source's menu has Make Proxy / Make Proxy
Again / Remove Proxy / Cancel Proxy (`app/App.cpp:196`); Workspace › Use Proxies (marked while on) and
Make Proxies for All Video (`:291`); the source bin's row says "proxy ·" or "proxy 40% ·"
(`app/widgets/SourceBin.cpp:36`); the monitor's caption says "proxies" (timeline) or "proxy" (Grade,
viewer) (`app/App.cpp:1021`). Guarded by L2 `proxies…` (refusals; two jobs done, 96 frames each, 48×27 →
24×14, ProRes Proxy 10-bit, the original's values; undo of an earlier edit keeps them; the .isp
fields; the monitor reading a marked proxy only while the switch is on; export-still and a render
reading originals and the monitor back on the proxy after; remove; a 4 px blur 6 px soft on the
original and 4 px through the half-size proxy), UI (the rows, the menu items, the switch both ways,
both captions), by hand (a 1280×720 clip: `a_320.mov` ProRes Proxy 320×180 10-bit 4:2:2 48 frames;
capture through it edge energy 1.31 against export-still's 4.2 from the original). Mutants run red:
renders or export-still on a proxy, the switch ignored, plugins sized in proxy pixels, undo dropping
proxies, no shrink, no OriginalsOnly in render jobs.

### DR-MEDIA-1 Camera RAW: CinemaDNG sequences through LibRaw; the vendor formats named (R-MEDIA-1)
A numbered DNG run is one video source named by its pattern, `dir/name_%06d.dng`
(`core/Sequence.h:45`) — FFmpeg's and Nuke's convention, and the source's identity in the .isp, the
Cosmo slot and every cache key; `looksLikeVideo` knows it (`core/FrameSelector.h:52`). `rack add <folder>`
takes the folder's numbered `.dng` run (the longest, `core/Sequence.h:104`) as its pattern; a pattern
with no frame on disk, or a folder with none, is refused (`core/service/InterstellarService.cpp:1418`);
the bind name is the clip's (`:1245`). The host chooses the decoder per moving source
(`host/HostFrameSource.cpp:35`) — for the timeline and for Cosmo's decoder seam alike
(`host/VideoFrameDecoder.cpp:48`), so Grade's reference frame is the timeline's frame. `FrameSourceDng`
(`host/FrameSourceDng.cpp:81`) maps frame i to number first + i, a missing number holding the one before
(`:121`), and develops each with LibRaw (`:111`) at 16 bits, the camera's white balance, its matrix to
Rec.709 primaries, the BT.709 curve and no auto-brightening (`:131`) — one exposure for the clip; the
frame is cached for the deep and 8-bit reads. FrameRate (51044) and TimeCodes (51043) are read from the
first frame's IFD0 (`:14`); without them, 24 fps and no timecode. The reel is the clip name. LibRaw is
the system's or the vendored build Cosmo uses (`host/CMakeLists.txt:30`). ARRIRAW (`.ari`), REDCODE
(`.r3d`) and Blackmagic RAW (`.braw`) are named with their SDKs (`core/Sequence.h:153`): `rack add`
refuses one unless the host reports a decoder for it (`Host::hasVendorDecoder`), naming the SDK and
the way round (transcode); the seam an SDK build fills is `registerVendorDecoder`
(`host/HostFrameSource.cpp:23`); a project naming one whose file is there shows it offline with
`rack[].offlineWhy` (`InterstellarService.cpp:660`), in lint, the source bin and the rack tree. Guarded by
host (a hand-written 12-frame LinearRaw sequence with a dropped frame: pattern from the folder, 64×36,
25 fps and 01:00:10:00 from the tags, 16-bit with fine values, brighter frame by frame, one exposure
(green equal across frames), the dropped frame held and frame k = number first + k, the 8-bit frame
the deep one rounded, Cosmo's seam developing the same frame; vendor refusals and a registered decoder
used), L2 `camera RAW…` (folder → pattern, video, placed without `--out`; refusals; an R3D named by a
project offline with its reason, in lint), shot `cut_vendor_raw`, and by hand: a 320×180 24-frame
CinemaDNG clip added as a folder, exported at 0 and 0.75 s (the moving bar where it was drawn), saved
and reopened with Cosmo holding the pattern as a live slot. Mutants run red: the sequence not a video,
FrameRate ignored, TimeCodes misread, auto-brightening on, the dropped frame showing the next, the
registered decoder ignored, the vendor refusal, the folder not turned to its pattern, `offlineWhy`.

### DR-EDT-4 Multicam: a nested timeline showing one angle, switched with a cut (R-EDT-5)
A clip placing a timeline may carry `angle` (`model/Schema.h:229`, written only when set): it shows
that timeline's k-th video track alone (`core/service/ServiceRender.cpp:297`), so a version can switch
angles as a `#tlset` delta like any clip field (`<clip>.angle`, `core/ParamRegistry.cpp:89`); an angle on
footage, or below 0, is refused on load and on edit (`model/Project.cpp:1356`, `model/Versions.cpp:503`).
`multicam new <name> --sources a,b,…` (`core/service/ServiceEditing.cpp:195`) checks everything first,
then builds a root timeline with one video track per source — angle k is the k-th source — placed by
timecode (each file's first-frame timecode at its own rate, `:240`; a source without one is refused,
pointing at `--sync in`) or by in-points (`--in a=1.5,b=0.4`, the in-points meet, `:242`), and an audio
track with one source's sound (`--audio`, default the first; `none`). `--track` also places it on the
current timeline at `--at` (else the playhead) showing angle 1. `multicam angle <n>` (`:308`) takes the
top-most angle-showing clip under the playhead (`multicamAt`, `:73`; else any placed timeline, or
`--clip`), refuses an angle past the timeline's video tracks (`anglesOf`, `:95`), and from that frame
on shows angle n — splitting the clip there (keys copied, as `clip split` does), or, on its first frame,
changing the whole clip; one undo step. The sound is the multicam's, held across switches, each cut
carrying its window of it — never doubled. Model: `clips[].angle`; `multicamClip`, `multicamAngle`,
`multicamAngles` (each angle named by the source its track shows, `InterstellarService.cpp:823`). UI:
in Cut, over a multicam clip, the monitor's angle bar (`app/widgets/Monitor.cpp:116`, painted `:395`)
— one chip per angle, the active one marked by a highlight that travels 220 ms, the bar fading with its
intent, set every frame since a tab switch changes it (`app/App.cpp:930`); a click or Alt+1…9
(`app/App.cpp:796`) dispatches `multicam angle n`; an angle clip reads "angle n ·" on the timeline.
Guarded by model `clipAngle` (written only when set; a version's switch is a delta; footage and −1
refused; a hand-edited angle on footage refused at load), L2 `a multicam…` (refusals: one source, no
timecode, `--audio` outside the sources, `--in` with timecode; a at 0 and b at 1 s by timecode; the
placed clip 5 s at angle 1; the model's angles a, b; frames (36, 60) before and (36, 200) after a switch
at 2 s; a switch on a first frame changes without a cut; each switch one undo step; the sound 0.1
across the cut and 0 past it; angle 3 refused; by in-points a at 0, b at 0.5 with b's sound), UI (the
bar absent in Grade, fading in in Cut, a click and Alt+3 dispatching, Alt+4 inert, the highlight caught
mid-travel, the bar fading out), shot `cut_multicam`. Mutants run red: the render ignoring the angle,
timecode or in-point sync dropped or reversed, no split on a switch, the sound's offset, the angle
switch not undoable, no angle-count check, the edit invariant, `angle` always written, the bar's
travel and fade snapped, the Alt key.

### DR-EDT-3 A timeline placed as a clip (R-EDT-4)
A `#clip`'s `src` may name a `#timeline` as well as a `#rackobj` (`model/Project.cpp:1348`,
`model/Arrange.cpp:154`, `model/Versions.cpp:476` for `set <clip>.src=`). `nestingRefused`
(`model/Versions.cpp:354`) refuses a placement that would put a timeline inside itself: the clip
lives in the timeline and in every version of it, so what the placed timeline shows — at every depth,
as each resolves — must reach none of them; the refusal names the path (`main → reel`).
`nestingCycles` (`:380`) finds the loops only a hand-edited `.isp` can hold; `lint` names them
(`core/service/InterstellarService.cpp:3117`). The render (`core/service/ServiceRender.cpp:364`) plans
the nested timeline's frame at the clip's local time — speed, ramp and freeze applied — at the outer
frame's size and WITHOUT the view transform (it is a working-space picture; the view applies once,
outside), and the layer composites it (`:526`) where empty is clear, so the tracks below show
through; geometry, opacity, blend and dissolves are the clip's, as for footage. A timeline already
being planned around the frame places nothing (`planFrameIn`'s stack, `:283`), so a loop cannot
recurse. The plan key carries the nested plan's (`:273`), so the preview cache rebuilds exactly the
seconds an edit inside the nested timeline changed. The sound (`core/service/ServiceAudio.cpp:77`):
the nested mix's items cut to the clip's window, moved to its place, at its speed (a ramp's average),
fades cut by the window dropped; its video track's mute silences it, and so does any solo. `clip add
--src <timeline>` without `--out` runs to the timeline's end (`InterstellarService.cpp:2677`);
`timeline delete` refuses a timeline another places (`:2437`); interchange export leaves nested clips
out and says how many (`core/service/ServiceInterchange.cpp:139`). Model: `clips[].nested` (and
`srcName` = the timeline's name), `timelines[].placeable` (`InterstellarService.cpp:717`). UI: the
lane menu's **Place Timeline Here...** opens the placeable timelines in its place (`app/App.cpp:696`);
a nested clip's menu has **Open Timeline**; the clip draws a second edge inside its own and
"timeline ·" before its length (`app/widgets/Timeline.cpp:951`). Guarded by model `nestedTimelines`
(placement; the four loops — itself, back, through an inheriting version, a version of itself;
`set src`; the hand-edited loop named), L2 `a timeline placed as a clip…` (the nested picture equals
the reel's frame byte for byte in Rec.709 and ACEScct; clear where the reel is empty; the clip's speed;
`--out` defaulting; the mix 0.1 / 0.4 / 0.3 / 0.0 at 0.5 / 1.5 / 3.0 / 3.75 s; mute; refusals; an
inner edit rebuilding 2 of 4 cache segments; a hand-edited loop linted and drawn without recursing),
UI (the lane menu offers and lists only placeable timelines; picking one dispatches `clip add`), shot
`cut_nested`. Mutants run red: no nested key, the view transform left on the nested plan, no loop
guard (picture or sound), the sound's place, its mute, no delete refusal, `--out` not the timeline's
length, holders without versions, `set src` unchecked, the cycle finder, lint, the menu's filters.

### DR-EDT-2 A speed ramp: the source frame is the integral of the speed (R-EDT-3)
`<clip>.speed` is animatable (`core/service/ServiceAnim.cpp` — the clip set), keyed on the footage
clock like the clip's other curves: v(s) at source time s, held to 0.1 … 8×. `anim::Ramp`
(`model/Anim.h:210`) tabulates τ(s) = ∫ ds / v(s) from the in-point (trapezoid, ≥ 480 steps a second of
source) and inverts it; the service builds it once per (keys, in, out) (`core/service/ServiceEditing.cpp:36`)
and the plan takes a ramped clip's source time from it instead of in + offset × speed
(`core/service/ServiceRender.cpp:340`) — the rack's, the effects' and the clip's curves all read that
source time too. After every undoable command (`InterstellarService.cpp:247`) a ramped clip's stored speed
becomes the average that makes its length the curve's (`ServiceEditing.cpp:52`), inside the same undo
step, so spans, the timeline's end and a render's frame count need no second notion of length. The key
lane lists Speed among the clip's rows. Guarded by L2 `a speed ramp…` (1× → 3× linear over 2 s of
source: the stored average 1.8205 = 2 / ln 3; frames at 0.5 s and 1 s are source e^t − 1; monotonic
with growing steps; undo/redo), UI (the Speed row keys a ramp). Mutants run red: the plan ignoring
the ramp, no retime after an edit, integrating v instead of 1/v.

### DR-EDT-1 Editing like an editor: J/K/L, marks, a source viewer, three-point Insert and Overwrite (R-EDT-1, R-EDT-2)
The shuttle (`core/service/ServiceEditing.cpp:47`): `shuttle forward` plays at 1× then 2× and 4× on
each press, `back` the same reversed, `stop` pauses; the clock runs at the rate
(`InterstellarService.cpp:430`) and a reverse run stops AT the start; the read-ahead schedules in the
playback's direction at a stride of |rate| (`ServiceRender.cpp:580`) and the monitor takes the nearest
finished frame on the right side of the playhead (`:783`); sound plays at 1× forward only. Marks
(`ServiceEditing.cpp:77`): the timeline's In/Out, or the source viewer's with `--source`, at the
playhead or `--at`. The viewer (`:98`): `source view <node>` shows a rack source with its own playhead
and marks. Insert and Overwrite (`:136`) decide the fourth point from three — the source's In and
Out give the length and the timeline In (else the playhead) places it, or a timeline Out alone
backtimes it; one source mark and both timeline marks give the other; missing source marks are the
source's ends — on the target track (`edit target`, else the lowest video track). The edits are model
operations (`model/Arrange.cpp:354`, `:389`): insert splits a clip spanning the record In and moves
everything at or after it, on every track and every placed sound; overwrite splits at both ends and
removes what lies between; both undo as one step. The playhead lands at the clip's end and the
timeline marks are spent. Model: `shuttle`, `markIn/Out`, `sourceView`, `sourceIn/Out`,
`sourcePlayhead`, `sourceDuration`, `targetTrack`. UI: J/K/L with K held stepping (the GTK host
forwards K's release), I/O, comma/period (OEM key codes — 46 is Delete here), Edit-menu items, the
source bin's double-click opens the viewer (`app/App.cpp:1034`: SOURCE caption, the transport on the
source's clock in the accent — `app/widgets/Transport.cpp:41`), Escape returns; the In/Out band on the
ruler and brackets on the scrubber (`app/widgets/Timeline.cpp:1162`), the target bar on a track header
and its lane-menu item, a shuttle-rate badge — each eased. Guarded by model `threePointEdits`
(split, ripple across tracks and sounds, overwrite across and inside clips, refusals), L2 `J/K/L
shuttles both ways…` (rates, the clock at 2×, a reverse run stopping at 0, insert from the viewer,
overwrite with the fourth point from the timeline's marks, a backtimed edit, the target track,
undo/redo), UI `editing: J/K/L, marks, the source viewer…`. Mutants run red: the clock ignoring the
rate, insert without the ripple, the fourth point not taken from the marks.

### DR-XCH-1 A cut goes out to EDL, FCPXML and OTIO and comes back the same; media keep their timecode and reel (R-XCH-1..5)
`core/Interchange.{h,cpp}` holds one neutral cut (`XTimeline`: media with their source-timecode
start and reel, clips on numbered tracks in media-relative source seconds, dissolves) and a
writer and reader per format, text only: CMX 3600 (`:160`, `:225` — reels ≤ 32 characters, source
and record timecode from 01:00:00:00, a dissolve as the outgoing held at the cut then `D nnn`, `M2`
speed, FROM/TO CLIP comments as OTIO and Resolve write and read them), FCPXML 1.9 (`:480`, `:590` —
assets with `media-rep`, the lowest video track as the spine with gaps, every other track as a
connected clip on its lane in its parent's time, audio below, `Cross Dissolve`, a linear `timeMap`
for speed, `adjust-volume` for gain; a `<clip>`'s own `<video>` is its media), OTIO JSON (`:862`,
`:906` — Clip.1 with an external reference and its available range, Gap.1, Transition.1 with the
held outgoing as out_offset, LinearTimeWarp.1; Clip.2's media_references read too). Small readers
of XML (`:408`) and JSON (`:748`) — no new dependency. Timecode (`core/Timecode.h`, header-only so
the UI can show it) counts drop-frame at 29.97/59.94. The service maps a resolved timeline out
(`core/service/ServiceInterchange.cpp:57`: V1 is the lowest video track, A1 the lowest audio lane;
each file opened for its frames, its timecode and its reel — `host/FrameSourceFFmpeg.cpp:93`, the
reel found on QuickTime's timecode track) and a file in (`:194`): media found as written, else by
clip/reel name under `--media`, picture media added to the rack in one add, an EDL's timecode taken
back off each file's own start (`:266`), then a NEW root timeline with its tracks named after it,
its clips, speeds and dissolves, opened (`:277`); what cannot be found is listed, not placed. AAF
is refused with the way to it (R-XCH-4). UI: File › Import Timeline… / Export Timeline… through
host pickers (`app/App.cpp:234`); the clip inspector's Reel and Source TC (`app/widgets/ClipInspector.cpp:74`),
the column now scrolling (it outgrew 1024×640). Verified with OpenTimelineIO 0.18.1 in a scratch
virtualenv: its cmx_3600, fcpx_xml and otio_json readers read our files to the same clips,
positions, source timecode, the dissolve and the 2× speed (fcpx_xml with an hour's gap — it ignores
`tcStart`); its EDL and FCPXML writers' files import here to the same cut (their own FCPXML writer
drops the speed and the dissolve). Guarded by `interstellar_interchange` (timecode with drop-frame
and a 21-minute round trip; each format's exact lines and a round trip, refusals), L2 `a timeline
goes out as EDL, FCPXML and OTIO and comes back the same…` (reel A001 and 01:00:10:00 in the EDL;
`--start`; three imports equal to the original; --media search), UI `interchange: the File menu…`.
Mutants run red: the import ignoring a file's timecode start, the export ignoring the reel tag, the
FCPXML reader keeping `tcStart` in the record time.

### DR-AUD-3 Playback is heard on the audio clock, scrubs sound, the master is metered, clips show their waveform (R-AUD-6, R-AUD-7, R-AUD-8)
The core names an output seam, `IAudioOut` (`core/AudioOut.h`): stereo float whose blocking `write`
is the clock. The GTK host fills it with PulseAudio's simple API — PipeWire serves it — at a ~60 ms
target buffer (`host/AudioOutPulse.cpp:18`). The service's sound thread (`core/service/ServiceAudio.cpp:175`)
mixes 1024-frame blocks ahead from the playhead and writes them; play starts it with the picture's
clock (`InterstellarService.cpp:418`, after pre-roll; at once without the read-ahead pool), pause and
a seek bump its generation so it flushes what the device still holds (`ServiceAudio.cpp:121`); an
edit while playing replaces its plan, heard after the buffered tens of milliseconds (`:163`). While it
runs the playhead IS the sound: frames written minus the device's latency (`:152`, used at
`InterstellarService.cpp:427`); otherwise the wall clock, as before — no sound server, nothing to
hear, a test. A playhead move while paused sounds an 80 ms grain eased over 4 ms at each end, the
newest grain winning a fast scrub (`ServiceAudio.cpp:136`, `InterstellarService.cpp:3035`). Every
written block keeps its peak and RMS per channel; the model publishes the block being HEARD (written
minus latency), with the clip flag held three seconds (`ServiceAudio.cpp:267`; `soundPlaying`,
`meterPeakL/R`, `meterRmsL/R`, `meterClip`). The transport draws them (`app/widgets/Transport.cpp:50`):
RMS quiet, peak bright, −48…0 dB, the top 3 dB destructive, a held peak, a clip lamp — a meter's
ballistics (an attack eased over ~30 ms, a 24 dB/s fall, the hold 1.5 s then 20 dB/s), the meter
eased in only on a timeline with sound, the scrubber giving way. Waveforms: one file at a time on its
own thread (`ServiceAudio.cpp:333`), the peak of |L|,|R| per 10 ms, written to
`<stem>.peaks/<fnv(path,size,mtime)>.pk` and read back next time; `peaksEpoch` rises as each lands
and `AppHooks::audioPeaks` hands it over (`:322`); `clips[].media` names the file. The timeline draws
the clip's own span of the envelope behind its label, fading in when it lands (`app/widgets/Timeline.cpp:920`).
Guarded by L2 `playback is heard…` (a fake output that takes samples at real-time pace: the
playhead is written minus latency while the wall clock leaps 5 s; an edit while playing reaches the
meters; a pause flushes once; a scrub's grain is 3840 frames, eased; meters read 0.1 and 0.4; the
envelopes cached as two files; no sound server → picture only on the wall clock), UI `sound: meters
and waveforms` (attack eased, release rate, hold, lamp eased, the meter easing away on a silent
timeline, a waveform fading in and changing pixels). Mutants run red: the wall clock instead of the
audio clock, a pause without flush, an unramped grain, edits not reaching the player. Not exercised
here: the PulseAudio class against a real device — the suite never plays sound on this desktop.

### DR-AUD-2 The master mix, in every video render (R-AUD-5 amended, R-AUD-9)
The core names a decode seam, `IAudioSource` (`core/AudioSource.h`): any file read as interleaved
stereo float at the mix rate, frame-addressed. FFmpeg fills it (`host/AudioSourceFFmpeg.cpp`): the
best audio stream decoded, resampled and folded to stereo — a mono file at unity in both ears
(`:59`, libswresample's own mono fold is −3 dB and ignores its option, so the matrix is set) — kept
from the last read on, positioned by decoded timestamps, a seek landing 0.2 s early so a lapped
codec decodes the frame right (`:79`; without it a seek in AAC was off by 0.06). The service turns
a timeline into the mixer's plan (`core/service/ServiceAudio.cpp:21`): every `#aclip` on an audio
lane (an audio-kind `#track` or an `#atrack`) with its gain, fades, its lane's gain, an `#atrack`'s
pan, mute and solo; `audio clip add --src <bind>` places a rack source's own sound and runs to the
end of the file when `--out` is not given (`InterstellarService.cpp:2881`, `:2890`); `#atrack` gain,
pan, mute, solo and name are addresses. `render::mixAudio` (`render/AudioMix.cpp:34`) sums any span,
pure and chunk-independent: a clip owns the frames whose start is inside it (`:46`), balance with
unity at centre (`:52`), linear fades, varispeed by interpolation (`:78`). A render queued with sound
on its timeline gets an audio stream (`ServiceRender.cpp:958`) and each output frame k carries
exactly the samples [k·rate/fps, (k+1)·rate/fps) (`:1129`) — at 29.97 a frame owns 1601.6, and the
total never drifts. The writer adds AAC 320 kb/s beside H.264/H.265 and 24-bit PCM beside
ProRes/DNxHR (`host/FrameWriterFFmpeg.cpp:222`), framed to the encoder (`:350`, `:395`), a short or
padded last frame. Deliver says what the render will carry (`app/widgets/OutputSpec.cpp:189`;
model `timelines[].hasSound`); the queue row names it ("AAC 48 kHz"). Measured through
`interstellar-cc` on real files: a camera file's 440 Hz dialogue at 0.125 and a 1 kHz bed at −6 dB
(0.0626) with its fade, in ProRes's PCM; the stream exactly 2.5 s = 120000 samples for 2.5 s of
timeline. Guarded by render `mix: sample-accurate placement…`, L2 `a render carries the mix…` (the
cut sample-exact, gains, fades, mute, solo, pan, a range, 29.97 exact, PNG silent), host audio
(decoder level/frequency/mono unity/seek exactness; the writer's tone reads back at 0.5000 PCM and
0.5001 AAC, 2.000 s), `interstellar_render_codecs` (aac / pcm_s24le, 48 kHz stereo), UI `deliver:
the sound a render carries`. Mutants run red: per-frame sample counts rounded (drift), mute
ignored, fades ignored, the stream never added.

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
float precision, a speed or influence making its side a bezier, `key clear` leaving the value as the
parameter's own (`writeStatic`). (Until 2026-10-07 a `set` on an animated parameter keyed it at now
and the last key's removal dropped the curve. DR-ANIM-6 replaced both: a `set` writes the own value,
and a curve with no keys stays marked.) R-ANIM-5: curves are the root's (`curveEditable`) — a version
cannot key the rack or an effect, nor a clip it inherits; its `set` is a `#tlgrade` delta against the
parameter's own value (DR-ANIM-6) and renders on top of the animated value. The render path: `gradeFor` /
`gradeForBypassing` replace each tree node's own value with its curve at the layer's source time
before the deltas and the fold (`applyColourCurves`, `:477`; `ServiceRender.cpp:173`, `:276`),
`effectChain` evaluates effect curves, `planFrame` the clip's (`ServiceRender.cpp:310`), so plan keys
— and the frame cache, the ring, the preview cache — follow the animation; Grade's panels show each
parameter's own value (DR-ANIM-6). Pins freeze curves: the commit hashes the .cmp AND the rack's curve
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

### DR-ANIM-6 Marked to animate, offset or fixed; a `set` writes the base (R-ANIM-3 amended a third time, R-ANIM-5 amended, R-ANIM-9, R-ANIM-10)
**The format** (`model/Project.h:307`, `Schema.h:386`; project-format §5.3):
- `#anim … mode=offset` is written only for an offset, so a file without it is fixed.
- Validation refuses (`Project.cpp:1436`, `:1438`, `:1455`) a mode other than fixed or offset, an
  offset on a `#clip`, and an offset with shape keys.
- A curve with no keys is valid: it is a marked property.

**The service** (`core/service/ServiceAnim.cpp`):
- `curveMode` (`:232`): a curve keeps its own mode. A new one takes the default, offset, except a
  shape or a clip's own property, which are fixed.
- `keyRange` (`:240`): a key's value lies in the range (fixed) or ± its span (offset). `upsertKey`
  (`:271`) range-checks a key against it and makes a new curve in that mode.
- `animatedValue` (`:223`): fixed is the curve; offset is `clamp(own + curve)`.
- `key mark` and `key mode` (`:497`; the grammar rows are `Command.cpp:238`, `:244`):
  - a mark makes the curve. A fixed mark keys the own value (or text) at now;
  - a mode change turns every key against the own value (to offset `v − base`, to fixed
    `clamp(base + v)`), so the picture holds;
  - a shape or a clip's property is refused as an offset.
- `key remove` of the last key (`:412`) keeps the curve marked and folds what it showed into the own
  value: fixed takes the key's value, offset takes `clamp(own + key)`.
- `key clear` (`:434`) writes the value at now only when there were keys, then unmarks.
- `key paste` (`:679`): a key copied in one mode and pasted into the other turns through the two
  curves' own values (the clipboard keeps each curve's mode and base, `KeyClip`). A new target curve
  takes the copied one's mode.
- The render path:
  - `applyColourCurves` (`:368`) adds an offset to the own value read from the tree and clamps it to
    Cosmo's range, for both the live and the pinned path;
  - `curveAt` (`:792`), which serves effects, clips and the tracker, returns `fallback + curve` for
    an offset;
  - a pin's `.anim` line gains a trailing `offset` token only for an offset (`:812`), so a fixed
    line is byte-identical to before; `pinCurvesFor` reads it (`:839`).
- `copyAnims` keeps the mode (`:728`). The model's `anims[].mode` and `anims[].base` (`:884`) give
  the graph ± the span for an offset.

**`set` and the version path** (`InterstellarService.cpp`):
- the `setAnimated` hook is gone, so a `set` writes the own value;
- Grade's panels no longer apply curves (`:726`), so a slider shows what `set` writes;
- a version's delta is against the own value (`:2026`);
- `get` of a marked parameter with no keys is its own value (`:2257`, `:2370`), and `eval --explain`
  prints the own value BEFORE the curve with the curve's mode (`:2295`).

**The tracker** (`ServiceTrack.cpp:133`, `keyPath`) keys a new curve fixed, and the path less the
base on an offset centre.

**Tests**:
- Model `animModes`: round trip (fixed not written, `mode=offset` written), the three refusals, a
  keyless offset valid, and D-13.
- L2 `marked to animate`:
  - an offset mark changes no pixel and keys nothing; a key without a value is a zero offset;
  - an offset of +0.5 on base 0.25 renders exactly the still-0.75 frame, and moving the base to 0.5
    renders the still-1.0 frame, keys unchanged;
  - `key mode fixed` keeps the picture with the key turned to 1.0, after which a base change moves
    nothing; back to offset keeps it too; one undo step;
  - a fixed mark keys Grade's 20 and holds it against a later 40;
  - the refusals: a shape, a clip's property, a bad mode, an unmarked parameter;
  - a pin shows base + offset (a pin read as fixed would not);
  - an offset key pasted onto a fixed curve lands as its value;
  - the last key's removal keeps the mark and the picture; clear unmarks; the file says `mode=offset`
    only where it is one.
- L2 `keyframes: …` (rewritten): `set` moves the base (`explain` prints own 0.25, 2 keys, offset,
  effective 1); Grade's slider shows 0.25; on a version it shows 0.35 after a 0.35 drag (+0.1).
- L2 tracking: the tracked curve is fixed and `get` reads the key; a back-track on an offset-marked
  centre stores path − base and `get` reads the path.
- Eleven mutants were red: offset without the base, a mode change not converting, a pin dropping
  the mode, a new curve made fixed, the last key unmarking, paste not converting, the panel applying
  curves, the version delta against the curve, the tracker writing raw onto an offset, the tracker
  starting an offset, and D-13's id taken by reference (the model test).

### DR-ANIM-5 The properties sit under the clip's track — only what is animated (R-ANIM-3, R-ANIM-4, R-ANIM-8, amended 2026-10-07 twice)
This supersedes the lane-at-the-bottom layout of DR-ANIM-2 and DR-ANIM-4. What a row keys, the graph's
edits and the key menu are unchanged.

**Opening.** The selected clip's track opens to its animated properties. The control is a ▸ on that
track's header (`app/widgets/Timeline.cpp:85`, painted at `:1162`, turning to ▾ as it opens), and the
ruler's ◇ still opens it too.

**Layout.** The one lane mapping makes the room. `laneTop` (`:254`) adds `gapAbove` (`:72`): the rows'
live height, times a lane's eased distance past the opened track (clamped to 0…1). Every lane below
moves down by exactly that much, so a lane sliding past the opened one never jumps. `laneAtY` (`:265`)
inverts it, and a point inside the rows counts as the opened track.
- The opened track is eased (`:776`): placed when the rows open, travelling when the chosen clip is on
  another track.
- Only the rows' visible part is a segment. The timeline scrolls them partly out of view, and a segment
  above the ruler would take its clicks, so the rest is the lane's own window offset
  (`KeyLane::setWindow`).
- Opening reveals the track and its rows, re-aimed every frame while it opens (`:820`).

**Only what is animated has a row** (`app/widgets/KeyLane.cpp`).
- The lane knows every property the clip could animate (`:51`): its own six, its source's 34 colour
  keys, each effect's mix and parameters. Each row's presence is eased to 1 when its property is
  animated and to 0 when it is not (`:280`), so a row eases in when it becomes animated and out when
  its curve goes. A different clip's rows are placed, not travelled.
- The first line is **Animate…** (`:440`; click, `:392`) → `App::openAnimateMenu` (`app/App.cpp:733`).
  It lists only what is not animated yet: the clip's own properties at once, then "Grade · Light ›",
  "Grade · Colour ›", "Grade · Presence ›", "Grade · Detail ›", "Grade · Curves & Wheels ›",
  "Grade · Crop ›" and "Effects · <effect> ›" as a second step in the same place. The groups are
  Grade's own panels (`ColourKeys.h:16`), and no list outgrows a 640-px window. Choosing a property
  dispatches `key add <address> --at <now on the clip's footage clock>` and chooses it, so its row eases
  in with its curve open.
- A right-click on a row's name or lane, not on a key (`:385`), gives the row's menu
  (`App::openKeyRowContext`, `:767`): Show/Hide Curve, and **Remove Animation** → `key clear <address>`.
- While nothing is animated, the track opens to Animate… and one line: "Nothing on this clip is
  animated — Animate… keys a property at the playhead".

**Each row.**
- Left: the name and its keying diamond (outline: animated; filled: a key at the playhead) → `key
  add|remove … --at <now>`.
- Right: the keys on the clip's span under the frames they key, joined; a source key outside this
  clip's frames is drawn faded.
- A key dragged along the row → `key set <a> --at t --to t'`, between its neighbours on the millisecond
  grid. A double-click inside the clip → `key add <a> --at t`. A right-click on a key → the key menu.

**Curves inline.** Choosing a row opens its curve in a band under it, eased per row: one opens as the
other closes. Choosing it again closes it, and Ctrl/Shift adds curves to the same band. The chosen row
is re-revealed while the rows and bands move.

**Height.** The rows' own height (Animate… plus one line per animated property), no taller than
`keyLaneHeight` (80…600) nor than leaves the clip's track showing (`Timeline.cpp:57`). It is resized at
its bottom edge (`:65`) and saved.

**Guarded by** UI `testKeyframes` (`app/tests/ui/uiTests.cpp:1231`), 509 checks:
- only Exposure has a row (not Contrast, not the clip's opacity), and the track opens only as far as
  that; the keys under their frames; the curve opening under its row; a row drag to `--at 5 --to`;
- Animate… lists the clip's own and the panels and effects (≤ 20 items, nothing already animated);
  Grade · Light lists Contrast but not Exposure; choosing Contrast keys it at 3 and its row eases in,
  chosen; Animate… › Opacity and › Speed key the clip;
- a double-click keying Contrast at source 4; the diamond keying Exposure at 3; Remove Animation
  dispatching `key clear` and the row easing out; the front row closing its curve;
- the open rows moving to another track's clip, eased; the bottom-edge grip with ten animated
  properties (`:1465`); at 1024×640, the track and its rows in view;
- the earlier graph, menu, multi-curve and shape checks.

Shots: `cut_key_lane_empty`, `cut_key_lane_animate`, `cut_key_lane_animate_grade`, `cut_key_lane`
(Opacity animated, its curve open), `cut_key_lane_rows`, `cut_key_lane_curve_mid`, `cut_key_lane_mid`,
`cut_key_lane_grade` and `cut_key_lane_multi`, at both sizes.

Mutants run red:
- every row shown; a row's presence snapped; Animate… offering what is animated; a panel step that
  opens nothing; Remove Animation dispatching nothing;
- from the first amendment: no gap, the track move snapped, the curve band snapped, the row drag not
  dispatched, the opening not revealed.

### DR-ANIM-2 Animation is authored in the timeline's key lane (R-ANIM-3, R-ANIM-4, amended 2026-10-05)
Grade carries no keyframe control: no diamond on its rows or effect parameters, no curves face in
its deck (both were built and removed the same day at the user's request; cosmo's `SliderRow` and
`ParamPanel` are back to their own code, so cosmo is untouched). Its sliders still edit an animated
value where Grade stands (R-ANIM-3's `set` keying, the service's — editing, not animating).
**The key lane** (`app/widgets/KeyLane.{h,cpp}`) — since 2026-10-07 the rows under the clip's track,
DR-ANIM-5; the layout below is superseded, what each row and the graph do is not — belongs to the SELECTED clip
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
**The lane's size** (R-ANIM-8; resized at its BOTTOM edge since 2026-10-07, DR-ANIM-5): `settings.keyLaneHeight` (80..600, persisted) is the lane's height,
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

### DR-COLOR-4 LUTs in — on a source and in its stack — and out of its grade (R-COLOR-5, R-COLOR-6)
`render/Lut.{h,cpp}` reads Adobe/Resolve `.cube` files (`:114` — 1D or 3D, DOMAIN_MIN/MAX or
Resolve's INPUT_RANGE; a size that disagrees with the rows, a stray word or data before the size is
refused, naming the line), applies 1D linearly and 3D tetrahedrally (`:30` — exact on the lattice,
greys stay grey) to 8-bit or deep frames with a mix (`:77`), and writes them (`:189`). The service
reads each file once per (path, size, mtime) (`core/service/ServiceRender.cpp:131`) and the caches
key on that stamp. In: a source's `<bind>.lut` (`core/service/InterstellarService.cpp:1859`; `none`
clears; saved as `lut=` on the `#rackobj`) applies after its input transform, before Cosmo
(`ServiceRender.cpp:510`); the `lut.cube` effect (`render/Effects.cpp:225`, family Colour) has a
file parameter, `set ef_3.path=<file>` (`InterstellarService.cpp:1909`), which the effect chain
loads and hands to the run (`:2435`) — the render library knows no paths. Out: `lut export <source>
--out <file.cube> [--size 33] [--output …]` (`ServiceRender.cpp:1159`) evaluates the source's colour
on a 16-bit lattice in the .cube's own order — input transform, input LUT, Cosmo's grade as the open
version folds it with the non-per-pixel stages neutralised (`:1195`), the grade weight, its LUT
effects, an optional output transform — and writes the result with a header saying what is in it and
what was left out. Model: `rack[].lut`, `effects[].file`, `effects[].fileKey`. UI: the rack row's
menu gains Input LUT… / Change Input LUT… / Remove Input LUT and Export LUT… (`app/App.cpp:200`)
through host pickers (`linux_main.cpp:197`); a file-taking effect's section ends with a row naming
its file, which picks one (`app/widgets/EffectPanel.cpp:45`, `app/App.cpp:62`). Measured: FFmpeg's
own `lut3d` applying an exported 33-point LUT to the ungraded still of testsrc2 matches Interstellar's
graded still with a mean difference of 0.32 code values, 99.9 % of samples within 3; the worst, 27,
sits where the grade clips a saturated cyan's red to 0 while its green clips at 255 — two kinks in
one lattice cell (65 points: 23). Guarded by render `cube: identity, round trip, tetrahedral…`
(refusals included), L2 `a LUT goes on a source, in its stack, and comes out of its grade` (an
inverting LUT on the source and as an effect; the baked LUT on the plain frame reproduces the grade
— worst 0), UI `LUTs in and out`. Mutants run red: the export skipping the grade, the lattice order
swapped, the input LUT not applied, a tetrahedral case mis-weighted. D-12 found on the way.

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
