# Interstellar — Requirements (intent tier)

The single shared source of truth for **what Interstellar is asked to do, why, and what happened to
each request.** Requirements are numbered `R-<AREA>-<n>`; status lives in the section heading.
**Read this before writing any code, and check every new or changed requirement here for conflict
first** (`arstro.rule` §2).

- This file is the **intent** tier — normative, written *before* the code, never deleted, amended in
  place with `**AMENDED (<what forced it>, <date>)**` and a line of why.
- The **as-built** tier is [`docs/requirements.md`](docs/requirements.md) — `DR-<AREA>-<n>`,
  descriptive present tense, with `file:line` anchors. It is empty today, by construction: there is
  no code. Every implementation commit adds to it.
- Design law for anything a user can see: `.claude/skills/arstro.design.rule`. Process law:
  `.claude/skills/arstro.rule`. Neither is restated here.

**Status legend used in headings:** `📋 SPECIFIED` (agreed, not built) · `🚧 IN PROGRESS` ·
`✅ IMPLEMENTED` · `⚠️ REOPENED (D-n)` · `❌ WITHDRAWN`. Everything is `📋 SPECIFIED` today —
conformance rung **0**.

---

## Global rules (apply to every requirement) — 📋 SPECIFIED

- **R-G-1 The design law binds this app, by reference, not by copy.** Every visible property change
  animates; tokens only; R1–R6 of `arstro.design.rule` hold. **This requirement deliberately
  contains no values**: Cosmo's `R-G-1` predates the shared law and now duplicates it, and a second
  copy in a second app is how a rule drifts into two rules (`arstro.rule` §8). Interstellar's UI
  requirements (`R-UI`) cite the law; they never restate its palette, radii, durations or easings.
- **R-G-2 Cosmo is the reference app, and divergence needs a written reason.** Where Interstellar
  does something Cosmo already does — a filmstrip, a scrollable parameter panel, a modal, a project
  card, a group tree — it does it *the same way*, reusing the same widget class where the widget is
  reusable and the same layout arithmetic where it is not. A deliberate difference is recorded in
  [`docs/design.md`](docs/design.md); an accidental one is a defect.
- **R-G-3 One authority per fact.** No value is stored twice. A source's colour lives only in the
  embedded Cosmo project; a parameter's value at time *t* has exactly one producer (R-EVAL-1); a
  clip's position lives only in the clip node. Where a second reader needs the value it *derives*
  it. (This is the rule Cosmo's gotcha 15 states as "two copies of one fact always drift", promoted
  to a requirement because Interstellar's whole embedding design turns on it.)
- **R-G-4 Nothing is reachable only by clicking.** Every behaviour is a `Command`, observable in
  the `AppModel` or the `Event` stream, and assertable from a shell with no display
  (`arstro.rule` §5). A behaviour the GUI can reach that no command expresses is a defect in the
  command enum.

---

## R-SCOPE — What Interstellar v1 is — 🚧 IN PROGRESS (core, CLI and the UI shell built 2026-09-11)

- **R-SCOPE-1** Interstellar v1 is a **non-linear video editor for colour and cut**: it hosts a live
  Cosmo project as its colour authority (R-COSMO), cuts and composites video and still sources on a
  timeline (R-CUT, R-COMP), drives every parameter with automation clips and expression bindings
  (R-AUTO, R-BIND), plays back through a proxy pipeline (R-PLAY), and renders a deterministic master
  offline (R-RENDER). Its project is a Nebula text document (R-FMT) with version control and
  cross-project embedding (R-VCS).
- **R-SCOPE-2** `interstellar_core` is **UI-free**, in the sense `cosmo_core` is: no Artboard types,
  no window, no codec, no OS path handling. The CLI, the control socket, the test harness and a
  later Artboard front end are four peers over one service (R-SVC).
- **R-SCOPE-3** **The timeline cuts; it does not grade.** A clip node carries no colour parameters
  of any kind. This is a hard structural rule, not a default: a `grade` field on a clip is a
  schema-validation error (R-CUT-2).
- **R-SCOPE-4** Target user: the MV/short-form workflow of [`../../docs/vision.md`](../../docs/vision.md)
  §5 — a director who locks a look on stills and frames, and an editor who cuts to it.
- **R-SCOPE-5** **Audio in v1 is playback of an embedded Solaris mixdown or a plain audio file, and
  nothing more**: no mixing, no effects, no recording. An audio track holds clips, a gain and a mute.
  Sound design is Solaris's job and reaches Interstellar as an embed (R-VCS-5).
- **R-SCOPE-6 Not in v1**, stated so it is not silently attempted: GPU real-time playback at full
  resolution, node-graph compositing, effects beyond colour + composite + transform, motion
  tracking, stabilisation, speed-ramp interpolation beyond frame sampling, subtitles, and
  multi-user editing.
- **R-SCOPE-7** v1 platform is **Linux**, CLI first, GUI second, mirroring Cosmo's build
  (GTK3 + Cairo at the host). Windows follows Cosmo's MSYS2 path when the GUI lands.

---

## R-NFR — Non-functional requirements — 🚧 IN PROGRESS

- **R-NFR-1 An offline render is a pure function of (project, branch, range, output spec).** No
  wall-clock, no unseeded randomness, no dependency on how many times the caller pumped. Grain and
  any other stochastic stage is seeded from `(source id, frame index)`, so the same frame renders
  identically in a scrub, in a playback and in a master. This is what makes golden-frame tests and
  content-hashed commits possible, and it is the same requirement as Solaris's `SR-NFR-1`.
- **R-NFR-2 Per-render configuration, never a global.** Frame size, fps, colour space, proxy level
  and thread budget are supplied per render, so two projects, or a test and a playback, cannot
  collide.
- **R-NFR-3 One CPU budget, owned by one object.** Video decode, colour render and encode all draw
  from a single `ThreadBudget`, divided — never each taking the configured percentage of the
  machine. Cosmo shipped exactly that bug twice (D-11, D-12): "a budget nobody owns is not a
  budget", and Interstellar has three consumers where Cosmo had two. The **embedded Cosmo service
  does not own its own budget**; the host injects Interstellar's (R-COSMO-8).
- **R-NFR-4 Playback is best-effort; correctness is not.** Under load, playback drops frames and
  lowers the proxy level, and **says so in the model** (`playback.dropped`, `playback.level`). It
  never stalls the UI, and it never silently shows a frame that does not match the project.
- **R-NFR-5 Memory is bounded and the bound is readable.** The frame cache and the engine's pixel
  pools are byte-capped, LRU, and publish their resident size and their eviction/re-decode counts
  in the model — the R-MEM/R-CPU-4 lesson: a bound nobody can read back is a claim, not a bound.
- **R-NFR-6 The project text is legible and hand-editable.** One fact per line, canonical number
  formatting, unknown keys preserved, byte-identical round-trip (R-FMT-4).
- **R-NFR-7 Build and test like the rest of the umbrella.** An `apps/interstellar` CMake project
  behind `ARSTRO_BUILD_INTERSTELLAR`, unit + golden tests registered with the root `ctest`, and
  every suite able to report red on every host it runs on (Cosmo's `R-TEST`).

---

## R-SVC — The core is a service; every front end is a view — ✅ IMPLEMENTED (except R-SVC-8/9; R-SVC-10 partial)

Interstellar starts where Cosmo arrived rather than where Cosmo started. Cosmo's `R-SVC` is a
*migration* — its `App.cpp` still holds 105 direct session calls against 7 dispatches, admitted in
its own comments. Interstellar has no such history to unwind, so the service seam is a
precondition of its first commit, not a project.

- **R-SVC-1 `InterstellarService` is the application.** It owns the project, the timeline, the
  parameter graph, the embedded Cosmo service, the render/playback pipeline, the settings and the
  recents. Its whole surface is `dispatch(Command)`, `pump(nowMs)`, `model()` and an `Event` sink.
- **R-SVC-2 One way in: `Command`.** A tagged struct, one kind per behaviour, with a **generated**
  text form — one parser and one formatter shared by the CLI, `--script` files, the control socket
  and the journal. A front end never mutates the timeline, never touches `EditParams`, never opens a
  file.
- **R-SVC-3 One way out: `AppModel` + `Event`.** The model is plain data — no pixels, no Artboard
  types, no easing, no scroll offsets, no transition phases. A frame is referenced by
  `(layer, size, seq)`; the bytes stay in the engine. `revision` increments on every change.
- **R-SVC-4 Presentation is still a real category.** Easing, hover, scroll, zoom, transition phase
  and *which* frame the playhead is animating toward are the view's business. The service says the
  playhead is at 4.271 s and that frame 102 is ready; the view knows how to get there smoothly.
- **R-SVC-5 An `Event` IS the log line.** `formatEvent()` produces the text the CLI streams with
  `--watch`, the journal records, the socket sends and the file log writes. There is no separate
  logging layer, and no quantity is claimed in a requirement that a front end cannot read back.
- **R-SVC-6 An unknown input is REJECTED, naming it.** Every parser reports which inputs it
  consumed; an unmatched key is an error naming the key; the rejection reaches both `lastError` and
  the event stream. Cosmo's D-59 — `set exposre=1.2` returning success — must not be re-invented
  here, and Interstellar's address space (R-PARAM) is far larger, so a typo is far likelier.
- **R-SVC-7 The core touches no OS.** Codecs, sockets, argv, clocks, the filesystem and fonts are
  the host's; they reach the core as injected `std::function` seams (`IFrameSource`, `IFrameWriter`,
  `SourceLoader`, the worker-init hook).
- **R-SVC-8 A control socket, from the first GUI commit.** `interstellar --control <path>` accepts
  the same command grammar, so one front end can drive a window another is watching, and
  `ui dump` is answerable on a live tree.
- **R-SVC-9 The equivalence test is committed and re-runnable.** One committed script runs through
  both front ends — headless, and through a live window over the socket — asserting the event stream
  and diffing the two **stable** state dumps. Cosmo's `tests/acceptance/run.sh` is the reference;
  it found a real defect the first time it ran. Fields excluded from the stable dump are properties
  of *when* the dump was taken (frame sequence numbers, measured ms, chosen proxy level), and the
  exclusion list is part of the requirement: excluding a field to make the test pass is falsifying
  the test.
- **R-SVC-10 A generated, committed, drift-tested API document.** `interstellar-cc api [--json]`
  prints, *from the same tables the parser uses*: every command with its grammar line and accepted
  fields; every event with its formatted fields; every `AppModel` field with its type and whether
  it is excluded from the stable dump; and **the whole parameter address space** (R-PARAM-3) with
  units, ranges and defaults. The generated `docs/api.json` + `docs/API.md` are committed, and a
  test regenerates and diffs them. Nobody in the suite is on this rung yet; Interstellar is asked
  to be first, because an address space of several hundred parameters cannot be learned by reading
  C++.

---

## R-COSMO — The embedded Cosmo project: colour authority, project-in-project — 🚧 IN PROGRESS (the seam and the routing built; the hosted CosmoService is P3)

The defining requirement. Everything in `R-RACK`, and most of `R-PARAM`, is a consequence.

- **R-COSMO-1a The rack seam hands over a whole `EditParams`, not only scalars.** (**Added
  2026-09-12.**) Step 5 of the frame pipeline is `EditEngine` rendering a decoded frame with the
  source's effective parameters, and `EditEngine` takes an `EditParams` — so reading the rack one
  scalar at a time would mean rebuilding that struct field by field in Interstellar, which is the
  second copy R-G-3 forbids. `RackAccess::effectiveParams(node, out)` returns it whole, or false
  for "identity", and **`interstellar_core` therefore links `arstro_image`** — which is portable
  and codec-free, so the core stays free of GTK and the core suite stays displayless. Only
  `cosmo_core` drags a toolkit, and it stays behind the seam in the app layer.
- **R-COSMO-1 An Interstellar project embeds exactly one Cosmo project as its `rack`.** The embed
  is a Nebula `#embed … as=rack` node: `target=cosmo:<projectId>`, a `path` hint to the `.cmp`, a
  `branch`, and an optional pin. It is created either by importing an existing `.cmp`
  (`rack import`) or by creating an empty one with the Interstellar project (`new`).
- **R-COSMO-2 The rack is the ONLY place a source's colour exists.** Interstellar stores no
  `EditParams`, no curve, no mixer, no mask, and no LUT reference of its own. A clip references a
  rack node; the colour of the frame is that node's effective params. (R-G-3.)
- **R-COSMO-3 The embed is LIVE and WRITABLE — the "project in project" requirement.** Interstellar
  hosts a real `cosmo::CosmoService` in-process and dispatches `cosmo::Command`s to it. A colour
  edit made in Interstellar *is* an edit to the Cosmo project: it enters Cosmo's own history, it is
  saved into the `.cmp`, and opening that `.cmp` in Cosmo afterwards shows it. There is no import
  step, no export step and no synchronisation step, because there is nothing to synchronise.
  (**This amends the original brief's `as=look`, which made the Cosmo project a read-only input** —
  README §9.)
- **R-COSMO-4 Cosmo's model is not re-implemented, re-declared or wrapped in a parallel type.**
  (**AMENDED (implementation, 2026-09-11).** This said Interstellar's `AppModel` carries the Cosmo
  `AppModel` **by value, as a member**, so that there could not be a second copy of one fact
  (R-G-3). Carrying it turned out to force `cosmo_core` — and therefore GTK3, GdkPixbuf and LibRaw
  — onto `service/AppModel.h`, which every file in the library and every test includes: the core
  suite would have needed a display-capable host to LINK, for state no view reads.
  **What replaces it:** the rack reaches the model through the `RackAccess` seam
  (`core/RackAccess.h`), and the model carries a **projection** — `RackNodeModel`. R-G-3 is
  satisfied a different way, and the distinction is the point: **these fields are a READ, not a
  copy.** Nothing in them is writable, nothing is cached across a revision, and the authority is
  still the one hosted `CosmoService`. The cost is that a view wanting a Cosmo field the projection
  does not carry must ask for it to be added — the normal price of a projection, and cheaper than a
  library-wide dependency. The seam also made the core suite provable in 0.03 s with a three-line
  fake, which is what R-TEST-2's "lowest level that proves it" asks for.)
- **R-COSMO-5 Where the write lands is explicit.** The embed node carries `writeBranch` (default:
  its own `branch`). Every colour command from Interstellar commits there. A **pinned** embed
  (`branch=main@<commit>`) is **read-only**, and a colour command against a pinned rack is rejected
  with a message saying so — not silently dropped, and not silently un-pinning.
- **R-COSMO-6 Propagation in, conflict surfaced.** When the rack's branch advances outside
  Interstellar (the director regrades in Cosmo), the host project re-resolves the embed and its own
  living branches auto-rebase: clean → the cut's colour updates; conflict → the affected rack nodes
  are flagged, in the model, with the fields that clash, and the editor's cuts are never lost.
  (Nebula §3/§5; scheduled P9.)
- **R-COSMO-7 A video source is a first-class Cosmo node, graded on a reference frame.** This is
  the one thing Cosmo cannot do today and the one cross-app prerequisite Interstellar cannot work
  around:
  - a **video source** appears in Cosmo as an ordinary image node whose pixels are **one extracted
    reference frame**, and whose `imagePath` carries the frame selector (`…/DSC01.MOV#t=4.250`);
  - Cosmo's grouping, stacking, bypass, history, presets, before/after and export all work on it
    **unchanged**, because it is an image slot like any other;
  - the user may change which frame is the reference (a new command in Cosmo), which re-decodes the
    slot and changes nothing about the parameters;
  - **the grade is time-invariant.** A parameter that must change over the shot is automated in
    Interstellar (R-AUTO), never stored in Cosmo. Cosmo has no concept of time and must not grow
    one.
  - Landing this is `arstro.cosmo.core.implement`'s work, in Cosmo's own commit, against Cosmo's own
    requirements; it is listed as a cross-app prerequisite in [`docs/plan.md`](docs/plan.md) §P0.
- **R-COSMO-8 One budget, one settings owner, two services.** The embedded Cosmo service is
  constructed with **Interstellar's** `ThreadBudget`, **Interstellar's** pixel-memory caps and
  **Interstellar's** decoder — it does not read its own settings file, does not size its own thread
  pool and does not open its own log. Two services each converting the user's CPU percentage
  independently is D-11 with a new name, and Interstellar has *four* consumers where Cosmo had two.
  **Mostly already true, checked against the source rather than assumed:**
  `explicit cosmo::CosmoService(ThreadBudget &budget)` already takes the budget **by reference**,
  and `setDecoderFactory` / `setWorkerInit` / `setImageWriter` / `subscribe` / `applySettings`
  already inject the rest. **The one real gap is the pixel caps**, which are set on `RenderService`
  and reachable only through Cosmo's explicitly transitional `session()` accessor — so the
  requirement on Cosmo is narrow: plumb `setMemoryCaps` through `CosmoService`. Interstellar must
  not add a use to that accessor's count.
- **R-COSMO-9 Cosmo's front end is not embedded — only its service.** Interstellar does not host
  Cosmo's `App`, its widgets or its window. Where Interstellar's colour panels look like Cosmo's,
  it is because they *are* Cosmo's widget classes reused as libraries (R-UI-6), driven by
  Interstellar's own view code.
- **R-COSMO-10 A rack node may be a still, and stills are the point.** Behind-the-scenes
  photographs, reference frames, LUT test charts and graded stills sit in the same rack as the video
  sources, in the same groups, and stack the same way. A still that is not referenced by any clip is
  **not** an error: it is reference material, and the model marks it `unused` rather than warning.

---

## R-RACK — Sources, groups, and where colour is authored — 🚧 IN PROGRESS (the seam built; the hosted service is P3)

- **R-RACK-1 The rack is Cosmo's group tree, shown as Cosmo shows it.** Nested groups, images and
  video sources as leaves, per-group offset parameters that compose onto children, per-node bypass,
  per-node branching history. Interstellar adds no tree of its own.
- **R-RACK-2 Grouping is the layering mechanism for colour.** A group's parameters are an offset
  stacked onto every descendant (`composeParams`) — which is what an "adjustment layer" is, so
  Interstellar needs no adjustment track (README §9).
- **R-RACK-3 A source variant is a duplicate rack node on the same file.** When one shot needs two
  different looks, the answer is two rack nodes referencing the same source file with different
  parameters, and two clips. **Not** a per-clip grade override: that would put colour in two places
  (R-G-3). The model must make this cheap — `rack duplicate <node>` copies parameters and history
  root and shares the decoded pixels.
- **R-RACK-4 Every rack node is addressable by a bind name** (R-PARAM-2) and by its Cosmo node id.
  Names are the user's language; ids are everyone else's.
- **R-RACK-6 A rack node's BIND NAME and GRADE WEIGHT are Interstellar's, and live in the
  `.isp`.** (**Added 2026-09-11, forced by implementation.**) Two things the specification assumed
  turned out to need somewhere to live, and the `.cmp` is not it — the rack owns colour, and only
  colour (R-COSMO-2):
  * **the bind name.** Cosmo names a node after its file or after what the user typed for a group,
    so `"Tokyo Night"` and `"DSC01.MOV"` are both normal — and **neither is a legal address**
    (R-PARAM-2). An expression needs `gr1`, so Interstellar derives a legal identifier from
    Cosmo's own name and it is **stable once assigned**, because an expression spells it.
  * **the grade weight** (`<name>.opacity`), which the user's own `bind gr1.opacity = …` example
    addresses: the weight with which this node's own parameter offsets apply to its descendants — a
    continuous `bypass`. Cosmo has no concept of it, and it is composited in step 4.
  Both live on a `#rackobj` node, which is Interstellar's data ABOUT a rack node rather than colour
  data belonging to it. It is the tenth node type and it is why there are ten rather than nine.
  (**AMENDED (real decode, 2026-09-12): `#rackobj` also carries `media` and `frame`.** The timeline
  must decode frame N of a source, so it needs the source's PATH — and Cosmo exposes a slot's path
  only through `EditSession::sourcePathForSlot`, reachable only via the `session()` accessor its own
  ledger is counting down. Interstellar is the party that ADDED the source, so it already knows the
  path; storing it here is both cheaper and the only option that does not grow that count. `frame`
  is the reference-frame time a video source is graded on (R-COSMO-7's selector), carried in the
  same place for the same reason. Neither is colour, so R-COSMO-2 is untouched.)
- **R-RACK-7 `rack add <media...>` registers a source, and works before the rack is hosted.**
  (**Added 2026-09-12.**) A source's identity in Interstellar is its `#rackobj`: a stable id, a
  bind name, a media path and a reference-frame time. Its COLOUR is the hosted Cosmo project's, and
  until that exists (P3) a source's colour is **identity** — the engine drops every stage at its
  default, so an ungraded cut renders the decoded frame unchanged.
  **This is a decoupling, not a shortcut, and the distinction matters:** the address space, the
  timeline, the automation and the render path are all finished and all reach colour through the
  same `RackAccess` seam, so landing P3 fills in the values without changing one address or one
  test. What it must NOT become is a second place colour can live (R-COSMO-2) — `#rackobj` carries
  no parameter but the grade weight, which Cosmo has no concept of.
- **R-RACK-5 An offline or failed source reads as missing, never as a stall.** Cosmo's
  `pending`/`failed` distinction carries through into Interstellar's model, and a clip whose source
  is offline renders as a marked placeholder frame — the project stays openable.

---

## R-CUT — The timeline: cut, and only cut — ✅ IMPLEMENTED

- **R-CUT-1 Timeline node types are `track`, `clip` and `transition`.** A `track` has
  `kind = video | audio`, an `order` (z-order for video, top over bottom), a mute and a lock. A
  `clip` has a source reference, a source range (`in`/`out`), a timeline position (`at`), a
  `speed`, and the composite parameters of R-COMP. A `transition` spans two adjacent clips on one
  track with a `kind` and a duration.
- **R-CUT-2 A clip carries no colour.** No `grade`, no `curve`, no `mixer`, no `lut`, no
  `EditParams` field of any name. The schema validator **rejects** such a field rather than
  ignoring it, because ignoring it would silently discard a user's edit. (R-SCOPE-3.)
- **R-CUT-3 The cut operations are the ordinary ones**, and each is a `Command`: add, delete, trim
  head/tail, roll the edit point, slip, slide, split at the playhead, ripple delete, move to another
  track, set speed, and snap-to (playhead, clip edge, marker). Nothing here needs colour.
- **R-CUT-4 Transitions are time effects, not colour effects.** v1 kinds: `dissolve` (a linear
  opacity crossfade of the two composited layers) and `dip` (to a colour). **`grade-dissolve` is
  withdrawn**: easing one look into another is now an automation clip that spans the cut, which is
  strictly more general and does not need a second mechanism (README §9).
- **R-CUT-4a A transition HOLDS the outgoing clip past its out-point.** (**Added 2026-09-12,
  forced by D-6.**) Two adjacent clips leave nothing to dissolve *from*: the outgoing one ends
  exactly at the cut, so scaling the incoming clip's opacity across the overlap fades it up over
  **black** and the picture goes dark through every dissolve. So for a transition's duration the
  outgoing clip stays live, reading frames past its own `out` — its *handles* — and freezing on the
  source's last frame when there are none; its weight ramps 1 → 0 while the incoming side's ramps
  0 → 1, and **the two always sum to 1**. Both numbers are computed with the active-clip set rather
  than in the compositor, because "is this clip live" and "how much does it contribute" are the same
  question, and answering them in two places is exactly what let a clip be faded that was not there.
- **R-CUT-5 Time is frames, and frames are the authority.** The project has one `fps`; every time
  field is stored as a rational-safe frame index or a decimal second that is exactly representable
  at that fps, and the canonical serialization writes one form. A cut that lands between frames is
  a bug, and comparing two edits must never fail on a rounding difference. (Solaris made beats
  authoritative for the same reason.)
- **R-CUT-6 Speed changes sample, they do not interpolate.** `speed` selects source frames by
  nearest-neighbour in v1; optical-flow retiming is out of scope (R-SCOPE-6). The model states the
  sampling mode so a later mode is an addition, not a change of meaning.
- **R-CUT-7 The timeline is editable with no display.** Every operation in R-CUT-3 is reachable from
  `interstellar-cc`, and a committed script that cuts a sequence and dumps the stable model is the
  regression test for all of them.

---

## R-COMP — Compositing: geometry, opacity, blend — ✅ IMPLEMENTED

- **R-COMP-1 A video track stack composites top over bottom**, each layer being an already-graded
  frame, so a composite is a composite of graded frames and never of raw ones.
- **R-COMP-2 Per-clip composite parameters** — all of them addressable and automatable (R-AUTO-2):
  `geom.x`, `geom.y`, `geom.scale`, `geom.rotation`, `geom.anchor.x/y`, `geom.crop.x/y/w/h`,
  `opacity`, and `blend` (an enum: `normal · multiply · screen · overlay · add · subtract ·
  difference`).
- **R-COMP-3 A clip's `geom.crop` and a source's `xform.crop` are different things, deliberately.**
  The source's crop is its **framing** — one per source, authored in Cosmo, part of the look. The
  clip's crop is a **reframe** of that framed image inside the output raster — per shot, animatable,
  Interstellar's own. The distinction exists because one source may appear in three shots with three
  reframes and only one framing; naming them the same thing is how that becomes impossible.
- **R-COMP-4 Composite parameters may live on a track as well as a clip**, composing onto its clips
  the way a rack group composes onto its children — so "fade the whole layer" is one automation
  clip, not one per cut.
- **R-COMP-5 The output raster is fixed per project** (`width`, `height`, `fps`, colour space, and a
  pixel aspect). A source of a different size is fitted by an explicit, stated policy per clip
  (`fit = contain | cover | stretch | none`) — never by an implicit rule the user cannot see.

---

## R-PARAM — The parameter address space — ✅ IMPLEMENTED

The mechanism that makes automation and binding possible at all. It is the same problem Solaris
calls out as *critical* (`SR-` A3: "a stable, serializable string path for every adjustable
parameter"), and Interstellar has more parameters than any other app in the suite.

- **R-PARAM-1 Every adjustable value has one dotted, stable, serializable address**, of the form
  `<object>[.<filter>].<param>[.<component>]`. Examples, all real:

  ```
  gr1.basic.exposure          a rack group's exposure          (Cosmo EditParams.exposure)
  gr1.opacity                 an object-level parameter        (no filter segment)
  s_day01.mixer.hue.30        the hue curve's value at hue 30  (a component index)
  s_day01.xform.crop.w        the source's framing width
  clp_a.geom.scale            a clip's scale
  clp_a.geom.crop.w           a clip's reframe width
  v1.opacity                  a track's opacity
  ac_push.value               an automation clip's current output
  project.playhead            a read-only project value
  ```
  The user's own example, `bind gr1.opacity = gr1.basic.exposure`, is exactly two addresses in this
  space.
- **R-PARAM-2 Every object has a user-editable, project-unique bind NAME.** Rack groups, rack
  sources, tracks, clips, automation clips, markers and masks. Grammar
  `[A-Za-z_][A-Za-z0-9_]{0,31}`, case-sensitive, reserved words rejected (`project`, `t`, `frame`,
  and every function name in R-BIND-3). Renaming an object **rewrites every expression that
  references it**, atomically, in one command — a rename that breaks a binding is a defect, and a
  rename that leaves a stale name in an expression is worse. The generated default name is derived
  from the object (`gr1`, `s_day01`, `v1`, `clp_a`) and the user is expected to change it.
- **R-PARAM-3 One registry, generated from the same tables the codec uses.** A `ParamRegistry`
  enumerates, for every object type, every address with its **type** (`float · int · bool · enum ·
  curve · colour · point`), **unit**, **min/max**, **default**, **whether it is automatable**,
  **whether it is bindable** and **whether it is read-only**. The registry is what `set`, the API
  document (R-SVC-10), the expression compiler, the automation lane list and the UI panel builder
  all read. It cannot describe a parameter the app does not have, and cannot omit one it does —
  which is the entire reason it is generated rather than hand-written. Cosmo's `--help` proves the
  alternative: names generated and always right, argument hints hand-maintained and missing for 8
  of 30.
- **R-PARAM-4 Cosmo's parameters enter the registry through Cosmo's own naming, not a second
  vocabulary.** The filter segments are Cosmo's panels — `basic`, `detail`, `mixer`, `curve`,
  `grade`, `xform`, `mask.<i>` — and the leaf names are `EditParamsIO`'s keys, which already exist
  and already round-trip. Interstellar-only filters (`geom`, and the object-level `opacity` /
  `blend` / `speed` / `fit`) are added beside them. **A parameter that Cosmo names must not be
  renamed here**: a preset, a `.cmp`, a `set` line and an expression must all spell `exposure` the
  same way.
- **R-PARAM-5 An address that does not resolve is an error naming the address**, with the nearest
  candidates suggested. (R-SVC-6, and the reason it matters more here: `gr1.basic.exposer` is a
  typo a human will make weekly.)
- **R-PARAM-6 Curves and other compound parameters are addressable as a whole and by component.**
  `s1.curve` is the whole tone curve (settable from a text form, as Cosmo already does);
  `s1.curve.y@0.5` reads its value at x = 0.5. Automating a whole curve is **not** supported in v1
  (automate a component, or automate a parameter that a curve is bound to) — stated so the absence
  is a decision, not an oversight.

---

## R-AUTO — Automation clips: the DAW mixer model — ✅ IMPLEMENTED

The user's requirement, in their words: *"just like mixer in a DAW — when we want to animate a param
of a filter of a group we create an automation clip for that; all params can create an automation
clip; multi param can use the same automation."*

- **R-AUTO-1 An automation clip is a NAMED, REUSABLE SHAPE — not a property of a parameter.** An
  `#autoclip` node holds a duration and an ordered list of breakpoints `(t, value, easing)` in its
  **own local time**, with values in its **own unit space** (0..1 by default). It is an object with
  a bind name, it lives in the project's automation pool, and it knows nothing about which
  parameters use it.
- **R-AUTO-1a `auto new --points` is in NORMALISED time; the stored shape is in seconds.**
  (**Added 2026-09-11.** Found by running `eval --explain` while writing the debug skill: a
  `--points 0=0,1=1` shape sampled 0.25 at what looked like its midpoint, because the CLI scales
  each point's `t` by the shape's `dur` while the `.isp` stores local seconds. Neither was wrong,
  but nothing said so, and an undocumented normalisation is a value a user cannot account for.) The
  command's `t` values are fractions of the shape's duration; the file's are seconds. Both must
  round-trip.
- **R-AUTO-2 A link is what applies a clip to a parameter, and a clip may have many links.** An
  `#autolink` node carries: the `autoclip`, the target **address**, a timeline placement (`at`,
  and an optional `dur` that time-scales the shape), a **value mapping** (`from`/`to`, or
  `scale`/`offset`), and a **mode** (`absolute` · `add` · `multiply`). *This is what satisfies "multi
  param can use the same automation"*: one `ac_push` shape links to `gr1.basic.exposure` mapped
  0 → 0.8 EV and to `clp_a.geom.scale` mapped 1.0 → 1.08, at the same time, and moving the shape's
  breakpoints moves both.
- **R-AUTO-3 A lane is a view of the links on one address, and lanes are how the mixer is
  organised.** The mixer view lists objects; expanding an object lists its automated addresses; each
  address is a lane; a lane draws its links as clips in time. This is Ableton-style *automation
  clips*, not a single global envelope per parameter — chosen because it is what the user asked for
  and because a clip can be moved, copied, reused and named, which an envelope cannot.
- **R-AUTO-4 Links on one address may not overlap in time.** Two authorities for one value at one
  instant is R-G-3; the model rejects the overlap and the UI refuses the drop. (`add`/`multiply`
  links are the supported way to stack modulation, and they stack onto the *static* value, not onto
  each other — see R-EVAL-2.)
- **R-AUTO-5 Outside every link, a parameter is its static value** — the value the panel shows and
  the project stores. Entering a link is therefore a potential discontinuity in the rendered image.
  A link carries `fadeIn`/`fadeOut` (default 0, in frames) that ramps from the static value into the
  clip's first value; **and where a link's first value differs from the static value with
  `fadeIn = 0`, the model reports it** — `render --lint` names it and the lane draws the step. A
  hard change the user asked for is legitimate; one they did not notice is a defect they will blame
  on the renderer.
- **R-AUTO-6 Any address the registry marks automatable can be automated, including geometry, crop
  and opacity.** The user named those three explicitly. Non-automatable addresses are exactly those
  whose change is structural rather than continuous — a source reference, a blend *mode*, a track
  kind, a bind name — and the registry says which, so the UI never offers a lane that cannot exist.
- **R-AUTO-7 A link may be scoped to a clip.** `scope=clp_a` places the shape in the clip's local
  time and moves with it when the clip moves — which is how "this exposure ramp belongs to this
  shot" survives a re-cut. Unscoped links are in absolute timeline time.
- **R-AUTO-8 Automation is evaluated, never baked.** The stored project holds shapes and links; the
  value at time *t* is computed on demand. Nothing writes an automated value back into a static
  parameter unless the user explicitly asks (`auto flatten`), and that command says what it
  destroyed.
- **R-AUTO-9 Everything about automation is CLI-reachable**: create a shape, edit a breakpoint, link
  it, map it, scope it, move it, list the lanes, and **print the resolved value of any address at
  any time** (`eval <address> --at <t>`) — the last one is what makes automation testable at all.

---

## R-BIND — Bindings: a parameter driven by a calculation — ✅ IMPLEMENTED

The user's requirement: *"param can be bind to other param with calculation (like genesis) … so that
I can bind `gr1.opacity` to `gr1.basic.exposure`."*

- **R-BIND-1 Any bindable address may be driven by an EXPRESSION over other addresses.** The
  expression is stored as text in the project, compiled once, and evaluated per frame:

  ```
  #bind target=gr1.opacity  expr="clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)"
  #bind target=clp_b.geom.scale expr="1 + ac_push.value * 0.08"
  #bind target=gr2.basic.exposure expr="gr1.basic.exposure - 0.4"
  ```
- **R-BIND-2 The expression language is Gene — the one Genesis already has, promoted to a shared
  library and extended.** Genesis's `genesis::gene` is a parsed-once AST with a constant folder, an
  interpreter, a C++ emitter and dependency collection — which is exactly this problem, already
  solved, already tested. It must be **moved to a shared, UI-free library and aliased from
  `genesis_core`**, not copied: the suite has already paid for one forked file (genesis's palette)
  and `arstrobench` shows the right pattern (alias the namespace, compile the source). Two
  extensions are required:
  - **dotted paths of arbitrary depth.** Gene's `Member` node is exactly two levels
    (`object.field`), and `gr1.basic.exposure` is three. The parser must produce a path node;
    two-level members stay a special case, so Genesis is unaffected.
  - **a time scope.** `t` (timeline seconds), `frame`, `fps`, `dur` and `project.*` become built-in
    identifiers alongside `w`, `h`, `minSide`.
  (**IMPLEMENTED 2026-09-11**, and the promotion needed a third extension nobody predicted: a path
  **segment may start with a digit**, because Interstellar addresses a mask by index and a mixer
  band by hue — `s1.mask.0.adjust.exposure`. And a **two-segment `Member` now falls through to the
  path resolver**, so a consumer whose addresses are uniformly dotted registers one resolver rather
  than two: `gr1.opacity` is as real an address as `gr1.basic.exposure`, and the first run of
  `gene_tests` failed on exactly that. The pre-promotion parser did not merely lack deep paths — it
  ACCEPTED them and silently returned `a.c` for `a.b.c`, which is the one failure worse than an
  error.)
  Gene's `raw{}` C++ escape and its colour type are **not** exposed by Interstellar: there is no
  code generation here, and a colour-valued parameter is bound component-wise.
- **R-BIND-3 The function set is Gene's**, unchanged, so one language does not become two:
  `abs acos asin atan atan2 ceil clamp cos deg dist div exp fade floor hypot lerp log max min mix
  mod pct pow rad remap round sign sin smoothstep snap sqrt step tan turns wrap`.
- **R-BIND-4 The binding graph is a DAG, and a cycle is refused at set time, naming the cycle.**
  Gene's `collectMembers` already exists for exactly this. Evaluation is in topological order.
  `gr1.opacity = f(gr1.basic.exposure)` is legal; `a = f(b)` with `b = g(a)` is refused with both
  addresses printed.
- **R-BIND-5 One authority per parameter, enforced.** A parameter is driven by exactly one of:
  its **static** value, an **automation link** active at *t*, or a **binding**. A parameter with
  both a binding and a link is **rejected at edit time**, naming both. Where a user wants both, the
  binding reads the automation clip's output directly (`ac_push.value`), which is explicit about
  which drives which. (R-EVAL-1.)
- **R-BIND-6 Bindings read OWN values, not stacked ones.** `gr1.basic.exposure` in an expression is
  gr1's own parameter, before the rack's group composition. Reading a stacked/effective value is
  reserved as an explicit suffix (`gr1.basic.exposure.eff`) and is **not** in v1 — because a group
  reading its own descendants' effective values invites a dependency that is a cycle in meaning even
  when it is a DAG in form.
- **R-BIND-7 A binding is inspectable and explainable.** `bind list` prints every binding with its
  target, its expression and its dependencies; `eval <address> --at <t> --explain` prints the
  resolved inputs and the result. An expression whose value a user cannot account for is a support
  problem, and a video editor accumulates hundreds of them.
- **R-BIND-8 An expression that cannot evaluate does not stop the render.** An unresolvable
  reference (a deleted object, an offline source) makes the parameter fall back to its static value,
  reports once per render into the event stream, and marks the binding `broken` in the model. A
  master render that dies on frame 4 800 of 12 000 because of a typo three weeks old is the wrong
  behaviour.

---

## R-EVAL — The per-frame evaluation order — ✅ IMPLEMENTED

This is the contract that makes R-AUTO and R-BIND composable rather than merely coexistent, and it
is normative: two front ends, a scrub and a master render must all produce the same numbers.

- **R-EVAL-1 The value of an address at time *t* has exactly one producer**, chosen in this order,
  and the model refuses configurations where two apply (R-AUTO-4, R-BIND-5):
  1. a **binding** on the address → its expression's value;
  2. an **automation link** covering *t* → the mapped, faded shape value, applied per its mode to
     the static value;
  3. otherwise the **static** value.
- **R-EVAL-2 The order of a single frame is fixed:**
  ```
  1  resolve time            t, frame index, per-clip local time, speed mapping
  2  automation              every link active at t → its address's automated value
  3  bindings                topological order over the DAG; reads see step 2's values
  4  rack composition        per rack node: own params → composeParams up its ancestors  (Cosmo)
  5  per-layer colour        EditEngine renders each layer's source frame with its effective params
  6  per-layer geometry      geom/crop/fit applied to the graded frame
  7  composite               top over bottom, per-clip blend + opacity, transitions resolved
  8  output                  colour-space encode, then the frame cache / writer / display
  ```
  Automation before bindings, and both before composition, is what lets a binding read an automated
  value and a group offset read neither. Any other order produces a different picture, so the order
  is a requirement and a golden-frame test guards it.
- **R-EVAL-3 A parameter's resolved value is observable.** The model exposes the resolved value of
  every address the current frame used, and `eval` prints any address at any time. A number the
  renderer used and nobody can read back is the R-CPU-4 mistake.
- **R-EVAL-4 Evaluation is pure and re-entrant.** No global state, no cached value that survives a
  parameter change, no dependence on evaluation history. This is what R-NFR-1 rests on.

---

## R-PLAY — Playback, proxies and the frame cache — 🚧 IN PROGRESS (decode, cache and scrub built 2026-09-12; continuous playback and read-ahead are not)

- **R-PLAY-1 A host-layer frame-source seam, and no codec in the core.** `IFrameSource` opens a
  media file and answers `frameAt(index)` with straight RGBA8 plus its true size and timebase; the
  host implements it (FFmpeg `libavformat`/`libavcodec`), exactly as Cosmo's `IImageDecoder` wraps
  GdkPixbuf and LibRaw. Decode runs on budgeted worker threads (R-NFR-3), and every decode thread
  runs the worker-init hook — Cosmo's D-12 is a per-thread lesson.
- **R-PLAY-2 A proxy pyramid, reusing the preview machinery that exists.** `RenderService` already
  chooses a pyramid level against a measured interactive budget (Cosmo's R-PREVIEW); playback uses
  the same mechanism with the budget set from the frame interval. The model publishes the level, the
  level's long edge and the measured ms/megapixel, so "it is showing you a coarse frame" is a fact a
  view can state rather than a thing a user has to guess.
- **R-PLAY-3 A byte-capped LRU frame cache**, keyed by `(layer, source frame, resolved-parameter
  hash, level)`. The parameter hash is what makes a cached frame safe: change any parameter that
  feeds that frame and the key changes. Resident bytes and hit rate are in the model (R-NFR-5).
- **R-PLAY-4 Scrub before play.** The first playback capability is a *correct* scrub: move the
  playhead, get that frame, at the best level that fits. Continuous playback is a clock on top of
  that, with read-ahead. This ordering is deliberate — a scrub is testable frame-by-frame and a
  playback is not.
- **R-PLAY-5 Playback never lies and never blocks.** Under load it drops frames and lowers the
  level, reporting both. It does not stall the UI thread, and it does not display a frame produced
  from stale parameters (R-PLAY-3's key is what guarantees the second).
- **R-PLAY-6 Audio playback is a bed, synchronised to the video clock**, decoded and mixed by the
  host from an embedded Solaris mixdown or a plain file (R-SCOPE-5). v1 tolerance: the model
  publishes the measured A/V offset rather than claiming sample accuracy it has not measured.

---

## R-RENDER — Offline render, export and determinism — 🚧 IN PROGRESS (a real encoder built 2026-09-12; no committed golden, no resume)

- **R-RENDER-1 `render` is the product, not a feature.** `render(project, branch, range, spec)`
  walks frames in order, evaluates R-EVAL-2 per frame, and hands each finished frame to a writer.
  It is deterministic (R-NFR-1), resumable at a frame boundary, and reports progress as events with
  a frame count, a rate and an ETA.
- **R-RENDER-2 A host-layer writer seam.** `IFrameWriter` takes RGBA8 frames plus the output spec
  and produces a file; the host implements it (FFmpeg). v1 targets: ProRes 422 and H.264/H.265 in
  MOV/MP4, plus a PNG/TIFF **frame sequence**, which is the format a test can compare byte-for-byte.
- **R-RENDER-3 Golden-frame tests are the primary evidence.** A committed tiny project, rendered to
  a frame sequence, compared against committed goldens within a stated tolerance. Every requirement
  in R-EVAL, R-AUTO, R-BIND and R-COMP is provable this way, with no display and no codec beyond
  PNG.
- **R-RENDER-4 A render must not need the GUI, a display, or the embedded Cosmo's window** — there
  is no such window (R-COSMO-9).
- **R-RENDER-5 Export the still, too.** A single frame at the playhead, at full resolution, through
  Cosmo's own export path, so a frame grab from the cut is byte-identical to the same frame graded
  in Cosmo. That identity is the cheapest possible proof that R-COSMO-2 actually holds.

---

## R-FMT — The `.isp` project format — ✅ IMPLEMENTED

- **R-FMT-1** An Interstellar project is a **Nebula** text document, extension **`.isp`**, header
  `app = interstellar`, sharing the Nebula grammar with `.cmp` and `.slp`: stable ids, `key = value`
  fields, explicit order keys, canonical serialization
  ([shared-core.md §2](../../docs/shared-core.md#2-the-text-project-format)).
- **R-FMT-2** One file holds the whole project: output spec, tracks, clips, transitions, markers, the
  automation pool, the links, the bindings, the rack embed and the audio embed. **Colour is not in
  it** — colour is in the `.cmp` the rack embed names (R-COSMO-2).
- **R-FMT-3** Heavy media is never inline: sources are `res:<hash>` plus a path hint, relinkable, and
  flagged offline rather than fatal.
- **R-FMT-4** Parse → serialize is a **fixed point**, byte-identically, and unknown keys are
  preserved verbatim so a newer file opens in an older build without losing data.
- **R-FMT-5** **Every node has a stable id and a bind name, and neither is ever reused.** A retired
  id stays retired, including any enum value it owned — the R-AISEG lesson: re-pointing a stored
  value at a live meaning turns a thing that does nothing into a thing that does something wrong.
- **R-FMT-6** The full schema — every node type, every field, its type, unit, range and default — is
  [`docs/project-format.md`](docs/project-format.md), and it is what the validator and the generated
  API document are built from.

---

## R-VCS — Branches, merge and cross-project embedding — 📋 SPECIFIED (P9)

- **R-VCS-1** An `.isp` project is a small Nebula repository: commits form a DAG, branches are
  named pointers, a feature branch records its `base`.
- **R-VCS-2 Living branches.** When a base advances, its feature branches semantically rebase:
  clean → the branch silently gains the base's work; conflict → the rebase pauses, flags exactly
  the conflicting nodes and fields, and loses nothing.
- **R-VCS-3 Conflicts are field-scoped.** Two editors changing one clip's `at` and its `opacity` do
  not conflict; two setting its `opacity` differently do. Type-aware resolvers: automation
  breakpoint lists union by time; clip lists reconcile by `at`/`order`.
- **R-VCS-4 Project merge** unions two cuts into one, with a stated policy — `concatenate`,
  `overlay`, or `into <track>` — the "merge two edits into one big video" requirement.
- **R-VCS-5 Two embeds, two roles.** `as=rack` (a Cosmo project — the colour authority, writable,
  R-COSMO) and `as=audio` (a Solaris project — the audio bed, read-only in v1, mixdown or named
  stems). Both may be pinned to freeze a delivery.
- **R-VCS-6 A pin is honoured everywhere or it is not a pin.** A pinned embed is read-only through
  every front end and every command path (R-COSMO-5).

---

## R-CLI — The shell front end — ✅ IMPLEMENTED

- **R-CLI-1** `interstellar-cc` is the whole application without a window: it holds argv, stdout,
  the clock, the codecs and the filesystem, and **no behaviour** (Cosmo's `cli/main.cpp` header is
  the model, including its self-audit).
- **R-CLI-2** `--watch` streams `formatEvent()`; that stream *is* the log (R-SVC-5).
- **R-CLI-3** `run [--script f]` replays command lines; `attach <socket>` drives a running window
  with the same grammar; both accept `wait <condition> --timeout` and `expect <event-prefix>` so a
  script is a test.
- **R-CLI-4** `state print [--json] [--stable]` dumps the model; the stable form is what R-SVC-9
  diffs.
- **R-CLI-5** `api [--json]` prints the generated API document (R-SVC-10), and `eval <address>
  --at <t>` prints a resolved value (R-AUTO-9, R-EVAL-3).
- **R-CLI-6 Exit codes and stream discipline are part of the contract**: `0` success, `2` usage,
  `3` refused (a rejected command, a pinned rack, a cycle), `4` failure; diagnostics on stderr,
  data on stdout, so a pipeline can branch on the answer.

---

## R-UI — The Artboard front end — 🚧 IN PROGRESS (the shell, monitor, timeline, lanes and transport built 2026-09-11)

Full brief: [`docs/ui-brief.md`](docs/ui-brief.md). The law is `arstro.design.rule`; these are the
requirements that are *about Interstellar* rather than about every Arstro front end.

- **R-UI-1 Four workspaces, one project, one shell**: **Grade** (the rack — Cosmo's editor, near
  enough to be the same room), **Cut** (the timeline), **Mix** (the automation lanes and bindings),
  and **Deliver** (render + export). Switching workspace never changes the project, never reloads,
  and never moves the playhead.
- **R-UI-2 The monitor is one widget, shared by every workspace**, showing the composited frame at
  the playhead with the same colour pipeline the master render uses. A frame that looks different in
  two workspaces is a defect.
- **R-UI-3 The timeline draws cuts and nothing else** — clips, transitions, markers, the playhead.
  No colour, no curves, no automation lanes: automation lives in Mix, because putting a lane under a
  clip is what makes people believe the automation belongs to the clip (R-AUTO-1 says it does not).
- **R-UI-4 The Mix workspace is a mixer**: a column per object, its parameters as rows, automation
  lanes in time beside them, and a binding shown *on the parameter it drives*, with its expression
  legible and editable in place.
- **R-UI-5 An expression editor with completion over the registry.** The address space is too large
  to type from memory; completion, inline validation, and the resolved value beside the field are
  requirements, not niceties (R-PARAM-5, R-BIND-7).
- **R-UI-6 Cosmo's widgets are reused as libraries, not re-implemented.** `SliderRow`, `ParamPanel`,
  `MixerPanel`, `CurvePanel`, `GradePanel`, `XformPanel`, `HistogramWidget`, `Filmstrip`,
  `SegmentedControl`, `PillButton`, `IconButton`, `ConfirmDialog` and the `Theme` namespaces are
  compiled into Interstellar the way `arstrobench` already compiles `cosmo/Theme.cpp`. A control
  that turns out to be genuinely general goes to Artboard via `implement_artboard`; a copied widget
  is a divergence with a delay fuse.
- **R-UI-7 Every screen and every state renders headlessly to a PNG, and the set of states includes
  empty, loading and refused.** `interstellar_shots` and `interstellar_ui_tests` exist from the
  first UI commit — not added later — and the shot list includes mid-transition frames, because a
  snap is invisible in a still at rest.
- **R-UI-8 The UI logs what it did.** Under `--debug`, every gesture that lands with the widget that
  consumed it (and loudly when nothing did), every workspace change, every scroll with its clamp,
  every value commit with the address and the value sent to the service. Cosmo's biggest
  observability gap is that its widgets log nothing; Interstellar does not start there.

---

## R-TEST — Evidence — ✅ IMPLEMENTED

- **R-TEST-1** Every suite registers with the root `ctest`. A test that is not registered does not
  exist.
- **R-TEST-2 Pick the lowest level that actually proves it** (`arstro.rule` §4): unit, numeric,
  **the real service headlessly** (usually right), the CLI invocation with its output pasted into
  the commit, randomized/concurrent for anything touching a pool or a lock, and live for anything a
  user will see.
- **R-TEST-3 The new behaviour needs a test that fails without the fix** — verified by running it
  against the unfixed code, not assumed.
- **R-TEST-4 Four suites are load-bearing and exist from the phase that introduces them:** the
  `.isp` round-trip fixed-point test (P1), the golden-frame render test (P2), the resolved-value
  test over automation + bindings (P5/P6), and the GUI/headless equivalence test (P10, R-SVC-9).
- **R-TEST-5 A golden frame is committed as a small PNG with a stated tolerance**, and the tolerance
  is justified in the test's comment. "It looked right" is not evidence and neither is a byte
  comparison whose tolerance nobody chose.
