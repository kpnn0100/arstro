---
name: arstro.cosmo.core.debug
description: Use to investigate any suspected non-UI problem in the cosmo photo editor — wrong pixels, an adjustment that does nothing, a crash or hang, a project/preset/settings file that loses data, a slow or stalled image load, a bad export, GPU-vs-CPU divergence, a behaviour that is only reachable by clicking. Reproduces it with no GUI in the loop — a script of Commands driven against CosmoService, a diffable AppModel dump, the event stream, seeded config, the control socket for a live window — judges against the written requirements whether it is a defect or intended behaviour, files it in the committed defect list with the exact reproduction, RECOMMENDS a fix without applying one, and commits. It never edits product code — landing a fix is `arstro.cosmo.core.implement`, which the user must invoke. Invoke for "cosmo is broken", "the export looks wrong", "it crashed", "is this a bug?", "why is loading slow", "the CPU limit does not limit", "/arstro.cosmo.core.debug". For visual/layout/interaction problems use arstro.cosmo.design.debug.
---

# arstro.cosmo.core.debug

> **Invoke `arstro.rule` first.** It carries the rules this skill used to restate: the
> core/front-end split, requirements-first and the conflict rule, the V-model doc-sync loop, the
> agent-drivable surface and its API document, and the ledger/defect/commit conventions. **Follow
> both; where they overlap, this file's checklist is the one to satisfy** — except on the
> architecture, requirement and agent-drivability laws, where `arstro.rule` wins.

The **reproduce → judge → file → commit** workflow for cosmo's engine, session, persistence, load and
export. Its output is never just an answer in chat: it is a **committed defect entry with a command
anyone on any machine can re-run**, plus — when the requirements turned out to be silent — a requirement
that now says what the right behaviour is.

**This skill does not fix anything.** It reproduces, judges, files, and *recommends* — and then stops.
Landing a fix is `arstro.cosmo.core.implement`'s job, and the user invokes it; a recommendation is not
permission to act on itself. Never end a debugging session with the finding only in the conversation
either: filing is mandatory, and so is saying what you would do about it.

Why the split is absolute rather than "fix it if it is small and obvious", which is what this skill
used to say: **the size of a fix is not knowable until the diagnosis is finished, and by then you are
invested in it.** Every defect in this project's Closed list looked like a one-liner at the moment it
was understood, and several were not — D-11 needed a requirement amended, D-13 turned out to be an
event-ordering contract, D-23 wanted a change in two layers for one symptom. A diagnosis that ends in a
recommendation can be argued with. A diagnosis that ends in a commit has already decided.

You may still WRITE and RUN throwaway things to prove a point — a probe in the scratchpad, a fixture
under `core/tests/fixtures/`, a temporary assertion you delete again. What you may not do is change
product code, and the boundary is exactly that: **nothing under `apps/` or `core/` that ships**, except
`DEFECTS.md`, `PROGRESS.md`, `REQUIREMENTS.md` and a committed fixture the reproduction needs.

---

## 0. Orient

Read, in order: `apps/cosmo/docs/DEFECTS.md` (it may already be filed — if so, add evidence to the
existing entry rather than opening a duplicate) · `apps/cosmo/docs/PROGRESS.md` (what changed recently is
where bugs live; check the last commits) · `apps/cosmo/REQUIREMENTS.md` and
`apps/cosmo/docs/requirements.md` for the area involved. Then read
`arstro.cosmo.core.implement` §0 for the code map and §5 for the shell surface.

**Read `R-SVC-1…10` too, whatever the area is.** Since 2026-08-17 the application lives in
`CosmoService` (`apps/cosmo/core/service/`), not in the GTK host, which changes both halves of this
skill: a bug in loading, editing, history, persistence or export is now reproducible as a **script of
commands with no window** (§2), and "this is only reachable by clicking" is itself a defect class with a
requirement behind it (§3). The design is
`apps/cosmo/docs/service-architecture-proposal.md`; the per-milestone status is `PROGRESS.md`.

---

## 1. The loop

1. **Capture the report.** Write down what the user saw, in their words, before you theorise.
2. **Reproduce with no GUI in the loop** (§2). No reproduction, no defect entry — file it as
   *Unreproduced* with everything you tried, which is itself useful.
3. **Minimize** — smallest image, fewest params, fewest commands. The target is a script short enough to
   paste into the entry.
4. **Judge** (§3): defect, intended behaviour, or **requirement gap**.
5. **File it** (§4) in `apps/cosmo/docs/DEFECTS.md`, with the reproduction and the event/model excerpt
   that measures it.
6. **Close the requirement gap if there is one** (§3c) — write the requirement before anything else.
7. **Recommend the fix** (§6) — precisely enough to be acted on, and without acting on it.
8. **Commit** (§7) the diagnosis. The defect list is the deliverable; no product code moves.

---

## 2. Reproduce from a shell

**A bug in loading, editing, history, persistence or export is a command script, not a sequence of
clicks.** That is the single biggest change to this skill: `CosmoService` runs the whole application
with no window, no Artboard and no GTK, so the reproduction you write down is a text file anyone can
re-run on any machine. Until 2026-08-17 it was not so — two hours went into trying to drive a real load
from a shell with seeded config and synthetic X11 clicks, and it never ran once, which is how D-11 and
D-12 shipped (`PROGRESS.md` **NEXT** tells that story; do not repeat it).

### 2.1 Sandbox first

Always, so a run cannot corrupt the user's real config and so the log and the recents are only this run:

```bash
S=/tmp/cosmo-dbg && rm -rf $S && mkdir -p $S/cosmo_v2      # configDir() is $XDG_CONFIG_HOME/cosmo_v2
export XDG_CONFIG_HOME=$S
printf 'cosmosettings=1\npreviewEdge=1600\nthreads=1\nuseGpu=0\ncpuPercent=50\n' \
    > $S/cosmo_v2/settings.txt
```

Fix the variables that make runs differ: **`threads=1`, `useGpu=0` and a pinned `cpuPercent` first** —
if the symptom vanishes, you have learned something enormous (a race, a GPU-vs-CPU divergence, or a
budget effect) rather than lost the reproduction. Then turn each back on separately.

The sandbox matters even for a headless run: `CosmoService`'s constructor calls
`ProjectStore::recents()`, and `configDir()` reads `XDG_CONFIG_HOME`/`HOME` — so an unsandboxed test run
reads and *writes* the user's real `recent.tsv`.

### 2.2 A project, and a script of commands

A `.cmp`/`.cosmoproj` is line-oriented plain text with no escaping. The smallest correct one is what
`writeFakeProject()` in `sessionTests.cpp` emits — copy that rather than inventing a layout:

```
cosmoworkspace=1
#group
parent=-1
name=Tokyo
#image
parent=0
path=/fake/img0.raf
```

The reproduction itself is a script in the `Command` grammar (`arstro.cosmo.core.implement` §5.3;
`Command.h`'s header comment is the published list). `state print` before and after the suspect step is
what turns "it looks wrong" into a diff:

```
# repro.txt — D-nn: exposure is lost when the group is bypassed
settings set cpuPercent=25 previewEdge=1600
project open /tmp/cosmo-dbg/japan18.cmp
wait load-finished --timeout 120s
state print                      # BEFORE
select 3
set exposure=1.2 temp=7000
bypass 0 on
state print                      # AFTER  -> diff the two
```

**`formatModel(m, stable)` is the diffable artifact.** It excludes `revision`, `frameSeq` and
`budgetPeakDecode` precisely so that two front ends showing the same state produce byte-identical text
(`AppModelCodec.h`). So:

- **CLI run vs GUI run.** Drive the same commands headlessly and by hand in the window, dump
  `formatModel(model(), stable)` from each, and `diff` them. A difference is a real behavioural
  divergence between front ends — R-SVC-9 exists to make that check possible, and
  `test_two_services_dump_the_same_state` is the executable version of it. Pump count and elapsed
  animation are excluded by construction, so a diff is never noise.
- **Before vs after.** Two stable dumps around one command localise a bug to a field:
  `selectedNode`, `currentSlot`, `historyNodes`/`canUndo`, `loadDone`/`loadWorkers`,
  `budgetTotal`/`budgetEngineThreads`, or a `node= … kind=image|group|pending|failed` row.
  `kind=pending` where you expected `failed` is "still coming" versus "gone" — a distinction the model
  makes on purpose.
- **`--json`** (`state print --json`) when you want to compare with `jq` rather than by eye.

### 2.3 Watch the event stream

The events *are* the log (R-SVC-5): `formatEvent()` produces one deterministic line per state change,
which is what `--watch` streams, what the journal records and what an `expect` step matches. Grep it for
the milestone that should have happened and did not:

```
[evt] project.opening name=japan18 entries=18
[evt] load.progress done=6 total=18 name=DSCF5411.RAF
[evt] entry.decoded index=6 slot=5 name=DSCF5411.RAF
[evt] entry.failed index=7 path=/photos/gone.RAF
[evt] frame.ready slot=3 width=1600 ms=41.2
[evt] load.finished decoded=17 total=18
[evt] command.rejected select: no node 9999
```

A rejected command also lands in `AppModel::lastError`, so a failure is still visible after the fact to
a front end that was not subscribed.

### 2.4 How to actually run the script today

**Check `PROGRESS.md`'s S-milestones before choosing** — `cosmo-cc` is S3 and `apps/cosmo/cli/` may not
exist yet:

- **S3 landed** → `cosmo-cc --script repro.txt --watch` (and `cosmo-cc attach` for §2.5). Put the exact
  invocation in the defect's **Reproduce** block.
- **S3 not landed yet** → drive the service directly from `cosmo_core_tests`, which is a real
  reproduction and not a workaround. `sessionTests.cpp` already has every piece: `FakeDecoder`,
  `writeFakeProject()`, `pumpUntilIdle()`, and six worked service tests to copy from — start from
  `test_service_opens_a_project_with_no_ui`. A repro is ~15 lines:

  ```cpp
  EditSession session;  ThreadBudget budget(25, 24);
  CosmoService svc(session, budget);
  svc.setDecoderFactory([]{ return std::unique_ptr<IImageDecoder>(new NativeImageDecoder()); });
  svc.subscribe([](const Event &e){ printf("%s\n", formatEvent(e).c_str()); });   // = --watch
  std::string err;
  for (const char *line : { "settings set cpuPercent=25", "project open /tmp/.../japan18.cmp" })
      if (!svc.dispatchText(line, err)) printf("REJECTED: %s\n", err.c_str());
  pumpUntilIdle(svc);                                                            // = wait load-finished
  ModelDumpOptions o; o.stable = true; o.params = true;
  printf("%s", formatModel(svc.model(), o).c_str());                             // = state print
  ```

  Use the **real** `NativeImageDecoder` when the bug is in decoding and the `FakeDecoder` when it is in
  the tree, the ordering, the history or the budget. If the bug turns out to be real, this becomes the
  regression guard — so write the assertion in rather than only printing.

**A GUI-only reproduction is now a finding in itself.** If a behaviour cannot be reached by any
`Command`, file that as a defect against R-SVC-2 alongside the symptom (§3), because the missing command
is *why* the bug was hard to see.

### 2.5 When it only happens in a live window — the control socket

For a symptom that needs the real GTK app (a wiring mistake between the host and the service, a timing
problem against a real GPU, a leak over a long session), R-SVC-8 gives you the running process:

```bash
cosmo --control /tmp/cosmo.sock &            # the user watches this window
cosmo-cc attach /tmp/cosmo.sock <<'EOF'      # you type here; events stream back
settings set cpuPercent=25
project open /tmp/japan18.cmp
wait load-finished
state print
EOF
```

Same grammar, same events, same `state print` — the service is in-process with the GUI, so nothing is
being simulated. **This is S5 and `apps/cosmo/host/ControlChannel.{h,cpp}` does not exist yet; the
ledger is the authority on when it does.** Until then, a live-window bug is reproduced with a bare
`cosmo <image>` run plus the log, and the *behaviour* underneath it is reproduced headlessly per §2.4.

### 2.6 The lower levels, unchanged

**Narrow it with the existing suites.** A failing `cosmo_core_tests` or `image_tests` case is a better
reproduction than any app run: `cd build && ctest -R cosmo --output-on-failure`, or add a temporary test
that fails, and keep it as the regression guard if it turns out to be a real defect.

**Bugs that are genuinely below the service** — a wrong pixel, a bad decode, a GPU-vs-CPU divergence —
are still best pinned in `image_tests`/`cosmo_core_tests` against `EditEngine` directly. The service
adds nothing to a claim about one processor's arithmetic. `cosmo-cc info` / `render --cpu|--gpu` /
`bench` / `backends` remain the intended CLI surface for these and are S3
(`arstro.cosmo.core.implement` §5.2 maps each one).

**A platform claim gets a standalone fixture.** When the question is what a *library or the OS* does
rather than what cosmo does, write the 30-line C program and run it — that is how D-12 was proven and
disproven-by-assumption at once. Both live in `apps/cosmo/core/tests/fixtures/`
(`omp_env_order.c`, `omp_pin.c`) and are compiled by hand, not by CMake:

```bash
gcc -fopenmp -O1 -o /tmp/omp_env_order apps/cosmo/core/tests/fixtures/omp_env_order.c
/tmp/omp_env_order                      # setenv() inside main(): team size = every core
OMP_NUM_THREADS=1 /tmp/omp_env_order    # set before exec: team size = 1
```

---

## 3. Judge — defect, intended, or requirement gap

Read the requirement **before** you form an opinion. Search both tiers for the area
(`REQUIREMENTS.md` `R-<AREA>`, `docs/requirements.md` `DR-<AREA>`), and quote the sentence you judged
against in the defect entry.

**a) The behaviour contradicts a written requirement → it is a defect.** Confirmed. File it, cite the
requirement it violates.

**b) The behaviour matches a written requirement → it is not a defect.** Say so plainly, quote the
requirement, and explain the reasoning behind it if the doc gives one. If the user still wants it
changed, that is a **requirement change**, not a bug: amend the `R-` entry in place with
`**AMENDED (…)**` and one line of why, then hand the work to `arstro.cosmo.core.implement`. Do not
"fix" behaviour that a requirement asks for — you would break whatever that requirement was protecting.

**c) No requirement covers it → this is a requirement gap, and closing it is part of this task.**
This is the case the skill exists for. Do not silently pick a side.
1. Decide what the behaviour *should* be, from the surrounding requirements, the design rationale in
   `docs/design.md`, and how the neighbouring features behave. State your reasoning.
2. **Write the requirement** — a new `R-<AREA>-<n>` in `REQUIREMENTS.md` — and conflict-check it against
   every existing requirement in that area.
3. **Now re-judge against it.** Usually the observed behaviour is then a defect; occasionally the
   requirement you just wrote blesses it, and you file nothing but the requirement.
4. If the right answer genuinely depends on what the user wants (a product decision, not a technical
   one), write the requirement as the option you recommend, mark it `*Open — needs confirmation.*`, ask
   the user, and file the defect against it provisionally.

**Always a defect regardless of what any document says:** a crash, a hang, data loss or silent
corruption of a saved file, a memory or handle leak, a data race, wrong pixels the pipeline order
cannot explain, or a GPU path that disagrees with the CPU reference.

**And, since R-SVC: "this behaviour is only reachable by clicking" is a defect, with a requirement
behind it.** Unreachability used to be an informal complaint about tooling; R-SVC-2 makes it a
violation — a behaviour a front end can reach that no `Command` expresses is a defect in the command
set. File it against `R-SVC-2` (or `R-SVC-3` when the *state* cannot be read out of `AppModel`, or
`R-SVC-4` when logic has settled in a widget), with the same rigour as any other entry. It is not a
lesser class of bug: it is the class that let D-11 and D-12 ship, and each unreachable behaviour is the
reason the next bug behind it will be found by reading rather than by measuring.

**Before blaming new code, check these cosmo-specific traps** — each has bitten this codebase already:
slot id must equal the index in every `mSlot*` vector (`resetWorkspace()` must reset the service);
`RenderService::render()` coalesces, so a missing frame may be a *superseded* request, not a lost one;
`composeParams` follows `base` for crop and `curveLog` but adds `rotation` — a "wrong crop in a group" is
often correct-by-design; `effectiveParams` and `effectiveEditParams` differ on bypass deliberately; a
stalled load is usually the `OrderedParallelLoad` window/byte-cap predicate, whose only safety is the
`i == mConsumed` exemption; a param that round-trips lossily is a missing case in `EditParamsIO` or
`EditParamsApf`, not an engine bug; and the engine works in **linear light**, so a processor that looks
"too strong" may be assuming sRGB.

**Four more, each one a real trap that a single day of debugging produced.** They are here because each
one *looked* like working code, and three of the four passed every lower-level check:

- **An environment variable a library reads in a load-time constructor cannot be set from `main()`.**
  libgomp parses `OMP_NUM_THREADS` in an ELF constructor, so `setenv`/`g_setenv` from `main()` is read
  by nobody and the nested team stays machine-sized — D-12, proven by
  `core/tests/fixtures/omp_env_order.c`. The same trap applies to any `getenv`-configured dependency.
  The working mechanism was `omp_set_num_threads(1)` **called on each worker**, because the OpenMP
  thread count is a per-thread ICV — so also suspect *which thread* set a per-thread setting. And note
  that D-12's fix is inert on a build whose LibRaw has no OpenMP (this Linux host's vendored
  `libraw.a`), which is why `ompPinStatus()` is logged: check the runtime status line rather than
  assuming the mechanism engaged.
- **A "budget" that several consumers each convert for themselves is not a budget.** D-11: the decode
  pool and the engine each called `AppSettings::workersFor(cpuPercent)`, and they run *concurrently* by
  design (R-LOADPERF-3 streams decoded images into a live editor), so the total was about twice what the
  user chose — 17 threads of 16 cores at the shipped default. Nothing was arithmetically wrong in either
  call site; the defect was that nobody owned the sum, and nobody *could*, because the two lived in
  different layers. When a symptom is "the limit does not limit", count the consumers before checking
  the formula, and look for a single owner (`ThreadBudget`) rather than a better clamp.
- **`OrderedParallelLoad::stop()` latches its stop flag, so a reused pipeline must be fully
  re-initialised by `start()`.** This was invisible for as long as every load built a fresh `LoadJob`;
  the moment `ProjectLoader` kept one pipeline and called `stop()` before each `start()`, a second load
  produced a pool whose workers all returned immediately and a load that never advanced past zero.
  `start()` now re-initialises `mStop`/`mClaimed`/`mConsumed`/`mInFlight`. Generalise it: **a symptom of
  "works the first time, silently does nothing the second" is latched state in a reused object** —
  check every flag `start()` does not clear. It was found by a test, not by inspection.
- **An assertion about concurrency must be captured *during* the concurrent moment.** After a load ends,
  `ThreadBudget` correctly gives the engine the whole budget back, so a naive after-the-fact
  `peakDecode() + engineThreads() <= total()` looks violated when nothing is wrong. See
  `test_project_load_peak_never_exceeds_its_pool`, which snapshots `engineThreads()` while the load is
  running and asserts against that. The same applies to a screenshot of a transition or a reading of a
  queue depth: **if the invariant only holds during a window, sample inside the window.**

### 3d. A number the code cannot measure does not belong in a requirement

R-CPU-4 said the log "records the worker count actually used at each load, so the nominal budget and the
real one can be told apart", and quoted the line. That requirement was marked satisfied. **The line had
never been executed** — reaching it needed a project opened by clicking (D-6) — so for two commits the
feature's own honesty clause was the only evidence for it, and two defects hid behind it. It now reads,
amended: *a number cosmo cannot measure does not belong in this requirement*, and `ThreadBudget` counts
producers so a test can assert the peak.

Carry that forward as a judging rule, not a piece of history:

- When a requirement makes a **quantitative** claim, ask what reads the quantity back. If the answer is
  "a log line" and nothing has run it, the claim is unverified whatever the ledger says — and an
  unverified quantitative claim is where you should look *first* for the defect.
- **Prefer a measured assertion to an argued one.** "The pool is sized correctly because `workersFor`
  clamps to the cap" is an argument; `peakDecode() <= pool` in a test is a measurement. Both D-11 and
  D-12 survived a careful reading of the source and died to a fifteen-line test. When you file a defect,
  put the measurement in **Evidence** and keep the reasoning for **Cause**.
- If you close a requirement gap (§3c) with a number in it, write the assertion that reads it back in
  the same session, or write the requirement without the number. A requirement whose satisfaction cannot
  be observed is not a contract; it is a note.
- Corollary for `[!]` rows in `PROGRESS.md` — *done but UNVERIFIED*: treat every one as a candidate
  defect site, not as a formality. U1.1a was `[!]` for exactly this reason and both defects were inside
  it.

---

## 4. File it — `apps/cosmo/docs/DEFECTS.md`

One entry per defect, newest first in the Open section. IDs are `D-<n>`, sequential across both debug
skills, **never reused** — check the highest existing id in `DEFECTS.md` (both sections) before you pick
one; D-1…D-12 are taken. Keep the exact shape below; the debug skills and the ledger cross-reference it.

```markdown
### D-13 — Group crop is ignored when the child is bypassed
- **Area:** core / engine            <!-- core | design -->
- **Status:** Open                   <!-- Open | Confirmed | Fixed | Not-a-defect | Unreproduced | Deferred -->
- **Severity:** S2                   <!-- S1 data loss/crash · S2 wrong output · S3 wrong UX · S4 cosmetic -->
- **Found:** 2026-08-17, by inspection during D-11 / reported by the user
- **Reproduce:**
  ```bash
  S=/tmp/cosmo-dbg && rm -rf $S && mkdir -p $S/cosmo_v2 && export XDG_CONFIG_HOME=$S
  printf 'cosmosettings=1\npreviewEdge=1600\nthreads=1\nuseGpu=0\ncpuPercent=50\n' > $S/cosmo_v2/settings.txt
  cat > $S/repro.txt <<'EOF'
  project open apps/cosmo/core/tests/fixtures/group-crop.cmp
  wait load-finished
  select 1
  state print
  bypass 1 on
  state print
  EOF
  ./build/apps/cosmo/cosmo-cc --script $S/repro.txt --watch      # S3; until then the
                                                                 # sessionTests.cpp driver of §2.4
  ```
- **Expected:** the group's crop applies to the member (R-GROUP-3: "a group's params compose onto
  every member").
- **Actual:** the second `state print` shows the member's crop back to full frame; the group node still
  carries `crop=0.1,0.1,0.8,0.8`.
- **Evidence:** the two stable dumps, diffed — one field moved. Event stream shows
  `[evt] params.changed crop` on the group and no `frame.ready` for the member.
- **Judgement:** defect — contradicts R-GROUP-3. *(or: requirement gap, closed by new R-GROUP-5.)*
- **Cause:** `composeParams` follows `base` for crop, and the leaf's own crop is full-frame.
- **Requirement:** R-GROUP-3 (existing) / R-GROUP-5 (written while filing this)
- **Fix:** commit `abc1234`.
- **Guarded by:** `group_crop_composes_onto_member` in `sessionTests.cpp` — checked to fail on the
  pre-fix code, at the crop assertion.
```

Rules: **the Reproduce block must be something someone else can paste** — a command, or a command script
plus how it is driven (§2.4) — not a description. Prefer a **measurement** in Evidence over an argument
(§3d): a diff of two stable dumps, an event line, an assertion that failed. Quote the requirement you
judged against, verbatim enough to be checkable. **Say which front end you reproduced with**, since
"only in the GUI" and "in both" are different bugs now. If you could not reproduce, still file it with
Status `Unreproduced` and list everything you tried — the next session on another machine starts from
there instead of from zero. When a defect is resolved, move the whole entry to the `## Closed` section
with its commit hash and the test that now guards it; never delete an entry. If the fix is inert on the
host you are on (D-12's OpenMP pin is, on Linux), say so in a **Platform note** and name the check that
would confirm it elsewhere.

---

## 5. Severity, and what to do about it

- **S1** — crash, hang, data loss, corrupted save. File it, then tell the user plainly and immediately:
  what breaks, what to avoid until it is fixed, and what you recommend. Do not fix it — say that it is
  S1 and that `arstro.cosmo.core.implement` should be run next, which is the fastest honest path.
- **S2** — wrong output the user would ship (bad pixels, bad export, a lost adjustment). File it and
  make it the ledger's **NEXT**, so the implement skill picks it up first.
- **S3** — wrong behaviour that has a workaround. File; schedule in the ledger.
- **S4** — cosmetic or diagnostic. File; batch.

---

## 6. Report and recommend — the output IS the deliverable

A debugging session's product is an explanation someone can act on. **Lead with what the problem was**,
in the user's own terms, before any of the machinery. They asked because something was wrong; the first
thing they should read is what that was.

Say it in this order, and keep every part short:

1. **What the problem is.** One or two sentences, plain, no file paths yet. *"Images had no name
   anywhere, and export therefore called every one of them (missing image)."*
2. **What proves it** — the measurement, pasted. A before/after line, a timeline, a diff, a counter. Not
   an argument (§3e). If the numbers are what make the point, put the numbers first and the prose after.
3. **Why it happens** — the cause, with `file:line`. One paragraph. If a symptom the user did *not*
   mention shares the cause, say so; if something they expected to be broken is fine, say that too and
   why, because "the top bar looked correct the whole time" is often the reason a defect survived.
4. **Whether it is a regression**, checked rather than assumed. `git log -S "<the line>"` costs one
   command and changes how the fix is judged.
5. **What you recommend.** This is the part the user asked for, so make it decidable:
   - the change, named concretely — which function, which layer, and *why there* rather than at another
     layer that would also make the symptom go away;
   - the alternatives you rejected, in one line each, with the reason;
   - what it would touch, and whether it crosses a skill boundary (`widgets/` is design's) or a
     submodule (`core/ImageProcessing` is two commits);
   - **the test that should guard it**, named, and what it must assert to fail on today's code;
   - the risk, honestly — what else reads the thing you would change.
6. **Say that you have not applied it**, and name the skill that would: `arstro.cosmo.core.implement`.

Prefer one recommendation with its reasoning over a menu. If two options are genuinely balanced, say
which you would pick and why, then give the other — a survey with no verdict pushes the decision back
onto someone with less context than you now have.

**Recommend at the layer the cause is at, not the layer the symptom is at.** D-23 showed up in the
export dialog and belonged in the `.cmp` reader; fixing the dialog would have left the filmstrip and the
breadcrumb still wrong. If several consumers are wrong at once, that is evidence the fix belongs
upstream of all of them.

**Two fixes for one cause is sometimes right — say so explicitly.** When a failure is *silent* (an empty
string that draws as nothing, a dropped option, a count that stays at zero), recommend the fix at the
source AND a fallback at the sink, and justify the second one by the silence. When the failure is loud,
one fix is enough and a second is noise.

**If the requirement was the problem, the recommendation is a requirement.** A number nobody measures
(R-CPU-4), a hook that no longer exists (D-1), an option that parses and is dropped (D-14, D-18) — these
are fixed in `REQUIREMENTS.md` first and in code second, and you may write the requirement here (§3c)
because a requirement is a decision, not an implementation.

## 7. Commit — the diagnosis, never a fix

Every session ends in a commit, because the diagnosis is the deliverable. The commit contains
`DEFECTS.md`, any requirement written or amended, the fixture the reproduction needs, and the
`PROGRESS.md` update — **and no product code**. A reviewer should be able to read the commit and decide
whether they agree with the recommendation before any of it is built.

```
cosmo: file D-13 — group crop dropped when the member is bypassed (R-GROUP-3)

Reproduced headlessly against CosmoService with a six-line command script; the
group's crop is dropped because composeParams follows base for framing, and the
two stable state dumps differ in exactly that field. Requirement R-GROUP-5
written to state what a bypassed member should inherit. Not fixed yet; PROGRESS
NEXT now points at it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
```

Include in the same commit: `DEFECTS.md`, any requirement you wrote or amended, the fixture, the command
script or the throwaway service test your reproduction needs, and the `PROGRESS.md` update. **A
reproduction that lives only in the commit message is not committed** — put the script in
`apps/cosmo/core/tests/fixtures/` or the test in `sessionTests.cpp`. **Then pull and push**
(`arstro.rule` §7) — a filed defect that only exists on this machine is a defect nobody else can
pick up.

---

## 8. Definition of done

- [ ] The symptom was reproduced **with no GUI in the loop** — a command script against the service, a
      test, or a fixture — and the exact way to re-run it is written down; or it is filed as
      `Unreproduced` with everything tried, including why the headless path did not reach it.
- [ ] Judged against a **quoted** requirement — defect, not-a-defect, or requirement gap. `R-SVC-*` was
      considered, not only the area requirement.
- [ ] Any requirement gap is **closed in `REQUIREMENTS.md`** (conflict-checked), not left implicit, and
      any number in it has something that reads it back (§3d).
- [ ] The defect entry is complete: repro, expected, actual, evidence, judgement, severity — with a
      **measurement** in Evidence, not an argument.
- [ ] If the behaviour turned out to be unreachable except by clicking, that is filed too (R-SVC-2).
- [ ] **No product code changed.** `git status` shows only `DEFECTS.md`, `PROGRESS.md`, a requirement
      file, and at most a fixture the reproduction needs. If you edited anything under `apps/` or
      `core/` that ships, you ran the wrong skill.
- [ ] The report **leads with what the problem was**, in the user's terms, before any machinery (§6).
- [ ] A **recommendation** exists and is decidable: the change, where, why there and not elsewhere, the
      rejected alternatives, the test that should guard it, and the risk.
- [ ] Whether it is a **regression** was checked with `git log -S`, not assumed.
- [ ] Scheduled in `PROGRESS.md`, and the user was told which skill lands it.
- [ ] Committed to `main`.
