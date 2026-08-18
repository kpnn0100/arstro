# cosmo — Progress Ledger

**This file is the single source of truth for what is done and what to do next.** It is committed and
travels between computers. The four cosmo skills read the **NEXT** line below, do one task, update this
file, and commit.

- Implement: `.claude/skills/arstro.cosmo.core.implement/` · `.claude/skills/arstro.cosmo.design.implement/`
- Debug: `.claude/skills/arstro.cosmo.core.debug/` · `.claude/skills/arstro.cosmo.design.debug/`
- Defects: [`DEFECTS.md`](DEFECTS.md) · Intent: [`../REQUIREMENTS.md`](../REQUIREMENTS.md) ·
  As-built: [`requirements.md`](requirements.md) · Backlog: [`../PARITY.md`](../PARITY.md)
- Checkbox legend: `[ ]` not started · `[~]` in progress · `[x]` done + verified · `[!]` done but
  UNVERIFIED (say why in Verification notes).

---

## NEXT

**[`service-architecture-proposal.md`](service-architecture-proposal.md) is APPROVED** (2026-08-17,
in-process service + control socket, full S1-S5) and written up as **R-SVC-1…10**. P0 is superseded:
the harness stops being side doors bolted onto a GUI-shaped app and becomes a consequence of the
architecture. **S1 is done (S1a + S1b) and D-11 + D-12 are closed.** ► Next is **S2** — `AppModel` +
`Command`/`Event` + the `CosmoService`, **done** — a project opens headlessly — and **S5 is done**:
the control socket, verified by driving the live window over it. ► Next are **S3** (`cosmo-cc`) and
**S4b/S4c** (App's remaining 96 + 60 direct session calls, then session ownership).

The proposal exists because U1's CPU budget could not be debugged. Chasing "25% still uses 75%" on
2026-08-17 produced two confirmed defects and no runtime measurement: **D-12** — the
`OMP_NUM_THREADS=1` pin that U1.1 called "the layer that mattered" is a no-op, because libgomp reads
the environment in a constructor that runs before `main()` (proven with a 30-line fixture) — and
**D-11** — the budget is converted independently by the decode pool and by the engine, which run
concurrently, so a load peaks at about twice what the user asked for. Neither could be *measured*,
only read, because opening a project still requires a mouse (D-6). Two hours went into trying to
drive the real load path from a shell (seeded config + `.cmp`, XTEST and XSendEvent clicks into the
live window) and it never ran once. That is the case for the proposal, stated as evidence rather than
as preference.

U1 made P0's case concretely, twice over. **First**, the decode-pool log line added in U1.1 still
cannot be observed from a shell, because a project cannot be opened without clicking (D-6) — it is
`[!]`, not `[x]`. **Second**, U1.2's gesture-routing change was silently a no-op in the working
tree (an edit whose match string used `--` where the source has an em dash), and *every*
lower-level check passed anyway: it compiled, both suites were green, and the widget test proved
the link fires its callback. Only an end-to-end render — real `App`, real pointer event, look at
the PNG — caught it, and only on the second shot, when click-outside failed to dismiss. That is
precisely the gap P0.1–P0.3 close permanently; the throwaway harness used here is described in
the decisions log so the next session can rebuild it in one command if P0.2 is still pending.

Last updated: 2026-08-17 · Last commit: S3, cosmo-cc — and a CLI dump and a GUI dump of the same
project are byte-identical (R-SVC-9), which found and fixed D-14 + D-15.

---

## Milestone status at a glance

| M | Milestone | State |
|---|---|---|
| U1 | CPU budget + Settings reachable from home | reopened by D-11 + D-12, **now fixed in S1a** and measured |
| S  | Core-as-a-service: `CosmoService`, `Command`/`Event`, CLI + GUI as views | **in progress** — S1, S2, S3, S4a, S5 done; only S4b/S4c left. Supersedes P0 |
| P0 | Agent harness — CLI, headless render, debug logging, scripted input | superseded by S, except P0.11 / P0.12 |
| P1 | Doc-drift cleanup (D-1) and requirement coverage for what already shipped | not started |
| P2 | PARITY backlog: crop overlay (#3), preset picker (#4), settings (#5), split-drag (#6) | see `PARITY.md` |
| P3 | Known gaps: R-ZOOM-5 eased zoom (belongs in Artboard), unwired seams | not started |

---

## U1 — a CPU budget, and Settings reachable from the launcher

Reported symptom: "loading an image takes almost all the CPU"; wanted a 50% limit, changeable in a
Settings page that can also be opened from the home screen. Requirements: **R-CPU-1…5** (new),
amending **R-LOADPERF-1**; **R-SETTINGS-1** and **R-HOME-8** are amended by U1.2.

- [x] **U1.1** Core: `AppSettings::cpuPercent` (default 50) + `workersFor()`, enforced at the decode
      pool, at the engine's Auto thread count, and on LibRaw's OpenMP team. Persisted and tested.
- [x] **U1.1a** **Cleared.** The load's log line — unobserved since the CPU budget shipped,
      because reaching it needed a mouse (D-6) — is now observed from a bare
      `cosmo japan18.cmp`: `[evt] load.finished decoded=18 total=18` and
      `[evt] info load.peak decode=5 engine=6 budget=6`. It reads as events rather than one
      `LOGI` because an Event *is* the log line (R-SVC-5).
- [x] **U1.2** Design: the home sidebar's Settings link is live and opens the same modal over the
      launcher (Home advances/renders/routes to the dialog that lives in the editor tree), plus the
      CPU-limit chip row (25/50/75/100). Three `cosmo_widget_tests` assertions; verified end-to-end
      by rendering the real `App` driven by real pointer events at two window sizes.

## S — the core as a service (R-SVC-1…10)

Design + migration plan: [`service-architecture-proposal.md`](service-architecture-proposal.md).
Strangler: each step leaves both paths compiling, so the app works after every commit. S2 and S4
cross into `App`/`widgets/`, which belong to `arstro.cosmo.design.implement` — those are two commits,
core first.

- [x] **S1a** (core) `ProjectLoader` + `ThreadBudget` in `cosmo_core`; `linux_main.cpp` keeps only the
      UI-side consumer. Closes **D-11** and **D-12**. Two new tests, the first checked to fail against
      the old arithmetic; measured end to end on the reported 18-RAF project (25% → 19% of a 24-core
      machine, peak 5 of a budget of 6)
- [x] **S1b** (design) `App::applyThreadBudget` deleted; the Settings dialog's thread controls report
      the choice and let the host's `ThreadBudget` apply it, notified before the re-render so the new
      frame uses the new width. `App.cpp` no longer includes `base/Parallel.h` at all
- [x] **S2** `AppModel` + `Command`/`Event` + both codecs + `CosmoService`, and the GUI rewired onto
      it: the project load left `linux_main.cpp` entirely, `onServiceEvent` translates the event
      stream into App's animation, and `onTick` pumps the service once per frame. Four new tests,
      including **a project that opens, decodes and reaches the editor with no window at all**
- [x] **S3** `cosmo-cc` over the service — `info`, `backends`, `project --print`, `render`, `export`,
      `params --print/--diff`, `check`, `bench`, `run --script/-`, and the global `--json` /
      `--watch` / `--serial` / `--stable` / `--params`. Subsumes A1–A9 and old P0.8–P0.10.
      **R-SVC-9's headline check passes**: `cosmo-cc project japan18.cmp --print --stable` and the
      GUI's `state print --stable` over the socket are now **byte-identical** on the real 18-RAF
      project. Getting there found **D-14** and **D-15**, both fixed here — neither was visible any
      other way, which is the argument for the check existing at all. `cosmo-cc attach <socket>`
      completes the loop the proposal promised: it drives a live `--control` window from a script
      and exits non-zero if any command was rejected.
  - [x] **S5b** the acceptance test is a **committed, unattended artifact** —
        `apps/cosmo/tests/acceptance/run.sh` + `drive-a-live-window.txt`. It runs the SAME script
        through the headless service and through a live window over the socket, asserts the event
        stream, and diffs the two `--stable` dumps. It found **D-16** on its first run. The socket
        is also serviced during the splash now, so a client that attaches immediately is not
        ignored for the length of the intro.
- [~] **S4** the rest of `App`'s logic moves down — export batch, presets, copy/paste settings, group
      ops, save/load workspace. `App.cpp` ends as render + gestures + animation *(core, then design)*
  - [x] **S4a** `App::onCommand` — the view's outbound channel (R-SVC-2). Undo/redo now emit a
        `Command` and fall back to the direct call only when no service is wired (a bare `App` in
        a widget test). A menu item, a shortcut and a socket line take one path.
  - [~] **S4b** **The metric was wrong and is corrected here** — worth reading before continuing.
        Counting `mSession.` in `App.cpp` does not measure progress, because a converted method
        *keeps* its direct call as a deliberate fallback for a bare `App` with no service wired
        (`cosmo_widget_tests` builds one). The count stayed at 96 across four real conversions.
        The three numbers that do mean something:
        - **Behaviours expressible as a `Command`: 24** (was 22). Rising is progress, and
          `every_command_kind_has_a_grammar` fails if a kind is added without a grammar rule.
        - **`App` methods that route through a command when a service is wired: 4** — `undo`,
          `redo`, `deleteSelected`, `renameGroup`. This is the numerator; the denominator is
          every discrete user action App still performs itself.
        - **The actual gate, and the only one in the proposal's §5 checklist: `widgets/*.cpp`
          reaching past the service = 60**, all in `RightColumn.cpp`, which must reach **0**;
          plus **2** `svc->session()` uses in the host, also to 0.
  - [x] **S4b-2 — RightColumn, DONE.** All 23 sliders (one lambda change: it takes a field name
        and a unit converter instead of a setter closure, so the command carries engine units),
        the mask panel, the mixer, both curve sets, grading, the remap quad and transform — every
        one now leaves as a `Command`. **No new command was needed for any of it**, exactly as
        DR-SVC-2b said; the single genuine gap was `dabs`, added to `MaskSet` here, without which
        a Brush drag — whose whole product is dabs — was the one edit no command could express.
        The gate: `grep -c "mSession\.\|EditParams" widgets/*.cpp` → **RightColumn 29, every
        other widget 0**. Of those 29: 9 are prose in comments, 11 are *reads* through one named
        seam (`params()`/`effectiveParams()`, legitimate under R-SVC-4 and S4c's job), and 7 are
        the documented unwired fallback. **Converted-surface writes reaching past the service: 0.**

  - [ ] **S4c** move `EditSession` ownership from `App` into `CosmoService`; `App` holds a
        `CosmoService&`. Do this AFTER S4b, or every converted call site gets touched twice.
        It also converts RightColumn's 11 remaining reads into `AppModel` reads, which is what
        finally takes the gate to 0 rather than "0 writes".
  - [ ] **Watch item from S4b-2**, not yet a defect: now that every control emits a command, each
        `set` re-enters `App::syncFromSession()` synchronously, so a **crop drag** and a **mask
        feather drag** get `XformPanel::setState` / `MaskPanel::setMasks` pushed back at them
        mid-drag — the very thing the feather handler's comment warns against. The setters are
        programmatic and do not refire `onChange`, so it is believed harmless, and the acceptance
        run is clean; but it is a new interaction and it has not been driven by hand at 60 Hz.
        Check it before trusting a long crop drag.
- [x] **S5** `ControlChannel` (non-blocking `AF_UNIX`; Windows stubbed with a reason) + `--control`,
      wired into the frame tick. **The acceptance test passed on the real 18-RAF project**: the GUI
      driven entirely over the socket, `load.finished decoded=18 total=18`, and a window capture
      showing `DSCF5194.RAF (2/18)` with the Exposure slider moved. It also found **D-13** on its
      first run — a bug S2 had just committed, which no headless test could see.

**S is done when** the §5 checks in the proposal pass: a CLI-opened project and a GUI-opened project
dump the same `AppModel`; `grep "mSession\.\|EditParams" apps/cosmo/widgets/*.cpp` is empty; every
`R-*` behaviour has a command; a load's measured peak concurrency is within budget; and the
acceptance test runs unattended.

**Absorbed from P0 along the way:** P0.1/P0.2/P0.3 (headless UI render + assertions) land with S3's
harness; P0.4/P0.5/P0.6 (log levels, categories, UI logging, `--dump-ui`) become `Event` plumbing in
S2 — an event *is* a log line under R-SVC-5; P0.7's `--project` is S3; P0.11/P0.12 (`DEVELOPING.md`,
Windows `configDir()`) stay as written and are listed below.

## P0 — the agent harness (superseded by S; kept for the two tasks S does not absorb)

Each task is one session. Specs: `arstro.cosmo.core.implement` §5–§6 (A1–A12) and
`arstro.cosmo.design.implement` §6–§7.

- [ ] **P0.0** `R-AGENT-*` requirements written and conflict-checked *(do this first — V-model)*
- [ ] **P0.1** `COSMO_APP_NOMAIN` in `apps/cosmo/CMakeLists.txt` (A11) — one `list(REMOVE_ITEM … linux_main.cpp)`; it structurally blocks every headless UI harness
- [ ] **P0.2** `cosmo_shots` — headless PNG renders of every named app state, 2+ window sizes, mid-transition (A12)
- [ ] **P0.3** `cosmo_ui_tests` — headless assertions over the assembled app: non-overlap, reachability, text fit, reflow (A12)
- [x] **D-10** (was folded into P0.3/P0.11) `core/tests/TestMain.h` — a failing assert now exits
      non-zero with its message intact through a redirect, instead of hanging where a red suite
      looked like a slow one. Windows half code-verified only; confirm on MSYS2
- [x] **P0.4** `Log`: level honoured (compared before formatting), categories **derived** from the
      event stream's dotted names so the two vocabularies cannot drift, all four env vars + flags,
      `setStderrEcho()` alive, GLib routed through `g_log_set_default_handler`, Windows backtrace via
      dbghelp resolved at runtime. **Closes D-2, D-3, D-4** — D-4's Windows branch code-verified only
- [~] **P0.5** **Smaller than it was.** Session/load/selection/params/history/export are all logged
      already, because `onServiceEvent` writes `formatEvent(e)` and an Event *is* the log line
      (R-SVC-5) — that was most of D-5. What is genuinely left is **input**: which widget consumed
      a click, and screen/scroll/hover decisions, none of which are service state
- [ ] **P0.6** `--dump-ui` — Segment tree with rects, visibility, opacity, scroll offsets, hover
- [~] **P0.7** `cosmo` flags. **`--project` and a bare `.cmp` argument are done** (closes D-6), and
      `--control` landed with S5. Still open: `--headless`, `--script`, `--shot`, `--log-level`,
      `--debug`, `--exit-after`. Note `--script` is largely redundant now — `cosmo --control` plus
      `cosmo-cc attach --script` does the same job and is already tested by the acceptance run
- [x] **P0.8** `cosmo-cc` skeleton + `info` + `backends` + `check` — **done in S3**
- [x] **P0.9** `cosmo-cc render` + `params` + `project` — **done in S3**
- [x] **P0.10** `cosmo-cc export` + `bench` — **done in S3**
- [x] **P0.11** `docs/DEVELOPING.md` — **written**, and every command in it was run before it was
      written down. Declares **CMake into `build/` canonical on every platform**, because the
      alternative turned out not to link at all (D-19). Records the `build/` vs `build-mingw64/`
      distinction, the Release-vs-Debug trap in the same directory, the document map, the
      invariants, and the closed-defect lessons with ids. Closes **D-9**
- [x] **P0.12** `configDir()` outside MSYS2 — `XDG_CONFIG_HOME` first everywhere, then an adopted
      pre-D-8 directory, then `%APPDATA%`/`%USERPROFILE%`, then `$HOME`, then the old relative path.
      Adoption rather than migration, because this layer has no error channel to report a half-copy.
      Closes **D-8**
- [x] **P1 (part)** doc drift — **D-1 closed**, and it was six places rather than three: two of them
      were requirement files contradicting *themselves* (DR-SCREEN-2 vs DR-LOADUX-4 in one file;
      R-LOADING-0 vs its own R-LOADING-1 amendment), plus a puml class that no longer exists and a
      comment in `App.cpp`. A full `file:line` anchor sweep of `detailed_design.md` remains
- [ ] ~~**P0.11**~~ superseded above — the cold-start guide: where things are, which document decides
      what, the invariants, the worked commands, the mistakes already made (model it on
      `apps/genesis/docs/DEVELOPING.md`, which is the best example in this repo)


**P0 is done when** a session on a fresh machine can, from a shell alone: build, run every test, render
any screen to a PNG, dump the UI tree, replay a scripted interaction, drive the whole engine headlessly,
and read a debug log that explains what the UI did.

---

## Decisions & deviations log (newest first)

- **2026-08-17 (S5) — Announce before you mutate.** D-13: a subscriber runs synchronously inside
  `dispatch`, and a view is entitled to clear its own state when it hears "a project is opening" —
  the GTK host does exactly that. So an event describing what is ABOUT to happen must be emitted
  BEFORE the state it describes exists, or the handler destroys work the service already did. The
  bug shipped in S2 with a comment in the host that described the correct order while the service
  did the opposite; the first live socket run found it in seconds, and no headless test could,
  because with no subscriber doing real work both orderings look identical.
- **2026-08-17 (S2) — The service borrows the session; it does not own it yet.** App has owned
  `mSession` since long before any of this, and reparenting ownership in the same step as
  introducing the service would have meant rewriting App and the service at once with nothing
  working in between. So `CosmoService(EditSession&, ThreadBudget&)`, `App::session()` is the
  transitional accessor, and S4 moves ownership in. Every use of `svc->session()` is a line S4
  deletes — that is the strangler, stated as a countable number.
- **2026-08-17 (S2) — `refreshModel()` rebuilds the whole snapshot instead of patching it.** The
  tree is hundreds of nodes at most, and a snapshot that is always derived cannot drift from the
  session the way incrementally-maintained mirror state does. If this ever shows up in a profile,
  gate it on a dirty flag — do not hand-maintain the model.
- **2026-08-17 (S2) — `set` reuses the params text codec rather than a new key table.** Every
  `EditParams` field already round-trips through `deserializeParams` for `.cosmo`/`.cmp`/`.apf`, so
  `set exposure=1.2` accepts exactly the fields a project file does and can never fall behind them.
  A new adjustment becomes shell-reachable for free, which is the R-SVC-2 coverage rule made cheap.
- **2026-08-17 (S2) — An Event IS the log line.** `onServiceEvent` logs `formatEvent(e)` and
  nothing else. This is why P0.4/P0.5/P0.6 (log levels, categories, UI logging) stopped being
  separate work: emitting an event is logging it, and the same line feeds `--watch`, the journal
  and the control socket.
- **2026-08-17 (S1a) — The budget is a reservation, not a fixed ratio.** The engine gets the *whole*
  budget when nothing is loading and only the remainder while a load runs, rather than a permanent
  share. A fixed split would slow every render on an idle app to protect a load that is not
  happening. The floor on each side is 1: previews must not stop entirely (the editor is visible
  during a load, R-LOADPERF-3) and a load must always progress (R-CPU-1). Those two floors are the
  only case where the sum can exceed the total, and only on a machine whose entire budget is one
  thread.
- **2026-08-17 (S1a) — `OrderedParallelLoad` had never been restarted, and could not be.** `stop()`
  latches `mStop` and `start()` did not clear it, so a reused pipeline started with every worker
  returning immediately. It was invisible until now because each load built a fresh `LoadJob`;
  `ProjectLoader` keeps one pipeline and calls `stop()` before each `start()`. `start()` now
  re-initialises all run state. Found by the new test, not by inspection — which is the argument for
  S1 in one sentence.
- **2026-08-17 (S1a) — The measured peak is part of the feature, not instrumentation.** R-CPU-4 used
  to be satisfied by a log line that had never been executed. `ThreadBudget` counts producers, the
  load logs the high-water mark, and a test asserts it. Anything that claims a thread count from now
  on has to be able to report one.

- **2026-08-17 — U1.1's decisive claim was wrong, and the reason is worth keeping.** The decisions
  entry below says "the nested OpenMP team was the real cause … pinning `OMP_NUM_THREADS=1` is what
  actually changes the load, not the pool arithmetic." The pin does not work (D-12): libgomp reads the
  environment in a load-time constructor, so `main()` is too late. The entry is left standing rather
  than edited because *why* it was believed matters — it was reasoned from the source and never run.
  Anything asserted about thread counts from now on needs a measurement, not an argument.
- **2026-08-17 — Proposal: the core becomes a service and every front end becomes a view.** Written up
  in [`service-architecture-proposal.md`](service-architecture-proposal.md) at the user's request. The
  driver is that `startEntriesLoad`/`decodeEntry`/`pollLoad` live in `linux_main.cpp`, so the app's
  most important behaviour is unreachable without a window — and the CPU budget is read in three
  places by three owners with nobody owning the total. Recorded here so that if the proposal is
  declined, the reason it was raised is still on file.
- **2026-08-16 (U1.2) — One dialog, driven by two screens.** The launcher does not get its own
  settings surface: `mSettingsDialog` stays in the editor tree and Home sizes, advances and renders
  it, and routes gestures to it while it is open. A second instance would mean two copies of the
  callbacks and a second place for the persisted record to diverge. It is advanced on *every* Home
  frame, not only while open, because `isOpen()` is false during the closing fade and `show()`
  needs a current `nowMs`.
- **2026-08-16 (U1.2) — How to render cosmo headlessly before P0.2 exists.** Link the app sources
  minus `linux_main.cpp` plus `CairoTarget.cpp` against `artboard_core` + `cosmo_core` +
  `arstro_image`, and **pass the same defines the libraries were built with**
  (`-DARSTRO_ENABLE_THREADS -DARSTRO_GL_COMPUTE -DNDEBUG -O3`). Omitting them is an ODR violation
  that segfaults in `~EditSession` at exit, which reads as a teardown bug and is not one. Also note
  `App::pointer`'s kind codes: **0 = Down, 2 = Up**, anything else = Move — a `1` produces a Move
  and no click, with only a hover wash to show for it.

- **2026-08-16 (U1.1) — A CPU limit is enforced as a core count, not as a CPU percentage.** No
  portable per-process CPU-time cap exists across Linux/Windows/Android, and a sleep-based throttle
  would occupy the cores it is sparing. The user picks a percentage; `AppSettings::workersFor`
  converts it to a thread count once, and both pools read that. Recorded because "why isn't this a
  real % cap?" will be asked again.
- **2026-08-16 (U1.1) — The budget governs the engine's Auto threads too, not just the load.** The
  reported symptom was loading, but Auto meant every core for rendering as well, so a 50% budget
  that left renders at 16 threads would not be believed. An explicit thread choice still overrides.
- **2026-08-16 (U1.1) — The nested OpenMP team was the real cause.** MSYS2's LibRaw is built
  `-fopenmp`; 8 decode workers each opening a 16-thread team is ~128 threads, which saturated the
  machine *regardless* of pool size. On this 16-core host, 50% of the pool was already 8 (the cap),
  so pinning `OMP_NUM_THREADS=1` is what actually changes the load, not the pool arithmetic.
- **2026-08-16 — The harness is a requirement, not tooling.** Anything an agent cannot reach from a
  shell is treated as a product defect in cosmo, filed in `DEFECTS.md`, and given an `R-AGENT-*`
  requirement — not left as an informal wish. Rationale: this project is developed across machines and
  sessions by agents; unreachable behaviour is untestable behaviour.
- **2026-08-16 — Copy Genesis, do not invent.** `genesis-cc`, `genesis_shots`, `genesis_ui_tests` and
  `GENESIS_APP_NOMAIN` already solve exactly these problems in this repo. cosmo's harness mirrors them
  so the two apps stay learnable from each other.
- **2026-08-16 — Two requirement tiers stay.** `../REQUIREMENTS.md` (`R-*`, intent + history) and
  `requirements.md` (`DR-*`, as-built + `file:line`) are not merged; the split is deliberate and both
  files state it. Every change touches both.

---

## Verification notes

- **S1a is verified on Linux, not on Windows.** The suite is green (24/24), the new peak test fails on
  the old arithmetic, and a real 18-RAF load was measured on a 24-core Linux host. The D-12 pin is
  *inert here* — the vendored libraw.a has no OpenMP, and the startup line says so — so the fix's
  effect on the reported symptom still needs one confirmation on MSYS2: look for
  `cpu: nested OpenMP teams pinned to 1 per decode worker` in the log. The mechanism itself is proven
  by `core/tests/fixtures/omp_pin.c`.
- Nothing in P0 has been built or verified yet; every claim about it comes from reading the source on
  2026-08-16.
- The host used for that reading is Windows 10 + MSYS2 MinGW64. `build/` (Debug, Ninja) and
  `build-mingw64/` (Release, Ninja) both contain a working `cosmo.exe`; `build/` is the one wired to
  `.vscode`. The distinction between the two trees is undocumented — resolve it in P0.11.
- `${XDG_CONFIG_HOME:-$HOME/.config}/cosmo_v2/` exists on this host but is **empty** — no log, no
  settings, no recents — so the log has never been confirmed to be written here in practice. Confirm
  before trusting it as a debug channel (see D-8).
