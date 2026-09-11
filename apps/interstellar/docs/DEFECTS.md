# Interstellar — Defect list

`D-<n>`, **sequential across the unit, never reused, nothing ever deleted.** A resolved entry moves
whole to `## Closed` with its commit hash and the test that now guards it.

**No OPEN defects.** Five were found and fixed during the first implementation, all within the
commit that introduced the code they were in; they are in `## Closed` because the lessons are the
point, not the ids.

The conventions were established on day one for the same reason the as-built tier was: they are
cheap then and expensive to retrofit, and a unit without a defect list quietly loses the one record
that stops a lesson being learned twice.

## The boundary that makes this list worth keeping

**A debug skill reproduces, judges, files and *recommends* — and stops. Only an implement skill
changes product code.** So when a defect is picked up, the diagnosis, the measurement, the cause
with `file:line`, the recommended fix and the test that should guard it are already written here.
**Read the entry before re-deriving any of it.** You may disagree with the recommendation, and
should say so in the commit — but a recommendation silently ignored usually means the entry knows
something you have not read.

Two rules bind defects to requirements:

- a `Judgement` of **requirement gap** must produce a new requirement in
  [`../REQUIREMENTS.md`](../REQUIREMENTS.md);
- a defect may change a requirement's status — `⚠️ REOPENED (D-n)`.

## Entry format

The format is defined in `arstro.cosmo.core.debug` §4 and is not duplicated here beyond the shape,
so that the two cannot drift:

```markdown
### D-7 — one line naming the SYMPTOM as the user saw it
- **Reported:** the user's own words, quoted.
- **Reproduction:** the exact commands, runnable. A claim you cannot re-run is a claim, not a test.
- **Measured:** the numbers. Not "it is slow" — the ms, the bytes, the thread count.
- **Cause:** `file:line`, and the reasoning that gets from the measurement to the line.
- **Judgement:** code defect | requirement gap | doc drift | works-as-specified.
- **Recommended fix:** what to change, and what NOT to change.
- **Guard:** the test that must fail before the fix and pass after.
- **Requirement:** the `R-` tag this touches, and whether its status changes.
```

## Open

*(none)*

## Closed

Five defects, all found and fixed inside the commit that introduced them (2026-09-11, `interstellar:
build the core, the CLI and the UI shell`). They are recorded because **four of the five were found
by running or looking at something rather than by reading the code**, which is the whole argument
for the verification levels in `arstro.rule` §4.

### D-1 — `auto new --points 0=0,1=1` was refused as "wants t=v,t=v"
- **Area:** core / cli · **Status:** Fixed · **Severity:** S3 · **Found:** by running the first
  command script.
- **Cause:** `collectFlags` (`core/service/Command.cpp`) copied cosmo's rule of excluding a
  following token that contains `=`, so `--points` became a bare flag and `0=0,1=1` was parsed as a
  field named `0`.
- **Judgement:** defect. The exclusion exists in cosmo because its flags never take a value
  containing `=`; here one does, and bare `key=value` arguments only reach `set`, which does not go
  through `collectFlags` at all.
- **Fix:** the next token is the value unless it is another flag.
- **Guarded by:** `test_command_text_roundtrips`, whose `auto new` line carries `--points 0=0,1=1`.

### D-2 — `gene_tests` printed `[PASS]` for a test that was failing
- **Area:** tests · **Status:** Fixed · **Severity:** S1 (a suite that cannot report red is not
  evidence) · **Found:** by noticing an error line printed *above* eight `[PASS]` lines.
- **Cause:** `CMAKE_BUILD_TYPE=Release` puts `-DNDEBUG` in the flags and `<cassert>` then defines
  `assert` to `((void)0)`. This is **cosmo's D-43**, hit again in a new suite.
- **Fix:** undefine `NDEBUG` before `<cassert>` in both new suites.
- **What it then immediately caught:** a **dangling reference in the test's own fixture** —
  `mapScope` captured a by-value parameter by reference. So the lesson is not only "Release breaks
  asserts"; it is that a suite which cannot report red cannot report red **about itself**.
- **Guarded by:** the `#undef NDEBUG` header block in `coreTests.cpp` and `geneTests.cpp`, with the
  reason written above it.

### D-3 — every automation link in the first 190 px was invisible
- **Area:** ui · **Status:** Fixed · **Severity:** S2 (the automation was there and could not be
  seen) · **Found:** **by rendering `mix-1440x900.png` and looking at it.**
- **Cause:** `LaneStack` drew its labels in a 190 px gutter but started its time axis at x=0, so a
  link at t=1.0 with a 60 px/s zoom occupied x=60…180 — entirely under the gutter — and the paint's
  own `lr.x + lr.w > labelW` test then culled it.
- **Judgement:** defect, and a `arstro.design.rule` gotcha-15 instance: the lanes and the timeline
  held two copies of one fact (where time begins).
- **Fix:** one `time::headerWidth()`, read by **both** deck views and by both directions of their
  time↔x mapping. The timeline gained a track-header column as a consequence, which an NLE has
  anyway.
- **Guarded by:** `DR-UI-4`, and visibly by the `mix-*` shots.

### D-4 — the transport's timecode was drawn underneath the scrubber
- **Area:** ui · **Status:** Fixed · **Severity:** S4 · **Found:** **by looking at
  `cut-1440x900.png`.**
- **Cause:** `scrubberRect` used a hard-coded left gutter of 132 px; the mono timecode is ~50 px
  wide from x=96, so it ended at ~146.
- **Fix:** measure the real timecode in `onPaint` and derive the gutter from it. R5's rule —
  measured, not assumed — applied to a *sibling's* box rather than to the text's own.
- **Guarded by:** `DR-UI-5`.

### D-5 — `eval --explain` printed an input that contradicted its own result
- **Area:** core / evaluator · **Status:** Fixed · **Severity:** S2 · **Found:** **by running
  `--explain` while writing the debug skill**, which is the tool that skill tells an agent to reach
  for first.
- **Reproduce (pre-fix):** a binding `1 + ac_push.value * 0.08` resolved to **1.08** while its
  trace reported `ac_push.value = 0.0`.
- **Cause:** `ac_push.value` was resolved by a special case inside the Gene scope, so the number the
  expression used and the number the trace printed came from two different places.
- **Judgement:** defect — and the worst kind for this app, because `--explain` exists precisely to
  turn "a value the user cannot account for" into one they can. A lying trace is worse than no
  trace.
- **Fix:** seed every shape's output into the resolved map **before** the bindings run; the scope
  and the trace then both read the map. One authority for one number (R-G-3).
- **Guarded by:** `test_the_explain_trace_agrees_with_the_value_it_explains`, which asserts the
  result is arithmetically consistent with the input the trace claims produced it — **checked to
  fail on the pre-fix code.**

---

## Defects inherited by reference — other units' scars this app is exposed to

Not Interstellar defects, and **not renumbered into this list**. They are recorded here because this
app's design is shaped by them and because a new session should be able to find out *why* a rule
exists without reading another app's 172 KB defect file. Each is cited in
[`../REQUIREMENTS.md`](../REQUIREMENTS.md) at the requirement it produced.

| id | unit | what happened | what it forces here |
|---|---|---|---|
| **D-11** | cosmo | two owners each converted the user's CPU percentage independently → 13 of 24 cores at a 25% budget, 17 of 16 on a 16-core box | one `ThreadBudget`, held by reference, injected into the hosted Cosmo service (R-NFR-3, R-COSMO-8). Interstellar has **four** consumers |
| **D-12** | cosmo | the OpenMP thread count is a per-**thread** ICV, so pinning it on the wrong thread did nothing | every decode worker runs the init hook — including every `IFrameSource` worker (R-PLAY-1) |
| **D-13** | cosmo | events emitted *after* the mutation → 18 decoded images attached to nothing | announce before you mutate: `ProjectOpening` precedes `resetWorkspace` (architecture §4) |
| **D-21** | cosmo | `tryAcquire` *moves* the frame out, and while the view owned it no headless front end could tell a frame had arrived | the **service** polls; `takeFrame()` hands it on (detailed_design §1) |
| **D-36** | cosmo | one stray `nan` made a project unopenable | numeric corruption is **repaired and counted**, never refused (project-format §7) |
| **D-44/D-48** | cosmo | a frame that cost 2 537 ms and one that cost 250 ms logged identically; NaNs were substituted silently | every frame carries its ms and its non-finite count, and both reach the model |
| **D-52** | cosmo | a crop-preview flag flipped in one frame — the photo jumped out and snapped back | an *amount*, eased, never a boolean, for anything visible (R-G-1) |
| **D-56** | cosmo | `sourceSize` asked the engine, so the answer depended on how many times the caller had pumped | a size known at ingest is recorded at ingest |
| **D-59** | cosmo | `set exposre=1.2` (a typo) returned **success** and emitted `params.changed exposre` | every parser reports what it consumed; an unmatched key is an error naming it (R-SVC-6). This matters far more here: the address space is hundreds of names deep |
| **D-1** | cosmo | a doc describing a seam the code no longer had | a stale as-built entry is a defect with an id, not untidiness |
| **R-AISEG** | cosmo | a withdrawn feature's enum value `4` was retired forever, because a project written while it existed stores it | a retired id or enum value is never recycled (R-FMT-5) |
