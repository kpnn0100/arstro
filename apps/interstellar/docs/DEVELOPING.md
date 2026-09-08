# Interstellar — how to work on it

> **Nothing is built yet.** This file is written first on purpose: `arstro.rule` §2 calls a
> doc-authority table *"the single highest-leverage doc in the repo"*, and it costs ten lines. §4
> below describes commands that **do not exist yet** and says so in every row — per `arstro.rule`
> §8, a document may not name a tool that does not exist without marking it.

## 1. What Interstellar is, in one paragraph

A non-linear video editor for **colour and cut**. It does not own a colour engine: it **hosts
Cosmo** — a real `cosmo::CosmoService`, in-process, as the single authority for every source's
colour — and adds the timeline, the compositor, an address space over every parameter, automation
clips, expression bindings, playback and a deterministic master render. Its project is a Nebula text
document that contains **no colour at all**; colour lives in the `.cmp` the rack embed names, which
is why an edit made in Interstellar is an edit to the Cosmo project.

## 2. Where things will be

```
apps/interstellar/
  REQUIREMENTS.md        intent tier — R-<AREA>-<n>, status in the heading
  README.md              the brief (and, in §9, the superseded design)
  linux_main.cpp         host: GTK, FFmpeg, sockets, fonts, the budget, the decoder
  App.{h,cpp}            app: screens, the 4 workspaces, the Segment tree
  Theme.{h,cpp}          ALIASES cosmo_v2's token namespaces; compiles cosmo/Theme.cpp
  EditCommands.{h,cpp}   the one place a moved control becomes a Command
  widgets/               Monitor, TimelineView, LaneStack, ObjectList, BindingInspector,
                         ExpressionEditor, Transport, RackTree, SourceBin
  core/                  UI-FREE. service/{InterstellarService,Command,Event,AppModel,
                         AppModelCodec,ApiDoc}, Project, Timeline, ParamRegistry,
                         Automation, BindingGraph, Evaluator, Composite, FrameCache,
                         RackEmbed, RenderJob, Playback
  cli/main.cpp           interstellar-cc — argv, stdout, codecs, filesystem, NO behaviour
  tests/                 round-trip · golden frames · resolved-value tables · scripts
  docs/                  everything in §3
```

And the two things outside this directory that Interstellar depends on:

- `apps/cosmo/core/` — **hosted**, not copied. Interstellar links `cosmo_core`.
- `core/Gene/`, `core/Nebula/` — shared libraries that **do not exist yet** (see
  [`plan.md`](plan.md) §P0.2).

## 3. Which document decides what

| question | the answer lives in |
| --- | --- |
| What was asked for, and what is its status? | [`../REQUIREMENTS.md`](../REQUIREMENTS.md) — **intent + history**, `R-<AREA>-<n>`, status in the heading |
| What does the code actually do today? | [`requirements.md`](requirements.md) — **as-built**, `DR-<AREA>-<n>`, `file:line` anchors. Empty: no code |
| What is done, what is next? | [`PROGRESS.md`](PROGRESS.md) — **the ledger**, and the only authority on status |
| Is this a known bug? What did we learn? | [`DEFECTS.md`](DEFECTS.md) — `D-<n>`, never renumbered, nothing deleted |
| Why is it shaped this way? What was rejected? | [`design.md`](design.md) |
| Where does a module live, and what are the seams? | [`architecture.md`](architecture.md) + [`architecture.puml`](architecture.puml) |
| How does this class work, and what are its traps? | [`detailed_design.md`](detailed_design.md) |
| What exactly is in an `.isp` file? What is the command grammar? | [`project-format.md`](project-format.md) |
| What is every parameter called? | [`project-format.md`](project-format.md) §4–5, and `api.json` once it is generated |
| How do automation and bindings evaluate, and in what order? | [`binding.md`](binding.md) |
| What should the UI look and feel like? | [`ui-brief.md`](ui-brief.md), over `.claude/skills/arstro.design.rule` |
| What is the build order and what is each gate? | [`plan.md`](plan.md) |
| What are the rules here, whatever I am doing? | `.claude/skills/arstro.rule` — invoked first by every skill |
| Which skill do I run? | **None exists yet.** Until `arstro.interstellar.*` skills are written, run `arstro.rule` + `arstro.design.rule` directly, and follow this file's §3 and §6 |

**Both requirement tiers are authoritative, for different things, and every change touches both.**
If one exists and agrees, implement it and update its `DR-` entry; if one conflicts, **amend it in
the document** with a line of why; if none exists, write it first. A change with no requirement does
not ship, and neither does a requirement that no longer matches the code — that second failure is
Cosmo's D-1.

**And status lives in the ledger, never here. If the two disagree, the ledger wins and this file is
the bug.**

## 4. Build, run, prove — *none of this exists yet*

Planned, and written down so the first commits build the right thing. Every row is marked.

```bash
# ── planned ──────────────────────────────────────────────────────────────────
cmake -S . -B build -DARSTRO_BUILD_INTERSTELLAR=ON && cmake --build build -j
./build/apps/interstellar/interstellar                  # the editor (needs a display)   [P10]
./build/apps/interstellar/cli/interstellar-cc           # the CLI (needs none)            [P1]
./build/apps/interstellar/core/interstellar_core_tests  # the core suite                  [P1]
./build/apps/interstellar/interstellar_render_tests     # golden frames                   [P2]
./build/apps/interstellar/interstellar_shots            # headless PNG shots             [P10]
./build/apps/interstellar/interstellar_ui_tests         # headless layout assertions     [P10]
ctest --test-dir build                                  # every suite in the umbrella
```

**A structural blocker to fix in P1, before it bites.** Cosmo's `CMakeLists.txt` globs `*.cpp`
*including* `linux_main.cpp`, so no second `main()` can link its UI — which is why its shot harness
needed a `COSMO_APP_NOMAIN` list-removal added after the fact. Interstellar's CMake must exclude its
`linux_main.cpp` from the app library **from the first commit**, so `interstellar_shots` and
`interstellar_ui_tests` can link the UI without a retrofit.

## 5. Driving it from a shell — the part that matters

Everything must be reachable with no display (`arstro.rule` §5, R-G-4). The four capabilities, and
the command that gives each:

| | how |
|---|---|
| **discover** a feature | `interstellar-cc api --json` — the generated document, including the whole address space |
| **reach** it | one `Command`, in the grammar of [`project-format.md`](project-format.md) §8 |
| **observe** it | an `AppModel` field, or an `Event` line from `--watch` |
| **assert** it | `state print --stable` diffed, or `expect <event-prefix>`, or `eval <address> --at <t>` |

`eval` deserves its own line: **it is the unit of evidence for automation and bindings.** A
resolved-value table asserted through `eval` catches a mapping error, a mode error, an evaluation-
order regression and a broken expression, with no display, no codec and no tolerance.

## 6. The invariants — break one and something lies silently

1. **One authority per fact.** Colour only in the rack; one producer per parameter per frame; the
   Cosmo model carried by value, never mirrored field-by-field.
2. **The core touches no OS.** A codec, a socket, an `argv` or a `getenv` below the host is a
   layering bug. It is also what would make a master render need a display.
3. **`Evaluator::resolve` is pure.** No cache survives a parameter change; no global state; no
   dependence on evaluation history. Determinism (R-NFR-1) rests entirely on this, and so does every
   golden test.
4. **The frame-cache key hashes every resolved value that fed the layer** — not just the
   `EditParams` struct. A key that missed a shape a link maps in would serve a stale frame, and a
   stale frame is indistinguishable from a rendering bug.
5. **The order in [`binding.md`](binding.md) §6 is a contract.** Time → automation → bindings → rack
   composition → colour → geometry → composite → output. Any other order is a different picture.
6. **One `ThreadBudget`, held by reference, injected into the hosted Cosmo service.** Two owners
   converting one percentage is Cosmo's D-11, which produced 17 threads on a 16-core box.
7. **An unknown input is rejected naming itself.** Cosmo's D-59 (`set exposre=1.2` returning
   success) is far more likely here, because the address space is hundreds of names deep.
8. **Announce before you mutate.** `ProjectOpening` before `resetWorkspace`, so a view may clear
   itself when it hears a project is opening. Cosmo learned this by attaching 18 decoded images to
   nothing (D-13).
9. **`takeFrame` moves the frame, so exactly one owner may call it** — the service. While the view
   was the owner, no headless front end could tell a frame had arrived (Cosmo's D-21).
10. **A validation error refuses; a numeric corruption repairs and reports.** A colour field on a
    clip is refused; a stray `nan` is neutralised with a count (Cosmo's D-36).

## 7. The mistakes already made elsewhere — do not re-make them

Each of these cost another app in this repo real time. They are cheap to avoid while there is no
code.

- **A forked token file** — genesis copied Cosmo's palette and it drifted. `arstrobench` *aliases*
  and compiles `cosmo/Theme.cpp` and has cost nothing. Alias.
- **Fonts resolved at runtime** — genesis loads TTFs through Fontconfig from a build-time path and
  treats a miss as non-fatal, so it silently falls back to a generic sans. Cosmo compiles the fonts
  into the binary. Embed them.
- **`estimateTextWidth`** — `len × px × 0.6`, font-independent by construction and therefore wrong
  for any real font; it detached Cosmo's wordmark dot when the typeface changed. Use
  `IRenderTarget::measureText`, and measure only during a render.
- **A hand-maintained catalogue beside a generated one** — Cosmo's `--help` names are generated and
  right; the argument hints beside them are hand-written and missing for 8 of 30. Generate both.
- **Two budgets** (D-11), **a per-thread ICV set on the wrong thread** (D-12), **a log line nobody
  ever ran as proof** (R-CPU-4), **a clip with no scroll** (R6's failure mode), **a `hitTestSelf`
  returning bare `true`** (four panels; the topmost ate every click).
- **A doc naming a tool that does not exist** — four things Cosmo's skills told agents to run had
  never existed. Check that a command resolves before writing it as an instruction; if it does not,
  write it as a gap, marked.

## 8. Definition of done

`arstro.rule` §9 and `arstro.design.rule` §10 in full. The Interstellar-specific additions:

- [ ] The requirement was read first, written or amended, conflict-checked — **before** the code.
- [ ] Both tiers updated; ids stable, never renumbered, never reused.
- [ ] Behaviour landed in the core, not in a view; nothing new reaches past the service seam.
- [ ] **Colour did not leak out of the rack**, and no `EditParams` was stored outside it.
- [ ] The address space, if it grew, grew **in the registry** — and `api.json` was regenerated and
      its drift test passes.
- [ ] The evaluation order is unchanged, or the change is a requirement amendment with a re-baked
      golden set and a stated reason.
- [ ] `ctest` reports `0 failed`, and the new test fails without the fix — checked, not assumed.
- [ ] Ledger ticked, **NEXT** rewritten, defects updated with the commit hash and the guarding test.
- [ ] Committed to `main`, **pulled, re-tested, and pushed** — submodules fully pushed *before* the
      umbrella records their pointers.

## 9. Known limits, honestly

- **Nothing is implemented.** Every document here is a specification; rung 0 of `arstro.rule` §5.
- **Three dependencies are not started**: Nebula, `core/Gene`, and Cosmo's video-source support.
  Two of them are other units' commits.
- **The per-frame colour throughput is unmeasured** — P2's gate is that measurement, and the default
  proxy level depends on it.
- **Audio is a bed and nothing more** in v1 (R-SCOPE-5); sound design is Solaris's.
- **No touch shell.** Cosmo's R-TOUCH metric exemption does not apply here, and a phone video editor
  is not a v1 goal.
- **No skills exist for this app yet.** Until they do, work runs under `arstro.rule` +
  `arstro.design.rule` with this file's §3 and §6 as the local law.
