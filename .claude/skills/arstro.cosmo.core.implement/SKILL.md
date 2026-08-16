---
name: arstro.cosmo.core.implement
description: Use to implement or resume ANY non-UI work in the cosmo photo editor — the editing session, history, project/preset/settings persistence, the image-load and decode path, the export path, and the arstro_image processing engine (EditParams, EditEngine, RenderService, processors, GPU backends). Also owns cosmo's agent-drivable shell surface (the cosmo-cc CLI, debug logging, scripted input). Runs the V-model with both requirement tiers kept in sync, verifies headlessly from a shell, records progress in a committed ledger, and commits. Invoke for "add/fix an adjustment", "the engine renders wrong", "speed up loading", "add a format", "export is broken", "continue cosmo core", "/arstro.cosmo.core.implement". NOT for widgets/layout/theme — that is arstro.cosmo.design.implement.
---

# arstro.cosmo.core.implement

The **resume-driven V-model workflow for everything in cosmo that is not a pixel on a widget.**
All state lives in committed files, never in a session's memory, so you can stop on one machine,
`git pull` on another, invoke this skill, and know exactly where you are.

**You own:** `apps/cosmo/core/` (`cosmo_core`) · `apps/cosmo/core/decode/` · `apps/cosmo/ExportWriter.{h,cpp}` ·
`apps/cosmo/Log.{h,cpp}` · `apps/cosmo/cli/` (`cosmo-cc`) · `core/ImageProcessing/` (`arstro_image`) ·
the load orchestration in `apps/cosmo/linux_main.cpp` · `apps/cosmo/android/`.

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
| 5 | `apps/cosmo/docs/architecture.md` + `detailed_design.md` + `architecture.puml` | Where a class lives and what it promises. |
| 6 | `apps/cosmo/docs/design.md` | Why the code is shaped this way. |
| 7 | `apps/cosmo/PARITY.md` | The implement-or-descope backlog. |

If the ledger disagrees with the repo (a task marked `[ ]` whose code exists, a **NEXT** that is already
done), **reconcile the ledger to reality first and say so** — then continue. A stale ledger is the one
failure that breaks multi-device work.

### Where the code is

```
apps/cosmo/core/            cosmo_core — UI-free, owns NO artboard type
  EditSession.{h,cpp}         THE session. Read EditSession.h first; it is the de-facto spec.
  History.{h,cpp}             a BRANCHING DAG of full EditParams snapshots, one per slot AND per group
  ProjectStore.{h,cpp}        configDir() + recent.tsv
  PresetLibrary.{h,cpp}       filesystem scan of *.apf into a tree
  AppSettings.{h,cpp}         settings.txt: previewEdge / threads / useGpu
  OrderedParallelLoad.h       produce-on-a-pool, consume-strictly-in-order, dual back-pressure
  decode/                     IImageDecoder seam + Native (GdkPixbuf/LibRaw) + Android (stb/LibRaw)
  tests/sessionTests.cpp      cosmo_core_tests — 21 plain-assert() tests
apps/cosmo/ExportWriter.{h,cpp}   OS I/O + GdkPixbuf encode + JPEG APP1 / PNG sRGB metadata
apps/cosmo/Log.{h,cpp}            the log facility + crash handler (host-side on purpose)
apps/cosmo/linux_main.cpp         GTK host: decodeEntry (producer) / pollLoad (consumer) / export batch
core/ImageProcessing/src/     arstro_image — headless, codec-free, platform-free
  engine/EditParams.h           THE parameter struct; composeParams() is the group-stacking law
  engine/EditEngine.{h,cpp}     slots, flat setters, proxy cache, the render pipeline
  engine/RenderService.{h,cpp}  the threaded seam the UI binds to (coalescing, move-in add)
  engine/EditParamsIO / Apf     text serialization + the .apf preset envelope
```

### The invariants — break one and something lies silently

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
- **`arstro_image` and `cosmo_core` contain no logging, no asserts, no `getenv`.** That is deliberate —
  they must stay WASM/Android-portable. Diagnostics belong in the host layer or behind an injected seam
  (`EditEngine::setComputeAccelerator`, `activeBackendName()`).

---

## 1. The loop (every invocation)

1. **Orient** (§0).
2. **Scope one task.** Take the single next unchecked task under **NEXT**, or the task the user named.
   Mark it `[~]` in the ledger. One task per session — they are sized for that.
3. **Requirements first** (§2). No code until the requirement is written and conflict-checked.
4. **Design docs next** (§3). Architecture / detailed_design / puml updated *before* the code.
5. **Implement** (§4), respecting the invariants above.
6. **Make it reachable from a shell** (§5) — if the thing you built cannot be exercised, inspected,
   or asserted from a command line, it is not finished.
7. **Log it** (§6) — new behaviour emits debug-level log lines with a category.
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
- **A new adjustment** is: a field in `EditParams` → a rule in `composeParams` (decide add / follow-base
  / concatenate, and say why in the comment) → a case in `EditParamsIO` (serialize + parse, tolerant of
  old files) → a category entry in `EditParamsApf` → a processor in the right pipeline slot → a flat
  setter in `EditEngine` plus the `applyParams` fan-out → a `History::describeChange` label → a test.
  Miss one and a project file round-trips lossily.
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

## 5. The agent-drivable surface — no blind spots

**Rule: anything you implement must be reachable, inspectable and assertable from a shell command, with
no GUI in the loop.** If the only way to exercise your change is to click something, you have not
finished — extend the harness in the same task.

### 5.1 What exists today

```bash
cmake -S . -B build -G Ninja                      # MSYS2 MinGW64 shell on this host
cmake --build build --target cosmo -j 8           # -> build/apps/cosmo/cosmo.exe
cd build && ctest --output-on-failure             # 12 suites; cosmo_core + cosmo_widget are ours
./build/apps/cosmo/core/cosmo_core_tests.exe      # run one suite directly
XDG_CONFIG_HOME=/tmp/cosmo-scratch ./build/apps/cosmo/cosmo.exe photo.jpg   # sandboxed run
```

- `cosmo.exe <image...>` skips splash + home and lands in the editor. **Bare paths only — there are no
  flags today.**
- All mutable state is text under `${XDG_CONFIG_HOME:-$HOME/.config}/cosmo_v2/`: `settings.txt`,
  `recent.tsv`, `cosmo_v2.log`. Seed them to set up a scenario; read them to inspect one.
- `.cmp`/`.cosmoproj` projects are line-oriented plain text (`#image` / `#group` / `#hnode` sections,
  `parent=` is an entry index). `build/apps/cosmo/test.cmp` is a real 65-image example to copy.
- Note `configDir()` reads `HOME`, not `USERPROFILE` — launched outside an MSYS2 shell it writes to
  `./.config/cosmo_v2` relative to the CWD. Always set `XDG_CONFIG_HOME` explicitly in scripted runs.

### 5.2 The harness cosmo must have (Phase 0 — build it before or alongside your first feature)

Genesis already proves every one of these in this repo — copy its patterns rather than inventing
(`apps/genesis/cli/main.cpp`, `apps/genesis/tests/renderShots.cpp`, and the `GENESIS_APP_NOMAIN` list in
`apps/genesis/CMakeLists.txt`). Each item is a ledger task with its own `R-AGENT-n` requirement. Build
them in this order, and never mark one done without a worked example in `apps/cosmo/docs/DEVELOPING.md`.

| # | Deliverable | Contract |
|---|---|---|
| A1 | **`cosmo-cc`** — `apps/cosmo/cli/main.cpp`, links `cosmo_core` + `arstro_image`, **no GTK** | `--help` prints the full grammar. Exit 0 ok / 1 failure / 2 usage. `--json` on every subcommand for machine parsing. |
| A2 | `cosmo-cc info <file>` | decode and print dims, format, RAW-or-not, decoder used, ms taken |
| A3 | `cosmo-cc render <img> [--params f.cosmo\|f.apf] [--set exposure=1.2 --set temp=7000] [--preview-edge N] [--gpu\|--cpu] -o out.png` | the whole engine from a shell; `--set` accepts any `EditParams` key |
| A4 | `cosmo-cc project <f.cmp> --print` | dump the parsed node tree, per-node params, history DAG, bypass, pending — as text |
| A5 | `cosmo-cc export <f.cmp> --outdir D [--format jpg --quality N --long-edge N]` | headless batch export down the same path the modal drives |
| A6 | `cosmo-cc params --print <f>` / `--diff <a> <b>` | serialize plus a field-level diff, so "which param changed" is one command |
| A7 | `cosmo-cc backends` | CPU/GPU availability, `activeBackendName()`, thread count, LibRaw yes/no, build defines |
| A8 | `cosmo-cc check <f.cmp\|f.apf\|settings.txt>` | validate only; exit 1 with the first error located |
| A9 | `cosmo-cc bench <img> [--iters N]` | per-stage ms, so a perf claim is measured rather than asserted |
| A10 | **`cosmo.exe` flags** | `--project <f.cmp>` (a project cannot be opened from the shell at all today), `--headless`, `--script <f>`, `--shot <dir>`, `--log-level`, `--debug`, `--exit-after <ms>` |
| A11 | **`COSMO_APP_NOMAIN`** in `apps/cosmo/CMakeLists.txt` | `list(REMOVE_ITEM … linux_main.cpp)` — the one missing line that structurally blocks every headless UI harness |
| A12 | **`cosmo_shots`** + **`cosmo_ui_tests`** | specified in `arstro.cosmo.design.implement` §5; core work must not break them |

`cosmo-cc` is the layer this skill lives in — **every core feature you add gets a `cosmo-cc` surface in
the same commit**, and the worked command goes into the `DR-` entry so the next session can re-run it.

### 5.3 Scripted input (`--script`)

One line per step, replayed against `App` at a fixed 16 ms tick so runs are deterministic:

```
size 1440 900
open /path/photo.RW2
wait 800
click 320 540
drag 320 540 -> 420 540
key ctrl+z
wheel 640 400 -1
dump-ui ui-after-undo.txt
shot editor-after-undo.png
expect log "session: submit slot=0 exposure=1.20"
```

The same script file must work **headless** (`cosmo_shots --script`) and **live** (`cosmo.exe --script`).
That is what makes every UI-reachable behaviour also shell-reachable.

---

## 6. Debug mode — the log is how the user reports bugs to you

The user's workflow is: *they run the app, something looks wrong, they tell you what they saw, and you
read the log.* That only works if the log already contains the answer.

`apps/cosmo/Log.{h,cpp}` today: `Level{Debug,Info,Warn,Error}`, `LOGD/LOGI/LOGW/LOGE`, a file at
`configDir()/cosmo_v2.log` (flushed per line, so `tail -f` works) plus an unconditional stderr echo, and
`installCrashHandler()`. **But the level is never checked, there are no categories, `LOGD` is never
called anywhere, and `App.cpp` and `widgets/` log nothing at all.** Fixing that is `R-AGENT` work and
part of Phase 0:

- **Honour the level.** `--log-level=debug|info|warn|error`, `--debug` as shorthand for `debug`, and
  `COSMO_LOG_LEVEL` for when flags cannot be passed. Default `info`.
- **Categories**, filterable: `COSMO_LOG_CATEGORIES=ui,input,render,load,session,export,gpu`
  (`--log-categories=…`). Keep the existing timestamp and level, add the category:
  `2026-08-16 14:03:21.412 [DEBUG] [render] preview slot=0 1600x1067 gpu=0 12.4ms`.
- **`COSMO_LOG_FILE`** to redirect, and actually wire up the currently dead `setStderrEcho()`.
- **Route GLib through it** — `g_log_set_default_handler`, not just the print/printerr handlers, or every
  GTK/Cairo/GdkPixbuf warning stays invisible in the file.
- **Windows backtraces.** The crash handler dumps frames on POSIX only, so a crash on this user's host
  gives a signal name and nothing else. Use `CaptureStackBackTrace` plus dbghelp.
- **What debug level must log** (core side): every `submit()` with slot and the fields that changed;
  every render request and every frame acquired, with slot, size, backend, ms; every decode start/end
  with path, format, bytes, ms; every load-queue consume with index and back-pressure state; every
  history record/undo/redo with label and node seq; every project/preset/settings read and write with
  the path; every export step with source, destination, result. The UI half is specified in
  `arstro.cosmo.design.implement` §6.
- **Log lines are an interface.** Once a `DR-` entry quotes a line, keep its shape stable — the debug
  skills' `expect log` assertions and the user's bug reports both depend on it.

---

## 7. Verify — pick the lowest level that actually proves it

- **L0 unit** — `cosmo_core_tests` (plain `assert()`, `printf("[PASS] …")`, `main()` calling each test in
  sequence) and `image_tests` (MiniTest `TEST`/`CHECK`/`CHECK_NEAR`). Follow the neighbouring style; do
  not introduce a framework.
- **L1 numeric/pixel** — assert on real output: mean channel value after an edit, a resampled curve's
  value at a known x, byte-identical thumbnails across two code paths, GPU-vs-CPU conformance.
- **L2 shell** — the `cosmo-cc` command from §5, run, with its output pasted into the commit message.
- **L3 randomized/concurrent** — for anything touching the load pipeline or the render worker: the
  60-trial randomized `OrderedParallelLoad` test with its stall deadline, and a TSan run where available.
- **L4 live** — build and run `cosmo.exe` on a real image set.

**The new behaviour needs a test that fails without the fix — check that, do not assume it.** Run the
test against the un-fixed code first.

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
- [ ] `docs/DEVELOPING.md` — the worked `cosmo-cc` command or build step, if you added one.
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
- [ ] Docs, puml, PARITY, DEVELOPING all in sync (§8).
- [ ] `ctest --output-on-failure` reports **0 failed**; the new test fails without the fix.
- [ ] The change is exercisable end-to-end **from a shell**, and the exact command is written down.
- [ ] Debug-level logging covers the new behaviour, with a category, and a line was actually observed.
- [ ] No layering violation: no codec, OS path, or `getenv` inside `arstro_image` or `cosmo_core`.
- [ ] `PROGRESS.md` ticked and **NEXT** rewritten; `DEFECTS.md` updated.
- [ ] Committed to `main` (submodule first, then the pointer bump).
