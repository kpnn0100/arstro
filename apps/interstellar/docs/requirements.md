# Interstellar — Requirements (as-built tier)

**What the code contractually does today**, in the descriptive present, with `file:line` anchors.
Entries are `DR-<AREA>-<n>`, each citing the `R-` tag it implements. This tier keeps no history —
it is rewritten in place to the new truth, and the intent tier
([`../REQUIREMENTS.md`](../REQUIREMENTS.md)) keeps the record of what was asked for.

**An entry with a dead anchor is a defect, not untidiness** (cosmo's D-1).

## Conformance

Rung **3** of `arstro.rule` §5's ladder, as of 2026-09-11: the core is split out with one text
codec (rung 1), every suite is registered with the root `ctest` and the L2 tests drive the real
service headlessly (rung 2), and `interstellar-cc` is the whole application without a window
(rung 3). **Rung 4 is partially met and deliberately not claimed**: `interstellar-cc api [--json]`
is generated from the command table, the event table and the parameter registry
(`InterstellarService.cpp:915`), but it is **not committed to `docs/api.json` and has no drift
test** — so the rung's actual requirement is unmet. Rung 5 needs a control socket (R-SVC-8), which
does not exist.

---

## DR-FMT — the `.isp` project format

- **DR-FMT-1 The document is ten node types plus a header (R-FMT-1/2).** `Project`
  (`core/Project.h:1`) holds `embeds`, `rackObjs`, `tracks`, `clips`, `transitions`, `autoClips`,
  `autoLinks`, `bindings`, `markers` and `settings`. `parse` (`core/Project.cpp:489`) reads them;
  `serialize` (`core/Project.cpp:746`) writes them in the canonical order of
  [`project-format.md`](project-format.md) §2. **No colour field exists anywhere in the struct.**
- **DR-FMT-2 Parse → serialize is a byte-exact fixed point (R-FMT-4).**
  `Project::roundTripsExactly` (`core/Project.cpp:920`) parses, serializes, parses and serializes
  again and compares the two strings. Guarded by `test_isp_roundtrip_is_a_fixed_point`.
- **DR-FMT-3 Unknown keys are preserved verbatim and in place (R-FMT-4).** Every node carries
  `UnknownFields` (`core/Project.h:44`), filled by the `known({...})` lambda in `parse` and
  re-emitted by the `un()` lambda in `serialize`. Guarded by
  `test_unknown_keys_and_comments_survive_a_round_trip`, which round-trips a header key and a node
  key this build does not understand.
- **DR-FMT-4 A colour field on a clip is REFUSED, naming it (R-CUT-2).**
  `Project::fieldIsColour` (`core/Project.cpp:283`) matches 23 spellings and any dotted prefix of
  them; `parse` fails the whole document with `"a clip carries no colour: '<key>' belongs to the
  rack node it references"`. Guarded by `test_a_clip_may_not_carry_colour`.
- **DR-FMT-5 A non-finite number is REPAIRED and counted, never refused.** `num()`
  (`core/Project.cpp:~96`) substitutes the field default and increments the caller's `repaired`
  counter. The two behaviours differ on purpose: a *structural* error refuses, a *numeric*
  corruption repairs and reports (cosmo's D-36). Guarded by `test_a_nan_is_repaired_not_refused`.
- **DR-FMT-6 Numbers are canonical (R-FMT-4).** `canonicalNumber` (`core/Project.cpp:~197`) emits
  the shortest decimal that reads back bit-identically, keeping one `.0` on an integral float;
  `canonicalTime` fixes three decimals. One implementation, because "no change is no diff" rests
  on it.

## DR-CUT — the timeline

- **DR-CUT-1 Eight cut operations, each a `Command` (R-CUT-3/7).** `Timeline`
  (`core/Timeline.cpp:1`) implements `addTrack`, `addClip`, `trim`, `split`, `move`, `roll`, `slip`,
  `remove` (with ripple) and `addTransition`. Trimming the head moves the source in-point **and**
  the timeline position together, so the remaining frames stay where they were. Guarded by
  `test_the_cut_operations`, one script covering all of them.
- **DR-CUT-2 An operation that would empty a clip is refused, not clamped.** `trim` returns
  `"trim would leave no frames"`; `roll` refuses to empty either neighbour. Clamping would silently
  produce a clip the user did not ask for.
- **DR-CUT-3 Cut points are derived, never stored.** `Timeline::cutPoints` unions every clip edge
  and marker, sorts and de-duplicates within 1e-6; `nextCut`/`prevCut` walk it. This is what
  `playhead next-cut` and the view's snapping both read (R-G-3).

## DR-PARAM — the address space

- **DR-PARAM-1 One dotted address space, `<object>[.<filter>].<param>` (R-PARAM-1).**
  `ParamRegistry::resolve` (`core/ParamRegistry.cpp:180`) splits on the first dot, resolves the
  object by **bind name** across five kinds (rack, clip, track, autoclip, and the reserved
  `project`), then matches the suffix against the table. 153 addresses exist in a three-node
  project — asserted by `test_an_address_space_that_a_ui_can_enumerate`.
- **DR-PARAM-2 The table is built, not hand-written (R-PARAM-3).** `build()`
  (`core/ParamRegistry.cpp:46`) emits every entry with its type, unit, min, max, default,
  automatable/bindable/read-only flags and **owner**. `ParamRegistry::all()` is what `set`, `eval`,
  the API document, completion and the lane list all read.
- **DR-PARAM-3 Cosmo's parameters keep Cosmo's names (R-PARAM-4).** The `kRack` table's keys are
  `EditParamsIO`'s own (`exposure`, `mixerSpread`, `sharpenRadius`, …), so a preset, a `.cmp`, a
  `cosmo-cc set` line and an Interstellar expression spell a parameter identically. `crop` is the
  one extension: Cosmo stores it as one four-number key, and the registry addresses it per
  component (`xform.crop.w`) because the address space is per-scalar.
- **DR-PARAM-4 An unresolvable address is refused, naming the nearest candidates (R-PARAM-5).**
  `resolve` computes a capped edit distance over the real object names and over the entries of the
  right object kind, and reports `"no such parameter: 'gr1.basic.exposer' … did you mean
  'gr1.basic.exposure'?"`. Guarded by `test_a_typo_is_refused_naming_the_nearest_candidate`, which
  also asserts the `command.rejected` event fired.
- **DR-PARAM-5 A rename rewrites every reference atomically (R-PARAM-2).** `Project::rename`
  (`core/Project.cpp:427`) retargets automation `target`s and rewrites expression text **on whole
  identifiers only**, so renaming `gr1` leaves `gr10` and `min` alone. Guarded by
  `test_rename_rewrites_every_expression` and `test_a_rename_does_not_corrupt_a_longer_name`.

## DR-AUTO — automation

- **DR-AUTO-1 A shape is a named object; a link applies it (R-AUTO-1/2).** `AutoClip`
  (`core/Project.h:~118`) holds a duration, an interpolation family and breakpoints, and samples
  **analytically** — never from a resampled table, so the same shape gives the same number at any
  frame rate. `AutoLink` holds the placement, the value mapping, the mode, the scope and the fades.
- **DR-AUTO-2 One shape drives many parameters (R-AUTO-2).** `Automation::sample`
  (`core/Automation.cpp:40`) maps the shape value into the target's own range per link, so one
  `#autoclip` linked twice drives an EV and a scale factor at once, and moving one breakpoint moves
  both. **Guarded by `test_one_shape_drives_two_parameters`**, which is the requirement the whole
  automation model exists for.
- **DR-AUTO-3 Links on one address may not overlap (R-AUTO-4).** `Automation::wouldOverlap`
  (`core/Automation.cpp:86`) is checked by `addLink` (`core/Automation.cpp:114`), which refuses with
  `"two authorities for one value"`. Adjacent links are allowed. Guarded by
  `test_overlapping_links_on_one_address_are_refused`.
- **DR-AUTO-4 A non-automatable address is refused with the reason (R-AUTO-6).** The registry's
  `automatable` flag is checked in `dispatch`'s `AutoLinkAdd` case, which names the type and says
  the change is structural rather than continuous. Guarded by
  `test_a_non_automatable_address_is_refused_with_the_reason`.
- **DR-AUTO-5 The boundary step is reported with its size (R-AUTO-5).**
  `Automation::lintBoundaries` (`core/Automation.h:~105`) compares each link's first mapped value
  against the address's static value and reports `"steps by <n> on entry with fadeIn=0"`. A
  non-zero `fadeIn` silences it, and `sample` ramps across the fade. Guarded by
  `test_the_boundary_lint_fires_and_says_how_big_the_step_is`, which asserts the **text**, because
  a warning nobody has seen printed is a warning that does not work.
- **DR-AUTO-6 `auto new --points` is in NORMALISED time.** `--points 0=0,1=1` means "the start and
  the end of the shape": `dispatch`'s `AutoNew` case multiplies each point's `t` by the shape's
  `dur`, while the `.isp` stores local **seconds**. (**Undocumented until it was found by running
  `--explain` — see [`project-format.md`](project-format.md) §8.**)

## DR-BIND — bindings

- **DR-BIND-1 Any bindable address may be driven by a Gene expression (R-BIND-1/2).**
  `BindingGraph::set` (`core/BindingGraph.cpp:45`) parses, constant-folds and collects the
  dependencies with `gene::collectPaths`. **The dependency list is derived, never stored** (R-G-3).
  Guarded by `test_the_user_binding_example`, which asserts
  `clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)` at three exposures including both clamps.
- **DR-BIND-2 A cycle is refused at edit time, naming both ends (R-BIND-4).**
  `BindingGraph::topoSort` (`core/BindingGraph.cpp:95`) reports `"cycle a -> b -> a"`, and `set`
  **restores the previous graph** so a refusal changes nothing. Guarded by
  `test_a_cycle_is_refused_naming_both_ends`.
- **DR-BIND-3 A parameter has one producer (R-BIND-5, R-EVAL-1).** `set` refuses a target that has
  an automation link; `AutoLinkAdd` refuses a target that has a binding; `setAddress`
  (`core/service/InterstellarService.cpp:51`) refuses a direct write to a bound parameter, because
  the write would be overwritten on the next frame. Guarded by
  `test_a_binding_and_a_link_cannot_share_a_parameter` and the last assertion of
  `test_the_user_binding_example`.
- **DR-BIND-4 An expression may read a shape's output (R-BIND-5's escape).**
  `<autoclip>.value` is seeded into the resolved map before the bindings run
  (`core/Evaluator.cpp:~152`), so `1 + ac_push.value * 0.08` works and the precedence is written
  down in the project. Guarded by `test_a_binding_reads_an_automation_shape` and
  `test_the_explain_trace_agrees_with_the_value_it_explains`.
- **DR-BIND-5 Gene is shared, not copied (R-BIND-2).** `core/Gene/` is the promoted library;
  `apps/genesis/core/gene/Gene.h` is a four-line alias shim so `genesis::gene` still resolves at
  its 156 call sites. Two extensions: **dotted paths of any depth** (`NodeKind::Path`, with a
  segment list, and a segment may start with a digit so `s1.mask.0.adjust.exposure` parses) and a
  **time scope** (`t`, `frame`, `fps`, `dur` reach `lookupIdent`, so the library keeps no notion of
  time). A two-segment `Member` falls through to the path resolver, so a consumer whose addresses
  are uniformly dotted registers one resolver. Guarded by `gene_tests` (9 tests).

## DR-EVAL — the frame pipeline

- **DR-EVAL-1 The order is time → automation → bindings → rack composition (R-EVAL-2).**
  `Evaluator::resolve` (`core/Evaluator.cpp:135`) seeds shape outputs, samples every link, then
  evaluates the bindings **in topological order** so a binding reads an automated value. Guarded by
  `test_the_evaluation_order_is_automation_then_bindings`, which asserts a binding sees 0.5 (the
  automated value) rather than 0 (the static one).
- **DR-EVAL-2 `resolve` is pure (R-EVAL-4, R-NFR-1).** It takes `t`, returns a value map, and holds
  no state. Every golden and resolved-value assertion rests on this.
- **DR-EVAL-3 A broken expression falls back to the static value and does not stop the render
  (R-BIND-8).** The `evaluate` failure path in `resolve` substitutes the static value.
- **DR-EVAL-4 The grade weight scales a node's reach (R-PARAM §4.1, the `gr1.opacity` case).**
  `Evaluator::effectiveRackParams` (`core/Evaluator.cpp:252`) walks a rack node's ancestor chain,
  skips bypassed nodes, and adds each ancestor's **offset from the parameter's neutral value**,
  weighted by that node's grade weight. Guarded by `test_grade_weight_scales_a_groups_reach` at
  weights 1, 0.5 and 0.
- **DR-EVAL-5 The frame-cache key hashes every resolved value that fed the layer (R-PLAY-3).**
  `Evaluator::paramHash` (`core/Evaluator.cpp:311`) mixes the source id, the effective rack
  parameters and the clip's own geometry/opacity — not the parameter struct, which would miss a
  change to a shape a link maps in.
- **DR-EVAL-7 A transition holds the outgoing clip and crossfades (R-CUT-4a).**
  `Evaluator::activeAt` computes a `transitionWeight` per clip and keeps the **outgoing** side live
  past its own out-point for the transition's duration, reading its handles and freezing on the
  source's last frame when there are none. The renderer multiplies by that weight and computes
  nothing itself, so every consumer agrees about which clips are live. **This is D-6's fix**: two
  adjacent clips left nothing to dissolve *from*, so the incoming clip faded up over black.
  Guarded by `test_a_transition_holds_the_outgoing_clip_and_crossfades`, which asserts the two
  weights **sum to 1** mid-transition.
- **DR-EVAL-6 Source frames are nearest-neighbour (R-CUT-6).** `Evaluator::activeAt`
  (`core/Evaluator.cpp:108`) computes `floor(localTime * fps)` and the model says so, rather than
  implying an interpolation it does not do. Guarded by `test_active_clips_and_source_frames`.

## DR-COMP — compositing

- **DR-COMP-1 Seven blend modes, on already-graded layers (R-COMP-1/2).**
  `Composite::blendChannel` (`core/Composite.cpp:11`) is pure per-channel arithmetic, asserted on
  known operands by `test_blend_modes_and_geometry`.
- **DR-COMP-2 Geometry is inverse-mapped (R-COMP-2).** `Composite::placeLayer` walks the
  **destination** pixels and maps each back through the crop, fit, scale, rotation and anchor, so
  every output pixel is written once and a rotation leaves no holes. Sampling is nearest-neighbour
  and the doc says so.
- **DR-COMP-3 `fit` is explicit (R-COMP-5).** `contain`/`cover`/`stretch`/`none` are a per-clip
  field, never an implicit rule.

## DR-PLAY — decode, the cache and the scrub

- **DR-PLAY-1 Real decode, in the host layer (R-PLAY-1).** `FrameSourceFFmpeg`
  (`host/FrameSourceFFmpeg.cpp:1`) implements `IFrameSource` over libavformat/libavcodec/libswscale
  and emits straight RGBA8. It is **sequential**: it keeps its decoder position and seeks only
  backwards, or more than `kSeekThreshold` (24) frames forward — decoding forward a short distance
  is nearly free while a seek costs a keyframe search plus the decode from it, so a scrub that
  re-seeked per frame would be a seek storm. A **still image is a one-frame stream**, so stills
  need no second code path. One object per media path, never shared between threads.
- **DR-PLAY-2 An open decoder per media path.** `InterstellarService::sourceFor` keeps an
  `OpenSource` per path and remembers a **failure** rather than retrying, so a missing file is not
  reopened once per frame of a scrub.
- **DR-PLAY-3 A byte-capped LRU frame cache (R-PLAY-3).** `FrameCache` (`core/FrameCache.cpp:1`),
  keyed by `(media, sourceFrame, paramHash, level)`. Capped in **bytes** because a 4K frame is
  33 MB and a 1280-proxy 3.5 MB, so "sixty frames" is not a size. `get` **copies** on a hit: the
  caller composites afterwards and a reference would dangle at the next eviction. Counters
  (`residentBytes`, `entries`, `hits`, `misses`, `evictions`) are published in the model, because
  "the cache works" is a claim that needs a number (R-NFR-5). Guarded by
  `test_the_frame_cache_serves_a_second_visit_and_not_a_stale_one`, which asserts a repeat visit
  decodes **nothing** and that a parameter change decodes **again**.
- **DR-PLAY-4 The proxy edge is the scrub's affordability.** `renderFrame(t, out, proxyEdge)`
  grades every layer at that long edge; `proxyEdge <= 0` is full resolution, which is what an
  export asks for. Measured: 96 frames of two-layer 1280×720 with a dissolve, decoded, composited
  and H.264-encoded in **1.76 s** — about 55 fps, faster than real time.

## DR-RENDER — the master

- **DR-RENDER-1 A real encoder (R-RENDER-2).** `FrameWriterFFmpeg`
  (`host/FrameWriterFFmpeg.cpp:1`) writes **H.264 in MP4/MKV** (`crf 18`, preset `medium`) and
  **ProRes 422 in MOV**, chosen from the output **extension** rather than a flag — a `.mp4` that
  silently held ProRes would be a worse surprise than an unsupported-extension error. The time base
  is `av_d2q(1/fps)`, so 23.976 and 29.97 are exact rather than rounded: frames are the authority
  (R-CUT-5) and a drifting time base would make the file disagree with the project about when a cut
  happens.
- **DR-RENDER-2 `end()` flushes, and that is not optional.** An encoder holds frames back
  (B-frames, lookahead); a file closed without flushing is short by however many it held — a
  truncated render that reads as a rendering bug.
- **DR-RENDER-3 Odd dimensions are refused, not cropped.** Both codecs need even width and height;
  silently changing the raster the user asked for is worse than saying so.
- **DR-RENDER-4 The PPM sequence stays (R-RENDER-3).** The CLI's writer is chosen by extension:
  `.mp4`/`.mov`/`.mkv` get a codec, anything else gets the PPM sequence — which is deliberate
  rather than a fallback, because it is the only output a golden test can compare byte for byte.

## DR-COSMO — the rack

- **DR-COSMO-1 The rack reaches the core through one seam.** `RackAccess`
  (`core/RackAccess.h:1`) is four methods: `nodes`, `getParam`, `setParam`, `isPinned`. **This is
  the R-COSMO-4 amendment as built** — carrying `cosmo::AppModel` by value would have put GTK3 on
  every file in the library and every test; the model carries a projection
  (`service/AppModel.h`'s `RackNodeModel`) instead, which is a read rather than a copy.
- **DR-COSMO-2 A colour write goes through to the rack, and the `.isp` keeps none of it
  (R-COSMO-2/3).** `setAddress` routes by the registry's `owner`; a `Cosmo`-owned address becomes
  `RackAccess::setParam`. Guarded by `test_a_colour_edit_reaches_the_rack`, which asserts the write
  landed **and** that the serialized project contains no `exposure` anywhere.
- **DR-COSMO-3 A pinned rack refuses every colour write (R-COSMO-5).** Checked in the rack
  implementation and surfaced as `"the rack is pinned at <commit> — it is read-only"`. An
  Interstellar-owned rack parameter (the grade weight) stays writable, because it is not colour.
  Guarded by `test_a_pinned_rack_refuses_every_colour_write`.
- **DR-COSMO-1a The seam hands over a whole `EditParams` (R-COSMO-1a).**
  `RackAccess::effectiveParams(node, out)` returns the node's effective parameters whole, or
  **false for identity** — and false is a load-bearing answer rather than a failure:
  `GradeEngine::render` then hands the decoded bytes through untouched. `interstellar_core`
  therefore links `arstro_image`, which is portable and codec-free, so the core stays free of GTK.
- **DR-COSMO-1b Step 5 is `EditEngine`, used as a pure function.** `GradeEngine`
  (`core/GradeEngine.cpp:1`) owns one engine and per frame does `clearImages` → `addImage` →
  `applyParams` → `renderPreview`/`renderFull`. Nothing survives between frames because a video
  frame is different pixels every time, and a slot per frame would leak one per frame.
  **The identity short-circuit is the difference between usable and not**: the engine converts a
  1080p frame to 24.8 MB of linear float on ingest, and at default parameters drops all 17 stages
  and converts back — so an ungraded source (every source until P3) is never handed to it.
  `isIdentity` compares through the parameter **codec** rather than field by field, because a
  hand-written comparison is a second list of what `EditParams` contains and would go stale the
  first time a field was added.
- **DR-COSMO-5 A source is registered by `rack add`, before the rack is hosted (R-RACK-7).**
  Each source gets a `#rackobj` with a stable node id, a bind name derived from the **file name**
  (a filename is not a legal identifier), the media path and a reference-frame time.
  `rack add` is **not refused when nothing could be decoded**: registering a source and being able
  to decode one are different things, so a front end with no codec — or a file that has moved —
  still gets a source it can relink, and the clip reads offline. (The first version did refuse, and
  it broke the UI tests, which have no decoder and need none.)
- **DR-COSMO-6 `--src` names a source by its BIND NAME and stores a node id.** Node ids are
  assigned by the writer and are **not guessable** — the first `rack add` of two files produced
  `cn_1` and `cn_3`, because naming a source consumes an id too. `clip add --src rack:a` resolves
  the name, stores `rack:cn_1` canonically, and **refuses an unknown source, listing what the rack
  has** (R-SVC-6). Guarded by `test_a_clip_src_may_name_a_source_by_its_bind_name`.
- **DR-COSMO-4 A rack node's bind name is Interstellar's (R-RACK-6).** `Project::ensureRackObj`
  (`core/Project.cpp:344`) derives a legal identifier from Cosmo's own name — which may be a
  filename or contain spaces — and it is **stable once assigned**, because an expression spells it.

## DR-SVC — the service

- **DR-SVC-1 `dispatch` / `pump` / `model` is the whole surface (R-SVC-1).**
  `InterstellarService` (`core/service/InterstellarService.h:1`). 46 command kinds, one dispatch
  switch. `pump` never blocks and starts no thread.
- **DR-SVC-2 One codec for the grammar (R-SVC-2/5).** `parseCommand`/`formatCommand`
  (`core/service/Command.cpp:1`) with `commandSpecs()` carrying every name's **argument hint and
  one-line description, generated beside it**. Guarded by `test_command_text_roundtrips` (42 lines,
  each a fixed point) and `test_every_command_kind_has_a_grammar_and_a_hint`.
- **DR-SVC-3 An unmatched input is refused, naming it (R-SVC-6).** `collectFlags` rejects a token
  that is neither a flag nor a value; `set` rejects a token that is not an assignment; a rejection
  reaches **both** `lastError` and the `command.rejected` event.
- **DR-SVC-4 `formatEvent()` is the log line (R-SVC-5).** `core/service/Event.cpp:1`, 20 kinds with
  stable dotted names.
- **DR-SVC-5 The stable dump excludes what is a property of when it was taken (R-SVC-9).**
  `formatModel` (`core/service/AppModelCodec.cpp:1`) omits `revision`, `frameSeq`, `frameWidth/
  Height`, `frameLayers` and `frameMs` under `stable`. Guarded by
  `test_two_services_dump_the_same_state`, which gives two services identical commands and
  wildly different pump counts, asserts the stable dumps are **equal** and the unstable ones
  **differ** — the second half is what proves the exclusion list is doing work.
- **DR-SVC-6 The API document is generated (R-SVC-10, partially).**
  `InterstellarService::apiDocument` (`core/service/InterstellarService.cpp:915`) prints commands,
  events and the whole address space with units, ranges, defaults and flags, from the same tables
  the parser and the registry use. Guarded by
  `test_the_api_document_covers_the_whole_address_space`. **Not yet committed as `docs/api.json`
  and not drift-tested**, so rung 4 is not claimed.
- **DR-SVC-7 The core links `gene_core` and nothing else.** `core/CMakeLists.txt`. No Artboard, no
  GTK, no codec, no `getenv`. The core suite runs in 0.03 s with no display.

## DR-CLI — the shell front end

- **DR-CLI-1 `interstellar-cc` holds no behaviour (R-CLI-1).** `cli/main.cpp` owns argv, stdout and
  a PPM writer. `state print`, `api` and `wait` are handled in the driving loop because the model is
  the service's and the printing is the front end's.
- **DR-CLI-2 The writer is told the frame count up front.** `IFrameWriter::begin` takes `frames`,
  so a sequence numbers **every** frame from `0001`. Before that, the first frame of a sequence
  landed unnumbered and the rest numbered, which a golden comparison cannot use.
- **DR-CLI-3 PPM only, deliberately (R-RENDER-3).** A dozen lines, no image library, and
  byte-comparable — the only output a golden test can assert on. Real codecs are the GUI host's
  FFmpeg job and do not exist yet.

## DR-HOST — the window

- **DR-HOST-1 A real GTK3 window (R-SCOPE-7).** `linux_main.cpp` owns the window, the clock, the
  file dialogs, the fonts and the two codec seams, and holds **no behaviour**: every state change
  is a `Command` and everything drawn comes from the `AppModel`. That is why
  `interstellar_shots` builds the same `App` with no display.
- **DR-HOST-2 The keyboard is the edit surface.** `space` play/pause · `←`/`→` one frame ·
  `↑`/`↓` cut to cut · `S` split every clip under the playhead · `Del` delete the selection ·
  `ctrl+I` import · `ctrl+O` open · `ctrl+S` save · `ctrl+E` export · `1`–`4` workspace ·
  wheel scroll · `ctrl`+wheel zoom. Bare paths on the command line are imported and laid on the
  timeline, so `interstellar a.mp4 b.mp4` is already a cut.
- **DR-HOST-3 Playback derives the playhead from the wall clock**, not by counting frames, so a
  slow frame costs a **dropped frame** rather than a slowed-down edit (R-NFR-4).
- **DR-HOST-4 Repaint follows need.** A 16 ms tick that always redrew would re-render the whole
  window in software Cairo sixty times a second at rest — the core a decode needs. R-G-1 forbids a
  visible change in one frame, not a repaint at rest.
- **DR-HOST-5 The typeface is compiled in (R-FONT-1).** Cosmo's embedded faces, registered
  **before** the `App` is built so the first frame measures text in the app's own font — a layout
  measured in a fallback face reports overflow that does not exist.
- **DR-HOST-6 `[!]` Export blocks the window.** A render is not yet a background job
  (`RenderJob::step` is unbuilt), so `ctrl+E` is synchronous and the window is unresponsive until
  it finishes. It prints the path first so the user can see what it is doing.

## DR-UI — the front end

- **DR-UI-1 Four workspaces, one monitor, one shell (R-UI-1/2).** `App` (`App.cpp:1`) builds
  `WorkspaceBar`, `Monitor`, `Transport`, `TimelineView` and `LaneStack`. **The monitor is outside
  the cross-fading deck**, so its geometry does not change with the workspace — guarded by
  `test_the_monitor_is_one_widget_in_every_workspace`, which also asserts the playhead survives a
  switch.
- **DR-UI-2 `Theme.h` aliases cosmo's token namespaces** and the build compiles
  `cosmo/Theme.cpp` — the `arstrobench` pattern. Interstellar's own surface literals are named
  rungs in `surface::` and `time::`.
- **DR-UI-3 Nothing changes in one frame (R-G-1).** The workspace cross-fade (260 ms), the timeline
  zoom (200 ms), the lane expansion (200 ms), the play/pause glyph (120 ms) and the monitor's frame
  dissolve (160 ms, **`Linear`** — a dissolve eased in time reads as a luminance bump) are all
  eased `Property`s. Every live value is **public** (`App::workspaceFade`,
  `TimelineView::pixelsPerSecond`, `LaneStack::expansion`, `Transport::playFade`) because a private
  eased value is an unverifiable one. Guarded by three tests in `interstellar_ui_tests` that pump
  one frame at a time and keep the **first non-zero** value.
- **DR-UI-4 The two deck views share one time origin.** `time::headerWidth()` is read by both
  `TimelineView::xForTime` and `LaneStack::linkRect`. Before that the lanes' axis started at 0
  while their labels occupied the first 190 px, so **every automation link in the first 190 px was
  culled** — the automation was there and invisible. Found by rendering a shot and looking at it.
- **DR-UI-5 The transport's gutter is measured, not assumed.** `Transport::scrubberRect` reads a
  width measured in `onPaint` from the real timecode string; a magic 132 px put the text under the
  scrubber. Also found by looking.
- **DR-UI-6 Every state is renderable headlessly (R-UI-7).** `interstellar_shots` builds the real
  `App` over a real service and writes 8 named states — including **mid-transition** and a
  deliberately small window — with `--size`, `--script`, `--tree` and `--check` from the first
  commit. `--tree` names its nodes by role, because `child2 opacity=0.000` cannot answer "which
  node is at fault", which is the only question a tree dump exists for.
- **DR-UI-7 Contained and non-overlapping at two sizes (R4).**
  `test_nothing_overlaps_and_nothing_is_clipped_at_the_edge` asserts at 1440×900 and 1024×640 that
  every row is inside the window, that the four rows **tile the height exactly** (siblings snap,
  never stack) and that the monitor keeps a usable height in the small window.
- **DR-UI-8 The lane stack clips AND scrolls (R6).** One `viewport()` rect shared by the measure,
  the placement, the cull and the paint; clamped at both ends; a scrollbar only when there is
  something to scroll; and an unscrollable list **bubbles** the wheel. Guarded by
  `test_the_lane_stack_scrolls_and_clamps_both_ends`.
- **DR-UI-9 A gesture becomes a `Command` (R-G-4).** Every callback in `App`'s constructor builds
  one and dispatches it; the **snapped** value is what the command carries, so the service never
  receives an unsnapped value and re-derives it. Guarded by
  `test_a_scrub_goes_through_a_command_and_moves_the_playhead`.

## DR-TEST — evidence

- **DR-TEST-1 Four suites, all registered with the root `ctest` (R-TEST-1).** `gene` (9),
  `interstellar_core` (29), `interstellar_ui` (8) and `interstellar_shots_headless`. `ctest` reports
  21 suites, 0 failed.
- **DR-TEST-2 The suites keep their assertions in a Release build.** Both undefine `NDEBUG` before
  `<cassert>` (cosmo's D-43). This is not theoretical: `gene_tests` printed `[PASS]` for a genuinely
  failing test on its first run in this repo, and the first thing the un-disabled assertions caught
  was a dangling reference in the test's own fixture.
- **DR-TEST-3 A fake rack is the core suite's leverage.** `FakeRack`
  (`core/tests/coreTests.cpp:~40`) is a map of doubles satisfying `RackAccess`, so the whole
  application is provable with no decode, no GTK and no display.
