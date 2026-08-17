# cosmo — Defect List

**Committed, so it travels between machines and sessions.** Filed and closed by
`.claude/skills/arstro.cosmo.core.debug/` and `.claude/skills/arstro.cosmo.design.debug/`; the entry
format is defined in `arstro.cosmo.core.debug` §4 and is shared by both.

- IDs are `D-<n>`, sequential across both areas, **never reused**. Next free id: **D-18**.
- Status: `Open` · `Confirmed` · `Fixed` · `Not-a-defect` · `Unreproduced` · `Deferred`.
- Severity: `S1` data loss / crash / hang · `S2` wrong output or an unusable surface · `S3` wrong
  behaviour with a workaround · `S4` cosmetic or diagnostic.
- A resolved entry moves to `## Closed` with its commit hash and the test that now guards it. **Nothing
  is ever deleted.**

The entries below were found by reading the source on 2026-08-16, while writing the cosmo skills. They
are **confirmed in the code but not yet reproduced at runtime** — each says so. Most are observability
and reachability gaps, which is why `PROGRESS.md`'s NEXT is the P0 harness.

---

## Open

### D-17 — The control socket does nothing on Windows
- **Area:** core / service · **Status:** **Deferred** (stubbed, and it says so) · **Severity:** S3
- **Found:** 2026-08-17, while building S5 — a known limitation, filed so it is tracked rather
  than remembered.
- **Reproduce:** `cosmo.exe --control \\.\pipe\cosmo` on Windows → the log carries
  `control channel is not implemented on Windows yet` and the app runs normally, unattended.
- **Expected:** R-SVC-8 on every platform cosmo ships to.
- **Actual:** POSIX only. `ControlChannel::open()` returns false with a reason on Windows and
  every other method is an inert no-op, so `--control` fails loudly at startup instead of hanging
  later. The branch is `-fsyntax-only` clean but has never been built or run.
- **Judgement:** requirement gap against R-SVC-8, which does not name platforms. Deferred rather
  than open because it is a deliberate stop, not an oversight: an overlapped named pipe needs a
  per-instance state machine (`CreateNamedPipe` + `FILE_FLAG_OVERLAPPED`, a pending
  `ConnectNamedPipe` per client with its own event, a pending `ReadFile` per client,
  `GetOverlappedResult(..., FALSE)` per frame) whose failure mode is a **hung UI thread** — and
  it cannot be tested from this Linux host at all. `PIPE_NOWAIT` is not the shortcut: it is
  legacy and its writes silently drop data when the buffer fills, so the event stream would lose
  lines with no error anywhere.
- **Fix:** pending, and the cheaper route is recorded in `ControlChannel.cpp`: Winsock `AF_UNIX`
  (Win10 1803+, `<afunix.h>`) makes the POSIX branch nearly portable — `ioctlsocket(FIONBIO)`
  for `O_NONBLOCK`, `DeleteFileA` for `unlink`, and no SIGPIPE to suppress. Do that on a Windows
  box, where it can be run. Until then the CLI (`cosmo-cc run`) is the whole harness on Windows,
  and it is unaffected.

### D-10 — A failing assert in `cosmo_core_tests` hangs instead of exiting
- **Area:** core / test harness · **Status:** Confirmed (reproduced) · **Severity:** S3
- **Found:** 2026-08-16, while checking that the R-CPU-3 test fails without its fix
- **Reproduce:** break any assertion in `sessionTests.cpp` (e.g. stop `AppSettings::save()` writing
  `cpuPercent=`), rebuild, and run
  `XDG_CONFIG_HOME=/tmp/s ./build-mingw64/apps/cosmo/core/cosmo_core_tests.exe`.
- **Expected:** the assert prints to stderr and the process exits non-zero, so `ctest` reports a
  failure and an agent gets an answer in seconds.
- **Actual:** the process hangs indefinitely (killed at 5 min twice). MinGW/msvcrt `abort()` raises
  the Windows "terminated in an unusual way" path rather than exiting, and the assert text never
  reaches a redirected stderr because it is not flushed. A failing suite is indistinguishable from
  a slow one — and this suite legitimately takes 1–2 minutes (Release; longer in Debug), so the
  usual "it must be stuck" heuristic does not apply.
- **Evidence:** the same defect, isolated into a 10-line program linking `cosmo_core`, answered in
  under a second: `wrote cpuPercent=25, read back 50 -> FAIL`.
- **Judgement:** defect — no requirement covers the test harness, but "verify with a shell command"
  (the whole `R-AGENT` premise) is unusable when a red suite looks like a hung one.
- **Fix:** pending. Call `SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX)` +
  `_set_abort_behavior(0, _WRITE_ABORT_MSG|_CALL_REPORTFAULT)` on Windows in the suites' `main()`,
  and `setvbuf(stderr, nullptr, _IONBF, 0)`. Fold into P0.3 or P0.11.

### D-9 — Two divergent build paths, two undocumented build trees
- **Area:** core / build · **Status:** Confirmed (by source inspection) · **Severity:** S4
- **Found:** 2026-08-16, source inspection
- **Reproduce:** compare `./build.sh --project cosmo --target linux-native-app` (a hand-rolled one-shot
  `g++` producing `apps/cosmo/build/cosmo_linux`) with `cmake -S . -B build && cmake --build build`
  (producing `build/apps/cosmo/cosmo`). Also `ls build/apps/cosmo/cosmo.exe build-mingw64/apps/cosmo/cosmo.exe`.
- **Expected:** one documented way to build cosmo per platform.
- **Actual:** two Linux paths with different output names, different flags and no shared tests;
  `build.sh` cannot build cosmo on Windows at all (hardcoded `-lEGL -lGL`, `nproc`). Two Ninja trees
  exist on this host — `build/` (Debug, wired to `.vscode`) and `build-mingw64/` (Release) — with no
  documented distinction. `build.sh --target native-test` covers neither cosmo nor genesis.
- **Judgement:** requirement gap — nothing states which build path is canonical.
- **Fix:** pending. Resolve in P0.11 (`docs/DEVELOPING.md`) with an `R-AGENT-*` requirement naming the
  canonical path per platform.

### D-8 — `configDir()` resolves to the wrong place outside an MSYS2 shell
- **Area:** core / persistence · **Status:** Confirmed (by source inspection) · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** launch `build\apps\cosmo\cosmo.exe` from cmd.exe or Explorer (where `HOME` is unset)
  and look for `.config\cosmo_v2\` **relative to the current directory**.
- **Expected:** settings, recents and the log land in one predictable per-user location on Windows.
- **Actual:** `ProjectStore::configDir()` reads `XDG_CONFIG_HOME`, else `HOME` (never `USERPROFILE`),
  else `"."` — so a non-MSYS2 launch scatters config next to wherever it was started from. Supporting
  evidence: `C:\Users\Nam Doan\.config\cosmo_v2\` exists on this host but is **empty** — no log, no
  settings, no recents — so a real session's state has never been observed there.
- **Judgement:** requirement gap — no requirement states the Windows config location.
- **Fix:** pending (P0.12). Until then, always set `XDG_CONFIG_HOME` explicitly in scripted runs.

### D-7 — No headless UI render is possible; the CMake glob structurally prevents one
- **Area:** design / build · **Status:** Confirmed (by source inspection) · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `grep -n "file(GLOB COSMO_SOURCES" -A4 apps/cosmo/CMakeLists.txt` — the glob includes
  `linux_main.cpp`, so no second `main()` can link the cosmo UI.
- **Expected:** any screen or state renderable to a PNG with no display, as `genesis_shots` does.
- **Actual:** impossible today. `App` is already platform-free and drivable (`App(w,h)`, `setSize`,
  `render(target,nowMs)`, `pointer`, `wheel`, `key`, and public `showHome`/`showEditor`/
  `beginOpenTransition`/`setLoadProgress`/`finishOpenTransition`) — only the missing
  `list(REMOVE_ITEM … linux_main.cpp)` stands in the way. `cosmo_widget_tests` covers isolated widget
  hit-testing only, never the assembled app.
- **Judgement:** defect against the family design rule that a UI change must be *seen* before it ships
  (`arstro.design.desktop` §5) — currently unsatisfiable for cosmo.
- **Fix:** pending. P0.1 → P0.3.

### D-5 — The UI logs nothing, so a visual bug report cannot be traced
- **Area:** design / observability · **Status:** Confirmed (by source inspection) · **Severity:** S2
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `grep -rn "LOG[DIWE]" apps/cosmo/App.cpp apps/cosmo/widgets/ | wc -l` → 0. The only
  `LOGI` calls in the whole desktop app are startup and the export batch start/finish.
- **Expected:** the user says "this button does nothing" and the log says which widget consumed the
  click, or that nothing did.
- **Actual:** clicks, screen transitions, selections, panel and tab changes, scroll clamping, value
  commits and renders are all invisible. This is the single biggest observability gap in the app and it
  blocks the intended workflow (user reports a symptom → agent reads the log).
- **Judgement:** defect — no requirement guarantees it, but the reporting workflow is unusable without
  it. `R-AGENT-*` will state the contract.
- **Fix:** pending. P0.4 + P0.5.

### D-4 — No stack trace on a Windows crash
- **Area:** core / observability · **Status:** Confirmed (by source inspection) · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** inspect `apps/cosmo/Log.cpp` — the fatal-signal handler writes a banner for
  SIGSEGV/SIGABRT/SIGFPE/SIGILL (+SIGBUS on POSIX) but the `backtrace_symbols_fd` frame dump is POSIX-only
  and explicitly skipped on Windows.
- **Expected:** a crash on the development host produces frames.
- **Actual:** a signal name and nothing else — on the very host this project is developed on.
- **Judgement:** defect against R-NFR / DR-NFR-5's intent (a crash leaves a diagnosable record).
- **Fix:** pending. P0.4 (`CaptureStackBackTrace` + dbghelp).

### D-3 — GTK/GLib warnings never reach the log file
- **Area:** core / observability · **Status:** Confirmed (by source inspection) · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `linux_main.cpp` installs `g_set_print_handler` and `g_set_printerr_handler`, but
  `g_warning`/`g_critical`/`g_message` go through `g_log`, which neither handler sees.
- **Expected:** DR-NFR-5 says every `g_print`/`g_printerr` is routed through the log; the same intent
  covers GTK's own diagnostics, which are exactly what you need when rendering or a dialog misbehaves.
- **Actual:** they hit stderr only and vanish from the file. `G_DEBUG=fatal-warnings` under gdb is the
  only current way to catch them.
- **Judgement:** defect — DR-NFR-5's intent is not met for the diagnostics that matter most.
- **Fix:** pending. P0.4 (`g_log_set_default_handler`).

### D-2 — The log level is never checked; `LOGD` and `setStderrEcho()` are dead
- **Area:** core / observability · **Status:** Confirmed (by source inspection) · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** read `log::write()` in `apps/cosmo/Log.cpp` — no level comparison anywhere; then
  `grep -rn "LOGD\|setStderrEcho" apps/cosmo/` — `LOGD` is never called and `setStderrEcho()` is never
  called.
- **Expected:** a debug mode that can be turned up for an investigation and down for normal use, per
  `DR-NFR-5`'s "timestamped levelled log".
- **Actual:** the levels are decorative. Every line is always written to both the file and stderr; there
  is no verbosity flag, no category filter, no way to silence the stderr echo, and no way to relocate the
  file except via `XDG_CONFIG_HOME`/`HOME`.
- **Judgement:** defect — "levelled" is stated in DR-NFR-5 and is not implemented.
- **Fix:** pending. P0.4.

### D-1 — Three design docs still describe the removed `onLoadingReady` hook
- **Area:** core / docs · **Status:** Confirmed (by source inspection) · **Severity:** S4
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `grep -rn "onLoadingReady" apps/cosmo/docs/ apps/cosmo/REQUIREMENTS.md`
- **Expected:** `design.md`, `architecture.md` and `detailed_design.md` agree with `REQUIREMENTS.md` and
  `docs/requirements.md`.
- **Actual:** they still say the decode is deferred to an `onLoadingReady` hook firing at the intro
  boundary. `R-LOADPERF` amended that — the decode now starts *with* the transition — and
  `DR-LOADUX` records that the hook is gone.
- **Judgement:** defect — a doc-sync failure, which is exactly what the V-model's sync check exists to
  prevent. A future session reading `design.md` first would build against the old model.
- **Fix:** pending. Do it in the next task that touches loading (P1).

---

## Closed

### D-6 — A project cannot be opened from the command line
- **Area:** core / CLI · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `./build/apps/cosmo/cosmo.exe some.cmp` — `openPath()` special-cases only `.cosmo`, so
  a `.cmp`/`.cosmoproj` falls through to `openImageFile()` and fails to decode.
- **Expected:** the primary document format is openable from a shell.
- **Actual:** only bare images and `.cosmo` sessions are. Seeding `recent.tsv` is the sole way to make a
  project reachable, and that only puts it on the home screen.
- **Judgement:** requirement gap — no requirement covers command-line invocation at all.
- **Fix:** commit `127d213`. `openPath()` gained one branch — a `.cmp`/`.cosmoproj` dispatches
  `Command::ProjectOpen` — plus `--project <path>` for scripts where a bare path is ambiguous. It
  is one branch because the load is a command now (R-SVC-2); before the service there was nothing
  to dispatch it to, which is a fair summary of what S1–S3 bought.
  A project argument deliberately does **not** jump to the editor: it runs the normal animated
  load (R-LOADING) and the transition owns the screen. Only a bare image list lands straight in
  the editor.
- **Verified:** `cosmo japan18.cmp` on the 18-RAF project → `[evt] load.finished decoded=18
  total=18`, 18 `entry.decoded` lines, and `[evt] info load.peak decode=5 engine=6 budget=6` in
  the log. **This also clears U1.1a**, the `[!]` that had stood since the CPU budget shipped: the
  load's own numbers had never once been observed in the app, and six other defect entries cited
  D-6 as their reason for being unverifiable.

### D-16 — `wait` meant two different things to two front ends
- **Area:** core / service · **Status:** **Fixed** (same session, S5) · **Severity:** S3
- **Found:** 2026-08-17, the first time the committed acceptance script was run through BOTH
  front ends — which is the entire reason that test exists.
- **Reproduce:** before the fix, one script containing `wait load.finished`:
  ```bash
  cosmo-cc run --script s.txt          # -> "wait: unknown condition load.finished", then a
                                       #    120 s timeout and exit 1
  cosmo-cc attach /tmp/s.sock --script s.txt   # -> worked
  ```
- **Expected:** one documented command, one meaning, whichever front end reads it (R-SVC-5).
- **Actual:** `cosmo-cc run` matched model predicates (`load-finished`, hyphen) while the socket
  client matched event names (`load.finished`, dot). A script that worked against a live window
  failed headlessly, and the failure looked like a hang before it looked like a vocabulary error.
- **Judgement:** defect against R-SVC-5. Two front ends interpreting one command differently is
  the precise divergence the single-codec rule exists to prevent — and it appeared anyway,
  because `wait` is handled by the front end rather than the service, so it never went through
  the shared codec at all. Worth remembering: the rule protects what it routes.
- **Cause:** `wait` is deliberately front-end-owned (only the caller owns its loop), so both
  implementations grew independently.
- **Fix:** commit `e2d471a`. `canonicalWait()` normalises `-` to `.` and both front ends call it;
  the event name is canonical because events are the observable contract; hyphens stay accepted.
  `Command.h`'s documented example, the parser's error hint, the `--help` text, the coverage test
  and the proposal all now use the canonical spelling.
- **Guarded by:** `apps/cosmo/tests/acceptance/run.sh`, which runs the identical script through
  both front ends and diffs the results — the only check that could have caught this.

### D-15 — `AppModel::settings` reported defaults while `AppModel::budget` reported the truth
- **Area:** core / service · **Status:** **Fixed** (same session, S3) · **Severity:** S3
- **Found:** 2026-08-17, by diffing a `cosmo-cc` dump against a GUI dump of the same project —
  the R-SVC-9 check, doing precisely the job it was written for.
- **Reproduce:** before the fix, with `cpuPercent=25 useGpu=1` in `settings.txt`:
  ```bash
  cosmo --control /tmp/s.sock &   # then: state print
  ```
  → `settingsCpuPercent=50 settingsUseGpu=0` **and** `budgetPercent=25` in the same dump.
- **Expected:** one answer. What the user chose, everywhere it is reported.
- **Actual:** two halves of the same answer disagreeing, which is worse than either being wrong:
  anyone debugging from the dump would have believed the budget was 50%.
- **Judgement:** defect against R-SVC-3 — the model is the observable state, so a field that is
  never written is a lie, not an omission.
- **Cause:** the host applied the preferences piecemeal — percentage into `ThreadBudget`, edge and
  GPU into `EditSession`, model never told. `mModel.settings` was only ever written by the
  `settings set` command path, so a GUI that loaded settings from disk never populated it.
- **Fix:** commit `846bcef`. `CosmoService::applySettings(const AppSettings&)` does all of it in one
  place and refreshes the model; the host calls that instead of the three separate calls.
- **Guarded by:** `settings_and_dump_options_reach_the_model`, plus the CLI-vs-GUI dump diff, which
  is now byte-identical on the 18-RAF project.

### D-14 — `state print --stable` silently ignored `--stable`
- **Area:** core / service · **Status:** **Fixed** (same session, S3) · **Severity:** S3
- **Found:** 2026-08-17, same diff as D-15.
- **Reproduce:** `state print --stable` over the control socket, before the fix — the dump still
  contained `revision=`, `frameSeq=` and `budgetPeakDecode=`.
- **Expected:** the volatile fields dropped, so the dump can be compared with another front end's.
- **Actual:** they were all present, so **the comparison the option exists for was impossible**.
- **Judgement:** defect against R-SVC-9. An option that parses and is then dropped is worse than one
  that does not exist, because the caller believes it worked.
- **Cause:** `Command::StatePrint` had one `flag`, used for `--json`; `--stable` was parsed into a
  throwaway `Command` and discarded.
- **Fix:** commit `846bcef`. `--stable` and `--params` live in `fields` (so a third option needs no
  signature change), `--json` keeps `flag`, and `formatCommand` round-trips all three.
- **Guarded by:** `settings_and_dump_options_reach_the_model`.

### D-13 — A project opened from the GUI decoded all 18 images and attached none
- **Area:** core / service · **Status:** **Fixed** (same session, S5) · **Severity:** S2
- **Found:** 2026-08-17, by the FIRST live run over the control socket — minutes after S2 was
  committed with the bug in it, and by the very mechanism S2 existed to build.
- **Reproduce:** before the fix, with a GUI attached:
  ```bash
  cosmo --control /tmp/cosmo-ctl.sock &
  printf 'project open /tmp/japan18.cmp\nwait load.finished\nstate print\n' \
      | python3 attach.py /tmp/cosmo-ctl.sock
  ```
- **Expected:** `load.finished decoded=18 total=18`, `imageCount=18`.
- **Actual:** `load.finished decoded=0 total=18`, `imageCount=0`, `currentSlot=-1`, and every
  later command rejected (`select: no decoded images`). All 18 `load.progress` events fired —
  the files really did decode — but not one `entry.decoded`.
- **Evidence:** the event stream showed 18 progress lines and zero decoded lines, which localises
  it exactly: `pump()` only emits `entry.decoded` when `mNodeOf[r.index] >= 0` resolves to a live
  pending leaf.
- **Judgement:** defect against R-SVC-1/3. A view is *entitled* to clear its own state when it
  hears a project is opening — the GTK host calls `App::resetWorkspace()` there so the open
  transition captures the outgoing editor instead of fading in from nothing.
- **Cause:** `CosmoService::startProjectLoad` built the pending tree and *then* emitted
  `ProjectOpening`. Subscribers run synchronously inside that call, so the host's handler deleted
  all 18 nodes the load was about to attach to. The adapter's own comment claimed the event was
  emitted "BEFORE the session is reset" — the comment was right and the code was not.
- **Requirement:** R-SVC-3, and it produced a general rule now written into the service: **emit an
  event describing what is about to happen before the state it describes exists**, so a view that
  reacts by resetting cannot destroy work the service has already done.
- **Fix:** commit `4f6d82b`. Announce (`ScreenChanged` + `ProjectOpening`) first, then
  `resetWorkspace()`, then build the tree.
- **Guarded by:** `a_view_may_reset_on_project_opening`, which subscribes a handler that resets the
  session exactly as the host does and asserts all six images still attach. Checked to fail on the
  old ordering. The pre-existing headless tests could not catch this: with no subscriber doing real
  work, both orderings look identical — which is the lesson worth keeping.

### D-11 — The CPU budget is enforced per-subsystem, so a load peaks at ~2× what the user chose
- **Area:** core / load · **Status:** **Fixed** (S1) · **Severity:** S2
- **Found:** 2026-08-17, same investigation as D-12
- **Reproduce:** (no shell reproduction exists — that is D-6, and is the point) read the three call
  sites and evaluate them for one budget:
  ```bash
  grep -n "workersFor" apps/cosmo/linux_main.cpp apps/cosmo/App.cpp
  #  linux_main.cpp:724  workersFor(cpuPercent, kMaxDecodeWorkers)   -> decode pool
  #  App.cpp:869         workersFor(cpuPercent)                      -> engine Auto threads
  nproc
  ```
- **Expected:** R-CPU-1: "cosmo's background CPU work runs on a **budget**: a percentage of the
  machine's logical cores". A user who picks 25% expects cosmo to schedule at most 25% of the cores.
- **Actual:** the percentage is converted independently by each consumer and the two run
  **concurrently** — by design, since R-LOADPERF-3 streams decoded images into a live editor, so the
  decode pool and the engine's preview renders overlap for most of a load. Peak scheduled threads is
  `decodeWorkers + engineThreads + 1 (UI)`, i.e. roughly **twice the budget plus one**:

  | cores | budget | decode pool | engine Auto | peak threads | share of machine |
  |------:|-------:|------------:|------------:|-------------:|-----------------:|
  | 24 | 25% | 6 | 6 | 13 | **54%** |
  | 24 | 50% | 8 (cap) | 12 | 21 | **88%** |
  | 16 | 50% | 8 (cap) | 8 | 17 | **106% — oversubscribed** |

  So the shipped default (50%) can still schedule the whole machine, which is the complaint the
  feature was written to answer.
- **Judgement:** defect — contradicts R-CPU-1 as written. Not a requirement gap: R-CPU-1 already says
  "a share of the machine, not all of it", and R-CPU-2 lists the three consumers without ever saying
  they divide one budget rather than each taking the whole of it.
- **Cause:** `AppSettings::workersFor()` is a pure function each consumer calls for itself
  (`AppSettings.cpp:14`). Nothing owns the total, and nothing can — the two consumers live in
  different layers (host and app) and neither can see the other.
- **Requirement:** R-CPU-1, R-CPU-2 (existing). If the sum is judged acceptable, R-CPU-1 must be
  amended to say the budget is *per pool* — but that would make the number meaningless to a user.
- **Fix:** commit `95f3b78`. `cosmo::ThreadBudget` converts the percentage **once** into `total()`
  and divides it: the decode pool *reserves* `total - engineFloor` (capped at 8) for the load's
  duration and the engine gets the remainder, so it holds the whole budget when idle and shrinks only
  while a load runs. `AppSettings::workersFor` has no callers left in the app. R-CPU-2 and R-CPU-4
  amended; the load now logs the allotment and the **measured** peak.
- **Guarded by:** `one_budget_is_divided_not_duplicated` (fails on the old arithmetic at exactly the
  `decode + engine <= total` assertion — checked, not assumed) and
  `project_load_peak_never_exceeds_its_pool`. Measured end to end on 24 cores with the reported
  18-RAF project: 25% → pool 5, peak 5, **19% of the machine** mean over a 35 s load.

### D-12 — `OMP_NUM_THREADS` is set too late to bind, so R-CPU-2(c) does nothing
- **Area:** core / load · **Status:** **Fixed** (S1) · **Severity:** S2
- **Found:** 2026-08-17, debugging "CPU limit 25% still uses ~75% when opening a project"
- **Reproduce:**
  ```bash
  gcc -fopenmp -O1 -o /tmp/omp_env_order apps/cosmo/core/tests/fixtures/omp_env_order.c
  /tmp/omp_env_order                      # setenv() inside main(): team size = every core
  OMP_NUM_THREADS=1 /tmp/omp_env_order    # set before exec: team size = 1
  ```
- **Expected:** `linux_main.cpp:1282`'s `g_setenv("OMP_NUM_THREADS", "1", FALSE)` pins every nested
  OpenMP team to one thread, so "a decode worker means exactly one core" (R-CPU-2c).
- **Actual:** it has no effect. libgomp parses the environment in an **ELF constructor**, which runs
  when the library is loaded — before `main()` — so a `setenv`/`g_setenv` from `main()` is read by
  nobody. On this 24-core host:
  ```
  before setenv: omp_get_max_threads() = 24
  after  setenv: omp_get_max_threads() = 24
  actual parallel-region team size = 24  (machine has 24 cores)
  ```
  With the same variable set before `exec`, all three read 1. The commit that introduced the budget
  (5cb2688) calls this "the third layer, and the one that mattered" — on MSYS2, whose LibRaw *is*
  built `-fopenmp`, it is therefore still true that each decode worker opens a team sized to the
  whole machine, which is the reported 25%-budget-uses-75% symptom.
- **Judgement:** defect — contradicts R-CPU-2(c) ("nested parallelism inside a decoder … pin it to 1")
  and R-CPU-4's claim that the budget bounds "the one nested pool it can reach".
- **Cause:** environment variables consumed by a library constructor cannot be set from `main()`.
- **Platform note:** inert on this Linux host — the vendored `lib/LibRaw/lib/libraw.a` has zero
  `GOMP_*` symbols (`nm … | grep -c GOMP_` → 0), so there is no nested team here to pin. Live on
  MSYS2/Windows, where the CPU limit was developed and where the symptom was reported.
- **Requirement:** R-CPU-2, R-CPU-4 (both existing; neither needs amending — the code does not do
  what they say)
- **Fix:** commit `95f3b78` — option (a). `cosmo_v2::pinNestedOpenMPForThisThread()`
  (`apps/cosmo/OmpPin.cpp`) resolves `omp_set_num_threads` via `dlsym(RTLD_DEFAULT, …)` /
  `GetProcAddress`, so nothing links OpenMP, and `ProjectLoader`'s new per-worker start hook calls it
  **on each decode worker** — the ICV is per-thread, so the main thread could never have done it.
  `ompPinStatus()` is logged at startup so the mechanism is checked rather than assumed.
  **Mechanism proven** by a second fixture (`omp_pin.c`, scratch): three pinned pthreads report team
  size 1 while an unpinned control on the same machine reports 24. **Not observable in cosmo on this
  Linux host** — the vendored libraw.a has no OpenMP, so the startup line correctly reads
  `no OpenMP runtime loaded`. Confirm on MSYS2, where the symptom was reported, by checking for
  `nested OpenMP teams pinned to 1 per decode worker` in the log.
- **Guarded by:** `project_load_peak_never_exceeds_its_pool` asserts the hook ran once per worker.
