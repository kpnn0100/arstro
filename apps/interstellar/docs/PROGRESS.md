# Interstellar — Progress Ledger

**This file is the single source of truth for what is done and what to do next.** It is committed
and travels between computers. A session reads the **NEXT** line below, does **one** task, updates
this file, and commits — in the same commit as the work.

- Rules: `.claude/skills/arstro.rule` (process) · `.claude/skills/arstro.design.rule` (pixels).
- Skills: `.claude/skills/arstro.interstellar.implement` · `.claude/skills/arstro.interstellar.debug`
  (written 2026-09-11; the line here still said they did not exist and was reconciled 2026-09-12).
- Intent: [`../REQUIREMENTS.md`](../REQUIREMENTS.md) · As-built: [`requirements.md`](requirements.md)
  · Defects: [`DEFECTS.md`](DEFECTS.md) · Plan + gates: [`plan.md`](plan.md) ·
  How to work: [`DEVELOPING.md`](DEVELOPING.md)
- Checkbox legend: `[ ]` not started · `[~]` in progress · `[x]` done + verified · `[!]` done but
  UNVERIFIED (say exactly what is unverified and why).

*Last updated: 2026-09-12 — **it edits video.** Real decode, a real window, a real H.264/ProRes
master. `ctest` 21/21.*

---

## NEXT

**► P3 — the hosted `CosmoService` behind `RackAccess`, so colour is real too.**

Video works end to end: `rack add a.mp4` → cut → scrub → `render --out master.mp4`. **What is
missing is colour**, and the seam for it is finished and tested: `RackAccess::effectiveParams`
returns a whole `EditParams` or false for identity, `GradeEngine` renders a frame through
`EditEngine` with it, and every source returns identity today (R-RACK-7). So this is now a
fill-in-the-values job rather than an architecture one.

- [ ] **`RackEmbed`: own a `cosmo::CosmoService` and implement `RackAccess` over it.** Cosmo's
      surface is already the right shape — `explicit CosmoService(ThreadBudget &budget)` takes the
      budget by reference, and `setDecoderFactory` / `setWorkerInit` / `subscribe` /
      `applySettings` / `dispatch` / `pump` / `model` cover the rest. `effectiveParams` maps onto
      `EditSession::effectiveParams(slot)`; `setParam` onto a `Select` + `Set` pair, exactly as
      `cosmo-cc` does, so the two paths are one path.
- [ ] **R-COSMO-7 without changing Cosmo**, which is now possible and was not obvious:
      `CosmoService::setDecoderFactory` is injectable, so **Interstellar can hand Cosmo a decoder
      that extracts a video's reference frame** with the FFmpeg source it already has. Cosmo then
      treats a `.mov` as an ordinary image slot and grows no concept of time — which is exactly
      what the requirement asks for, through a seam that already exists.
- [ ] **The gate:** `rack import japan18.cmp` → `set gr1.basic.exposure=0.2` → `rack save` → open
      the `.cmp` in `cosmo-cc` and **see the value there**. Then R-RENDER-5's byte-identical still.
- [ ] **Then the budget peak** at `cpuPercent=25` across rack decode, colour render and video
      decode — D-11's regression test, and there are now *four* consumers.

**Also open, roughly in order of how much they are missed:**

- [ ] **Continuous playback in the core** (P7). The window plays by driving the playhead from the
      wall clock and dropping frames, which works — but there is no read-ahead, so playback is
      decode-bound and a 4K source will not keep up. `Playback` (bounded read-ahead, level
      selection) is the missing piece.
- [ ] **A background render** (P8). `ctrl+E` blocks the window because `RenderJob::step` is
      unbuilt. One frame per call, then yield.
- [ ] **A committed golden frame** (P2's real gate). `render --format png-seq` is deterministic and
      byte-comparable; nothing compares it yet.
- [ ] **The Grade and Deliver workspaces have no columns.** Cosmo's parameter panels are not reused
      yet — the largest single piece of R-UI-6 outstanding, and it unblocks nothing until P3 gives
      it values to show.
- [ ] `docs/api.json` + `docs/API.md` **generated but not committed and not drift-tested**, so
      rung 4 is still not claimed.

## Phases

Detail, dependencies and gates in [`plan.md`](plan.md). Status only, here.

| phase | what | status |
|---|---|---|
| **P0** | freeze the contracts; the cross-unit prerequisites | `[x]` Gene promoted; the format and grammar frozen in `project-format.md`. Nebula and FFmpeg **deferred by decision**, not done: the `.isp` has its own canonical reader/writer, which is the "stubbed subset" the plan allowed |
| **P1** | project model, `.isp`, `Command`/`Event`/`AppModel`, `interstellar-cc`, the API document | `[x]` 29 core tests. API document generated; **`[!]` not committed or drift-tested** |
| **P2** | frame seams, one deterministic frame, the first golden test, the throughput measurement | `[x]` real FFmpeg decode and encode, a byte-capped frame cache, the per-frame grade. **Measured: 96 frames of two-layer 1280×720 with a dissolve — decode, composite and H.264 — in 1.76 s (~55 fps, faster than real time).** `[!]` still no COMMITTED golden frame |
| **P3** | the Cosmo embed as colour authority — the architectural risk | `[~]` the `RackAccess` seam, the owner-based routing, the pin refusal and the bind-name derivation are built and tested against a fake. **The hosted `CosmoService` is not** — this is **NEXT** |
| **P4** | timeline cut operations + the compositor | `[x]` eight operations, seven blend modes, inverse-mapped geometry, transitions |
| **P5** | the generated parameter registry + automation clips and links | `[x]` 153 addresses in a 3-node project; shapes, links, lanes, the overlap refusal, the boundary lint |
| **P6** | expression bindings | `[x]` Gene promoted and extended; cycles refused with rollback; the user's own example asserted at three exposures |
| **P7** | scrub, then playback: the frame cache, read-ahead, level selection | `[~]` the **cache and the scrub are built** and a repeat visit decodes nothing; the window plays by driving the playhead from the wall clock and dropping frames. **No read-ahead and no level selection**, so playback is decode-bound |
| **P8** | the master render | `[~]` real H.264/MP4 and ProRes/MOV, progress events, and the PPM sequence kept for golden comparison. **No cancel, no resume, and it blocks the caller** — `RenderJob::step` is unbuilt |
| **P9** | Nebula VCS, embed propagation, the audio bed | `[ ]` |
| **P10** | the Artboard UI, and the equivalence test | `[~]` the shell, monitor, timeline, lanes and transport, **plus a real GTK3 window** with import/open/save/export, split, delete, frame and cut navigation, playback and zoom. **No Grade or Deliver columns**, no control socket, so **no equivalence test** |

---

## Documents

| document | state |
|---|---|
| [`../README.md`](../README.md) | **written** 2026-09-08 — amended to the project-in-project / mixer direction; the superseded design kept in §9 |
| [`../REQUIREMENTS.md`](../REQUIREMENTS.md) | **written** 2026-09-08 — 19 areas, all `📋 SPECIFIED` |
| [`requirements.md`](requirements.md) | **written** (empty by construction — rung 0) |
| [`architecture.md`](architecture.md) | **written** — layers, seven seams, workspaces, threading, the full module map |
| [`architecture.puml`](architecture.puml) | **written** — every planned type has a box |
| [`detailed_design.md`](detailed_design.md) | **written** — proposed signatures for 14 core classes and 5 widgets |
| [`design.md`](design.md) | **written** — 5 major decisions with their rejected alternatives, 7 known risks |
| [`project-format.md`](project-format.md) | **written** — 9 node types, the address space, the command grammar, a worked example |
| [`binding.md`](binding.md) | **written** — the three producers, the frame pipeline, the failure modes |
| [`ui-brief.md`](ui-brief.md) | **written** — 4 workspaces, the new widgets, the state matrix |
| [`plan.md`](plan.md) | **written** — P0–P10 with gates, the dependency graph, the open decisions |
| [`DEVELOPING.md`](DEVELOPING.md) | **written** — the doc-authority table, 10 invariants, others' scars |
| [`DEFECTS.md`](DEFECTS.md) | **written** (empty; carries the inherited-scars table) |
| `api.json` / `API.md` | **generated by `interstellar-cc api [--json]`, but NOT committed and NOT drift-tested** — so R-SVC-10 is unmet and rung 4 is not claimed |
| `.claude/skills/arstro.interstellar.implement` | **written** 2026-09-11 — one skill for both halves; every command in it was run before it was written |
| `.claude/skills/arstro.interstellar.debug` | **written** 2026-09-11 — reproduce/judge/file/recommend, with the four `eval --explain` outcomes and the six recurring judgements |

---

## Decisions log (newest first)

**2026-09-12 — it edits video, and four decisions made that possible.**

1. **`rack add` decoupled from the hosted Cosmo project** (R-RACK-7). A source's identity is its
   `#rackobj` — id, bind name, **media path**, reference frame — and its colour is the rack's,
   which is identity until P3. That turned "we cannot edit video until the colour authority is
   hosted" into "we can edit video now and colour fills in behind the same seam". `#rackobj`
   carries the media path because **Interstellar is the party that added the source**; Cosmo
   exposes a slot's path only through the `session()` accessor its own ledger is counting down.
2. **The seam hands over a whole `EditParams`, so the core links `arstro_image`**
   (R-COSMO-1a). Reading the rack one scalar at a time would mean rebuilding the struct in
   Interstellar — the second copy R-G-3 forbids. `arstro_image` is portable and codec-free, so the
   core is still displayless and the core suite still runs in milliseconds; only `cosmo_core` drags
   a toolkit and it stays behind the seam.
3. **The identity short-circuit is what makes a scrub affordable.** `EditEngine` converts a 1080p
   frame to 24.8 MB of linear float on ingest and, at default parameters, drops all 17 stages and
   converts back — to produce its input. So an ungraded source is never handed to it. Identity is
   compared through the parameter **codec**, not field by field: a hand-written comparison is a
   second list of what `EditParams` contains and would go stale the first time a field was added.
4. **`--src` takes a bind name.** Node ids are assigned by the writer and are **not guessable** —
   the first `rack add` of two files produced `cn_1` and `cn_3`, because naming a source consumes
   an id too. This was found by writing a script that guessed `cn_2` and getting an offline clip
   three commands later; the fix stores the id canonically and refuses an unknown source *at the
   command*.

**And the process note worth carrying forward:** **three of this session's defects were found by
looking at a picture, and the fourth by `ctest`.** D-6 (a dissolve fading up over black) was
invisible in every number — the frame count was right, the source frames were right, the weights
were right *for the clips that were live* — and obvious the moment a rendered frame was extracted
and viewed. The burned-in frame counters in the test media are what made it checkable: `A 48`,
a genuine blend, `B 35`, `B 36`.

**2026-09-11 — the first implementation, and the five things it changed about the specification.**

1. **`RackAccess` replaced "carry `cosmo::AppModel` by value".** R-COSMO-4 amended in place. Carrying
   it forced GTK3 onto every file in the library and every test, for state no view reads. The seam
   is narrower, the model carries a projection (a read, not a copy), and the core suite runs in
   **0.03 s** against a three-line fake — which is what made 29 L2 tests affordable at all.
2. **A tenth node type, `#rackobj`** (R-RACK-6, new). Bind names and the grade weight needed
   somewhere to live and the `.cmp` is not it. The forcing detail nobody predicted: **Cosmo's node
   names are not legal addresses** — `"Tokyo Night"` has a space and `"DSC01.MOV"` has a dot — so an
   expression could never have spelled one.
3. **Gene needed a third extension.** Beyond deep paths and a time scope: a path segment may start
   with a **digit** (`s1.mask.0.adjust.exposure`), and a two-segment `Member` **falls through to the
   path resolver** so a consumer with uniformly dotted addresses registers one resolver. The first
   run of `gene_tests` failed on the second of those. And the pre-promotion parser did not merely
   lack deep paths — it accepted them and silently returned `a.c` for `a.b.c`.
4. **One `time::headerWidth()` shared by both deck views.** Found by rendering a shot and looking at
   it: the lanes' time axis started at 0 while their labels occupied the first 190 px, so **every
   automation link in the first 190 px was culled**. The automation was there and invisible. Fixing
   it also gave the timeline a track-header column, which is what an NLE has anyway.
5. **`IFrameWriter::begin` learned the frame count.** A sequence writer cannot decide its naming
   after the first write: frame 1 landed unnumbered and the rest numbered, which a golden comparison
   cannot use.

**And two process notes worth carrying forward:**

* **`NDEBUG` disabled the assertions and a suite reported green while genuinely failing.** Cosmo's
  D-43, hit again here on `gene_tests`' first run. Both new suites now undefine it before
  `<cassert>`. The first thing the un-disabled assertions caught was a dangling reference in the
  test's own fixture — so the lesson is not only "Release breaks asserts", it is **"a suite that
  cannot report red is not evidence", including about itself**.
* **`eval --explain` printed a number that contradicted its own result.** `ac_push.value` was
  resolved by a special case inside the Gene scope, so the value the expression used and the value
  the trace printed came from two different places. One authority for one number (R-G-3): the shape
  output is seeded into the resolved map before the bindings run, and the trace reads what the
  evaluation read. Guarded by `test_the_explain_trace_agrees_with_the_value_it_explains`, checked to
  fail without the fix.

**2026-09-08 — the specification, and the six decisions inside it.**

1. **Interstellar HOSTS a `cosmo::CosmoService` as its colour authority.** Not a shared
   `EditParams`, not a read-only `as=look` embed, not a second process. This is what makes "project
   in project" structural and what makes R-RENDER-5's byte-identical still true by construction
   rather than by re-establishment. Rejected alternatives and their costs:
   [`design.md`](design.md) §1.
2. **Colour comes off the timeline.** A clip references a rack node and carries no colour field of
   any name — and such a field is a validation **error**, not an ignored key, because ignoring it
   would silently discard a user's edit. A shot that needs a different look duplicates the rack
   node (R-RACK-3). [`design.md`](design.md) §2.
3. **Animation is `autoclip` (a named, reusable shape) + `autolink` (its application).** Not
   keyframes. The split is what makes "multi param can use the same automation" *true* rather than
   *maintained*. `keyframe` and `grade-dissolve` are withdrawn as two mechanisms for one job, and
   what they could express is mapped onto the replacement in [`design.md`](design.md) §3.
4. **One generated address space, `<object>[.<filter>].<param>`, with the registry as the router.**
   Cosmo's own leaf names, so a preset, a `.cmp`, a `cosmo-cc set` line and an expression spell
   `exposure` identically. Generated, not hand-written, because Cosmo's `--help` already ran the
   experiment: generated names always right, hand-maintained hints missing for 8 of 30.
5. **Gene is promoted to `core/Gene` and aliased from `genesis_core`, not copied**, and extended
   with dotted paths and a time scope. The forked-palette scar is why; `arstrobench`'s aliasing of
   `cosmo/Theme.cpp` is the pattern.
6. **Two crops, deliberately named differently**: `s1.xform.crop` is the source's *framing* (Cosmo's,
   static, part of the look) and `clp_a.geom.crop` is the shot's *reframe* (Interstellar's,
   animatable, per clip). They also sit on opposite sides of the colour stage, which is the deeper
   reason. [`design.md`](design.md) §4.

**2026-09-08 — no `arstro.interstellar.*` skill was created, deliberately.** A skill that names a
file, a command or a flag which does not exist costs the next agent a whole investigation to discover
it is fiction — `arstro.rule` §8 records four such instructions in Cosmo's own skills. There is
nothing to build against yet, so work runs under `arstro.rule` + `arstro.design.rule` with
[`DEVELOPING.md`](DEVELOPING.md) §3 and §6 as the local law. **The right moment to write
`arstro.interstellar.core.implement` and `.design.implement` is at the end of P1**, when the service
seam, the CLI and the test harness exist and can actually be named.

**2026-09-08 — the as-built tier and the defect list were created empty, on purpose.** Both cost
nothing now and are expensive to retrofit; the suite already has one unit that skipped the second
tier and carries it as an open task.

---

## Verification notes

**What was actually run, 2026-09-12.** `ctest --test-dir build` → **21 suites, 0 failed**
(`interstellar_core` now 35 tests).

| claim | evidence |
|---|---|
| it decodes real video | `rack add a.mp4 b.mp4` → `added a 1280x720 24.0fps frames=120` |
| the right source frame reaches the right output frame | test media with **burned-in frame counters**: output 0 → `A 0`, output 24 → `A 24`, output 72 → `B 48`, each read off an extracted PNG |
| the same claim, with no codec | `test_the_right_source_frame_reaches_the_right_output_frame` + `test_speed_selects_source_frames`, against a synthetic source that encodes its index in its pixels |
| a dissolve crossfades | frames 48/54/59/60 of the master: `A 48` → a genuine blend → `B 35` → `B 36`; and `test_a_transition_holds_the_outgoing_clip_and_crossfades` asserts the weights sum to 1 |
| the cache works | `test_the_frame_cache_serves_a_second_visit_and_not_a_stale_one` — a repeat visit decodes **nothing**; a parameter change decodes **again** |
| it writes a real master | `render --out master.mp4` → `h264,1280,720,24/1,96`, 919 KB, **1.76 s for 96 frames** |
| the window builds and runs the same App | `interstellar` links and starts; `interstellar_shots --script` renders the same tree headlessly, and two of those PNGs were **read** — which is how D-6 and D-8 were found |
| contained at two sizes with real footage | `cut-1440x900.png` and `cut-1024x640.png`, the second showing the dissolve mid-blend |

**`[!]` What is NOT verified, and must not be claimed:**

1. **The GUI was never run against a display in this session** — there is no X server and no
   `xvfb`. It builds, links and starts; its tree, layout, motion and input paths are covered by
   `interstellar_ui_tests` and `interstellar_shots`, which drive the same `App`. **Nobody has
   clicked it.**
2. **No colour has reached a real `.cmp`.** Every rack is a fake or unhosted. This is P3.
3. **No committed golden frame**, so determinism is by construction rather than by test.
4. **Playback is decode-bound.** No read-ahead, no proxy-level selection. A 4K source will not
   keep up, and the dropped-frame count is the honest readout rather than a fix.
5. **Export blocks the window**, and audio is entirely absent — no audio decode, no mixing, no
   A/V sync.
6. **The measurement is one machine, one codec, one resolution.** 1.76 s for 96 frames of
   1280×720 says nothing reliable about 4K ProRes, which is what P2's risk in `design.md` §8.2
   actually asks about.
