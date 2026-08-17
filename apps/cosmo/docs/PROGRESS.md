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

**Awaiting a decision on [`service-architecture-proposal.md`](service-architecture-proposal.md).**
► If approved, P0 is superseded: the harness stops being a set of side doors bolted onto a GUI-shaped
app and becomes a consequence of the architecture. The proposal's S1 (move the project loader and a
single `ThreadBudget` into `cosmo_core`) replaces P0.0 as the first task, and it is also the fix for
D-11 + D-12. ► If declined, resume at **P0.0** as written below.

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

Last updated: 2026-08-16 · Last commit: U1.2, Settings from the launcher (R-SETTINGS-5).

---

## Milestone status at a glance

| M | Milestone | State |
|---|---|---|
| U1 | CPU budget + Settings reachable from home | **DONE, then reopened** — D-11 + D-12 mean the budget is not enforced as R-CPU-1 states |
| S  | Core-as-a-service: `CosmoService`, `Command`/`Event`, CLI + GUI as views | **proposed, awaiting approval** — supersedes P0 if taken |
| P0 | Agent harness — CLI, headless render, debug logging, scripted input | **not started** (folded into S if the proposal is approved) |
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
- [!] **U1.1a** The decode-pool log line is code-verified but **not observed at runtime** — reaching
      `startEntriesLoad` needs a project opened by clicking (D-6). Clears with P0.7 (`--project`).
- [x] **U1.2** Design: the home sidebar's Settings link is live and opens the same modal over the
      launcher (Home advances/renders/routes to the dialog that lives in the editor tree), plus the
      CPU-limit chip row (25/50/75/100). Three `cosmo_widget_tests` assertions; verified end-to-end
      by rendering the real `App` driven by real pointer events at two window sizes.

## P0 — the agent harness

Each task is one session. Specs: `arstro.cosmo.core.implement` §5–§6 (A1–A12) and
`arstro.cosmo.design.implement` §6–§7.

- [ ] **P0.0** `R-AGENT-*` requirements written and conflict-checked *(do this first — V-model)*
- [ ] **P0.1** `COSMO_APP_NOMAIN` in `apps/cosmo/CMakeLists.txt` (A11) — one `list(REMOVE_ITEM … linux_main.cpp)`; it structurally blocks every headless UI harness
- [ ] **P0.2** `cosmo_shots` — headless PNG renders of every named app state, 2+ window sizes, mid-transition (A12)
- [ ] **P0.3** `cosmo_ui_tests` — headless assertions over the assembled app: non-overlap, reachability, text fit, reflow (A12)
- [ ] **P0.4** `Log`: honour the level, add categories, `COSMO_LOG_LEVEL` / `COSMO_LOG_CATEGORIES` / `COSMO_LOG_FILE`, wire up the dead `setStderrEcho()`, route GLib through `g_log_set_default_handler`, Windows backtrace via dbghelp (closes D-2, D-3, D-4)
- [ ] **P0.5** UI + input debug logging in `App.cpp` and the widgets (closes D-5)
- [ ] **P0.6** `--dump-ui` — Segment tree with rects, visibility, opacity, scroll offsets, hover
- [ ] **P0.7** `cosmo.exe` flags: `--project` (closes D-6), `--headless`, `--script`, `--shot`, `--log-level`, `--debug`, `--exit-after`
- [ ] **P0.8** `cosmo-cc` skeleton + `info` + `backends` + `check` (A1, A2, A7, A8)
- [ ] **P0.9** `cosmo-cc render` + `params` + `project` (A3, A4, A6)
- [ ] **P0.10** `cosmo-cc export` + `bench` (A5, A9)
- [ ] **P0.11** `docs/DEVELOPING.md` — the cold-start guide: where things are, which document decides
      what, the invariants, the worked commands, the mistakes already made (model it on
      `apps/genesis/docs/DEVELOPING.md`, which is the best example in this repo)
- [ ] **P0.12** `configDir()` on Windows outside MSYS2 (D-8) — decide and document the rule

**P0 is done when** a session on a fresh machine can, from a shell alone: build, run every test, render
any screen to a PNG, dump the UI tree, replay a scripted interaction, drive the whole engine headlessly,
and read a debug log that explains what the UI did.

---

## Decisions & deviations log (newest first)

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

- Nothing in P0 has been built or verified yet; every claim above comes from reading the source on
  2026-08-16.
- The host used for that reading is Windows 10 + MSYS2 MinGW64. `build/` (Debug, Ninja) and
  `build-mingw64/` (Release, Ninja) both contain a working `cosmo.exe`; `build/` is the one wired to
  `.vscode`. The distinction between the two trees is undocumented — resolve it in P0.11.
- `${XDG_CONFIG_HOME:-$HOME/.config}/cosmo_v2/` exists on this host but is **empty** — no log, no
  settings, no recents — so the log has never been confirmed to be written here in practice. Confirm
  before trusting it as a debug channel (see D-8).
