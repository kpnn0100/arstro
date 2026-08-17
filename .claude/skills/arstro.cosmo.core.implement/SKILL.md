---
name: arstro.cosmo.core.implement
description: Use to implement or resume ANY non-UI work in the cosmo photo editor — the CosmoService service layer (Command / Event / AppModel, R-SVC), the editing session, history, project/preset/settings persistence, the image-load and decode path (ProjectLoader, ThreadBudget), the export path, and the arstro_image processing engine (EditParams, EditEngine, RenderService, processors, GPU backends). Also owns cosmo's agent-drivable surface (the cosmo-cc CLI, the control socket, the event stream, scripted input). Runs the V-model with both requirement tiers kept in sync, verifies headlessly by driving the real service with no display, records progress in a committed ledger, and commits. Invoke for "add/fix an adjustment", "add a command", "the engine renders wrong", "speed up loading", "add a format", "export is broken", "continue cosmo core", "/arstro.cosmo.core.implement". NOT for widgets/layout/theme — that is arstro.cosmo.design.implement.
---

# arstro.cosmo.core.implement

The **resume-driven V-model workflow for everything in cosmo that is not a pixel on a widget.**
All state lives in committed files, never in a session's memory, so you can stop on one machine,
`git pull` on another, invoke this skill, and know exactly where you are.

**You own:** `apps/cosmo/core/` (`cosmo_core`) — including **`core/service/`**, the `CosmoService` +
`Command`/`Event`/`AppModel` contract every front end binds to — and `core/decode/` ·
`apps/cosmo/ExportWriter.{h,cpp}` · `apps/cosmo/Log.{h,cpp}` · `apps/cosmo/OmpPin.{h,cpp}` ·
`apps/cosmo/cli/` (`cosmo-cc`, S3) · `core/ImageProcessing/` (`arstro_image`) · the host-side *consumer*
of the load in `apps/cosmo/linux_main.cpp` · `apps/cosmo/android/`.

**You do not own:** `apps/cosmo/widgets/`, `App.{h,cpp}` screen composition, `Theme.h`, `apps/cosmo/touch/`.
Those belong to **`arstro.cosmo.design.implement`**. A task spanning both is two commits: core first
(it is the layer below), then design.

---

## 0. Orient — read these, in this order, every single invocation

| # | File | Why |
|---|---|---|
| 1 | `apps/cosmo/docs/PROGRESS.md` | The ledger. **NEXT** says what to do. Read it before anything else. |
| 2 | `apps/cosmo/docs/DEFECTS.md` | Open defects. Your task may already be one; a defect you fix gets closed here. |
| 3 | `apps/cosmo/REQUIREMENTS.md` | The **intent** ledger, `R-<area>-<n>`, status in the heading. The contract. |
| 4 | `apps/cosmo/docs/requirements.md` | The **as-built** spec, `DR-<AREA>-<n>`, with `file:line` anchors. |
| 5 | `apps/cosmo/docs/architecture.md` + `detailed_design.md` + `architecture.puml` | Where a class lives and what it promises. §7 threading and §9 the module map are current as of S1; `detailed_design` §2.4c/§2.4d are `ThreadBudget` and `ProjectLoader`. |
| 6 | `apps/cosmo/docs/service-architecture-proposal.md` | The **design** behind `R-SVC-1…10` and the S1–S5 migration plan. Approved 2026-08-17. Not a ledger — it is not updated per commit, so trust `PROGRESS.md` for status and this for shape. |
| 7 | `apps/cosmo/docs/design.md` | Why the code is shaped this way. |
| 8 | `apps/cosmo/PARITY.md` | The implement-or-descope backlog. |

If the ledger disagrees with the repo (a task marked `[ ]` whose code exists, a **NEXT** that is already
done), **reconcile the ledger to reality first and say so** — then continue. A stale ledger is the one
failure that breaks multi-device work.

### Where the code is

```
apps/cosmo/core/            cosmo_core — UI-free, owns NO artboard type
  service/                    THE application, with no front end attached (R-SVC). Read this first.
    CosmoService.{h,cpp}        dispatch(Command) · dispatchText(line) · pump(nowMs) · model() ·
                                subscribe(sink). Owns the load, the export, settings, recents.
                                Borrows EditSession by reference until S4 moves ownership in.
    Command.h/.cpp              the one way IN. Tagged struct, one Kind per behaviour (22 + None),
                                with parseCommand / formatCommand / commandNames() as the single codec.
    Event.h/.cpp                the one way OUT, with AppModel. eventName() is the dotted name and
                                the log category; formatEvent() output IS the log line.
    AppModel.h                  the entire observable state as plain data: screen, recents, nodes[],
                                selection, params, history, load, exports, settings, budget, frame
                                metadata, revision. No pixels, no Artboard types, no presentation.
    AppModelCodec.{h,cpp}       formatModel(m, {stable, json, params}) — what `state print` emits and
                                what R-SVC-9 diffs. `screenName()` lives here too.
  EditSession.{h,cpp}         THE session. Read EditSession.h first; it is the de-facto spec.
  History.{h,cpp}             a BRANCHING DAG of full EditParams snapshots, one per slot AND per group
  ProjectLoader.{h,cpp}       THE project load: decode pool, per-worker decode + thumbnail, delivery
                              strictly in entry order. Was startEntriesLoad/decodeEntry/pollLoad in
                              linux_main.cpp until S1a; moving it down is what made a load runnable
                              and measurable with no window (R-SVC-1).
  ThreadBudget.{h,cpp}        the ONE owner of the CPU budget: total() and its two slices,
                              plus the MEASURED peakDecode() (R-SVC-10, fixes D-11)
  ProjectStore.{h,cpp}        configDir() + recent.tsv
  PresetLibrary.{h,cpp}       filesystem scan of *.apf into a tree
  AppSettings.{h,cpp}         settings.txt: previewEdge / threads / useGpu / cpuPercent. It no longer
                              converts the percentage — workersFor() survives for one legacy test.
  OrderedParallelLoad.h       produce-on-a-pool, consume-strictly-in-order, dual back-pressure.
                              Now genuinely restartable, and calls a per-worker start hook.
  decode/                     IImageDecoder seam + Native (GdkPixbuf/LibRaw) + Android (stb/LibRaw)
  tests/sessionTests.cpp      cosmo_core_tests — 28 plain-assert() tests, the last six of which drive
                              the real service with a fake decoder and no display
  tests/fixtures/*.c          standalone C fixtures that prove a platform claim (omp_env_order.c,
                              omp_pin.c) — compiled by hand, not by CMake
apps/cosmo/ExportWriter.{h,cpp}   OS I/O + GdkPixbuf encode + JPEG APP1 / PNG sRGB metadata
apps/cosmo/Log.{h,cpp}            the log facility + crash handler (host-side on purpose)
apps/cosmo/OmpPin.{h,cpp}         host: pin one decode worker's nested OpenMP team to one thread, via
                                  dlsym/GetProcAddress so nothing links OpenMP. Namespace is
                                  `arstro::cosmo_v2`, not `arstro::cosmo`. ompPinStatus() is logged at
                                  startup so the mechanism is checked, not assumed (R-CPU-2c, D-12).
apps/cosmo/linux_main.cpp         GTK host: window, dialogs, fonts, logging, the threaded batch export,
                                  and the load's UI-SIDE consumer only — startEntriesLoad now calls
                                  ProjectLoader::start and pollLoad applies results to App. The decode
                                  and the pool are no longer here.
core/ImageProcessing/src/     arstro_image — headless, codec-free, platform-free
  engine/EditParams.h           THE parameter struct; composeParams() is the group-stacking law
  engine/EditEngine.{h,cpp}     slots, flat setters, proxy cache, the render pipeline
  engine/RenderService.{h,cpp}  the threaded seam the UI binds to (coalescing, move-in add)
  engine/EditParamsIO / Apf     text serialization + the .apf preset envelope. `deserializeParams` is
                                the reason `set exposure=1.2` is free — see §4.
```

`core/service/Seams.h` is named in the proposal but **does not exist yet**: `IImageDecoder` is the
only injected seam today, and the service takes the rest as `std::function` wiring
(`setDecoderFactory`, `setWorkerInit`, `setImageWriter`). `IFileStore`/`IClock`/`ITaskPool`/`ILogSink`
are R-SVC-7 and pending — check the ledger before citing them.

### The invariants — break one and something lies silently

**The service invariants (R-SVC).** These are newer than the rest and the whole architecture rests on
them; a violation reads as a working feature and is not one.

- **`Command` is the only way in; `AppModel` + `Event` are the only way out** (R-SVC-2/3). A front end
  never calls `EditSession`, never mutates an `EditParams`, never opens a file. A behaviour a front end
  can reach that no `Command` expresses is a defect in the enum, not a licence to reach past it —
  which is why `CosmoService::session()` is marked *transitional*: every caller of it is a line S4 has
  to delete.
- **A view holds presentation only, and presentation is a real category** (R-SVC-4). Animation, easing,
  transitions, hover and scroll offsets **stay in the view** — R-G-1 is a view requirement and this
  architecture does not touch it. The service knows a load is 6-of-18; the view knows the bar eases
  toward it. Anything a *second* front end would also need is not presentation.
- **The text form is generated from the struct by one codec, never hand-written twice** (R-SVC-5).
  `parseCommand`/`formatCommand` and `formatEvent` are the only grammar. Two representations
  maintained by hand drift the first time one grows a field, and this project already paid for that
  with a log line nobody had ever run.
- **`pump(nowMs)` never blocks, and the service starts no thread on its own initiative** (R-SVC-6). The
  caller drives the clock: a GTK timeout at frame rate, a CLI loop, or a test at a fixed 16 ms tick.
  That single property is what makes a scripted run reproducible and a live run smooth on one code
  path — do not add a `wait`, a `sleep` or a `join` inside `dispatch`.
- **`ThreadBudget` is the single owner of the CPU budget** (R-SVC-10). It converts the percentage
  **once** into `total()` and hands out slices; `apply()` is the only place the budget touches
  `par::setThreads`. Nothing else may convert `cpuPercent` — a second conversion by a concurrent
  consumer is exactly D-11, and `AppSettings::workersFor()` is kept only for one legacy test.
- **An `Event` *is* the log line, so emitting one is logging it.** `formatEvent()` produces the text the
  journal records, `--watch` streams, the control socket sends and the host writes to `cosmo_v2.log`.
  Once a `DR-` entry or an `expect` assertion quotes a line, its shape is frozen (§6).
- **`AppModel` carries no pixels and no Artboard type.** Frames are referenced by slot + size +
  `frameSeq`; the bytes stay in the engine and the view fetches them through `RenderService` as
  before. `formatModel(..., stable)` excludes `revision`, `frameSeq` and `budgetPeakDecode` on purpose
  — a front end that has animated 400 ms longer is not in a different *state*.

**The older invariants, all still in force:**

- **Slot id == index** in every `mSlot*` vector in `EditSession`. `resetWorkspace()` must call
  `mService.reset()` so slot-id assignment restarts, or the next open segfaults.
- **The engine works in linear light.** Decode once at ingest, `color::encodeInPlace()` once at egress.
  A processor that assumes sRGB is a bug.
- **Pipeline order is a contract** (`EditEngine.h`): Crop, Rotate, LensCorrection, NoiseReduction,
  Exposure, Contrast, ToneRegions, WhiteBalance, ToneCurve, Texture, Clarity, Vibrance, ColorMixer,
  ColorGrading, Dehaze, Sharpen, Grain, then masks, then encode. It is split into three chain segments
  only so histograms can be tapped at the boundaries. Reordering is a requirement change.
- **`composeParams` is asymmetric on purpose.** Scalars add; `temp` adds its offset from 6500 and
  `sharpenRadius` its offset from 1; curves stack additively as deviation-from-identity; masks
  concatenate; **`curveLog` and crop follow `base`** — framing is per-item and never stacks. `rotation`
  and `quarterTurns` *do* stack. Changing any of these changes what every existing project renders.
- **`effectiveParams` and `effectiveEditParams` differ deliberately.** The first honours bypass
  everywhere (render truth); the second honours bypass on ancestors but never on the edit target, so a
  panel shows meaningful stacked reach. Do not "simplify" them into one.
- **`RenderService::render()` coalesces** — an older pending preview is overwritten, not queued. Code
  that assumes every submit produces a frame is wrong.
- **`OrderedParallelLoad`'s freedom from deadlock rests on one line**: the item at `i == mConsumed` is
  exempt from both the window bound and the byte cap. Touch the wait predicate and you must re-run the
  60-trial randomized test.
- **The node tree is built before any pixel decodes** (R-LOADUX-1): leaves exist with `slot = -1,
  pending = true`, so out-of-order arrival can never reparent anything.
- **`arstro_image` and `cosmo_core` contain no logging, no asserts, no codec and no `getenv`.** That is
  deliberate — they must stay WASM/Android-portable. Diagnostics belong in the host layer or behind an
  injected seam (`EditEngine::setComputeAccelerator`, `activeBackendName()`, the service's
  `setImageWriter`). **The one standing exception is `ProjectStore::configDir()`**, which reads `HOME`
  and `XDG_CONFIG_HOME` (`ProjectStore.cpp:26,34`) — and it is the exception `IFileStore` (R-SVC-7) is
  meant to remove. Do not add a second one; do not cite it as precedent.

---

## 1. The loop (every invocation)

1. **Orient** (§0).
2. **Scope one task.** Take the single next unchecked task under **NEXT**, or the task the user named.
   Mark it `[~]` in the ledger. One task per session — they are sized for that.
3. **Requirements first** (§2). No code until the requirement is written and conflict-checked.
4. **Design docs next** (§3). Architecture / detailed_design / puml updated *before* the code.
5. **Implement** (§4), respecting the invariants above.
6. **Make it reachable without a GUI** (§5) — a `Command` in, an `AppModel` field or `Event` out. If the
   thing you built cannot be exercised, inspected or asserted with no window in the loop, it is not
   finished.
7. **Emit an `Event` for it** (§6) — which is how it gets logged, watched, journalled and asserted.
8. **Verify** (§7). `0 failed`, plus the evidence the task's level demands.
9. **Sync check** (§8). Any artifact that lags means not done.
10. **Update the ledger + defect list** (§9) and **commit** (§10).

---

## 2. Requirements first — and the conflict rule

cosmo keeps **two tiers, both authoritative, for different things**:

- `apps/cosmo/REQUIREMENTS.md` — **intent and history**. `## R-<AREA> — <title> — ✅ IMPLEMENTED`
  sections with bold `**R-AREA-n Sentence.**` bullets underneath. Status lives in the heading.
- `apps/cosmo/docs/requirements.md` — **as-built behaviour**. `### DR-<AREA>-<n> <Title> (R-<AREA>-<n>)`
  prose entries with inline `file:line` anchors. The parenthesised R-tag is the traceability link.

**The rule you were invoked to enforce:**

> Before writing code, find the requirement that governs the change.
> - **It exists and agrees** → implement it, then update its `DR-` entry to the new as-built truth.
> - **It exists and conflicts** with what is being asked → **stop and resolve the conflict in the
>   document.** Amend the `R-` entry in place, marked `**AMENDED (R-<newarea>)**` with one line of why
>   (this is the house style — see R-LOADING / R-LOADPERF). Never leave two requirements that disagree,
>   and never let code silently win over a written requirement.
> - **It does not exist** → **write it before any code.** New `R-<AREA>-<n>` in `REQUIREMENTS.md`
>   (intent, one paragraph, why), then the matching `DR-<AREA>-<n>` in `docs/requirements.md` once the
>   code exists (as-built, with `file:line`). Check the new requirement against every existing one in
>   the same area for conflict before you accept it.
>
> A change with no requirement is not allowed to ship, and neither is a requirement that no longer
> matches the code.

Numbering: append within the area, never renumber (the docs already use `DR-BROWSE-2a`, `7.8b` to avoid
renumbering — follow that habit). If the change closes a `PARITY.md` row, update its `Status` and `Req`
cells in the same commit.

**`R-SVC-1…10` now sits above everything else you touch**, so read it before the area requirement, not
after. It is the reason two thirds of this skill changed: the architecture requirement can veto a design
the area requirement would have allowed. In practice:

- A **new front-end-reachable behaviour needs a `Command`**, not a new accessor. If your first instinct
  is to add a getter to `CosmoService` or reach through `session()`, you have found the wrong shape:
  add the `Kind`, the parse/format case, the dispatch case, and the round-trip line in
  `test_command_text_roundtrips`. R-SVC-9 makes coverage assertable, so a behaviour with no command is
  meant to fail a test rather than be noticed later by a human reading the grammar.
- **New observable state goes into `AppModel`** and out through `formatModel`, not into a side channel.
- R-CPU-2 and R-CPU-4 are **AMENDED by R-SVC-10** (2026-08-17). Read the amendment before you touch
  anything thread-shaped; it is the house-style example of resolving a conflict in the document rather
  than in the code.

---

## 3. Design docs before code

- `docs/architecture.md` — update when a module is added/moved/removed, when a **seam** changes (the
  decode seam `IImageDecoder`, the engine seam `RenderService`, the compute seam `IComputeBackend`), or
  when the threading/data-flow story changes. The **module map table** at the end must list every new
  file with its layer and the R-tag that justifies it.
- `docs/detailed_design.md` — one `### N.M ClassName` per class, with real signatures, real constant
  values, callbacks, and behaviour. New class → new subsection (suffix rather than renumber).
- `docs/architecture.puml` — **a class in code with no box in the puml is an unsynced design.** New
  public type, new seam, or a new field on a modelled struct means the puml changes too, with the R-tag
  in a trailing comment.
- `docs/design.md` — only when the *rationale* changes. If you reverse a decision, fix the sentence that
  stated the old one; do not leave it standing next to the new behaviour.

---

## 4. Implement

Match surrounding style: this codebase comments *why*, not *what*, and each file opens with a block
comment stating its seam rule. Keep that.

- **Layering is strict and downward.** `arstro_image` knows nothing of cosmo; `cosmo_core` knows nothing
  of Artboard or GTK; the host knows everything. A codec, an OS path, or a `getenv` inside
  `arstro_image`/`cosmo_core` is a layering bug — put it behind the existing seam or add one.
- **Behaviour goes in the service; only drawing goes in the view.** The test for "is this behaviour?" is
  not "does it feel like logic" — it is *would a second front end need it?* If `cosmo-cc` would have to
  reimplement it to print the same answer, it belongs in `CosmoService` (R-SVC-1/4). Easing a progress
  bar toward 6-of-18 is the view's; deciding that 6 of 18 have arrived is the service's. When a task
  needs both halves it is two commits, **core first**, and the second one belongs to
  `arstro.cosmo.design.implement`.
- **A new adjustment** is: a field in `EditParams` → a rule in `composeParams` (decide add / follow-base
  / concatenate, and say why in the comment) → a case in `EditParamsIO` (serialize + parse, tolerant of
  old files) → a category entry in `EditParamsApf` → a processor in the right pipeline slot → a flat
  setter in `EditEngine` plus the `applyParams` fan-out → a `History::describeChange` label → a test.
  Miss one and a project file round-trips lossily.
- **And it must be reachable as `set <field>=<v>`** — which costs you nothing, and that is the point.
  `CosmoService::applySetFields` builds `key=value\n` lines and feeds them to the **same
  `deserializeParams`** the `.cosmo`, `.cmp`/`.cosmoproj` and `.apf` formats use
  (`CosmoService.cpp:239-243`), instead of growing a second key→field table. So a field that
  round-trips through `EditParamsIO` is *automatically* settable from a script, and `set` can never
  accept a smaller set of keys than a project file does. The corollary is the real rule: **if
  `set myfield=1` does not work, the `EditParamsIO` case is missing** — the bug is in persistence, not
  in the command, and your project files are already lossy.
- **Every persisted format must round-trip and must read old files.** `.cmp`/`.cosmoproj`, `.cosmo`,
  `.apf`, `settings.txt`, `recent.tsv` are all plain text with no escaping; adding a key is safe,
  changing the meaning of a key is a migration (see the legacy 12-scalar `offset=` migration in
  `readWorkspaceFile` for the pattern).
- **Threading:** anything touching `RenderService` or `OrderedParallelLoad` must still work in the
  non-threaded build (`ARSTRO_ENABLE_THREADS` off degrades to synchronous) and needs a ThreadSanitizer
  or randomized-trial argument, not a "looks fine".
- **GPU:** the CPU path is the reference. A GPU backend that cannot do a stage must **decline** and fall
  back, and a conformance test must assert GPU output matches CPU. Today the GL/GLES backends accelerate
  only exposure, contrast, white balance and the sRGB encode — extending that set means extending the
  conformance tests in the same commit.

---

## 5. The agent-drivable surface — now a consequence, not a bolt-on

**Rule: anything you implement must be reachable, inspectable and assertable from a shell command, with
no GUI in the loop.** If the only way to exercise your change is to click something, you have not
finished.

What changed on 2026-08-17 is *how* you satisfy that rule. The A1–A12 items below were specified when the
harness had to be bolted onto a GUI-shaped app — a dozen side doors, each with its own plumbing. Under
R-SVC most of them are the same door: a `Command` dispatched to `CosmoService`. **So the question is no
longer "which harness item do I need"; it is "which `Command` and which `AppModel` field", and then
whichever front end is available today.** Adding a side door that bypasses the service is now a
regression, not progress.

### 5.1 What exists today

```bash
./build.sh --project cosmo --target linux-native-app     # or cmake -S . -B build -G Ninja
cmake --build build --target cosmo cosmo_core_tests -j 8
cd build && ctest --output-on-failure                    # 12 suites; cosmo_core + cosmo_widget are ours
./build/apps/cosmo/core/cosmo_core_tests                 # run one suite directly (.exe on MSYS2)
XDG_CONFIG_HOME=/tmp/cosmo-scratch ./build/apps/cosmo/cosmo photo.jpg    # sandboxed run
```

- **The whole application runs with no display, from `cosmo_core_tests`.** `CosmoService` + a fake
  `IImageDecoder` opens a project, decodes it, attaches the tree, edits, undoes, groups, saves and
  dumps its state — see the last six tests in `sessionTests.cpp`, listed by name in §7. This is the
  level that did not exist before, and it is the one you should reach for first.
- `cosmo <image...>` skips splash + home and lands in the editor. **Bare paths only — there are still no
  flags**, and a `.cmp` still cannot be opened from a shell (D-6 stays open until S3's `--project`).
- All mutable state is text under `${XDG_CONFIG_HOME:-$HOME/.config}/cosmo_v2/`: `settings.txt`
  (`previewEdge` / `threads` / `useGpu` / `cpuPercent`), `recent.tsv`, `cosmo_v2.log`. Seed them to set
  up a scenario; read them to inspect one. **The service reads them too** — `ProjectStore::recents()`
  runs in `CosmoService`'s constructor — so a headless run needs `XDG_CONFIG_HOME` set just as much as
  a GUI run does.
- `.cmp`/`.cosmoproj` projects are line-oriented plain text (`#image` / `#group` / `#hnode` sections,
  `parent=` is an entry index). The smallest correct example is `writeFakeProject()` in
  `sessionTests.cpp` — copy that rather than hand-writing one.
- Note `configDir()` reads `HOME`, not `USERPROFILE` — launched outside an MSYS2 shell it writes to
  `./.config/cosmo_v2` relative to the CWD. Always set `XDG_CONFIG_HOME` explicitly in scripted runs.

### 5.2 What the architecture gives you, and what is still pending

**`PROGRESS.md` is the authority on status; the table below is the authority on shape.** S2's service
files and S3's `cosmo-cc` are being written as this is read — check the ledger's S-milestone checkboxes
before you assume either exists, and if the ledger and the repo disagree, reconcile the ledger first
(§0).

| Was | Now | Where |
|---|---|---|
| A1 `cosmo-cc` skeleton | **`cosmo-cc` is front end #2 over the service**, not a parallel implementation: argv / stdin / `--script` / `--watch` / `--json` / `attach`. `apps/cosmo/cli/` does not exist yet | **S3** |
| A2 `info`, A7 `backends`, A9 `bench` | still genuinely CLI-shaped — they inspect a decoder or a backend, not application state | **S3** |
| A3 `render --set k=v` | `set k=v` is a `Command`, already implemented and tested; the CLI adds only argv parsing and a PNG writer via `setImageWriter` | **S3** (behaviour: done) |
| A4 `project --print` | `state print` / `state print --json` — `formatModel()`, already implemented. `StatePrint` dispatches to a no-op in the service on purpose: **printing is the front end's job**, the model is the service's | **S3** (behaviour: done) |
| A5 `export` | `export --outdir D` is a `Command`; `CosmoService::runExport` renders every decoded slot synchronously and hands bytes to the injected `ImageWriter`, so the codec stays in the host. **`--format` / `--quality` / `--long-edge` parse into `fields` but `runExport` does not read them yet** — the written name and extension follow the source. Threading them through the `ImageWriter` signature is open work | **S3 / S4** |
| A6 `params --diff` | `formatModel(m, {params=true})` plus `diff`. A field-level differ is only worth building if the text dump proves insufficient | **S3** |
| A8 `check <file>` | `parseCommand` already reports the first error with a reason; a project/preset validator is still a CLI subcommand | **S3** |
| A10 `cosmo` flags | `--project` is `project open` behind an argv flag; `--control <path>` is R-SVC-8; `--script` reads the same grammar as everything else | `--project`/`--script` **S3**, `--control` **S5** |
| A11 `COSMO_APP_NOMAIN` | **unchanged and still pending.** `apps/cosmo/CMakeLists.txt` still globs `*.cpp` including `linux_main.cpp` (no `list(REMOVE_ITEM …)`), so no second `main()` can link the cosmo UI — D-7 | **P0.1** |
| A12 `cosmo_shots` + `cosmo_ui_tests` | unchanged, and not yours: specified in `arstro.cosmo.design.implement` §6. Core work must not break them | **P0.2 / P0.3** |
| — | **`ControlChannel`** — host-side socket/pipe reader → `dispatch`, event stream out. `apps/cosmo/host/` does not exist yet | **S5** |
| — | `IFileStore` / `IClock` / `ITaskPool` / `ILogSink` (R-SVC-7). Only `IImageDecoder` is a real seam today | pending |

Genesis remains the pattern to copy for the *front-end* mechanics rather than inventing them
(`apps/genesis/cli/main.cpp`, `apps/genesis/tests/renderShots.cpp`, and the `GENESIS_APP_NOMAIN` list in
`apps/genesis/CMakeLists.txt`) — but copy its plumbing, not its architecture: genesis has no service
layer, so do not copy a CLI that reaches into the session directly.

**Every core feature you add still gets a shell surface in the same commit.** Since S1 that usually means
one `Command`, one `Event`, one `AppModel` field and one test that dispatches it — and the worked
command line goes into the `DR-` entry so the next session can re-run it. `apps/cosmo/docs/DEVELOPING.md`
is where the worked examples collect; it does not exist yet (**P0.11**), so until it does, put them in
the `DR-` entry and the commit message.

### 5.3 Scripted input (`--script`) — one grammar, and it is `Command`'s

A script is a **file of command lines**, parsed by `parseCommand` and dispatched to the service, plus a
small set of steps only a UI front end can honour. Blank lines and `#` comments are successful no-ops,
so a script reads naturally. Every line below parses today, and all but two round-trip in
`test_command_text_roundtrips` — `export` and `wait` are parsed and dispatched but are **not** in that
test's line list yet, so add them the next time you touch either:

```
settings set cpuPercent=25 previewEdge=1600
project open /tmp/japan18.cmp
wait load-finished --timeout 120s
select 3
select next
set exposure=1.2 temp=7000
bypass 4 on
group new "Tokyo Night"
undo
preset apply "Portrait/Soft Skin"
export --outdir /tmp/out --format jpg --quality 90 --long-edge 2048
state print --json
quit
```

Grammar rules that are real and easy to get wrong — read `Command.cpp` before extending it:

- **Quoting, no escaping.** `"Tokyo Night"` survives; there are no backslash escapes anywhere in
  cosmo's text formats and this stays consistent with them.
- **`set` and `settings set` take bare `key=value`**; `export`, `state print` and `wait` take
  `--flag value` or `--flag=value`, and a bare `--flag` becomes `flag=1`. Do not mix the two styles.
- **`select` takes a node id, not a slot, not a filmstrip position** — `AppModel::NodeModel::node` is
  the handle every command uses. `select next`/`prev` walk decoded images over the *flat* tree.
- **`state print` and `wait` are accepted by `dispatch` and do nothing there.** They are front-end
  verbs: the service has no printer and never blocks (R-SVC-6), so the driving loop implements them by
  pumping and reading `model()`. Do not "fix" this by making `dispatch` wait.
- **`quit` sets `quitRequested()`**; a CLI loop polls it.

The UI-only steps stay as they were, for `cosmo_shots`/`cosmo --script` (design skill's territory):

```
size 1440 900
click 320 540
drag 320 540 -> 420 540
key ctrl+z
wheel 640 400 -1
dump-ui ui-after-undo.txt
shot editor-after-undo.png
expect event "[evt] params.changed exposure"
```

The same file must work **headless** and **live**. `expect` matches a **stable prefix of a `formatEvent`
line** (§6) — that is what makes a UI-reachable behaviour also shell-assertable, and it is why the event
shapes are frozen once quoted.

---

## 6. Observability — emit an `Event`, and everything downstream gets it

The user's workflow is: *they run the app, something looks wrong, they tell you what they saw, and you
read the log.* That only works if the log already contains the answer.

**The way you make that true is now one line: emit an `Event`.** Under R-SVC-5 `formatEvent()` output
*is* the log line — the same text the journal records, `--watch` streams, the control socket sends, an
`expect` step matches, and the host writes to `cosmo_v2.log`. `eventName()` doubles as the category
(`load.progress`, `frame.ready`, `entry.failed`). This is why P0.4 / P0.5 / P0.6 were three separate
pieces of work and are now mostly one: they existed because there was no event stream to filter, and
there is one.

**So the rule for new core behaviour is: add the `Event::Kind`, format it, emit it — do not add a
`LOGD`.** A core `LOGD` would also be a layering violation (`cosmo_core` carries no logging), which is
the same rule seen from the other side.

- **What must be emitted** (core side): every project open / opened / closed / saved; every load
  progress step, decoded entry, failed entry and finish; every selection change; every params change
  with the field names; every history step with the label; every settings change; every frame that
  lands, with slot, width and ms; every export step; every rejected command, with why. The current
  `Event::Kind` list covers exactly this — extend it rather than working around it.
- **Emit the numbers you claim.** The `load.peak` info line (`decode=… engine=… budget=…` in
  `CosmoService.cpp:220`) exists because R-CPU-4 was once "satisfied" by a line nobody had run. A
  quantity a front end cannot read out of the model or the event stream is not observable, whatever the
  requirement says.
- **Line shapes are an interface.** Once a `DR-` entry or an `expect` assertion quotes a
  `formatEvent()` line, its shape is frozen — that is a stated contract in `Event.h`, not a courtesy.
  Add fields at the end; never reorder or rename an existing one.
- **`AppModel::lastError` is the other half.** A rejected command emits `command.rejected` *and* lands
  in `lastError`, so a failure is inspectable after the fact by a front end that was not listening.
  Keep both.

**What is still genuinely `Log`'s job**, and still pending — it is host-side, and the `Event` stream does
not replace it (statuses in `PROGRESS.md`, defects D-2/D-3/D-4):

- `apps/cosmo/Log.{h,cpp}` has `Level{Debug,Info,Warn,Error}`, `LOGD/LOGI/LOGW/LOGE`, a per-line-flushed
  file at `configDir()/cosmo_v2.log` (so `tail -f` works) plus an unconditional stderr echo, and
  `installCrashHandler()`. **The level is still never checked and `setStderrEcho()` is still dead** (D-2).
- Filtering: `--log-level` / `--debug` / `COSMO_LOG_LEVEL`, and category filtering over the event names
  (`COSMO_LOG_CATEGORIES=load,render,session,export` — the names now come from `eventName()`, so there
  is no second list to maintain). `COSMO_LOG_FILE` to redirect.
- **Route GLib through it** — `g_log_set_default_handler`, not just the print/printerr handlers, or every
  GTK/Cairo/GdkPixbuf warning stays invisible in the file (D-3).
- **Windows backtraces.** The crash handler dumps frames on POSIX only, so a crash on the user's MSYS2
  host gives a signal name and nothing else. Use `CaptureStackBackTrace` plus dbghelp (D-4).
- The UI's own narration (gestures, transitions, layout, motion) is not yours: it is specified in
  `arstro.cosmo.design.implement` §7.

---

## 7. Verify — pick the lowest level that actually proves it

- **L0 unit** — `cosmo_core_tests` (plain `assert()`, `printf("[PASS] …")`, `main()` calling each test in
  sequence) and `image_tests` (MiniTest `TEST`/`CHECK`/`CHECK_NEAR`). Follow the neighbouring style; do
  not introduce a framework.
- **L1 numeric/pixel** — assert on real output: mean channel value after an edit, a resampled curve's
  value at a known x, byte-identical thumbnails across two code paths, GPU-vs-CPU conformance.
- **L2 the real service, headlessly** — **this level is new, it exists today, and it is strictly better
  evidence than reading code.** Construct a `CosmoService` with a fake `IImageDecoder`, dispatch the
  command script, pump to a fixed tick, then assert on `model()`, on `formatModel(…)`, or on the
  collected `formatEvent()` lines. No display, no GTK, no clicking, no `cosmo-cc` needed. The worked
  examples are in `apps/cosmo/core/tests/sessionTests.cpp` — read them before writing a new one:
  - `test_service_opens_a_project_with_no_ui` — the whole point of the architecture: open, decode,
    attach, land in the editor, with a group and a deliberately missing image. Shows the
    `writeFakeProject` / `FakeDecoder` / `pumpUntilIdle` trio you will reuse, and asserts on **event
    prefixes** rather than on internals.
  - `test_commands_drive_the_session` — select / `set` / undo / redo / bypass / group / settings /
    screen / save, each asserted to move the model, plus two *rejected* commands checked for a
    populated `lastError`. The template for "my new behaviour is reachable".
  - `test_two_services_dump_the_same_state` — R-SVC-9: two services given identical commands and
    wildly different pump counts must produce the identical `formatModel(…, stable)` text. This is how
    you prove a change did not alter behaviour: golden the stable dump.
  - `test_command_text_roundtrips` — R-SVC-5: `format(parse(x))` is a fixed point for every documented
    line. Add your new command's line here in the same commit.
  - `test_project_load_peak_never_exceeds_its_pool` — a full 18-entry load through a sleeping fake
    decoder, asserting in-order delivery, worker-built thumbnails, the per-worker start hook firing
    once per worker, and the **measured** peak. The model for any concurrency claim.
  - `test_one_budget_is_divided_not_duplicated` — the same claim checked as arithmetic across
    cores ∈ {2,4,8,12,16,24,32} × percent ∈ {1,25,50,75,100}, machine-independently.
- **L3 shell front end** — the `cosmo-cc` invocation from §5, run, with its output pasted into the commit
  message. Pending until S3; **L2 is not a placeholder for it but the real thing** — the CLI adds argv
  parsing and printing, not behaviour, so a service test proves the behaviour and the CLI test proves
  the front end.
- **L4 randomized/concurrent** — for anything touching the load pipeline or the render worker: the
  60-trial randomized `OrderedParallelLoad` test with its stall deadline, and a TSan run where available.
- **L5 live** — build and run `cosmo` on a real image set. Still the only way to catch a wiring mistake
  between the host and the service, and still required for anything the user will see.

**Pick the lowest level that actually proves it, and prefer a measured assertion to an argued one.** If
your evidence is a sentence about what the code does, you are at the level that shipped D-11 and D-12.

**The new behaviour needs a test that fails without the fix — check that, do not assume it.** Run the
test against the un-fixed code first; `one_budget_is_divided_not_duplicated` was checked this way and
names the exact assertion that fails on the old arithmetic.

The existing suites hardcode POSIX `/tmp/...` paths and resolve `$HOME`/`$XDG_CONFIG_HOME`, so they are
MSYS2/Linux-shaped; `settings_roundtrip_and_survive_a_bad_file` backs up and restores the user's real
settings file. Preserve both habits.

---

## 8. Sync check — if any of these lags, you are not done

- [ ] `REQUIREMENTS.md` — the `R-` entry exists, is conflict-checked, and its heading status is current.
- [ ] `docs/requirements.md` — the `DR-` entry describes the code **as built**, cites the R-tag, and its
      `file:line` anchors point at lines that exist now.
- [ ] `docs/architecture.md` — module-map row, plus any seam or threading change.
- [ ] `docs/detailed_design.md` — the class subsection matches the real signatures and constants.
- [ ] `docs/architecture.puml` — no class without a box.
- [ ] `docs/design.md` — no sentence left standing that the change reversed.
- [ ] `PARITY.md` — row updated if this closed or descoped one.
- [ ] `docs/DEVELOPING.md` — the worked command or build step, if you added one. **The file does not
      exist yet (P0.11)**; until it does, the worked command lives in the `DR-` entry and the commit.
- [ ] **A new `Command` or `Event`** — the grammar comment at the top of `Command.h` / the sample lines
      in `Event.h` list it, `commandNames()` includes it, and `test_command_text_roundtrips` covers it.
      Those doc comments are the published grammar; a case the header does not mention is undiscoverable.
- [ ] Tests updated and passing; the harness (§5) still builds.

> **Known drift to fix when you next touch loading:** `docs/design.md`, `docs/architecture.md` and
> `docs/detailed_design.md` still describe an `onLoadingReady` hook firing at the intro boundary, which
> R-LOADPERF/R-LOADUX removed — the decode now starts with the transition. Do not copy the stale text.

---

## 9. Ledger + defect list

**`apps/cosmo/docs/PROGRESS.md`** — update it in the same commit as the work:
- tick the task `[x]`, or `[!]` if it is done but unverified (and say exactly what is unverified and why),
- rewrite the **NEXT** line to the next task,
- update "Last updated" and the last-commit note,
- add anything that departs from the plan to the **Decisions log** (newest first) so no other machine
  re-litigates it.

**`apps/cosmo/docs/DEFECTS.md`** — if you fixed a listed defect, move it to Closed with the commit hash
and the test that now guards it. If you found a new one you are not fixing now, file it (format in
`arstro.cosmo.core.debug` §4) rather than leaving it in a chat message.

---

## 10. Commit

One focused commit per completed task, in the umbrella repo on `main`, **including the doc and ledger
updates**. Never batch two features. Message style, matching this repo's history:

```
cosmo: <lowercase sentence naming the user-visible effect> (R-AREA-n, DR-AREA-n)

<Symptom, then cause, then fix, then how it was proven.> Bold lead-ins per issue
when there are several. Paste the cosmo-cc command and its output that proves it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
```

`core/ImageProcessing` and `core/Artboard` are **git submodules**: a change there is two commits — the
submodule first, then the umbrella bumping the pointer. Forgetting the second is this repo's most common
mistake. Do not `git push` unless asked; remind the user at the end of a session, since pushing is what
actually makes the work portable to their other machine.

---

## 11. Definition of done

- [ ] The requirement was read first, written or amended, and conflict-checked — **before** the code.
      `R-SVC-*` was checked too, not only the area requirement (§2).
- [ ] Docs, puml, PARITY, DEVELOPING all in sync (§8).
- [ ] `ctest --output-on-failure` reports **0 failed**; the new test fails without the fix.
- [ ] The change is exercisable end-to-end **with no GUI** — a `Command` reaches it, an `AppModel` field
      or an `Event` shows it — and the exact command or test is written down.
- [ ] Behaviour landed in the service, not in a view; nothing new reaches past `dispatch` (R-SVC-2/4).
- [ ] An `Event` covers the new behaviour and **its line was actually observed**, not just written.
- [ ] No layering violation: no codec, OS path, logging, or `getenv` inside `arstro_image` or
      `cosmo_core` — `ProjectStore::configDir()` is the one standing exception and gets no company.
- [ ] `PROGRESS.md` ticked and **NEXT** rewritten; `DEFECTS.md` updated.
- [ ] Committed to `main` (submodule first, then the pointer bump).
