# cosmo — the core as a service, the UI as a listener

**Status: PROPOSAL, awaiting approval.** Nothing here is built yet. It is written as a proposal
rather than as `architecture.md` because it changes the app's shape, and that is the user's call.
Once approved it becomes `R-SVC-*` in [`../REQUIREMENTS.md`](../REQUIREMENTS.md), folds into
[`architecture.md`](architecture.md), and P0 in [`PROGRESS.md`](PROGRESS.md) is rewritten around it.

---

## 1. What is being asked, and why it is the right shape

> "All core function implemented independently of the UI, so we can run the app in CLI mode or full
> UI mode and still look at the same thing. The UI contains no logic. You can control the app
> without looking at the UI, and I can still see what happens on the UI. The CLI and the GUI are
> listeners only; the core app is a service."

The layering doc already claims most of this (`architecture.md` §2.1/§2.3: "this is what lets the
same session/engine be driven headlessly"). **It is not true today**, and two defects found while
debugging the CPU budget show exactly where the claim breaks:

- **Opening a project — the single most important core behaviour — lives in the GTK host.**
  `startEntriesLoad` / `decodeEntry` / `pollLoad` are functions in `linux_main.cpp` (lines 570-732),
  not in `cosmo_core`. There is therefore no way to open a project without a mouse (D-6), which is
  why the CPU-budget log line shipped as `[!]` unverified, and why a 25%-budget load could not be
  measured at all in the session that reported it.
- **The CPU budget is read in three unrelated places by three different owners** — the pool in the
  host (`linux_main.cpp:724`), the engine's thread count in the app (`App.cpp:869`), and an
  environment variable at `main()` (`linux_main.cpp:1282`). Nobody owns the total. Two of the three
  are wrong as a result (D-11, D-12 in [`DEFECTS.md`](DEFECTS.md)).

Both are the same root cause: **application behaviour lives in the two layers that need a window.**
So the proposal is not "add a CLI". It is *move the behaviour down one layer* — where the docs
already say it is — and let every front end, including the CLI, be a view of it.

---

## 2. The shape

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  FRONT ENDS  — no logic, interchangeable, may run at the same time           │
│                                                                              │
│   cosmo (GTK+Artboard)    cosmo-cc (text/JSON)    cosmo_shots (PNG)          │
│   draws AppModel          prints AppModel         renders AppModel           │
│   emits Command           emits Command           emits Command              │
└───────────────▲──────────────────▲────────────────────▲──────────────────────┘
                │ Event / AppModel │                    │
                │ Command          │                    │
┌───────────────┴──────────────────┴────────────────────┴──────────────────────┐
│  CosmoService  — cosmo_core, no Artboard, no GTK, no getenv, no logging       │
│                                                                              │
│    dispatch(Command)  ->  mutates AppModel, emits Event                      │
│    pump(nowMs)        ->  drains workers, emits Event  (called by the host)   │
│    model()            ->  const AppModel&  (+ revision counter)              │
│                                                                              │
│    owns: EditSession · project load pipeline · export queue · settings ·      │
│          recents · presets · ThreadBudget                                    │
└───────────────┬──────────────────────────────────────────────────────────────┘
                │ injected seams (the host supplies the platform)
        IImageDecoder · IFileStore · IClock · ITaskPool · ILogSink
```

Three rules make it hold:

1. **One way in: `Command`.** A front end never calls `EditSession`, never touches `EditParams`,
   never opens a file. It builds a `Command` and dispatches it. If a front end can reach a
   behaviour that a `Command` cannot, that is a bug in the command set.
2. **One way out: `AppModel` + `Event`.** `AppModel` is the whole observable state as plain data
   (no Artboard types, no pixels — frames are referenced by id). `Event` is what just changed.
   A front end renders the model and reacts to events. It computes nothing it could be told.
3. **The service never blocks and never owns a thread policy.** `pump(nowMs)` is called by whoever
   is driving — a GTK timeout at 60 Hz, or a CLI `while` loop at 1000 Hz, or a test at a fixed 16 ms
   tick. That is what makes a scripted run deterministic and a live run smooth, with one code path.

### 2.1 What "the UI contains no logic" means precisely

A widget may: read `AppModel` fields, animate its own presentation, hit-test itself, and emit
`Command`s. A widget may not: decide *what* a click means beyond naming a command, hold state that
is not purely visual, compute a value another front end would also need, or call into
`EditSession`/`EditEngine`.

Concretely, from today's code: screen transitions, easing, hover, scroll offsets, the loading
animation and the reveal **stay** in the UI (they are presentation). Selection, group tree edits,
undo/redo, params fan-out, copy/paste settings, preset apply, the export batch, project open/save
and the recents list **move** into the service. `App.cpp`'s ~40 non-render methods become ~8.

### 2.2 Text is the second interface, generated from the first

Every `Command` and `Event` has a text form, produced by one codec, so the grammar cannot drift:

```
# commands (stdin, --script file, or the control socket)
settings set cpuPercent=25 previewEdge=1600
project open /tmp/japan18.cmp
select 3
set exposure=1.2 temp=7000
group new "Tokyo" --parent 0
undo
export --outdir /tmp/out --format jpg --quality 90 --long-edge 2048
state print --json
wait load-finished --timeout 120s
expect model selection == 3

# events (stdout, --watch, the journal, and the debug log — same line)
[evt] project.opening name=japan18 entries=18
[evt] load.progress done=6 total=18 workers=6 budget=25% cores=24
[evt] frame.ready slot=3 1600x1067 backend=cpu ms=41.2
[evt] load.finished entries=18 decoded=18 failed=0 ms=18432
```

`state print` is the shared truth: the CLI prints the same `AppModel` the GUI is drawing, so "look
at the same thing" is checkable rather than hoped for. A golden `state print` is how a UI change is
proven not to have changed behaviour.

### 2.3 Driving the running GUI — the user's actual goal

`cosmo --control <path>` opens a control channel (unix socket; named pipe on Windows) that accepts
exactly the grammar above and streams events back. The service stays **in-process** with the GUI.

```bash
cosmo --control /tmp/cosmo.sock &            # the user watches this window
cosmo-cc attach /tmp/cosmo.sock <<'EOF'      # the agent types here
settings set cpuPercent=25
project open /tmp/japan18.cmp
wait load-finished
state print
EOF
```

**Why in-process rather than a real daemon:** a preview frame is 5-20 MB and a full-res frame ~100 MB.
A separate service process would have to ship those over a socket or into shared memory on every
frame, which buys nothing here — the front ends are not remote, they are alternative faces of the
same program. The command/event channel is small and text; the pixels never cross a boundary. If a
true multi-process daemon is ever wanted (a web front end, a render farm), this design is the
prerequisite for it, not an obstacle: the channel already exists, only the transport changes.

---

## 3. The pieces to build

| # | Piece | Where | What it is |
|---|-------|-------|-----------|
| 1 | `AppModel` | `core/service/AppModel.h` | Plain-data snapshot: `screen`, `recents[]`, `nodes[]` (tree, slot, pending, failed, bypass, name), `selection`, `params` for the selected slot, `history{depth,current,canUndo,canRedo}`, `load{done,total,workers,status}`, `export{done,total}`, `settings`, `frames[]` metadata, `revision` |
| 2 | `Command` + codec | `core/service/Command.{h,cpp}` | Tagged struct, one case per behaviour; `parse(line)` / `format(cmd)`. The struct is the truth, the text is generated |
| 3 | `Event` + codec | `core/service/Event.{h,cpp}` | Same treatment. `format()` output *is* the log line and the journal line |
| 4 | `CosmoService` | `core/service/CosmoService.{h,cpp}` | `dispatch` / `pump` / `model` / `subscribe`. Owns `EditSession`, the load pipeline, the export queue, settings, recents |
| 5 | `ProjectLoader` | `core/service/ProjectLoader.{h,cpp}` | `startEntriesLoad`/`decodeEntry`/`pollLoad` moved out of `linux_main.cpp`, unchanged in behaviour, now with no GTK: `start(entries)` + `poll()` over `OrderedParallelLoad` |
| 6 | `ThreadBudget` | `core/service/ThreadBudget.{h,cpp}` | **The one owner of the CPU budget.** Hands out `decodeWorkers()` and `engineThreads()` from a single total, so the sum is what the user chose (fixes D-11) |
| 7 | Seams | `core/service/Seams.h` | `IClock`, `IFileStore`, `ITaskPool`, `ILogSink` (+ existing `IImageDecoder`). Keeps `getenv`/logging/OS paths out of `cosmo_core`, per the layering invariant |
| 8 | `ControlChannel` | `apps/cosmo/host/ControlChannel.{h,cpp}` | Host-side socket/pipe reader → `dispatch`; event stream out. Not in `cosmo_core` (it is OS code) |
| 9 | `cosmo-cc` | `apps/cosmo/cli/main.cpp` | Front end #2: argv/stdin/`--script`/`attach`, `--json`, `--watch`. Subsumes A1-A9 of the harness plan |
| 10 | `App` as a view | `apps/cosmo/App.{h,cpp}` | Keeps `render`/`pointer`/`wheel`/`key`/`setSize` + animation state; every `std::function<void()> on*Requested` collapses into `emit(Command)` |

`ITaskPool` matters more than it looks: the GUI gives the service a real thread pool, and
`cosmo-cc --serial` gives it a synchronous one. Same load code, deterministic in tests, parallel in
the app — and it is the seam where a CPU budget can actually be enforced in one place.

---

## 4. Migration — strangler, five commits, each shippable

No big-bang rewrite; the app keeps working after every step.

- **S1 — `ProjectLoader` + `ThreadBudget` move into `cosmo_core`.** `linux_main.cpp` calls the new
  class instead of its own functions. Nothing else changes. *This alone makes the CPU bug
  measurable from a shell and fixes D-11/D-12.* Proof: a `cosmo_core_tests` case that opens an
  18-entry project with a fake decoder and asserts peak concurrent workers ≤ budget.
- **S2 — `AppModel` + `Command`/`Event` + `CosmoService` skeleton**, wrapping the existing
  `EditSession`. The GUI dispatches commands for open/select/undo/redo/set-param and reads the model
  for those; everything else stays on the old path. Two front ends now exist.
- **S3 — `cosmo-cc`** over the service: `info`, `backends`, `project open/print`, `set`, `render`,
  `state print`, `export`, `bench`, `--script`, `--json`. P0.8-P0.10 collapse into this.
- **S4 — the rest of `App`'s logic moves down**: export batch, presets, copy/paste settings, group
  ops, save/load workspace. `App.cpp` ends as render + gestures + animation.
- **S5 — `ControlChannel`**, then the acceptance test in §2.3 becomes the standing proof: the agent
  drives, the user watches.

Rollback is per-commit: each step leaves both paths compiling, and S1-S3 do not touch `widgets/`.

---

## 5. How this is judged done

1. `cosmo-cc project open X.cmp --wait --state-print` and a GUI open of `X.cmp` produce the **same
   `AppModel` dump** (modulo animation fields, which the dump excludes by design).
2. Every behaviour in `PARITY.md` and every `R-*` requirement is reachable by a `Command`; the
   command list is generated from the enum, so a behaviour with no command fails a test.
3. `grep -n "mSession\.\|EditParams" apps/cosmo/widgets/*.cpp` is empty.
4. A load's peak concurrency, measured, is ≤ the chosen budget (§3 item 6).
5. The acceptance test in §2.3 runs unattended in CI-shaped form: launch, drive over the socket,
   assert the event stream, exit non-zero on a missing event.

---

## 6. Costs, honestly

- It is a **large refactor of `App.cpp`** (1378 lines) and `linux_main.cpp` (1374 lines). S1-S3 are
  contained; S4 touches the widget wiring and will surface behaviour that only exists implicitly in
  a callback today.
- **Two crossings of the skill boundary.** `App.{h,cpp}` and `widgets/` belong to
  `arstro.cosmo.design.implement`; S2 and S4 need a core commit followed by a design commit, in that
  order, as the skill requires.
- **A command set is a contract.** Once `cosmo-cc` scripts exist, renaming a command breaks them —
  the same discipline the log lines already have.
- **What it does not buy:** no speedup, no new user-visible feature. It buys testability,
  and it makes the class of bug found this session (behaviour stranded in the host, unmeasurable,
  shipped unverified) structurally impossible.
