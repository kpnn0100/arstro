---
name: arstro.interstellar.implement
description: Use to implement or resume ANY work in the Interstellar video editor — the InterstellarService layer (Command/Event/AppModel), the .isp project format, the timeline cut operations, the parameter address space and its generated registry, automation clips and links, expression bindings over Gene, the evaluator and the compositor, the hosted Cosmo rack that owns colour, the interstellar-cc CLI, AND the whole UI (App, widgets, Theme, the shot harness). Runs the V-model with both requirement tiers in sync, verifies headlessly by driving the real service and by rendering real frames to PNG, records progress in a committed ledger, and commits. Invoke for "add a command", "add a cut operation", "automate a new parameter", "the binding resolves wrong", "add a panel/widget", "the timeline zoom snaps", "continue interstellar", "/arstro.interstellar.implement". NOT for diagnosing a reported bug without fixing it — that is arstro.interstellar.debug.
---

# arstro.interstellar.implement

> **Invoke `arstro.rule` first, then `arstro.design.rule` when the task touches a pixel.** They
> carry the core/front-end split, requirements-first and the conflict rule, the V-model doc-sync
> loop, the agent-drivable surface, the ledger/defect/commit conventions, and the whole design law.
> **Follow all three; where they overlap, this file's checklist is the one to satisfy** — except on
> the architecture, requirement and agent-drivability laws, where `arstro.rule` wins, and on the
> motion, token and overflow rules, where `arstro.design.rule` wins.

The **resume-driven V-model workflow for everything in Interstellar.** All state lives in committed
files, so a session on any machine can pick up exactly where the last one stopped.

**One skill, both halves.** Cosmo splits core from design across two implement skills because it has
~40 widgets and a 172 KB defect file; Interstellar is young enough that the split would cost more
than it saves, and its two halves share the address space. **But the two-commit rule still holds: a
task spanning core and UI is core first, then UI.**

**You are the only skill that changes product code for a defect.** `arstro.interstellar.debug`
never fixes anything: it reproduces, files and *recommends*. When you pick a defect up, the
diagnosis, the measurement, the cause with `file:line`, the recommended change and the test that
should guard it are already written in `docs/DEFECTS.md` — **read the entry before re-deriving any
of it.** You may disagree, and should say so in the commit; a recommendation you silently ignore
usually means the entry knows something you have not read.

**You own:** everything under `apps/interstellar/`, plus `core/Gene/` (the shared binding language
Interstellar promoted out of `genesis_core`).

**You do not own:** `apps/cosmo/` — the rack's colour authority. A change Interstellar needs there
(a video source, a plumbed-through memory cap) is `arstro.cosmo.core.implement`'s commit, in
Cosmo's own requirements. Nor `core/Artboard` (that is `implement_artboard`, a submodule).

---

## 0. Orient — read these, in this order, every invocation

| # | File | Why |
|---|---|---|
| 1 | `apps/interstellar/docs/PROGRESS.md` | The ledger. **NEXT** says what to do. |
| 2 | `apps/interstellar/docs/DEFECTS.md` | Open defects — yours may already be filed with a fix recommended. |
| 3 | `apps/interstellar/REQUIREMENTS.md` | **Intent**, `R-<AREA>-<n>`, status in the heading. |
| 4 | `apps/interstellar/docs/requirements.md` | **As-built**, `DR-<AREA>-<n>`, with `file:line`. |
| 5 | `apps/interstellar/docs/binding.md` §6 | **The frame pipeline order. It is a contract.** Read it before touching anything that produces a value. |
| 6 | `apps/interstellar/docs/project-format.md` §4–5 | The address space and the registry — what every `set` routes through. |
| 7 | `apps/interstellar/docs/architecture.md` §9 | The module map: where a file goes and the R-tag that justifies it. |
| 8 | `apps/interstellar/docs/design.md` | Why it is shaped this way, and the seven known risks. |
| 9 | `apps/interstellar/docs/ui-brief.md` | Only when touching a widget. |

Reconcile a stale ledger to reality first, and say so.

### Where the code is

```
apps/interstellar/
  core/                       interstellar_core — UI-FREE. No Artboard, no GTK, no codec.
    Project.{h,cpp}             the .isp document: 10 node types, canonical text, the fixed point
    Timeline.{h,cpp}            the cut operations — add/trim/split/move/roll/slip/ripple
    ParamRegistry.{h,cpp}       THE ADDRESS SPACE. Generated. The router for every `set`.
    Automation.{h,cpp}          AutoClip (shapes) + AutoLink (placement/mapping/mode) + the lint
    BindingGraph.{h,cpp}        compiled Gene expressions, derived deps, cycle refusal, topo order
    Evaluator.{h,cpp}           steps 1-4 of the frame pipeline. PURE.
    Composite.{h,cpp}           steps 6-8: geometry, blend modes, transitions
    RackAccess.h                the seam onto the hosted Cosmo project. A fake is 3 lines.
    service/
      InterstellarService.{h,cpp}  THE application: dispatch / pump / model / renderFrame
      Command.{h,cpp}              the one way in + the generated codec + commandSpecs()
      Event.{h,cpp}                the one way out; formatEvent() IS the log line
      AppModel.h                   the whole observable state, plain data
      AppModelCodec.{h,cpp}        formatModel(m, {stable,json,resolved}) — R-SVC-9's diff
    tests/coreTests.cpp         28 L2 tests: the real service, headless, in milliseconds
  cli/main.cpp                interstellar-cc — argv, stdout, a PPM writer, NO behaviour
  Theme.h                     ALIASES cosmo's token namespaces. Never a copy.
  App.{h,cpp}                 the shell: 4 workspaces, one monitor, the Command seam
  widgets/                    Monitor · TimelineView · LaneStack · Transport · WorkspaceBar
  tests/shots/renderShots.cpp interstellar_shots — every named state to a PNG, with --size/--script/--tree
  tests/ui/uiTests.cpp        interstellar_ui_tests — assertions over the ASSEMBLED app
core/Gene/                    gene_core — the shared binding language (promoted from genesis)
```

### The invariants — break one and something lies silently

- **Colour never leaves the rack.** No `EditParams`, no curve, no LUT in the `.isp`. A clip
  references a rack node; the colour is that node's (R-COSMO-2). `Project::fieldIsColour` REFUSES
  a colour key on a clip rather than ignoring it — ignoring it would silently discard a user's
  edit. **The test for a new field is: would a second front end need it to draw the same frame?**
- **One authority per address** (R-EVAL-1): a binding, else an active link, else the static value.
  The model **refuses** configurations where two apply, so the evaluator never has to choose. If
  you find yourself writing a precedence rule, you are about to break this.
- **The frame pipeline order is a contract** (`binding.md` §6): time → automation → bindings → rack
  composition → colour → geometry → composite → output. Automation before bindings is what lets an
  expression read an automated value; both before composition is what stops a group's offsets being
  folded from a value that was going to change. **Any other order is a different picture**, so a
  change here is a requirement amendment plus a re-baked golden set.
- **`Evaluator::resolve` is PURE.** No global state, no cache surviving a parameter change, no
  dependence on evaluation history. R-NFR-1 and every golden test rest on it.
- **The frame-cache key hashes every resolved value that fed the layer**, not the parameter struct.
  A key that missed a shape a link maps in would serve a stale frame, which is indistinguishable
  from a rendering bug.
- **Parse → serialize is a byte-exact fixed point** (R-FMT-4), and unknown keys are preserved in
  place. `Project::roundTripsExactly` is the guard; it is the cheapest test in the project.
- **A structural error REFUSES; a numeric corruption REPAIRS and reports.** A colour field on a
  clip is refused; a stray `nan` is neutralised with a count (cosmo's D-36). Both behaviours are
  requirements, and they are deliberately different.
- **An unknown input is rejected NAMING it, with the nearest candidates** (R-SVC-6). Cosmo's D-59
  — `set exposre=1.2` returning success — matters far more here: the address space is 153 names
  deep in an empty project and a typo is weekly.
- **A bind name is Interstellar's, not Cosmo's.** Cosmo names a node after its file or after what
  the user typed ("Tokyo Night"), and neither is a legal address. `Project::ensureRackObj` derives
  one and it is STABLE once assigned, because an expression spells it.
- **A rename rewrites every reference atomically** — automation targets and expression text, on
  whole identifiers only (renaming `gr1` must not touch `gr10` or `min`).
- **`interstellar_core` links `gene_core` and nothing else.** No Artboard, no GTK, no codec, no
  `getenv`. That is what keeps the core suite at 0.03 s and runnable with no display.
- **`Theme.h` ALIASES cosmo's tokens and the build compiles `cosmo/Theme.cpp`.** A forked token
  file is a divergence with a delay fuse — genesis proved it.

---

## 1. The loop (every invocation)

1. **Orient** (§0).
2. **Scope ONE task**, mark it `[~]` in the ledger.
3. **Requirements first** (§2) — no code until the requirement is written and conflict-checked.
4. **Design docs next** (§3) — architecture / detailed_design / puml, *before* the code.
5. **Implement** (§4), respecting the invariants.
6. **Make it shell-reachable** (§5) — a `Command` in, an `AppModel` field or `Event` out.
7. **Emit an `Event`** (§6) — which is how it gets logged, watched and asserted.
8. **Verify** (§7). `0 failed`, at the level the task demands. **Look at a frame if it draws.**
9. **Sync check** (§8).
10. **Ledger + defects, then commit** (§9, §10).

---

## 2. Requirements first — and the conflict rule

Two tiers, both authoritative: `REQUIREMENTS.md` (`R-<AREA>-<n>`, intent, status in the heading)
and `docs/requirements.md` (`DR-<AREA>-<n>`, as-built, `file:line` anchors, citing its R-tag).

> - **Exists and agrees** → build it, then refresh its `DR-` entry.
> - **Conflicts** → resolve it **in the document** first: amend the `R-` entry in place, marked
>   `**AMENDED (<what forced it>, <date>)**` with one line of why. Never ship code that contradicts
>   a written requirement, and never leave two requirements that disagree.
> - **No requirement covers it** → **write it before any code**, then conflict-check it against
>   every requirement in the same area.

**Two amendments already stand as house-style examples** — read them before writing your first:
**R-COSMO-4** (carrying `cosmo::AppModel` by value would have put GTK3 on every file in the library
and every test, so the rack reaches the model through the `RackAccess` seam instead) and the
**`#rackobj` node** (bind names and the grade weight needed somewhere to live, and the `.cmp` is not
it). Both were forced by *implementing* the specification, which is the normal way a requirement
gets amended.

---

## 3. Design docs before code

- `docs/architecture.md` — a module is added/moved, a **seam** changes, or the threading story
  changes. The **module map** must list every new file with its layer and its R-tag.
- `docs/detailed_design.md` — a class is added or its real signatures/constants change.
- `docs/architecture.puml` — **a class in code with no box is an unsynced design.**
- `docs/project-format.md` — a node type, a field, or an **address** changes. This one is normative:
  the validator and the generated API document are built from it.
- `docs/binding.md` — anything about how a value is produced.
- `docs/design.md` — only when a decision is *reversed*; then fix the sentence that stated the old
  one rather than leaving it beside the new behaviour.
- `docs/ui-brief.md` — when the UI changes.

---

## 4. Implement — the recipes

**Match the surrounding style: this codebase comments *why*, not *what*, and each file opens with a
block comment stating its seam rule.**

### 4.1 A new parameter (the commonest task)
One entry in `ParamRegistry::build()` — object kind, filter, leaf name, type, unit, min/max/default,
automatable, bindable, owner — and **that is the whole of it** for the address space: `set`, `eval`,
completion, the lane list and the API document all read the registry. Then a case in
`Evaluator::staticValue` and one in `InterstellarService::setAddress`. **If the parameter is
Cosmo's, its leaf name must be `EditParamsIO`'s own key** (R-PARAM-4) — a preset, a `.cmp`, a
`cosmo-cc set` line and an expression must all spell it identically.

### 4.2 A new command
A `Kind`, a parse case, a format case, a dispatch case, a `commandSpecs()` row **with its argument
hint**, and a line in `test_command_text_roundtrips`. The spec row is not optional: cosmo's hints
are hand-maintained and missing for 8 of 30, and this app's `--help` and API document are both
generated from `commandSpecs()`.

### 4.3 A new node type in the `.isp`
A struct in `Project.h`, a parse branch, a serialize branch **in the canonical order**, a `Ctx`
enum value, a lookup, the id-uniqueness scan, `freshId`/`rename` coverage, and a round-trip test
including an unknown key. Miss one and the format round-trips lossily.

### 4.4 A new widget
`arstro.design.rule` §5 is the law. One class per file, filename == class name, a block comment
saying what it is and why, `layout()` non-virtual and called every frame, callbacks as public
`std::function`s named `onVerb`, a setter that never fires its own callback, **published geometry a
test can aim at**, and a **live eased value exposed wherever a test must tell a tween from a snap**.
Reuse `Theme`'s tokens; a bare number is the bug.

### 4.5 Changing the frame pipeline
Don't, unless the requirement says to. If it does: amend `binding.md` §6, amend the requirement,
re-bake the goldens, and say in the commit which frames changed and why.

---

## 5. The agent-drivable surface

**Anything you implement must be reachable, inspectable and assertable from a shell with no
display.** All of this exists today — checked, not asserted:

```bash
cmake -S . -B build && cmake --build build -j
./build/apps/interstellar/core/interstellar_core_tests     # 28 L2 tests, 0.03 s
./build/apps/interstellar/cli/interstellar-cc help         # the generated grammar
./build/apps/interstellar/cli/interstellar-cc api --json   # the generated API document
./build/apps/interstellar/cli/interstellar-cc run --script f.txt --watch
./build/apps/interstellar/interstellar_ui_tests            # the assembled app
./build/apps/interstellar/interstellar_shots --outdir /tmp/s --check [--size 1024x640] [--script f] [--tree]
ctest --test-dir build                                     # 21 suites
```

| | how |
|---|---|
| **discover** | `interstellar-cc api --json` — commands, events and the whole address space |
| **reach** | one `Command`, in the grammar of `project-format.md` §8 |
| **observe** | an `AppModel` field, or an `Event` line from `--watch` |
| **assert** | `state print --stable` diffed, or **`eval <address> --at <t>`** |

**`eval` is the unit of evidence for automation and bindings.** A resolved-value table asserted
through it catches a mapping error, a mode error, an evaluation-order regression and a broken
expression — with no display, no codec and no tolerance.

**Two gaps, stated rather than papered over:** there is no control socket yet (R-SVC-8) and
therefore no GUI/headless equivalence test (R-SVC-9) — `test_two_services_dump_the_same_state`
covers the service half. And `render` writes PPM only; real codecs are the GUI host's FFmpeg job
(R-RENDER-2), which does not exist. Do not write either into a doc as though it does.

---

## 6. Observability — emit an `Event`

`formatEvent()` output **is** the log line (R-SVC-5): the same text `--watch` streams, a journal
records and an `expect` matches. So for new behaviour: add the `Event::Kind`, format it, emit it.
**A rejected command emits `command.rejected` AND lands in `lastError`** — keep both, so a failure
is inspectable by a front end that was not listening.

**Line shapes are an interface.** Once this file, a `DR-` entry or an assertion quotes one, fields
are appended at the end and never reordered.

**Emit the numbers you claim.** A quantity a front end cannot read out of the model or the stream is
not observable, whatever the requirement says — which is why every resolved address at the playhead
is in `AppModel::resolved`.

---

## 7. Verify — pick the lowest level that proves it

- **L0 unit** — plain `assert()` in `coreTests.cpp`'s style. **Undefine `NDEBUG` first**: a Release
  build otherwise turns every assertion into `((void)0)` and the suite prints `[PASS]` while
  checking nothing (cosmo's D-43 — it bit `gene_tests` on its first run in this repo).
- **L1 numeric** — `Composite::blendChannel` on known operands; `AutoClip::value` at known times;
  the frame-time round trip.
- **L2 the real service, headless — usually the right level.** Construct the service with a
  `FakeRack`, dispatch a command script, assert on `model()`, on `formatModel(…, stable)`, or on the
  collected `formatEvent()` lines. `coreTests.cpp` has the `FakeRack`/`Fixture`/`seedProject` trio
  to copy.
- **L3 the CLI** — the actual `interstellar-cc` invocation, run, output pasted into the commit.
- **L4 randomized/concurrent** — for anything touching a pool or a lock. Nothing does yet; the
  first thing that does needs a stall deadline and a sanitizer run.
- **L5 look at it** — for anything that draws: `interstellar_shots`, and **read the PNG**. At two
  window sizes, **mid-transition and at rest**.

**The new behaviour needs a test that fails without the fix — check that, do not assume it.**

**And for anything animated, compliance is established by comparing two frames half a tween apart,
never by reading the code.** Pump ONE frame at a time and keep the **first non-zero** value: the
frame that *starts* a tween legitimately reads 0, and settling first lets a 260 ms fade finish so
the assertion reads 1.0 and passes a snap. `uiTests.cpp` does exactly this three times.

---

## 8. Sync check — if any of these lags, you are not done

- [ ] `REQUIREMENTS.md` — the `R-` entry exists, conflict-checked, heading status current.
- [ ] `docs/requirements.md` — the `DR-` entry matches as-built, cites its R-tag, anchors live.
- [ ] `docs/architecture.md` — module-map row; any seam change.
- [ ] `docs/detailed_design.md` — real signatures, real constants.
- [ ] `docs/architecture.puml` — no class without a box.
- [ ] `docs/project-format.md` — any node, field or address change; §8 for a new command.
- [ ] `docs/binding.md` — anything about how a value is produced.
- [ ] `docs/design.md` — no sentence left standing that the change reversed.
- [ ] `docs/ui-brief.md` — if the UI changed.
- [ ] A new `Command`/`Event` — `commandSpecs()` row with its hint, the round-trip test line, and
      the header's grammar comment.
- [ ] `interstellar_shots` has a shot for every new state; `interstellar_ui_tests` asserts the new
      layout.
- [ ] `ctest` reports `0 failed`.

---

## 9. Ledger + defects

`apps/interstellar/docs/PROGRESS.md`, **in the same commit as the work**: tick `[x]` (or `[!]` with
exactly what is unverified and why), rewrite **NEXT**, update the last-updated note, and add
anything that departed from the plan to the **Decisions log** so no other session re-litigates it.
`apps/interstellar/docs/DEFECTS.md`: close what you fixed with its commit hash and its guarding
test; file what you found.

---

## 10. Commit

One focused commit per completed task, on `main`, including the doc and ledger updates.

```
interstellar: <lowercase sentence naming the user-visible effect> (R-AREA-n, DR-AREA-n)

<Symptom, then cause, then fix, then how it was proven.> Paste the interstellar-cc
command and its output, or the test line, that proves it.

Co-Authored-By: <the model writing it> <noreply@anthropic.com>
```

`core/Artboard` is a **submodule**: a change there is `implement_artboard`'s, and it is two commits
— the submodule fully pushed *before* the umbrella records its pointer. **Then pull, re-test, and
push** — `arstro.rule` §7 has the order and why it is not negotiable.

---

## 11. Definition of done

- [ ] Requirement read first, written or amended, conflict-checked — **before** the code.
- [ ] Both tiers updated; ids stable, never renumbered, never reused.
- [ ] **Colour did not leak out of the rack**, and no `EditParams` is stored outside it.
- [ ] The address space, if it grew, grew **in the registry**, and `api --json` shows it.
- [ ] The evaluation order is unchanged, or amended deliberately with re-baked goldens.
- [ ] Behaviour landed in the service, not in a view; nothing new reaches past `dispatch`.
- [ ] An `Event` covers it and **its line was actually observed**, not just written.
- [ ] Reachable with no GUI, and the exact command is written down.
- [ ] `ctest` `0 failed`; the new test fails without the fix.
- [ ] **If it draws: you looked at the PNG**, at two sizes, mid-transition and at rest — and
      nothing changes in one frame.
- [ ] Ledger ticked, **NEXT** rewritten, defects updated. Committed, pulled, re-tested, pushed.
