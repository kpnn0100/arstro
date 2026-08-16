# cosmo — Defect List

**Committed, so it travels between machines and sessions.** Filed and closed by
`.claude/skills/arstro.cosmo.core.debug/` and `.claude/skills/arstro.cosmo.design.debug/`; the entry
format is defined in `arstro.cosmo.core.debug` §4 and is shared by both.

- IDs are `D-<n>`, sequential across both areas, **never reused**. Next free id: **D-10**.
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

### D-6 — A project cannot be opened from the command line
- **Area:** core / CLI · **Status:** Confirmed (by source inspection) · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `./build/apps/cosmo/cosmo.exe some.cmp` — `openPath()` special-cases only `.cosmo`, so
  a `.cmp`/`.cosmoproj` falls through to `openImageFile()` and fails to decode.
- **Expected:** the primary document format is openable from a shell.
- **Actual:** only bare images and `.cosmo` sessions are. Seeding `recent.tsv` is the sole way to make a
  project reachable, and that only puts it on the home screen.
- **Judgement:** requirement gap — no requirement covers command-line invocation at all.
- **Fix:** pending. P0.7 (`--project`).

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

*(none yet — the first closed entry lands here with its commit hash and the test that guards it)*
