# Interstellar — Defect list

`D-<n>`, **sequential across the unit, never reused, nothing ever deleted.** A resolved entry moves
whole to `## Closed` with its commit hash and the test that now guards it.

**No defects. There is no code.**

This file exists from the first day for the same reason the as-built tier does: the conventions are
cheap to establish and expensive to retrofit, and a unit without a defect list quietly loses the
one record that stops a lesson being learned twice.

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

*(none)*

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
