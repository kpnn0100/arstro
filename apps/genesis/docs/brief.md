# Genesis — product brief

**Genesis is an animation designer for Arstro Artboard whose deliverable is source code.**
You pick an abstract control to inherit from, draw the component, bind its geometry to
expressions, wire animations to the base class's events, and Genesis writes you a
`.h` / `.cpp` pair that compiles against `artboard::` and nothing else.

**Status: implemented.** This document is kept as the original proposal; what actually
shipped is described in [requirements.md](requirements.md), [architecture.md](architecture.md)
and [../README.md](../README.md). Where the two differ, those win — notably the milestone
list below was completed in one pass, and Artboard grew ten features (FR-32…FR-41) rather
than the six anticipated in §9.

---

## 1. Why this app, and why it fits Artboard

Arstro already has the animation runtime. `Property` is an animated scalar,
`Tween` is a complete from→to spec with delay/easing/repeat/yoyo, `Animator` is a
callback timeline with `onUpdate`/`onComplete`, `Spring` is a framerate-independent
follower, `MotionTokens` is the shared duration vocabulary, and `Segment` is the
interactive node with animatable `x/y/width/height`. What Arstro *doesn't* have is a
way to **author** motion. Today, a new loading spinner is written by hand, tuned by
rebuild-and-look, and lives only in one app.

Genesis closes exactly that gap, and only that gap. It is not a new framework — it is a
canvas and a compiler for the framework that already exists.

The reason this fits so well is a one-to-one mapping. Every concept Genesis needs
already exists as an Artboard primitive:

| Genesis concept | Artboard primitive |
| --- | --- |
| an animated field | `artboard::Property` |
| an animation | `artboard::Tween` (from/to/delay/easing/repeat/yoyo) |
| "…then do this" | `Property::animate(spec, now, onComplete)` |
| a springy follow | `artboard::Spring` + `motion::kSpatial*` |
| a drawn shape | `Segment` subtree emitting `IRenderTarget` path ops |
| "reduce motion" | `artboard::reducedMotion()` — already honored by all of the above |
| proving it works | `RecordingTarget` op-stream assertions |

There is no impedance mismatch to paper over, so the generated code reads like code a
person wrote. That is the whole thesis.

---

## 2. The one bet: **Genesis ships no runtime**

The export is plain C++17 that includes `<artboard/artboard.h>` and nothing else.
No `libgenesis`, no JSON parsed at app startup, no interpreter embedded in your product,
no plugin API.

Three consequences, all of them the point:

- **Easy to learn.** The tool has five nouns (§3). There is no scene-graph runtime
  semantics to learn on top of Artboard's.
- **Unlimited ceiling.** The generated file is a normal class. If Genesis can't express
  something, you edit the `.cpp` — or drop a `raw{ … }` C++ escape into the document so
  the escape survives regeneration (§5.4). The ceiling is C++, not a plugin surface.
- **Reviewable.** A component change shows up as a readable diff in the repo, not as a
  binary blob. It works in `cosmo`, `pulsar`, the Android shell, the web/WASM build, and
  a headless `RecordingTarget` test, identically — because it *is* Artboard code.

The corollary is the hard engineering problem, stated up front: Genesis's live preview is
a **second implementation** of the same semantics as the emitted code, and two
implementations drift. §7 is the answer.

---

## 3. Mental model — five nouns, and that's the whole app

1. **Component** — the project. A name, an **abstract base** to inherit from, a design
   size, and a list of **params** (the knobs a consumer can set: accent colour, thickness,
   speed).
2. **Shape** — what you draw. Circle, rect, path, label, image; nested into a tree.
   Each becomes a `Segment` child in the generated class.
3. **Binding** — *every numeric field is an expression, not a number.* Type `40` and it's
   an expression; type `min(w, h) * 0.5` and it is one too. This single rule is where
   responsiveness comes from — nothing is hard-coded to the design size unless you write
   a constant.
4. **Signal** — something the base class tells you happened: `onLoopStart`, `onClick`,
   `onValueChanged`, `onCheck`. Fixed per base class, so the list is short and discoverable.
5. **Reaction** — *when* `<signal>`, *animate* `<field>` *to* `<expression>` *over*
   `<ms>` *with* `<easing>` — with `then:` to chain, and steps that run in parallel.

Learn those five and you can build anything the framework can draw. There is deliberately
no sixth concept: no global timeline, no state machine editor, no node graph, no
scripting runtime.

---

## 4. The workflow, walked end to end

Using your example verbatim.

**Step 1 — New Component.** Name `CoolVisualLoop`. Base: `VisualLoop`. Genesis shows the
base's contract in the New dialog before you commit — what you must draw, what signals you
get, what the host will call. Design size 120×120 (a preview size, not a constraint).

**Step 2 — Draw.** Drag a circle onto the canvas. It lands as a shape named `ring`.

**Step 3 — Bind.** Select `ring`, click its `radius` field, type:

```
min(w, h) * 0.5
```

`w` and `h` are the component's live size. The field turns into a binding chip. Resize the
preview frame and the ring tracks it — you are already testing responsiveness, before
writing a line of animation.

**Step 4 — Initial state.** Set `ring.opacity` to `0`.

**Step 5 — Make a field animatable.** Click the ⟳ next to `opacity`. That promotes the
field to a `Property` in the generated class and makes it a legal reaction target. Same for
`rotation`.

**Step 6 — React.** In the Reactions panel:

```
on loopStart:
    ring.opacity  →  1        over 300ms   ease-out-cubic
    then
    ring.rotation →  turns(1) over 1200ms  linear   loop

on loopEnd:
    ring.opacity  →  0        over 300ms   ease-in-cubic
```

`then` is literally the completion callback of the previous step. Add a second line inside
a step to run tweens in parallel.

**Step 7 — Watch it.** The preview runs the real event, live. Scrub the reaction, step
frames, toggle **Reduced motion**, drag the preview frame to any size.

**Step 8 — Export.** `CoolVisualLoop.h` + `CoolVisualLoop.cpp`.

### What comes out

```cpp
// CoolVisualLoop.h — generated by Genesis 0.1 from CoolVisualLoop.genesis.
// Edits inside GENESIS-OWNED regions are overwritten on regenerate.
#pragma once
#include <artboard/artboard.h>
#include <memory>

namespace app
{
    /** VisualLoop: a ring that fades in on loop start, then spins until loop end. */
    class CoolVisualLoop : public artboard::VisualLoop
    {
    public:
        CoolVisualLoop();

        // ---- authored params ----
        void setAccent(const artboard::Color &c);
        void setThickness(double px);

        void advance(double nowMs) override;

    protected:
        void onLoopStart() override;    // base-class signals (FR-34)
        void onLoopEnd() override;

    private:
        void layout(double transitionMs);   // (re)evaluate bindings from w/h/params

        artboard::Color mAccent{0.35, 0.78, 1.0, 1.0};
        double mThickness = 4.0;
        double mNowMs = 0.0, mLastW = -1.0, mLastH = -1.0;
        std::shared_ptr<artboard::CircleSegment> mRing;
    };
}
```

```cpp
// CoolVisualLoop.cpp — generated by Genesis 0.1.
#include "CoolVisualLoop.h"
#include <algorithm>

using namespace artboard;

namespace app
{
    namespace
    {
        constexpr double kFadeMs = motion::kDurationMedium2;  // 300
        constexpr double kSpinMs = 1200.0;
        constexpr double kTurn   = 6.283185307179586;         // turns(1) in radians
    }

    CoolVisualLoop::CoolVisualLoop()
    {
        mRing = std::make_shared<CircleSegment>();
        mRing->opacity.set(0.0);          // authored initial state (FR-32)
        addChild(mRing);
        layout(0.0);                      // first evaluation snaps; later ones ease
    }

    // Bindings. Re-evaluated on resize; eased, never snapped (design-taste §2A).
    void CoolVisualLoop::layout(double ms)
    {
        const double w = width.value(), h = height.value();
        const double r = std::min(w, h) * 0.5;      // gene: min(w, h) * 0.5
        const auto to = [&](Property &p, double v) {
            if (ms > 0.0) p.animateTo(v, ms, Easing::EaseOutCubic, mNowMs);
            else          p.set(v);
        };
        to(mRing->width,  r * 2.0);
        to(mRing->height, r * 2.0);
        to(mRing->x,      w * 0.5 - r);
        to(mRing->y,      h * 0.5 - r);
        mRing->pivotX.set(r);                        // spin about its own centre (FR-33)
        mRing->pivotY.set(r);
        mRing->style.paint = Paint::stroked(mAccent, mThickness);
    }

    // reaction: on loopStart -> fade in, THEN spin forever
    void CoolVisualLoop::onLoopStart()
    {
        mRing->opacity.animate(
            Tween::range(mRing->opacity.value(), 1.0, kFadeMs).withEasing(Easing::EaseOutCubic),
            mNowMs,
            [this] {                                  // "animation done" -> next step
                mRing->rotation.animate(
                    Tween::range(0.0, kTurn, kSpinMs).looping(), mNowMs);
            });
    }

    void CoolVisualLoop::onLoopEnd()
    {
        mRing->opacity.animate(
            Tween::range(mRing->opacity.value(), 0.0, kFadeMs).withEasing(Easing::EaseInCubic),
            mNowMs);
    }

    void CoolVisualLoop::advance(double nowMs)
    {
        mNowMs = nowMs;
        if (width.value() != mLastW || height.value() != mLastH)
        {
            mLastW = width.value();
            mLastH = height.value();
            layout(motion::kDurationShort3);
        }
        VisualLoop::advance(nowMs);   // ticks children; Properties update themselves
    }
}
```

Note what is *not* there: no Genesis header, no registry, no init call, no interpreter.
`mRing->opacity` / `rotation` / `pivotX/Y` are the one thing that doesn't exist in Artboard
today — they are §9's FR-32/FR-33, and they are additive, platform-free, and useful to every
app in the repo, not just Genesis.

---

## 5. Gene — the binding language

A deliberately tiny expression language. It is the "script language" from the sketch, and
its whole job is to let a number be *derived* instead of *typed*.

### 5.1 Grammar

```
expr    := ternary
ternary := or ( "?" expr ":" expr )?
or      := and ( "||" and )*
and     := cmp ( "&&" cmp )*
cmp     := add ( ("<"|"<="|">"|">="|"=="|"!=") add )*
add     := mul ( ("+"|"-") mul )*
mul     := unary ( ("*"|"/"|"%") unary )*
unary   := ("-"|"!")? postfix
postfix := primary ( "." IDENT )*
primary := NUMBER | COLOR | IDENT | "(" expr ")" | IDENT "(" args ")" | RAW
```

One type: `double`. Plus colour literals (`#3ac7ff`, `rgba(…)`, `theme.accent`) which are
only legal where a colour is expected. Booleans are doubles; `cond ? a : b` works.

### 5.2 Scope

| Name | Meaning |
| --- | --- |
| `w`, `h` | the component's live size (`width.value()`, `height.value()`) |
| `minSide`, `maxSide`, `aspect` | conveniences over `w`/`h` |
| `<param>` | any param you declared (`accent`, `thickness`, `speed`) |
| `self.<field>` | another field of the same shape (`self.r`, `self.x`) |
| `<shape>.<field>` | any field of any shape in the component (`ring.r`, `label.height`) |
| `base.<field>` | state exposed by the abstract base (`base.value` for Slider/Progress, `base.pressed` for Button, `base.cycle` for VisualLoop) |
| `theme.*` | the `artboard::Theme` in force — colours and radii, so components inherit the app's look |

Bindings form a DAG; Genesis rejects cycles at edit time with the cycle drawn out.

### 5.3 Functions

`min max abs clamp lerp floor ceil round sign sqrt pow mod sin cos tan atan2 exp log`
plus `deg(x) rad(x) turns(n) pct(n)` and constants `PI TAU`. Every one maps to a `<cmath>`
call or a literal in the emitted code — no runtime helper library.

### 5.4 The escape hatch

```
raw{ myHelper(w, h) * base.value }
```

Passed through verbatim into the generated `.cpp`. It cannot be previewed by the
interpreter (Genesis draws the shape with a hatched "unpreviewable" badge and uses the
last known value), but it means the tool never blocks you. Marked in the document so it
survives regeneration.

### 5.5 Two evaluators, one grammar

Gene is parsed once into an AST and then consumed twice: by the **interpreter** (live
preview) and by the **C++ emitter** (export). They must agree. Constant subexpressions are
folded in both. §7 is how we keep them honest.

---

## 6. The abstract bases and their signals

An abstract base is a contract: *here is what the host will do to you, here is what you
must draw, here is when you'll be told about it.* Genesis shows this contract in the New
dialog and again in the Signals panel.

| Base | Signals | Author must draw |
| --- | --- | --- |
| **VisualLoop** *(new)* | `onLoopStart` `onCycle(n)` `onLoopEnd` | an indeterminate busy visual |
| **ProgressIndicator** *(new)* | `onValueChanged(v)` `onComplete` `onIndeterminate` `onDeterminate` | a determinate `[0,1]` visual |
| **Button** | `onHoverEnter` `onHoverExit` `onPressDown` `onRelease` `onCancel` `onClick` `onFocus` `onBlur` `onEnable` `onDisable` | idle / hover / pressed / disabled |
| **Slider** | `onDragStart` `onValueChanged(v)` `onDragEnd` `onReset` + hover/focus | track, thumb, fill |
| **Checkbox** | `onCheck` `onUncheck` + hover/focus/press | box, indicator |

Two generic signals exist on every base: `onAttach` (first frame) and `onResize(w, h)`.

Genesis refuses to export a component that draws only one state of its base — the
`design-taste` rule that "a control that renders only its selected state is unfinished" is
enforced by the tool, as a checklist in the Signals panel, not a lint you can ignore.

**`VisualLoop` and `ProgressIndicator` do not exist in Artboard yet.** `ProgressBar` exists
but is a finished concrete control with no author-facing lifecycle. Adding them is §9.

---

## 7. The animation model, and the drift problem

### 7.1 The model

A **Track** is `(target field, from, to, durationMs, delayMs, easing, repeat, yoyo)` — a
literal `artboard::Tween` plus a target. A **Step** is a set of tracks that start together.
A **Reaction** is `signal → [step, step, …]`, where step *N+1* starts on step *N*'s
completion (`onComplete`). Spring tracks are the one variant: `(target, stiffness preset)`
emitting `artboard::Spring` with an `omega` from `motion::kSpatial*` / `kEffects*`.

`from` defaults to *"wherever it is now"* — which is what makes interruptions look right and
is exactly what `Property::animate` already does.

**Cancellation policy is a first-class field on every reaction**, because it's what makes
event-driven motion behave and it's the thing hand-written code always gets wrong:
`restart` (default) · `ignore if running` · `queue` · `reverse`.

### 7.2 Why there is no global timeline

A UI component is not a 0–60s movie; it is a set of responses to events, any of which can
fire at any time and interrupt any other. A global scrubber would be lying about the
model. Genesis therefore keeps the **event graph as the source of truth** and gives you a
scrubber *per reaction* — a local timeline you can drag, with the interrupting signals
listed beside it so you can fire one mid-scrub and see what happens.

### 7.3 The drift problem — and the answer

The preview interprets; the export compiles. Two implementations of one semantics will
diverge, and a design tool that lies about the result is worthless.

**The answer is `RecordingTarget`.** Artboard's test adapter records every draw call as a
comparable `DrawOp`. So Genesis ships a **Verify** action:

1. Emit the `.h`/`.cpp`.
2. Compile it headlessly into a scratch shared object against `artboard_core`.
3. Drive both the compiled class *and* the interpreter through the same signal script.
4. Render both into a `RecordingTarget` at `t = 0 / 25% / 50% / 75% / 100%` of each reaction.
5. Diff the op streams.

Any divergence is a bug report with the exact op, coordinate, and time. Verify runs on
demand while editing, and always before export. `genesis-cc --verify` runs the same check
in CI, so the property "the preview equals the code" is *tested*, not hoped for.

This is the single most Artboard-native idea in the brief, and it is only possible because
the framework already has a deterministic, device-free render target.

---

## 8. The document

`.genesis` — one JSON file, stable key order, human-diffable, checked into the repo next to
the generated sources.

```json
{
  "genesis": 1,
  "component": { "name": "CoolVisualLoop", "base": "VisualLoop",
                 "namespace": "app", "designSize": [120, 120] },
  "params": [ { "name": "accent",    "type": "color",  "default": "#3ac7ff" },
              { "name": "thickness", "type": "double", "default": 4.0, "range": [1, 24] } ],
  "shapes": [
    { "id": "ring", "type": "circle",
      "bind":     { "r": "min(w, h) * 0.5", "cx": "w / 2", "cy": "h / 2" },
      "animated": { "opacity": 0.0, "rotation": 0.0 },
      "style":    { "stroke": "accent", "strokeWidth": "thickness" } }
  ],
  "reactions": [
    { "on": "loopStart", "cancel": "restart", "steps": [
        [ { "target": "ring.opacity",  "to": "1",        "ms": 300,  "easing": "EaseOutCubic" } ],
        [ { "target": "ring.rotation", "to": "turns(1)", "ms": 1200, "easing": "Linear", "repeat": -1 } ] ] },
    { "on": "loopEnd", "steps": [
        [ { "target": "ring.opacity", "to": "0", "ms": 300, "easing": "EaseInCubic" } ] ] }
  ]
}
```

The generated code is a pure function of this file. Regenerating produces byte-identical
output — so `genesis-cc` can run in the build and a stale generated file is a CI failure,
not a mystery.

---

## 9. What Artboard has to gain first

All of these are additive, live in the **platform-free core**, need **no `IRenderTarget`
change**, and are verifiable with `RecordingTarget` — i.e. they are ordinary
`/implement_artboard` work. Each is useful to `cosmo`, `pulsar`, and the Android shell
independently of Genesis, which is the test of whether it belongs in the framework.

| Proposed | What | Why |
| --- | --- | --- |
| **FR-32** | `Segment::opacity` — an animated `Property`; `render()` brackets the subtree in the existing `pushLayer(alpha)` / `popLayer()` when it is < 1 | Your example needs "opacity = 0, then animate to 100%". The HAL primitive already exists and is unused by the core; today every app fades by hand, per shape, which double-blends overlaps |
| **FR-33** | `Segment::rotation` / `scaleX` / `scaleY` / `pivotX` / `pivotY` as `Property`s folded into `localTransform()` | "then rotate" should be one animated field, not per-frame transform arithmetic. `Drawable::transform` composes already; this just makes it animatable and pivot-aware |
| **FR-34** | `VisualLoop` abstract base — start/stop/tick lifecycle + `onLoopStart` / `onCycle` / `onLoopEnd`, **no visuals** | The base for every loading/busy visual. Nothing like it exists |
| **FR-35** | `ProgressIndicator` abstract base — `[0,1]` + determinate/indeterminate + signals; `ProgressBar` becomes a concrete subclass of it | Separates "what progress means" from "what a bar looks like" (SRP), the same split `AbstractSlider`/`Slider` already models |
| **FR-36** | Protected virtual signal hooks on `Button` / `Slider` / `Checkbox` (`onPressDown`, `onValueChanged`, …) alongside the existing `std::function` callbacks | Subclassing is the authoring model; the state changes already happen internally, they're just not overridable |
| **FR-37** | `PathSegment` — a `Segment` wrapping the existing `Path` drawable | `RectangleSegment` / `CircleSegment` / `LabelSegment` exist; freeform authored shapes have no node type |

FR-32 and FR-33 are the two that unblock the example you gave. FR-34 is the one that makes
`VisualLoop` a real thing rather than a Genesis fiction.

---

## 10. Architecture of the app itself

Mirrors `cosmo`'s split, for the same reason: the interesting logic must be testable
without a window.

```
genesis/
  core/            -> genesis_core   (static lib, ZERO UI, 100% unit-tested)
    Document.{h,cpp}      the .genesis model + load/save
    gene/Lexer|Parser|Ast|Interpreter|Emitter
    codegen/CppEmitter.{h,cpp}    Document -> .h/.cpp
    codegen/Verifier.{h,cpp}      the §7.3 op-stream diff
    bases/BaseCatalog.{h,cpp}     the signal contracts of §6, as data
  app/             -> genesis       (the GUI, built ON Artboard — it dogfoods itself)
  cli/             -> genesis-cc    (headless: generate, --verify, --check)
  tests/           -> genesis_tests (MiniTest, same harness as Artboard)
  docs/            requirements.md architecture.md detailed_design.md architecture.puml
```

Three things follow from that layout:

- **Genesis is written in Artboard and previews Artboard.** Every rough edge in the
  framework's animation API gets found by the tool that exists to exercise it.
- **`genesis-cc` makes it scriptable.** A component can be regenerated by CMake; a repo can
  assert its generated sources are current.
- **`BaseCatalog` is data, not code.** Adding a new authorable base is a table entry plus
  the Artboard base class — not a new code path in the emitter.

Visual language follows `arstro.design.rule` with `cosmo` as reference: same tokens,
same motion, its own palette.

### Screen

```
┌─ Genesis ─ CoolVisualLoop : VisualLoop ────────────────────── [Verify] [Export] ─┐
│ shapes        │                                        │ inspector              │
│  ▾ CoolLoop   │                                        │  radius  min(w,h)*0.5 ⓕ│
│     ● ring ⟳  │              ( ◍ )                     │  opacity 0          ⟳  │
│               │        live preview, resizable         │  rotation 0         ⟳  │
│ + circle      │                                        │  stroke  accent        │
│ + rect        │                                        │ ───────────────────────│
│ + path        │                                        │ params                 │
│ + label       │                                        │  accent    #3ac7ff     │
├───────────────┴────────────────────────────────────────┤  thickness 4.0         │
│ reactions                              [reduced motion]│                        │
│  on loopStart  (restart)                               │                        │
│   1 ▸ ring.opacity  → 1        300ms  ease-out-cubic   │                        │
│   2 ▸ ring.rotation → turns(1) 1200ms linear  ∞        │                        │
│     ├────────────────●──────────────────────┤  scrub   │                        │
│  on loopEnd            + add reaction                  │                        │
└────────────────────────────────────────────────────────┴────────────────────────┘
```

---

## 11. Milestones

Each ends with something demonstrable; none is a refactor of the last.

| | Milestone | Done when |
| --- | --- | --- |
| **M0** | Artboard FR-32/33/34 land via `/implement_artboard` | a hand-written `VisualLoop` subclass fades + spins in `ui-demo`; tests at 100% |
| **M1** | `genesis_core`: Document + Gene (parse/fold/interpret) + `CppEmitter` | `genesis-cc CoolVisualLoop.genesis` emits the §4 code, and it compiles and runs |
| **M2** | The GUI: shape tree, canvas draw, inspector, bindings, live preview | you can build CoolVisualLoop by hand and watch it |
| **M3** | Reactions panel + per-reaction scrubber + reduced-motion toggle | your example is authored entirely in the UI |
| **M4** | **Verify** (§7.3) + `genesis-cc --verify` in CI | preview/codegen divergence is a failing test |
| **M5** | The other four bases (Button, Slider, Checkbox, ProgressIndicator) + state checklist | a themed button with press/hover/disabled animation, exported and dropped into `cosmo` |
| **M6** | Component library: import a `.genesis`, fork it, params as presets | ship 6 stock components (3 loaders, 2 buttons, 1 slider) as `.genesis` sources |

M0–M1 is the load-bearing half: if `genesis-cc` can't emit code a human would accept, the
GUI doesn't matter. Build it first, in that order.

---

## 12. Non-goals

- Not a general vector illustrator. Shapes are what Artboard can draw, no more. Import SVG
  paths, don't rebuild Illustrator.
- Not a layout designer. Genesis authors *one component's interior*; app layout stays in
  app code.
- No runtime document loading. See §2.
- No general-purpose scripting. Gene computes numbers. Control flow lives in reactions, and
  anything past that is `raw{ }` or a hand-edit.
- No cross-component orchestration (screen transitions, shared element transitions) in v1.

## 13. Risks

| Risk | Mitigation |
| --- | --- |
| **Preview/codegen drift** — the tool lies about the result | §7.3 Verify, and it runs before every export, not on request only |
| **Generated code nobody wants to read** — the classic codegen death | The §4 sample *is* the spec; review it as a code artifact each milestone. If a human wouldn't write it, the emitter is wrong |
| **Base-class contracts leak into Genesis** — the tool learns each control's internals | `BaseCatalog` is declarative data; the base class publishes its own signals (FR-34/35/36). If Genesis needs a special case per base, that base is under-specified |
| **Gene grows into a language** | Hard cap: one type, no user functions, no statements, no loops. Pressure goes to `raw{ }` |
| **Regeneration clobbers hand-edits** | Generated files declare ownership regions; `raw{ }` blocks and a trailing `// GENESIS: user` region round-trip. Default posture: don't edit, re-author |

---

## 14. Decisions I need from you

1. **Repo placement.** I put this at `arstro/apps/genesis/` alongside `cosmo` / `pulsar`,
   since Genesis is an app, while the FR-32…FR-37 work belongs inside `core/Artboard/` under
   `/implement_artboard`. Say the word if you meant it to live somewhere else.
2. **Language name.** "Gene" for the binding language — keep, or something else?
3. **Verify by real compile (§7.3)** costs a toolchain dependency at authoring time. Worth
   it, or should v1 diff the interpreter against a *second* interpretation of the emitted
   AST (cheaper, weaker)?
4. **Scope of v1 bases.** All five at M5, or ship `VisualLoop` + `Button` first and learn?
5. **Web target.** Artboard has a Canvas2D/WASM adapter. Should Genesis itself run in the
   browser eventually, or is native-only fine forever?

---

*Next step, on your go: run `/implement_artboard` for FR-32 (`Segment::opacity`) — the
smallest piece that makes your exact example expressible, and useful to every app in the
repo on its own.*
