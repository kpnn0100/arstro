# Interstellar — Progress Ledger

**This file is the single source of truth for what is done and what to do next.** It is committed
and travels between computers. A session reads the **NEXT** line below, does **one** task, updates
this file, and commits — in the same commit as the work.

- Rules: `.claude/skills/arstro.rule` (process) · `.claude/skills/arstro.design.rule` (pixels).
  **No `arstro.interstellar.*` skill exists yet** — see the decision of 2026-09-08 below.
- Intent: [`../REQUIREMENTS.md`](../REQUIREMENTS.md) · As-built: [`requirements.md`](requirements.md)
  · Defects: [`DEFECTS.md`](DEFECTS.md) · Plan + gates: [`plan.md`](plan.md) ·
  How to work: [`DEVELOPING.md`](DEVELOPING.md)
- Checkbox legend: `[ ]` not started · `[~]` in progress · `[x]` done + verified · `[!]` done but
  UNVERIFIED (say exactly what is unverified and why).

*Last updated: 2026-09-11 — P1, P4, P5, P6 and most of P10 are built and verified. `ctest` 21/21.*

---

## NEXT

**► P3 — the hosted `CosmoService` behind `RackAccess`, so a colour edit really reaches a `.cmp`.**

Everything above the seam is done and tested against a fake rack: `set gr1.basic.exposure=0.2`
resolves, routes by owner, and writes — into a `FakeRack`. **The one thing that has never happened
is the write landing in a real Cosmo project**, and that is the requirement the whole design exists
for (R-COSMO-3). It is also the plan's most important test:

- [ ] **`RackEmbed`: own a `cosmo::CosmoService`, implement `RackAccess` over it.** Cosmo's surface
      is already the right shape — `explicit CosmoService(ThreadBudget &budget)` takes the budget by
      reference, and `setDecoderFactory` / `setWorkerInit` / `setImageWriter` / `subscribe` /
      `applySettings` / `dispatch` / `dispatchText` / `pump` / `model` / `takeFrame` cover the rest.
      Map `getParam`/`setParam` onto a `Select` + `Set` pair, exactly as `cosmo-cc` does, so the two
      paths are one path. `crop.x/y/w/h` needs the read-modify-write of the `crop` quadruple.
- [ ] **The gate, and it is one command sequence:** `rack import japan18.cmp` →
      `set gr1.basic.exposure=0.2` → `rack save` → open the `.cmp` in `cosmo-cc` and **see the value
      there**. Paste the output into the commit.
- [ ] **Then R-RENDER-5's identity:** `export-still` from Interstellar and the same source exported
      from `cosmo-cc` must be byte-identical. If they differ, the two apps are not sharing the
      authority they claim to.
- [ ] **Then the budget peak:** with `cpuPercent=25`, the measured concurrent thread peak across
      rack decode and colour render must be ≤ the budget. D-11's regression test, and it must exist
      before there are four consumers instead of two.

**Blocked on one thing in another unit:** R-COSMO-7 — a video source graded on an extracted
reference frame — is `arstro.cosmo.core.implement`'s commit, against Cosmo's own requirements. Until
it lands, the rack can hold stills only, which is enough to prove every item above.

**Also open, and smaller:** `docs/api.json` + `docs/API.md` are **generated but not committed and
not drift-tested** (R-SVC-10), so rung 4 is not claimed. That is one test and two files, and it is
cheap now while the tables are small.

## Phases

Detail, dependencies and gates in [`plan.md`](plan.md). Status only, here.

| phase | what | status |
|---|---|---|
| **P0** | freeze the contracts; the cross-unit prerequisites | `[x]` Gene promoted; the format and grammar frozen in `project-format.md`. Nebula and FFmpeg **deferred by decision**, not done: the `.isp` has its own canonical reader/writer, which is the "stubbed subset" the plan allowed |
| **P1** | project model, `.isp`, `Command`/`Event`/`AppModel`, `interstellar-cc`, the API document | `[x]` 29 core tests. API document generated; **`[!]` not committed or drift-tested** |
| **P2** | frame seams, one deterministic frame, the first golden test, the throughput measurement | `[!]` the seams are declared and a PPM writer exists, and `render` produces a deterministic frame sequence — but there is **no FFmpeg source, no committed golden, and no measurement**. The measurement is the gate and it has not been taken |
| **P3** | the Cosmo embed as colour authority — the architectural risk | `[~]` the `RackAccess` seam, the owner-based routing, the pin refusal and the bind-name derivation are built and tested against a fake. **The hosted `CosmoService` is not** — this is **NEXT** |
| **P4** | timeline cut operations + the compositor | `[x]` eight operations, seven blend modes, inverse-mapped geometry, transitions |
| **P5** | the generated parameter registry + automation clips and links | `[x]` 153 addresses in a 3-node project; shapes, links, lanes, the overlap refusal, the boundary lint |
| **P6** | expression bindings | `[x]` Gene promoted and extended; cycles refused with rollback; the user's own example asserted at three exposures |
| **P7** | scrub, then playback: the frame cache, read-ahead, level selection | `[ ]` `renderFrame` is synchronous and uncached. The parameter hash that the cache key needs exists (`Evaluator::paramHash`) |
| **P8** | the master render | `[~]` `render --out --range` walks frames, reports progress and writes a PPM sequence. No codec, no cancel, no resume |
| **P9** | Nebula VCS, embed propagation, the audio bed | `[ ]` |
| **P10** | the Artboard UI, and the equivalence test | `[~]` the shell, monitor, timeline, lanes and transport are built, shot at two sizes and asserted mid-tween. **No Grade or Deliver columns yet** (those workspaces render the shell and the monitor only), no `linux_main.cpp`, no control socket, so **no equivalence test** |

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

**What was actually run, 2026-09-11.** `ctest --test-dir build` → **21 suites, 0 failed** (up from
19: `gene`, `interstellar_core`, `interstellar_ui`, `interstellar_shots_headless` are new).

| claim | evidence |
|---|---|
| the promotion did not break Genesis | `genesis_tests` → **79 tests, 0 failed** |
| the `.isp` round-trips byte-exactly | `test_isp_roundtrip_is_a_fixed_point`, over a project with every node type |
| a colour edit reaches the rack and the `.isp` keeps none | `test_a_colour_edit_reaches_the_rack` — asserts the write landed **and** that the serialized text contains no `exposure` |
| one shape drives two parameters | `test_one_shape_drives_two_parameters` — moves one breakpoint, asserts both resolved values change |
| the user's binding example | `test_the_user_binding_example` — three exposures, including both clamps |
| automation runs before bindings | `test_the_evaluation_order_is_automation_then_bindings` — a binding reads 0.5, not 0 |
| nothing changes in one frame | three `interstellar_ui_tests` that pump one frame at a time and keep the first non-zero value |
| contained at two sizes | `test_nothing_overlaps_and_nothing_is_clipped_at_the_edge` at 1440×900 and 1024×640; the four rows tile the height exactly |
| the CLI is the whole app | `interstellar-cc run --script` drove the full scenario, saved the `.isp`, and rendered 12 PPM frames + a still |
| **the frames were looked at** | `interstellar_shots --outdir … --check` wrote 8 PNGs; two were **read**, which is how the header-origin and gutter defects were found |

**`[!]` What is NOT verified, and must not be claimed:**

1. **No colour has ever reached a real `.cmp`.** Every rack test uses a `FakeRack`. This is P3 and
   it is **NEXT**.
2. **No throughput measurement.** P2's gate is ms/frame at 4K and at a 1280 proxy edge, and it
   decides the default proxy level. Nobody has taken it, so `design.md` §8.2's risk stands open.
3. **No committed golden frame.** `render` is deterministic by construction and the PPM writer is
   byte-comparable, but no golden is committed and no test compares one.
4. **No equivalence test** (R-SVC-9), because there is no control socket and no window.
   `test_two_services_dump_the_same_state` covers the service half only.
5. **The Grade and Deliver workspaces are empty shells.** They switch, cross-fade and keep the
   monitor, and they have no columns — so Cosmo's parameter panels are **not** reused yet, which is
   the largest single piece of R-UI-6 still outstanding.
