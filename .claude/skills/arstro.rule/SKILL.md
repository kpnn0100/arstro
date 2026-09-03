---
name: arstro.rule
description: The base rules every Arstro skill obeys — the core/front-end split, requirements-first with the conflict rule, the V-model doc-sync loop, the agent-drivable surface and its generated API document, the ledger/defect/commit conventions, and the rule for updating these rules. Invoked FIRST by every other arstro.* skill (and by implement_artboard / the DSP skills) so eleven skills stop carrying eleven copies of the same law. Invoke directly when starting work in an Arstro repo with no more specific skill, when scaffolding a new app (interstellar, solaris, pulsar), or when asked "what are the rules here".
---

# arstro.rule

**The law every Arstro skill builds on.** It is invoked first, by every other skill, so that a rule
lives in one place and cannot drift into eleven slightly different versions of itself.

It already had. Before this file existed the eleven skills carried the same requirements-conflict
rule eight times, the same ledger legend four times, the same V-model sentence verbatim twice, and
the same commit trailer in three mutually contradictory forms (`Claude Opus 5`,
`Claude Opus 5 (1M context)`, `Claude Opus 4.8 (1M context)`). One skill was 150 of its 185 lines
copy-pasted from its sibling. **That is the failure this file exists to stop, and it is also the
argument for its own §8: a rule worth repeating is a rule worth hoisting.**

## Scope

Everything under `/home/namdln/1.workspace/m.yspace/arstro` — the apps (`cosmo`, `genesis`,
`arstrobench`, `pulsar`, `launcher`, and the two specified-but-unbuilt ones, `interstellar` and
`solaris`) and the libraries (`core/Artboard`, `core/ImageProcessing`,
`core/DigitalSignalProcessing`).

**This file is not a substitute for the skill you came for.** Follow both; where they overlap, the
more specific skill's checklist is the one to satisfy — except on §1, §2 and §5, where this file
wins, because those are the rules the suite is built on rather than one app's taste.

**If `arstro.rule` is not in your skill list**, you are in a standalone checkout of a submodule
(`core/Artboard`, `core/DigitalSignalProcessing`) that does not carry it. The rules still apply;
the copy of record is `.claude/skills/arstro.rule/SKILL.md` in the umbrella.

---

## 0. Orient — before the first edit, every invocation

1. **The unit's ledger**, if it has one (`docs/PROGRESS.md` for cosmo, `docs/plan.md` for solaris,
   `docs/piano-physics-progress.md` for the piano). **NEXT** says what to do.
2. **The unit's defect list**, if it has one (`docs/DEFECTS.md`). Your task may already be filed,
   diagnosed, and have a recommended fix waiting.
3. **The requirement that governs the change** (§2). Not the code — the requirement.
4. **`docs/DEVELOPING.md`**, if it has one. Its §3 "Which document decides what" table routes every
   later question.

**If the ledger disagrees with the repo — a task marked `[ ]` whose code exists, a **NEXT** that is
already done — reconcile the ledger to reality first and say so.** A stale ledger is the one
failure that breaks work spread across sessions and machines.

---

## 1. Law 1 — the core is the product; every front end is a client

`docs/vision.md` §7 already says this for the whole suite, and says why:

> Every app is specified **CLI-first** so the shared model, versioning, embedding, and rendering
> can be built and tested headlessly (scriptable, CI-able, no UI dependency) before an Artboard UI
> is layered on… The CLI is not a throwaway: it is the automation surface (batch renders, pipeline
> hooks, merge tooling) that survives after the UI ships.

**The rule is not "write a CLI too". It is that the GUI must not be able to do anything the CLI
cannot.** A behaviour reachable only by clicking is a behaviour no agent, no script, no test and no
second front end can reach — and it is how a core stops being the product.

### The shape

```
   host          window, dialogs, fonts, file I/O, codecs, sockets      ← knows everything
   ────────────────────────────── seam ──────────────────────────────
   front ends    GUI · CLI · control socket · tests    (peers, not layers)
   ────────────────────────────── seam ──────────────────────────────
   service       Command in · AppModel + Event out                      ← THE application
   engine        parameters in, pixels/samples out                      ← knows nothing above it
```

- **One way in.** A tagged `Command` struct with one kind per behaviour. A front end never calls
  the session, never mutates the parameter struct, never opens a file.
- **One way out.** An observable `AppModel` of plain data plus an `Event` stream. No pixels, no
  samples, no UI types in the model.
- **One codec, generated from the struct** — `parse`/`format` for commands, `format` for events.
  Two representations maintained by hand drift the first time one grows a field.
- **The layers only look down.** The engine knows nothing of the app; the service knows nothing of
  the toolkit; a codec, an OS path or a `getenv` below the host is a layering bug.

**Where a behaviour belongs is not decided by whether it "feels like logic".** It is decided by:
*would a second front end have to reimplement it to give the same answer?* Easing a progress bar
toward 6-of-18 is the view's. Deciding that 6 of 18 have arrived is the service's.

### How you PROVE it — the equivalence test

An architecture rule that is only asserted is a rule that is already broken somewhere you have not
looked. Cosmo proves this one, and every app must:

**Run one committed script through both front ends — headless, and through a live window over the
control socket — assert the event stream, and diff the two stable state dumps.**
`apps/cosmo/tests/acceptance/run.sh` is the reference. It found a real defect (**D-16**) the first
time it ran, and it is the only check that can see a widget quietly stop routing through the
service.

Two properties make it worth copying rather than admiring:

- **It drives the ordinary product surface.** `--control`, `cosmo-cc attach`,
  `cosmo-cc project --print`. There is no test-only code path anywhere in it — a harness with a
  private back door proves the back door works.
- **It is committed and re-runnable.** From its own header: *"before this existed the acceptance
  evidence lived only in a commit message — and a claim you cannot re-run is a claim, not a test."*

The dumps compare equal only because the codec excludes what is a property of *when* the dump was
taken rather than of the state (revision counters, frame sequence numbers, measured peaks). Keep
that exclusion list honest: excluding a field to make a test pass is falsifying the test.

---

## 2. Law 2 — requirements first, and the conflict rule

> **Before writing code, find the requirement that governs the change.**
>
> - **It exists and agrees** → implement it, then update its as-built entry to the new truth.
> - **It exists and conflicts** with what is being asked → **stop and resolve the conflict in the
>   document.** Amend the entry in place, marked `**AMENDED (<what forced it>, <date>)**` with a
>   line of why. Never leave two requirements that disagree, and never let code silently win over
>   a written requirement.
> - **It does not exist** → **write it before any code**, then check it against every existing
>   requirement in the same area for conflict before you accept it.
>
> A change with no requirement is not allowed to ship, and neither is a requirement that no longer
> matches the code.

That second failure has an id: **D-1**. A doc describing a seam the code no longer has is a defect,
not untidiness.

### Two tiers, and why one is not enough

| | intent tier | as-built tier |
|---|---|---|
| answers | what was asked for, why, and what happened to it | what the code contractually does **today** |
| written | **before** the code | **with** the code |
| voice | normative — shall / must / may not | descriptive present, with `file:line` anchors |
| status | in the section heading | none — an entry is current or it is a bug |
| history | never deleted; amended in place; withdrawn areas kept collapsed | rewritten in place to the new truth |

A single tier cannot do both jobs: the moment you rewrite a requirement to match what shipped, you
have destroyed the record of what was asked for — and the moment you refuse to rewrite it, the doc
stops describing the code. **Cosmo is the reference** (`apps/cosmo/REQUIREMENTS.md` = `R-<AREA>-<n>`,
`apps/cosmo/docs/requirements.md` = `DR-<AREA>-<n>`, the DR heading citing its R tag).

### ID discipline

- **Stable, never renumbered, never reused.** Insert with a letter suffix (`R-G-1a`, `DR-BROWSE-2a`),
  never by shifting the numbers below.
- **A withdrawn id is retired, not recycled** — including any wire-format value it owned. See the
  `R-AISEG` withdrawal: type value `4` is retired forever, because re-pointing it at a live type
  would turn a mask that does nothing into a mask that does something *wrong*.
- **The prefix may be the app's** (`R-`/`DR-` in cosmo, `SR-` in solaris, `FR-`/`NFR-` in Artboard,
  `G-`/`NG-` in genesis). The prefix is not the rule; **two tiers and stable ids are the rule.**

### The markers, with their real shapes

```markdown
## R-CPU — A CPU budget, so the machine stays usable — ⚠️ REOPENED (D-41; was D-11, D-12)
## R-AISEG — A mask that finds its own subject — ❌ WITHDRAWN (2026-09-03)

- **R-SVC-3 …** (**AMENDED (R-SVC-10), 2026-08-17:** (a) and (b) are one budget divided, not two
  budgets each taking the whole percentage — D-11. …)
- **R-MIXER-10 …** (**Added 2026-09-03. AMENDS the `y -> l += y*0.5` contract.**)
```

An amendment says **what was wrong, what evidence proved it, and what replaces it** — and cites the
defect id where there is one. A withdrawal needs four parts: the trigger (quote the user), a "what
was tried, so nobody tries it again" post-mortem, an explicit **"what the withdrawal must not
break"**, and the old requirements kept in a collapsed `<details>` block ending **"None of them is
in force."**

### Every unit carries a doc-authority table

`apps/cosmo/docs/DEVELOPING.md` §3 is the model. One table, one row per question a newcomer asks,
so no one has to guess which of seven files decides. It costs ten lines and it is the single
highest-leverage doc in the repo.

**And status lives in the ledger, never in the how-to doc.** From that file, verbatim: *"If the two
disagree, the ledger wins and this file is the bug."*

---

## 3. Law 3 — the V-model: doc, design, code and tests move together

**Nothing ships unless all four move together. Skipping a stage is the bug.**

Left side specifies and designs, top-down. Right side builds and verifies, bottom-up. Each right
stage validates the left stage at its own level. **Do not start a stage until the left-side artifact
above it is updated.**

### The loop, every invocation

1. **Orient** (§0).
2. **Scope ONE task.** Take the single next unchecked item under **NEXT**, or the task the user
   named. Mark it `[~]`. One task per session — they are sized for that.
3. **Requirements first** (§2). No code until the requirement is written and conflict-checked.
4. **Design docs next** (§3.1) — architecture / detailed design / diagram, *before* the code.
5. **Implement**, matching the surrounding style. This codebase comments *why*, not *what*.
6. **Make it reachable with no GUI** (§5).
7. **Verify** (§4) — and check the new test fails without the fix.
8. **Sync check** (§3.2). Anything that lags means not done.
9. **Ledger + defects** (§6), then **commit** (§7).

### 3.1 The design artifacts, and what triggers each

| artifact | update it when | the violation |
|---|---|---|
| `architecture.md` | a module is added/moved/removed; a **seam** changes; the threading or data-flow story changes | a source file with no module-map row |
| module map (inside it) | every new file — with its layer and the requirement tag that justifies it | a file nothing justifies |
| `detailed_design.md` | a class is added or its real signatures/constants/callbacks change | a constant in the doc the source no longer has |
| `architecture.puml` | a new public type, a new seam, a new field on a modelled struct | **a class in code with no box in the puml is an unsynced design** |
| `design.md` (rationale) | a decision is *reversed* | a sentence stating the decision you just reversed, left standing beside the new one |
| backlog (`PARITY.md`, `plan.md`) | a row is closed or descoped | a row that says "Missing" about something that shipped |

### 3.2 The sync check — if any of these lags, you are not done

- [ ] intent tier — the entry exists, is conflict-checked, heading status current
- [ ] as-built tier — describes the code **as built**, cites its intent tag, `file:line` anchors live
- [ ] `architecture.md` — module-map row; any seam or threading change
- [ ] `detailed_design.md` — real signatures, real constant values
- [ ] `architecture.puml` — no class without a box
- [ ] `design.md` — no sentence left standing that the change reversed
- [ ] backlog row updated if this closed or descoped one
- [ ] the API document regenerated and matching (§5)
- [ ] tests updated and passing; the harness still builds

---

## 4. Verify — pick the lowest level that actually proves it

- **L0 unit** — plain asserts in the unit's own suite, in its existing style. Do not introduce a
  framework.
- **L1 numeric** — assert on real output: a mean channel value, a resampled curve at a known x, a
  conserved quantity, byte-identical output across two code paths.
- **L2 the real service, headlessly** — construct the service with a fake decoder/device, dispatch a
  command script, pump to a fixed tick, assert on the model, on the stable dump, or on the collected
  event lines. No window, no clicking. **This is usually the right level and it is stronger evidence
  than reading code.**
- **L3 the shell front end** — the actual CLI invocation, run, with its output pasted into the commit.
- **L4 randomized / concurrent** — for anything touching a pool, a worker or a lock: randomized
  trials with a stall deadline, and a sanitizer run where available.
- **L5 live** — build and run the real app. The only way to catch a wiring mistake between host and
  service, and required for anything the user will see.

**Prefer a measured assertion to an argued one.** If your evidence is a sentence about what the code
does, you are at the level that shipped D-11 and D-12.

**The new behaviour needs a test that fails without the fix — check that, do not assume it.** Run it
against the un-fixed code first.

**`0 failed`, and every unit registers with the root `ctest`.** A test that is not registered does
not exist. If a line is genuinely unreachable, delete the dead code rather than fake a test.

---

## 5. Law 4 — agent-drivable, and the API document

**Rule: anything you implement must be reachable, inspectable and assertable from a shell, with no
GUI in the loop. If the only way to exercise your change is to click something, you have not
finished.**

Concretely, for every feature named in a requirement, an agent must be able to:

| | how |
|---|---|
| **discover** it | the API document (below) — without reading C++ |
| **reach** it | one `Command`, in the shared text grammar |
| **observe** it | an `AppModel` field, or an `Event` line |
| **assert** it | a stable dump diff, or an `expect` on an event prefix |

A behaviour a front end can reach that no `Command` expresses is a defect in the enum, not a licence
to reach past it. Adding a getter, or reaching through a transitional `session()` accessor, is how
this law is broken in practice — so grep for those before you claim it holds. (Cosmo's own audit:
`App.cpp` still has 105 `mSession.` call sites against 7 dispatches, and `cosmo-cc` reaches past the
service twice, both admitted in comments. The law is a direction of travel with a measurable
distance remaining, not a box that is already ticked.)

### An unknown input is REJECTED, never accepted quietly

**A command that accepts a key it does not understand and reports success is worse than one that
crashes.** An agent reads the success, believes the edit landed, and builds everything after it on a
state that never changed.

This is live in cosmo today: `set exposre=1.2` (typo) returns success and emits
`[evt] params.changed exposre`, because the parameter parser marks "something was parsed" on any
line containing `=` before it tests whether the key exists, and its `if/else` chain has no final
`else`. Filed as **D-59**.

So: every parser reports **which** inputs it consumed, every unmatched key is an error naming the
key, and the rejection reaches `lastError` as well as the event stream. The same rule covers a
subcommand with no argument hint and a `wait` condition one front end understands and another does
not — an agent cannot branch on a difference it cannot see.

### Observability is not logging

**Emit an `Event`; do not add a log call.** The formatted event *is* the log line — the same text
the journal records, `--watch` streams, the socket sends, and an `expect` step matches. One
mechanism, so a thing that happened cannot be visible in one channel and invisible in another.

- Emit the numbers you claim. A quantity a front end cannot read out of the model or the stream is
  not observable, whatever the requirement says. (R-CPU-4 was once "satisfied" by a log line nobody
  had ever run.)
- **Line shapes are an interface.** Once a doc or an assertion quotes one, it is frozen: add fields
  at the end, never reorder or rename.
- A rejected command emits a rejection **and** lands in `lastError`, so a failure is inspectable
  after the fact by a front end that was not listening.

### The API document — generated, committed, and tested

This is the piece the suite does not have yet, and the reason an agent must currently read C++ to
learn the grammar. `DEVELOPING.md` §5 is excellent prose and it is **not** a substitute: prose
cannot fail a build when it drifts, and everything hand-maintained here has drifted at least once.

**Every app with a service exposes `<app>-cc api [--json]`**, which prints, from the code itself:

- every command: name, its text-form grammar line, its arguments and accepted fields;
- every event: name, and its formatted field list;
- every `AppModel` field name and type, flagged if excluded from the stable dump;
- the app's own vocabulary where it is enumerable (parameter keys, node types, file extensions).

It is generated from the same tables the parser uses — the command-name list, the event-name
function, the model codec — so it **cannot** describe a command the app does not have, and cannot
omit one it does.

Then:

1. **Commit the generated form** — `docs/api.json` (machine) and `docs/API.md` (rendered) — so an
   agent can read it without building the app. Generated covers the *catalogue*; the argument
   hints, types, ranges and units must be generated too, or they rot at a different rate from the
   names beside them. Cosmo's `--help` proves the point: the command names come from
   `commandNames()` and are always right, while the hand-maintained argument hints beside them are
   missing for 8 of 30.
2. **A test regenerates and diffs.** It fails on drift. This is what makes the document trustworthy
   and is the entire difference between this and the prose that preceded it.
3. **Load it before you test.** An agent driving the app reads `docs/api.json` first and builds its
   script from that, not from memory and not from the source. If a feature you need is not in the
   API document, that is the finding — file it (§6) rather than reaching around it.

### The conformance ladder

No app but cosmo is near the top. **State which rung the unit is on, and build the next one** —
do not pretend the whole ladder is a prerequisite for any work at all.

| rung | what it means | who has it |
|---|---|---|
| 0 | spec only | interstellar, pulsar |
| 1 | core split out; `Command`/`Event`/model with one text codec | solaris (specified) |
| 2 | registered with the root `ctest`; L2 headless service tests | genesis, arstrobench |
| 3 | a real CLI front end that is the whole app without a window | genesis (partial), cosmo |
| 4 | generated API document, committed, drift-tested | **nobody yet** |
| 5 | control socket + the equivalence test (§1) | cosmo |

---

## 6. Ledger, defects

**The ledger is the only authority on status**, it is committed, and it is what lets work resume on
another machine. Legend: `[ ]` not started · `[~]` in progress · `[x]` done and verified · `[!]`
done but UNVERIFIED — and `[!]` obliges you to say exactly what is unverified and why.

In the same commit as the work: tick the task, **rewrite the NEXT line**, update the "last updated"
note, and add anything that departed from the plan to the **Decisions log (newest first)** so no
other session re-litigates it.

**Do not skip the ledger update. A finished task whose ledger was not updated is the single failure
that breaks resuming.**

**Defects** are `D-<n>`, sequential across the unit, **never reused**, nothing ever deleted; a
resolved entry moves whole to `## Closed` with its commit hash and the test that now guards it. The
entry format is defined in `arstro.cosmo.core.debug` §4 — read that section for the format; it is
not repeated here.

Two rules bind defects to requirements: a `Judgement` of **requirement gap** must produce a new
requirement, and a defect may change a requirement's status (`⚠️ REOPENED (D-41)`).

**The debug/implement boundary is absolute.** A debug skill reproduces, judges, files and
*recommends* — and stops. Only an implement skill changes product code. When you pick a defect up,
the diagnosis and the recommended fix are already written: read the entry before re-deriving it.
You may disagree, and should say so in the commit — but a recommendation you silently ignore
usually means the entry knows something you have not read.

---

## 7. Commit

One focused commit per completed task, on `main`, **including the doc and ledger updates**. Never
batch two features. Do not open a side branch unless the skill you are running says to.

**Then pull, and push. Every task, without being asked.**

(**AMENDED 2026-09-03.** This rule used to be *"do not push unless asked — remind the user at the
end of a session"*, and every skill carried a copy of it. It was wrong for the same reason a stale
ledger is wrong: **this whole discipline exists so work resumes on another machine**, and a commit
that only exists on this one defeats that exactly as completely as a `[x]` nobody ticked. Reminding
the user at the end of a session put the last and most failure-prone step of the loop outside the
loop.)

```
<unit>: <lowercase sentence naming the user-visible effect> (<intent tags>, <as-built tags>)

<Symptom, then cause, then fix, then how it was proven.> Bold lead-ins per issue when
there are several. Paste the command and the output that proves it.

Co-Authored-By: <the model writing it> <noreply@anthropic.com>
```

**Take the trailer from the harness instruction in force, not from a copy in a skill file.** Every
skill that hard-coded one has drifted — three different values across eleven files is what proved
it.

### The order, and why it is not negotiable

`core/Artboard` and `core/DigitalSignalProcessing` are **git submodules** (on `feature/1.0.0`, not
`main`). `core/ImageProcessing` is *not* one in this checkout despite the nested `.git`; it commits
with the umbrella.

**A submodule is finished — committed, pulled, tested and PUSHED — before the umbrella records its
pointer.** Not because it is tidier: a pointer bump names a specific commit hash, and a rebase
during a later pull rewrites that hash. Record the pointer first and you have committed the umbrella
to a commit that no longer exists on any branch, which every other machine then fails to check out.

```
in each changed submodule:      commit → pull --rebase → re-run its tests → push
then, in the umbrella:          stage the pointer bump + your files → commit
                                → pull --rebase → re-run ctest → push
```

Forgetting the umbrella's pointer bump is this repo's most common mistake; pushing it before the
submodule is the one that breaks other people rather than yourself.

### Pull before you push, and test after you pull

`git pull --rebase` — this history is linear and a merge commit per push would bury it.

**Then re-run the tests, before pushing, if the pull brought anything down.** A rebase produces a
state that neither you nor the other author has ever built: your change against their code. "Both
sides were green" is not evidence about the combination, and the combination is what you are about
to publish.

**Never force-push over a commit you did not write.** A conflict is resolved, re-tested and pushed
forward — a conflict is information about work someone else did, not an obstacle.

### When NOT to push

Three cases, and they are the only ones:

- the skill you are running names a different branch (`arstro.dsp.implement.experimental` commits to
  `experimental` and pushes *that* — it must never merge or fast-forward `main`);
- **the tests do not pass.** An unpushed red commit is a local problem; a pushed one is everybody's.
  Say so plainly rather than pushing and mentioning it;
- the user has said not to, this session.

---

## 8. Law 5 — update these rules when you learn something

**When a rule proves itself worth stating, put it here rather than in the skill you happened to be
running.** That is the whole reason this file exists: the same eight rules were rediscovered and
re-written in eleven places, and each copy drifted.

Add a rule here when **any** of these is true:

- you found yourself about to copy a paragraph from one skill into another;
- a mistake cost real time and the lesson is not specific to one app;
- two units answered the same question differently and one of them is right;
- a rule in a specific skill turned out to apply to every unit.

**When you add one:** state it as an imperative, justify it with the scar that produced it (name the
defect id or the measurement), and delete or replace the copies it supersedes — a hoisted rule that
leaves its duplicates behind has made the problem worse. Say in the commit that you changed the
rules and why.

**When you find a rule here that is wrong**, fix it here. Do not work around it locally: the next
session will hit the same wall and will not know you already found it.

### A skill may not name a tool that does not exist

**Before you write "run X" into any skill, run X.** A skill is read by an agent that will believe
it, and an instruction to use a flag that was only ever proposed costs that agent a whole
investigation to discover it is fiction.

This was not hypothetical. Four things the cosmo skills told agents to use — `--dump-ui`, a GUI
`--script` file, `cosmo_shots --size`, and an `expect` verification step — **have never existed**;
one of them (`ui dump`) was deliberately built as a socket command instead and the ledger records
that decision, while the skill kept instructing the flag. The same class of rot hits docs: an
as-built entry claiming 22 command kinds where the coverage test counts 30.

So when a skill names a command, a flag or a file: check it resolves, and if it does not, either
build it or write it as a gap with the reason — never as an instruction.

---

## 9. Definition of done

- [ ] The requirement was read first, written or amended, and conflict-checked — **before** the code.
- [ ] Both tiers updated; ids stable, never renumbered, never reused.
- [ ] Docs, diagram and backlog in sync (§3.2). A doc describing a seam the code no longer has is
      a defect with an id, not untidiness.
- [ ] Behaviour landed in the core, not in a view. Nothing new reaches past the service seam.
- [ ] Reachable, inspectable and assertable **with no GUI** — the exact command is written down.
- [ ] The API document regenerated; its drift test passes.
- [ ] `ctest` reports `0 failed`, and the new test fails without the fix.
- [ ] The equivalence test run, if the change touched the service, a front end's outbound path, or
      the grammar.
- [ ] Ledger ticked and **NEXT** rewritten; defect list updated with the commit hash and the
      guarding test.
- [ ] Committed to `main`, **pulled, re-tested, and pushed** — submodules fully pushed *before* the
      umbrella records their pointers.

---

## Appendix — who invokes this

Every Arstro skill invokes `arstro.rule` first, then applies its own:

| skill | adds |
|---|---|
| `arstro.design.rule` | the design law — invoked first by every design skill |
| `arstro.cosmo.core.implement` | cosmo's service/engine map, R-SVC invariants, the add-an-adjustment recipe |
| `arstro.cosmo.design.implement` | cosmo's design values + process (on top of `arstro.design.rule`) |
| `arstro.cosmo.core.debug` · `arstro.cosmo.design.debug` | reproduce/judge/file, and the `D-<n>` entry format |
| `implement_artboard` | the HAL seam rule, RecordingTarget assertions, 100% core coverage |
| `arstro.dsp.implement` (+ `.experimental`) | compose-don't-duplicate, derive the math, document every equation |
| `arstro.piano.implement` | which milestone is next, and how to measure it |
| `android.theme.implement` | the shell milestones and its own ledger |

**Known gaps this file does not paper over.** `core/ImageProcessing` has no requirements document
at all while its `design.md` claims V-model discipline — its requirements live in cosmo's files
today. Genesis has one requirement tier where it needs two. Solaris has a third id convention
(`SR-` + phase tags) and no as-built tier. Nobody is on rung 4. Each is a real task, not a
formatting complaint.
