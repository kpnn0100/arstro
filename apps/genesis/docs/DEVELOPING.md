# Developing Genesis

For the next person — or the next agent — picking this up cold. It says where things are, which
document decides what, the invariants that are load-bearing rather than stylistic, the mistakes
this codebase has already made, and how to prove a change is right.

Read `README.md` first if you have never *used* Genesis; this file assumes you have.

---

## 1. What Genesis is, in one paragraph

Genesis is a desktop app for authoring an animated UI component and **exporting it as a C++ class**
built only on `<artboard/artboard.h>`. The document is five nouns — a name, a base to inherit, a
design size, params, and a tree of shapes whose every field is a Gene *expression* — plus reactions
(signal → steps → tracks). The bet the whole product rests on: **Genesis ships no runtime.** The
generated class includes Artboard and the standard library, nothing else. If you find yourself
wanting to add a `genesis_runtime` library that the export links against, stop: that is the one
decision that is not open.

## 2. Where things are

```
apps/genesis/
  core/                 genesis_core — headless, no GTK, no Artboard UI
    Json.{h,cpp}        a small reader/writer; the document format
    gene/Gene.{h,cpp}   the expression language: parse -> fold -> (evaluate | emitCpp)
    Document.{h,cpp}    the model + validate(); THE field table lives here
    BaseCatalog.{h,cpp} the authorable bases and their signals/reads
    Runtime.{h,cpp}     the interpreter that drives the live preview
    codegen/CppEmitter  the compiler that writes the class
    Verifier.{h,cpp}    compiles the emitted class and diffs it against the interpreter
  cli/main.cpp          genesis-cc: compile, --check, --verify, --trace, --new, --bases
  app/                  the editor, built on Artboard + a GTK3/Cairo host
    App.{h,cpp}         owns Document + Runtime + panels; ONE funnel: documentChanged()
    Theme.h             tokens (palette/type/radius/metrics) — cosmo's, deliberately
    widgets/            Chrome, ShapeTree, CanvasView, Inspector, ReactionsPanel, Modal, Panel.h
    main.cpp            the GTK host: window, frame clock, input, clipboard, splash
  samples/*.genesis     one verified component per base, plus SnapBack and ArstroLoading
  tests/
    genesisTests.cpp    genesis_tests — core (79)
    uiTests.cpp         genesis_ui_tests — the editor, headless through Cairo (40)
    renderShots.cpp     genesis_shots — writes PNGs of real screens for eyeballing
  docs/                 requirements / architecture / detailed_design / brief / this file
```

Artboard is a **git submodule** at `core/Artboard`. A change there is two commits: one in the
submodule, one in the umbrella bumping the pointer. Forgetting the second is the most common
mistake in this repo.

## 3. Which document decides what

| question | the answer lives in |
| --- | --- |
| What must the product do? Is this behaviour a bug? | `docs/requirements.md` (`G-*`, `R-G-*`) |
| What must Artboard do? | `core/Artboard/docs/requirements.md` (`FR-*`, `NFR-*`) |
| Why is the code shaped this way? | `docs/architecture.md` |
| How does this class work? What are the traps? | `docs/detailed_design.md` |
| What should the UI look and feel like? | `.claude/skills/arstro.design.desktop` (R1–R6) |
| How do I change Artboard? | `.claude/skills/implement_artboard` (V-model, per-stage) |

**Requirements first, always.** If a change has no requirement, write the requirement, check it
against the existing ones for conflict, resolve the conflict *in the document*, and only then
write code. Several requirements here were refined rather than added — `G-6` grew the "one field,
one track per step" rule; `G-22` grew the "the target is followed, not snapshotted" clause — because
the first version was not wrong so much as silent about a case.

## 4. Build, run, prove

```bash
cmake -S . -B build && cmake --build build -j8      # from the umbrella root
./build/apps/genesis/genesis                        # the editor
./build/apps/genesis/genesis_tests                  # core
./build/apps/genesis/genesis_ui_tests               # the editor, headless
./build/core/Artboard/../../artboard_tests          # (core/Artboard/build/artboard_tests)
ctest --test-dir build                              # all 12 suites
```

Three tools do the proving, in ascending order of strength:

**`--check`** — validate only. Warnings are the design checklist, errors block export.

**`--trace`** — what a field IS, frame by frame, and who last wrote it. This is the first thing to
reach for when a shape is not where its binding says:

```bash
genesis-cc samples/ArstroLoading.genesis --trace rect.w \
           --signal loopStart@0 --until 5200
```

```
    1920         130b     # b = its binding
    2000  <- fire loopEnd
    2000         130r     # r = being released back to its binding (G-22)
    2240         150b
```

`o` means **owned by motion and standing still** — a field that ignores a resize. That suffix has
found more bugs than any other single thing in this codebase. Read it against `t`, which means
**resting on a completed track's target, still re-evaluated every frame** (G-6b): both have stopped
moving, and only `o` is deaf to a resize. A field you expected to follow something showing `o`
means its `to` folded to a constant.

**`--verify`** — the real proof. It emits the class, compiles it with a generated harness, drives
the compiled object *and* the interpreter through one signal script, renders both into a
`RecordingTarget`, and diffs the op streams. `preview == compiled` or a per-op difference with a
timestamp. **Run it on every sample after touching `Runtime`, `CppEmitter`, or `Gene`:**

```bash
for f in apps/genesis/samples/*.genesis; do
  ./build/apps/genesis/genesis-cc --verify "$f" | tail -1
done
rm -f apps/genesis/samples/*.h apps/genesis/samples/*.cpp   # emitted byproducts, gitignored
```

**`genesis_shots`** writes real screens to PNGs. Look at them. Three of the layout bugs fixed here
were invisible in a passing test suite and obvious in a render.

## 5. The invariants — break these and something silently lies

**One semantics, two consumers.** `Runtime` interprets Gene; `CppEmitter` compiles it. Every rule
must be implemented in *both*, and the Verifier is what keeps them honest. When you add a name, a
function, or a behaviour, ask "where is the other half?" before you finish. Anything computed in
one place and consumed by both belongs in `Document` — `expandSteps`, `animatedFields`,
`liveTracks`, `releasesToBinding`, `followsTarget` are all there for exactly that reason.

**Order is part of the contract.** Field-table order, not insertion or alphabetical order, decides
the order of emitted members, animate calls, and release blends. `std::map` is alphabetical, and
using it cost a real divergence (`preview=36.66 compiled=29.73`) because a release target read
another releasing field. If two things must agree, they must walk the same list in the same
direction.

**The generated source must be byte-stable.** Same document, same bytes, always. It is asserted
(`Document_json_round_trips_byte_for_byte`) and it is what makes `--stale` meaningful.

**`documentChanged()` is the only funnel.** It re-validates, rebuilds the preview, refreshes every
panel, and reports the diagnostic count. No panel may mutate the document and leave the app
half-updated.

**Derive, don't duplicate.** There is no "animated" mark, because the tracks already say what moves
(G-21). There is no second copy of a list box's top and bottom, because the copies drift (G-20).
Every time this codebase has stored a fact it could compute, the two copies eventually disagreed.

**A track's `to` is an expression, not a number (G-6b).** It is re-evaluated every frame while the
track runs and the field goes on taking it once the track rests there, so "motion owns this field"
does not mean "this field has stopped answering". Only a target that folds to a literal leaves a
field genuinely frozen. `followsTarget` is the one predicate that decides which, and both the
interpreter and the emitter call it — derive that answer twice and the Verifier reports a
divergence instead of the design decision it is.

**A field's value is its live value once motion owns it (G-6a),** and layout re-runs every frame
when it reads something that changes every frame. The flag for that is "some binding reads a live
value" — not "we discovered a new live field", which is a bug this codebase shipped and then found:
`all` made every field animated, seeded the whole document, and per-frame layout silently stopped.

## 6. The mistakes already made (do not re-make them)

Each of these is now a test. If a test with an odd-looking name is in your way, this is why it
exists.

| symptom | cause | rule now |
| --- | --- | --- |
| Every button dead | four panels overrode `hitTestSelf` to `return true`, so the topmost swallowed all clicks | test the point, or don't override |
| TextBox lost focus each keystroke | `refresh()` destroyed and rebuilt the focused child | rebuild on a *structure* key, restore focus + caret |
| Click didn't place the caret | measured text from an input handler, using a destroyed per-frame `cairo_t` | measurement is only valid during a render — defer it |
| Panels cut off with no scroll | clipped without scrolling | G-20: clip **and** scroll, one box definition |
| Last row unreachable at any offset | the measured viewport was a `pad` taller than the box rows may occupy | measure the box you lay out in |
| Second step's tracks invisible | rows shown only when whole, plus the above | assert *reachability*, not that the offset moved |
| A returned field then ignored resizes | `to = original` handed back a number, not the binding | G-22: release the field to layout on completion |
| A returned field landed short and jumped | `original` snapshotted at fire time while its inputs animated | blend toward the binding re-evaluated each frame |
| A field stranded where an earlier step left it | only `original` was followed; every other `to` was read once and kept | G-6b: **every** non-constant `to` is followed, and the field rests on it |
| A loop control that vanished at 1200px, then starved the list | put on the header line (no room), then on a line under it (26px out of a ~65px list viewport) | G-26's controls live in the **footer band**, which was already there and already empty |
| Objects animated in lockstep came apart after a few laps | the chain started each step at the FRAME clock, losing the overshoot once per step — so the error scaled with the step COUNT, not the duration | G-28: chain from the ideal clock (`stepAtMs += stepDurMs`) |
| ...and fixing that made four samples fail `--verify` | the driver tick walked `ShapeNode::releasing` while a callback inside it INSERTED into that same map; `std::map` order decided whether the new driver ticked this frame, while the emitter walked a static field-table list | tick a **precomputed** list (`mFollowed`), never the live map |
| Step 3 never ran | two tracks on one field in one step; the second replaced the first *and its callback* | `liveTracks`: only the survivors are started and counted |
| Buttons over the divider | sized off `kListW` instead of `kListW - 2*pad` | a column's contents fit inside the column |
| Dropdown ran off the window | the popup always opened down, at full height | FR-48: place against the root, cap, scroll |
| The Verifier agreed with a one-sided change | the plan resized but sampled only the transition's first frame | G-9a: sample after the resize settles |

Note the shape of the last one. **A green Verifier is only as strong as the plan.** If you add
behaviour that shows up under some condition, make sure the default plan reaches that condition.

## 7. How to add things

**A shape field** — one row in `fieldDefs()` (`Document.cpp`). The inspector, emitter, runtime and
validator all read that table, so a new field is a row, not five edits. Give it a `segmentProperty`
if Artboard has one, or leave it empty for a style value the class holds itself.

**A Gene function** — the table in `Gene.cpp`, then three places: `fold`/`evaluate` and `emitCpp`.
Keep it **total**: division by zero is 0, `sqrt` of a negative is 0, `asin` clamps. A preview must
never silently draw nothing, and the emitted C++ must reproduce each definition exactly.

**A base to inherit** — `BaseCatalog.cpp`: its class, signals (mark the expected ones), the
`base.*` reads it exposes, and what a starter document for it looks like. Add a sample, and a
`VerifyPlan::defaultFor` branch that drives its signals.

**An editor panel** — subclass `artboard::Segment`, take `App &`, own your widgets, and ask: can my
content outgrow my box? If yes, `ui::ListScroll` and the G-20 checklist. Draw your empty state.

**Anything in Artboard** — use the `implement_artboard` skill and follow it stage by stage. The
platform-free core is the default; the `IRenderTarget` seam grows only when a thing genuinely
cannot be composed from the existing primitives, and then *every* adapter changes in the same
commit.

## 8. Definition of done

- The requirement is written and conflict-checked, before the code.
- `docs/` and the `.puml` match the code — no class without a box.
- `genesis_tests`, `genesis_ui_tests`, `artboard_tests` and `ctest` all report 0 failed.
- **Every sample verifies** (`preview == compiled`), and the emitted `.h`/`.cpp` are cleaned up.
- A render was actually looked at, at more than one window size.
- The new behaviour has a test that **fails without the fix** — check that, don't assume it.
- Committed to `main`: the submodule first, then the umbrella with its pointer bump.

## 9. Known limits, honestly

- **Colours don't animate.** `fill`/`stroke` are `animatable = false`, so `all` skips them. Nothing
  in the design prevents it; it needs a colour-valued tween and an emitter path.
- **A release forces per-frame work** while it runs, since the blend is applied every frame.
- **`raw{ … }` escapes are unpreviewable** by construction — the interpreter says so rather than
  guessing.
- **The scrubber is per reaction**, deliberately: there is no global timeline, because a component
  is a set of responses to events, not a movie. Do not add one without re-reading G-6.
- **`--verify` needs a compiler** at runtime. It reports *unavailable* rather than passing, and a
  build without one is UNVERIFIED, not green.
