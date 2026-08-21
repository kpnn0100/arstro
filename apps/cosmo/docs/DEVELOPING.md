# Developing cosmo

For the next person — or the next agent — picking this up cold. It says which build path is the
one, where things are, which document decides what, the invariants that are load-bearing rather
than stylistic, how to drive the whole app from a shell, and the mistakes this codebase has already
made.

Read the repo `README.md` first if you have never *used* cosmo; this file assumes you have.

**Status lives in [`PROGRESS.md`](PROGRESS.md), never here.** This file says how to do things, not
what is finished. If the two disagree, the ledger wins and this file is the bug.

---

## 1. What cosmo is, in one paragraph

cosmo is a non-destructive photo editor: a project (`.cmp`) is a tree of groups and images, an edit
is an `EditParams` struct rather than a mutation of pixels, and every surface — the GTK window, the
`cosmo-cc` CLI, a script over a socket — is a **view onto one `CosmoService`**. That last part is
the newest and most important thing about the codebase: since S1–S5 (`R-SVC-1…10`) the application
is a service that runs with no window at all, a `Command` goes in, an `AppModel` and a stream of
`Event`s come out, and the GUI has no privileged path to anything. If you find yourself adding a
getter to `CosmoService`, or reaching through `session()`, or teaching a widget to call
`EditSession` directly, stop: that is the one shape the architecture is built to prevent, and
`PROGRESS.md`'s S4b counts every remaining instance of it as a number that must reach zero.

## 2. Where things are

```
apps/cosmo/
  core/                    cosmo_core — UI-free, owns NO Artboard type, no logging, no getenv
    service/                 THE application with no front end attached (R-SVC). Read this first.
      CosmoService.{h,cpp}     dispatch(Command) · dispatchText(line) · pump(nowMs) · model() ·
                               subscribe(sink). Owns the load, the export, settings, recents.
      Command.{h,cpp}          the one way IN; parseCommand/formatCommand are the single codec
      Event.{h,cpp}            the one way OUT; formatEvent() output IS the log line
      AppModel.h               the entire observable state as plain data — no pixels, no widgets
      AppModelCodec.{h,cpp}    formatModel(m, {stable, json, params}) — what `state print` emits
    EditSession.{h,cpp}      THE session. EditSession.h is the de-facto spec; read it first.
    History.{h,cpp}          a BRANCHING DAG of full EditParams snapshots, per slot AND per group
    ProjectLoader.{h,cpp}    the project load: decode pool, per-worker decode + thumbnail,
                             delivery strictly in entry order
    ThreadBudget.{h,cpp}     the ONE owner of the CPU budget, and the MEASURED peak (R-SVC-10)
    ProjectStore.{h,cpp}     configDir() + recent.tsv        AppSettings.{h,cpp}  settings.txt
    OrderedParallelLoad.h    produce-on-a-pool, consume-strictly-in-order, dual back-pressure
    decode/                  IImageDecoder seam + Native (GdkPixbuf/LibRaw) + Android (stb/LibRaw)
    tests/sessionTests.cpp   cosmo_core_tests — plain assert(), the last several drive the real
                             service with a fake decoder and no display
    tests/fixtures/*.c       standalone C fixtures that PROVE a platform claim (omp_env_order.c,
                             omp_pin.c). Compiled by hand, not by CMake — see §7, D-12.
  cli/main.cpp             cosmo-cc: the second front end over the same service
  App.{h,cpp}              the editor UI: screens, transitions, gestures, animation
  widgets/                 TopBar, LeftRail, CenterStage, RightColumn, dialogs, HomeScreen …
  linux_main.cpp           the GTK host: window, dialogs, fonts, logging, the batch export, and
                           the adapter that turns the service's events into App's animation
  Log.{h,cpp}              the log facility + crash handler (host-side ON PURPOSE — see §6)
  ControlChannel.{h,cpp}   the AF_UNIX control socket behind `--control` (POSIX only, D-17)
  ExportWriter.{h,cpp}     OS I/O + GdkPixbuf encode + JPEG APP1 / PNG sRGB metadata
  OmpPin.{h,cpp}           pin a decode worker's nested OpenMP team to one thread, via
                           dlsym/GetProcAddress so nothing links OpenMP
  tests/acceptance/        run.sh + drive-a-live-window.txt — the unattended end-to-end test
  docs/                    requirements / architecture / detailed_design / design / this file
core/ImageProcessing/src/  arstro_image — headless, codec-free, platform-free
  engine/EditParams.h        THE parameter struct; composeParams() is the group-stacking law
  engine/EditEngine.{h,cpp}  slots, flat setters, proxy cache, the fixed render pipeline
  engine/RenderService.*     the threaded seam the UI binds to (coalescing, move-in add)
  engine/EditParamsIO / Apf  text serialization + the .apf preset envelope
```

Two of cosmo's dependencies are **git submodules** — `core/Artboard` and
`core/ImageProcessing/lib/LibRaw` (and `core/DigitalSignalProcessing`, which cosmo does not use).
A cold clone needs `git clone --recurse-submodules`, or `git submodule update --init --recursive`
after the fact. A change inside a submodule is two commits: one in the submodule, one in the
umbrella bumping the pointer.

**RAW decode is optional and easy to lose.** CMake takes a system `libraw` if pkg-config finds one,
else the vendored tree — but only if `core/ImageProcessing/lib/LibRaw/lib/libraw.a` has been
*built*, which nothing in the CMake path does for you:

```bash
make -C core/ImageProcessing/lib/LibRaw -f Makefile.dist lib/libraw.a -j8
```

Without it the configure line reads `cosmo_core: RAW disabled (LibRaw not found)` and every `.RAF`
in a project fails to decode with no other symptom. `cosmo-cc backends` prints `libraw=1` when it
is on — check there before concluding a RAW file is corrupt.

## 3. Which document decides what

| question | the answer lives in |
| --- | --- |
| What was asked for, and what is its status? | [`../REQUIREMENTS.md`](../REQUIREMENTS.md) — **intent + history**, `R-<AREA>-<n>`, status in the heading |
| What does the code actually do today? | [`requirements.md`](requirements.md) — **as-built**, `DR-<AREA>-<n>`, with `file:line` anchors |
| What is done, what is next? | [`PROGRESS.md`](PROGRESS.md) — **the ledger**, and the only authority on status |
| Is this a known bug? What did we learn from it? | [`DEFECTS.md`](DEFECTS.md) — `D-<n>`, never renumbered, nothing deleted |
| Why is the code shaped this way? | [`design.md`](design.md) |
| Where does a class live and what does it promise? | [`architecture.md`](architecture.md) + [`architecture.puml`](architecture.puml) |
| How does this class work, and what are its traps? | [`detailed_design.md`](detailed_design.md) |
| Why is the core a service at all? | [`service-architecture-proposal.md`](service-architecture-proposal.md) — the design behind R-SVC, approved 2026-08-17. **Not a ledger**: it is not updated per commit |
| What is still missing versus the original app? | [`../PARITY.md`](../PARITY.md) |
| What should the UI look and feel like? | `.claude/skills/arstro.design.desktop` (R1–R6) |
| How do I work on this? | `.claude/skills/arstro.cosmo.core.implement` (non-UI) · `arstro.cosmo.design.implement` (widgets/layout/theme) · the two matching `.debug` skills |

**Both requirement tiers are authoritative, for different things, and every change touches both.**
Requirements come before code: if one exists and agrees, implement it and update its `DR-` entry;
if one exists and conflicts, **amend it in the document** (marked `**AMENDED (R-…)**` with a line of
why — R-LOADING/R-LOADPERF is the house-style example); if none exists, write it first. A change
with no requirement does not ship, and neither does a requirement that no longer matches the code —
that second failure is exactly D-1.

## 4. Build, run, prove

**CMake is the canonical build, on every platform. Use `build/`.**

```bash
cmake -S . -B build && cmake --build build -j        # from the umbrella root
./build/apps/cosmo/cosmo                             # the editor (needs a display)
./build/apps/cosmo/cli/cosmo-cc                      # the CLI (needs none)
./build/apps/cosmo/core/cosmo_core_tests             # the core suite
./build/apps/cosmo/cosmo_widget_tests                # widget hit-testing
ctest --test-dir build                               # every suite in the umbrella
```

On Windows the same commands run in the **MSYS2 MINGW64** shell, which is where GTK3, Cairo,
GdkPixbuf, LibRaw and pkg-config come from; the root `CMakeLists.txt` carries the `pacman` line.

**`./build.sh --project cosmo --target linux-native-app` is not a supported way to build cosmo.**
It is a hand-rolled one-shot `g++` producing `apps/cosmo/build/cosmo_linux`, and today it does not
even link:

```
/usr/bin/ld: /tmp/ccSJbzUQ.o: in function `main':
linux_main.cpp:(.text.startup+0x0): multiple definition of `main';
      /tmp/ccJu58r8.o:main.cpp:(.text.startup+0x0): first defined here
```

— because its source glob is "every `.cpp` under `apps/cosmo` except `tests/`", and since S3 that
set contains two `main()`s (`linux_main.cpp` and `cli/main.cpp`). Even repaired it would still be
the wrong tool: it builds no tests and no `cosmo-cc`, so nothing it produces can be verified; it
hardcodes `-lEGL -lGL` and `nproc`, so it cannot build on Windows at all; and its defines are
maintained by hand next to CMake's, which is an ODR violation waiting to happen (the decisions log
records one that segfaulted in `~EditSession` and read as a teardown bug). `build.sh` is still the
entry point for the rest of the umbrella — the Emscripten web builds (scope, studio, synth, pulsar,
ui-demo), the other native apps (ui-demo, synth, piano, pulsar), and cosmo's **Android** build,
`--project cosmo --target android-app`, which just `exec`s `apps/cosmo/android/build_apk.sh`. For
cosmo on the desktop it is dead weight. If it is ever revived, `--target native-test` has to cover
cosmo too: today it builds and runs the tests of the three `core/` libraries only, and reaches
neither cosmo nor genesis.

**Two Ninja trees exist on the Windows dev host and only one of them is real.** `build/` is the
tree `.vscode/settings.json`, `tasks.json` and `launch.json` are all pinned to, the one
`ctest --test-dir build` reads, and the default second argument of the acceptance script — every
command in this file and in `DEFECTS.md` assumes it. `build-mingw64/` is one developer's second,
Release-configured tree on that host; **nothing in the repo references it**, and it does not exist
on the Linux host at all. Treat it as private: never write a command that assumes it, and if you
want a Release build, configure one explicitly rather than reaching for it.

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release -j
```

That matters more than it sounds, because **the build type is not obvious from the path.** A plain
`cmake -S . -B build` gives you **Release** — the root `CMakeLists.txt` forces it when nothing is
set — but `build/` on a machine where VSCode configured it first is **Debug**, which is what it is
on both dev hosts today. Debug changes how long everything takes (`DEFECTS.md`'s D-10 records
`cosmo_core_tests` at 1–2 minutes in Release on the Windows host and longer in Debug), so a timing
or thread-count claim that does not say which it was measured in is not a claim. Check, do not
assume:

```bash
grep '^CMAKE_BUILD_TYPE' build/CMakeCache.txt
```

Two more things about proving a change:

**Sandbox the config, always.** cosmo writes `settings.txt`, `recent.tsv` and `cosmo_v2.log` into
one directory, and a test run that writes your real one is a test run that can lose your recents.
`XDG_CONFIG_HOME` is the override that wins on every platform (D-8), so every scripted run sets it:

```bash
XDG_CONFIG_HOME=/tmp/cosmo-run ./build/apps/cosmo/core/cosmo_core_tests
XDG_CONFIG_HOME=/tmp/cosmo-run ./build/apps/cosmo/cli/cosmo-cc backends   # prints configDir=
```

`cosmo_core_tests` backs up and restores the real `settings.txt` if you forget, but only that one
file, and only if it survives the run.

**A suite listed by `ctest` but reported `Not Run` has simply not been built** — `ctest` does not
build. Run `cmake --build build` first, and use `ctest --test-dir build -N` rather than this file
to find out which suites the tree currently has; the list grows as work lands.

## 5. Driving cosmo from a shell — the part that is unusual

This is the point of the S milestone and it is what makes cosmo debuggable at all. Everything
below is the ordinary product surface; there is no test-only code path anywhere in it.

**`cosmo-cc` — the whole application, no window.** All of these were run to write this section:

```bash
cd /tmp && export XDG_CONFIG_HOME=/tmp/cosmo-run
CC=…/build/apps/cosmo/cli/cosmo-cc

$CC backends                       # cores, budget, GPU backend, libraw, ompPin, configDir
$CC info photo.RAF                 # one decode: size, RAW-or-not, decoder, ms
$CC check project.cmp              # validate only; exit 1 with the first error located
$CC project project.cmp --print --stable          # open through the service, dump the model
$CC params --print project.cmp                    # serialize the params in a .cmp/.cosmo/.apf
$CC params --diff before.cmp after.cmp            # field-level diff
$CC render photo.png -o out.png --set exposure=0.8 --set contrast=15 --long-edge 512
$CC export project.cmp --outdir out --format jpg --quality 90 --long-edge 800
$CC bench photo.png --iters 3                     # per-stage ms, so a perf claim is MEASURED
```

`run` replays the shared command grammar from a file or stdin, which is how you build a fixture
from nothing:

```bash
printf 'project new /tmp/demo.cmp\nimport /tmp/a.png /tmp/b.png\nwait load.finished\nproject save\n' \
  | $CC run -
```

Global flags worth knowing: `--watch` streams every `Event` as it happens (an Event **is** the log
line, R-SVC-5); `--stable` drops `revision`/`frameSeq`/`budgetPeak…` so a CLI dump and a GUI dump
compare equal (R-SVC-9); `--params` puts the full `EditParams` block in a dump; `--serial` forces
one decode worker, one engine thread and no GPU, which is how you take determinism out of the
question; `--json` for machine-readable output.

**`cosmo --control` + `cosmo-cc attach` — the same script against a live window.** The window keeps
its animation and its gestures; the script drives the service underneath it, and the event stream
comes back:

```bash
cosmo --control /tmp/cosmo.sock &
printf 'project open /tmp/demo.cmp\nwait load.finished\nselect next\nset exposure=0.75\nstate print --stable\n' \
  > /tmp/drive.txt
cosmo-cc attach /tmp/cosmo.sock --script /tmp/drive.txt --watch --timeout 60
```

```
[evt] screen.changed loading
[evt] project.opening name=demo entries=2
[evt] info load.started workers=8 engine=4 budget=12 entries=2
[evt] entry.decoded index=0 slot=0 name=a.png
[evt] load.progress done=1 total=2 name=a.png
[evt] screen.changed editor
[evt] load.finished decoded=2 total=2
```

`attach` exits non-zero if any command was rejected or a `wait` timed out, so it fails loudly
rather than half-working. **The control socket is POSIX-only** — on Windows it logs
`control channel is not implemented on Windows yet` and the app runs unattended (D-17), so there
`cosmo-cc run` is the whole harness.

**The acceptance test is the strongest single check in the repo.** It runs one committed script
through *both* front ends — headless, and through a live window over the socket — asserts the event
stream, and diffs the two `--stable` dumps:

```bash
apps/cosmo/tests/acceptance/run.sh <project.cmp> [build-dir]
```
```
PASS: the same script drove a headless service and a live window; 5 nodes;
      both dumps identical (R-SVC-9), event stream complete (R-SVC-8)
```

It needs a display and a project with **at least three images** (the script does two
`select next`s from the first). It skips with exit 0 where there is no display. Run it after
anything that touches the service, a widget's outbound path, or the command grammar — it is the
only check that can see a widget quietly stop routing through the service, and it found **D-16** on
its first run.

**A project can be opened from argv** (D-6): `cosmo project.cmp`, or `cosmo --project project.cmp`
when a bare path would be ambiguous with an image. It runs the normal animated load rather than
jumping to the editor; only a bare image list lands straight in the editor.

## 6. The invariants — break one and something lies silently

The full list is `.claude/skills/arstro.cosmo.core.implement` §0. These are the ones a newcomer
breaks first.

**`Command` in, `AppModel` + `Event` out — nothing else** (R-SVC-2/3). A front end never calls
`EditSession`, never mutates an `EditParams`, never opens a file. A behaviour a front end can reach
that no `Command` expresses is a **defect in the enum**, not a licence to reach past it. That is why
`CosmoService::session()` is marked transitional: every caller of it is a line S4 has to delete.

**A view holds presentation, and presentation is a real category** (R-SVC-4). Animation, easing,
transitions, hover and scroll offsets stay in the view. The service knows a load is 6-of-18; the
view knows the bar eases toward it. Anything a *second* front end would also need is not
presentation.

**One codec, never two hand-written representations** (R-SVC-5). `parseCommand`/`formatCommand` and
`formatEvent` are the only grammar there is. Two representations maintained by hand drift the first
time one grows a field — see D-16, which is exactly that, in the one place the codec does not
route.

**`pump(nowMs)` never blocks, and the service starts no thread on its own initiative** (R-SVC-6).
The caller drives the clock: a GTK timeout at frame rate, a CLI loop, a test at a fixed tick. That
single property is what makes a scripted run reproducible and a live run smooth on **one** code
path. Never add a `wait`, a `sleep` or a `join` inside `dispatch` — a `wait` belongs to the client,
because only the caller owns its loop.

**Emit the event describing what is ABOUT to happen before the state it describes exists.**
Subscribers run synchronously inside `dispatch`, and a view is entitled to clear its own state when
it hears "a project is opening". Announce, then mutate. This is D-13, and it cost 18 decoded images
that attached to nothing.

**`ThreadBudget` is the single owner of the CPU budget** (R-SVC-10). It converts the percentage
**once** into `total()` and hands out slices. Nothing else may convert `cpuPercent`; a second
conversion by a concurrent consumer is D-11 exactly.

**`cosmo_core` and `arstro_image` contain no logging, no asserts, no codec and no `getenv`** — they
must stay WASM/Android-portable, which is why `Log.{h,cpp}` lives in the app layer and not in
`core/`. The **one** standing exception is `ProjectStore::configDir()`, and it is a documented
exception rather than a precedent: do not add a second one, and do not cite it as licence.

**Slot id == index** in every `mSlot*` vector in `EditSession`, so `resetWorkspace()` must reset
slot-id assignment or the next open segfaults. **The engine works in linear light** — decode once at
ingest, encode once at egress; a processor that assumes sRGB is a bug. **Pipeline order is a
contract** (`EditEngine.h`); reordering it is a requirement change. **`composeParams` is asymmetric
on purpose** — scalars add, `temp` adds its offset from 6500, curves stack as deviation-from-identity,
masks concatenate, and crop and `curveLog` follow `base` because framing is per-item and never
stacks. Changing any of those changes what every existing project renders.

**The node tree is built before any pixel decodes** (R-LOADUX-1): leaves exist with `slot = -1,
pending = true`, so an out-of-order arrival can never reparent anything.

## 7. The mistakes already made (do not re-make them)

Every one of these is a closed entry in [`DEFECTS.md`](DEFECTS.md) with its commit and its guard.
The entries are worth reading in full; these are the one-line lessons.

| id | what happened | the lesson |
| --- | --- | --- |
| **D-11** | the CPU budget was converted independently by the decode pool and by the engine, which run concurrently, so a load peaked at ~2× what the user chose | if a number is a total, **one object owns it and divides it**; two consumers each taking "the budget" is two budgets |
| **D-12** | `OMP_NUM_THREADS=1` set from `main()` did nothing — libgomp reads the environment in an ELF constructor that runs *before* `main()` | a claim about threads needs a **measurement**, not an argument. This one was reasoned from the source, believed, and shipped wrong |
| **D-13** | the service built the pending tree and *then* announced the open; the host's handler ran synchronously and deleted all 18 nodes | **announce before you mutate.** No headless test could see it: with no subscriber doing real work, both orderings look identical |
| **D-14** | `state print --stable` parsed `--stable` into a throwaway and dropped it | an option that parses and is then ignored is **worse** than one that does not exist, because the caller believes it worked |
| **D-16** | `wait` meant a model predicate to one front end and an event name to the other, so one script passed live and failed headlessly | the single-codec rule **protects only what it routes**; `wait` is front-end-owned and so grew twice |
| **D-18** | `state print --params` inside a script was parsed and ignored — D-14 again, one layer up | when a `Command` carries options AND the front end has a global flag for the same thing, **the per-command value wins and the global is the default** |

Two more that are not in the table because they are about *method*:

- **D-10** — a failing assert in `cosmo_core_tests` used to hang instead of exiting, so a red suite
  was indistinguishable from a slow one — and on the Windows host that suite legitimately takes 1–2
  minutes, so the usual "it must be stuck" heuristic did not apply. Every verification claim in this
  project rests on "the suite is green"; a suite that cannot report red is not evidence.
  `core/tests/TestMain.h` fixes it, and both suites call `testMainInit()` first.
- **D-36** — that same header then failed to build on Windows, the platform it was written for, and
  took an unrelated suite down with it: `_set_abort_behavior` is declared by msvcrt and exported
  only by UCRT (a **link** error), and the `<windows.h>` it pulled in for one call still defines
  `near` as an empty macro, so `widgetTests.cpp`'s `bool near(a,b,eps)` stopped parsing. D-10's own
  entry had said its Windows half was *code-verified only* and asked for confirmation on MSYS2;
  nobody ran it. **"Code-verified on platform X" means unverified.** And a header included by more
  than one suite owes them a clean namespace — the failure it caused was in a file that had nothing
  to do with it, with a diagnostic naming neither the macro nor the header. R-TEST-1/2.
- **S4b-2, in the ledger** — a spec written from *reading* the code claimed curves, the mixer and
  grading each needed a new command family. Running it showed they were already addressable through
  `set`, because `EditParamsIO` names them. **Try it before you spec it.**

## 8. Definition of done

- The requirement is written and conflict-checked, in **both** tiers, before the code.
- `architecture.md` / `detailed_design.md` / `architecture.puml` match the code. A doc that
  describes a seam the code no longer has is a defect with an id (D-1), not untidiness.
- The change is reachable with no GUI: a `Command` in, an `AppModel` field or an `Event` out. If it
  cannot be exercised, inspected or asserted with no window in the loop, it is not finished.
- `cosmo_core_tests`, `cosmo_widget_tests` and `ctest --test-dir build` report 0 failed.
- The acceptance script passes if the change touched the service, a widget's outbound path or the
  grammar.
- The new behaviour has a test that **fails without the fix** — check that, do not assume it.
- `PROGRESS.md` and, if a defect was closed, `DEFECTS.md` are updated in the same commit, with the
  commit hash and the test that now guards it.

## 9. Known limits, honestly

- **The control socket is POSIX-only** (D-17). Deliberately stopped rather than half-built: a
  Windows named pipe needs a per-instance overlapped state machine whose failure mode is a hung UI
  thread, and it cannot be tested from the Linux host. Winsock `AF_UNIX` is the cheaper route and is
  recorded in `ControlChannel.cpp`.
- **The log is not levelled and has no categories** (D-2/D-3/D-4). `LOGD` is never called,
  `setStderrEcho()` is dead, GLib's own `g_warning`/`g_critical` never reach the file, and a Windows
  crash produces a signal name with no frames.
- **Input is not logged** — which widget consumed a click, and the screen/scroll/hover decisions,
  are still invisible. Session, load, selection, params, history and export all are logged, because
  an Event is the log line.
- **The GPU backend is one of two paths.** `--serial` and `--cpu` take the GPU out of the picture
  when you need a stable comparison; `cosmo-cc backends` says which is active.
- **The Windows halves of D-10 and D-12 are code-verified only.** Both need one confirmation from an
  MSYS2 shell; `PROGRESS.md`'s Verification notes say exactly what to look for.
