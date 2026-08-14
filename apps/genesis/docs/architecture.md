# Genesis — Software Architecture

## 1. The shape of the system

```
                    ┌───────────────────────────────────────────┐
   author ────────► │  genesis (GTK3 + Cairo editor)            │
                    │  SplashScreen → HomeScreen ⇄ the editor   │
                    │  App · Chrome · ShapeTree · CanvasView ·  │
                    │  Inspector · ReactionsPanel · Modal        │
                    └───────────────┬───────────────────────────┘
                                    │ (no UI below this line)
                    ┌───────────────▼───────────────────────────┐
   genesis-cc ────► │  genesis_core                             │
                    │   Document ── BaseCatalog ── Theme        │
                    │      │                                    │
                    │   gene::  parse → fold → { interpret,     │
                    │                            emit C++ }     │
                    │      │              │                     │
                    │   Runtime        CppEmitter               │
                    │      └──── Verifier ────┘                 │
                    └───────────────┬───────────────────────────┘
                                    │
                    ┌───────────────▼───────────────────────────┐
                    │  artboard_core (Segment, Property, Tween, │
                    │  Spring, IRenderTarget, RecordingTarget)  │
                    └───────────────────────────────────────────┘
```

The dependency arrow never points upward. `genesis_core` knows nothing about GTK, and the
**generated code knows nothing about `genesis_core`** — that second property is the product
(G-1), not an implementation detail.

## 2. The central decision: one grammar, two consumers, one proof

Gene is parsed once into an AST, constant-folded once, and then consumed twice — by
`Runtime` (the live preview interprets it) and by `CppEmitter` (the export compiles it).
Two implementations of one semantics *will* drift, and a design tool that lies about the
result is worthless. Genesis therefore does not ask to be trusted: `Verifier` compiles the
emitted class, drives it and the interpreter through the same signal script, renders both
into an `artboard::RecordingTarget`, and diffs the op streams (G-9).

This is only possible because Artboard already has a deterministic, device-free render
target. It is the architectural reason the two-implementation design is safe.

Everything the two sides must agree on is written down once and mirrored deliberately:
`bindProp`'s retarget rule (G-6), step chaining on completion, infinite tracks never
completing, and the cancellation policies. Changing one without the other is exactly what
the verifier catches.

## 3. Modules

| Module | Responsibility | Depends on |
| --- | --- | --- |
| `core/Json` | a minimal insertion-ordered JSON reader/writer, so the document format is text with a stable byte order and no third-party dependency (NG-2) | — |
| `core/gene` | the language: lexer/parser → AST, `fold`, `evaluate`, `emitCpp`, plus name collection for dependency analysis | — |
| `core/Document` | the model (G-2), the ONE field table, JSON persistence, and `validate()` (G-7) | Json, gene, BaseCatalog |
| `core/BaseCatalog` | the authorable bases as data: signals, `base.*` reads, summaries (G-4) | — |
| `core/Theme` | the `theme.*` palette, shared by interpreter and emitter so a previewed colour and a compiled one are identical | — |
| `core/codegen/CppEmitter` | Document → `.h`/`.cpp` (G-1, G-11) | Document, gene, Theme, BaseCatalog |
| `core/Runtime` | Document → a live Segment tree rooted in the authored base (G-8) | Document, gene, Theme, artboard |
| `core/Verifier` | emit → compile → drive both → diff (G-9) | CppEmitter, Runtime, artboard |
| `cli/` | `genesis-cc` (G-12) | genesis_core |
| `app/` | the editor (G-13) | genesis_core, artboard, GTK3/Cairo |

## 4. Evaluation order is a topological sort

A field may read another field (`self.w`, `ring.h`), so both the emitter and the runtime
build the same dependency graph over `(shape, field)` pairs and evaluate in topological
order — the emitter as a run of `const double <shape>_<field> = …;` locals, the runtime as a
map. `Document::validate()` rejects cycles first, so neither has to handle one.

The same analysis answers a second question: if any binding reads `base.*`, the layout must
be re-evaluated **every frame** rather than only on resize (G-6). Both sides derive that flag
the same way.

## 5. Where the two sides deliberately differ

The runtime interprets against live values; the emitter writes C++ that reads them. They are
kept in step by resolving names through two small tables rather than by scattered `if`s:

- **during layout** — `self.x` / `ring.w` resolve to the local just computed (emitter) or the
  map entry just written (runtime);
- **at signal time** — the same names resolve to the LIVE animated value
  (`mRing->opacity.value()`, or the `Property` the runtime holds), because a reaction's
  `from`/`to` are evaluated when it fires.

Two names exist only at signal time, and are the reason the split is worth its cost:
`current` (the target's value right now) and `original` (the value of the target field's own
binding, re-evaluated now). `original` is compiled/evaluated in the **live** scope on both
sides — the emitter inlines the binding expression, the runtime evaluates the same source —
so `to = original` is a return-to-rest that follows a resize instead of freezing a number.

One more thing is deliberately shared rather than duplicated: `all` (a target meaning "every
animatable field of this object") is expanded by `Document::expandSteps`, which the runtime,
the emitter **and** the validator all call. Neither side owns the meaning, so neither can
drift from the other — and `animatedFields` derives what is animated from those expanded
tracks, which is why there is no animate mark to keep in sync with them.

## 6. The editor

Authoring follows from that. A field is animated because a track animates it, so the inspector
has no animate toggle and the reactions panel needs no "mark it first" warning; and ordering
motion is a rearrangement, so a track row is **dragged** by its grip into another step (or into
a new final one). Both are single-source-of-truth arguments rather than UI preferences: the mark
was a second copy of what the tracks already said, and a step's order was only editable by
retyping.

`App` owns the `Document`, the `Runtime`, and the panels, and exposes **one** funnel —
`documentChanged()` — that re-validates, rebuilds the preview, refreshes every panel, and
reports the diagnostic count. No panel may leave the app half-updated.

Layout is fixed chrome + fixed rails + a flexing centre, all from named constants
(`ui::metrics`). The rails shrink to a usable minimum before the canvas is squeezed (R-G-4).
`Modal` is the app's last child and draws its scrim and card in the **normal** pass, so its
own controls — ordinary child Segments — sit on top of the card rather than under it; panels
that paint in the overlay pass check `Modal::coversApp()` so nothing floats over a dialog.

**Two screens, one window.** The app opens on `HomeScreen` (sidebar + a grid of recents and
base cards) and crosses to the editor as a group fade on `Segment::opacity` (FR-32) — so
neither screen ever cuts, and whichever is faded out stops taking input for free. `Recents`
persists the list under the user's config dir. Before either, the host shows `SplashScreen` in
its own undecorated window and does its startup work behind it, naming each step into the
progress bar; the main window is not shown until that is done. This is cosmo's launch shape,
which is the point: the two apps should feel like one product.

Text is measured against the live render target: the app publishes it once per frame
(`ui::setMeasureTarget`), so every panel measures with the metrics it will draw with (R-G-5).

**Overflow is scrolled, not truncated (G-20).** Four of the editor's lists grow with the
document — objects, properties, reactions, tracks — so each owns a `ui::ListScroll`: a ~40-line
value that holds an offset, is handed `(viewport, content)` every layout, clamps, and draws its
own bar. It is a plain member rather than an `artboard::ScrollView` wrapper because these panels
draw their rows themselves (measured columns, shed columns, step headers) instead of parenting a
child per row, so what they need is the *arithmetic* and the *indicator*, not another container.
`ListScroll::wheel` returns `false` when there is nothing out of view, which is what lets a
scroll bubble past a full-view list to an ancestor (FR-46).

## 7. What Genesis added to Artboard

Genesis is also the application that exercises Artboard's animation surface, and building it
surfaced sixteen gaps that belonged in the framework rather than in the app:

| | |
| --- | --- |
| FR-32 | `Segment::opacity` — group fade through the (previously unused) `pushLayer`/`popLayer` |
| FR-33 | animated `rotation`/`scale` about a `pivot` |
| FR-34 | `VisualLoop` — the indeterminate-loop base |
| FR-35 | `ProgressIndicator` — the determinate base; `ProgressBar` re-based onto it |
| FR-36 | protected signal hooks on `Segment`/`Button`/`Slider`/`Checkbox` |
| FR-37 | `PathSegment` + `Path::emit`/`clear`/`segmentCount` |
| FR-38 | `TextBox` caret and editing gestures |
| FR-39 | text always fits its control (TextBox clip + caret scroll, ComboBox ellipsis) |
| FR-40 | disabled controls look disabled |
| FR-41 | `drawsBuiltInVisuals` — a subclass can supply its own appearance |
| FR-42 | `Path` trim — any shape can be drawn as a partial one |
| FR-43 | ellipse sector — a circle can be a pie, a ring, or a pac-man |
| FR-44 | `TextBox` selection, clipboard, and a blinking caret placed by the pointer |
| FR-45 | rounded-rectangle corners that are actually circular arcs |
| FR-46 | scroll input — `RawPointer::Kind::Scroll` → `Gesture::Type::Scroll`, routed and bubbled |
| FR-47 | a clipped panel must scroll, and must show that it can |

Each is in the platform-free core, needed no HAL change, and is useful to cosmo, pulsar, and
the Android shell independently of Genesis — which is the test of whether it belonged there.

## 8. Known limits

- Verification needs a C++ toolchain at authoring time; without one it reports *unavailable*
  rather than passing (G-9).
- Verification splits across two threads because its two halves have different constraints:
  `verifyCompile` (emit → write → compile → run the harness) touches no Artboard object and
  runs on a worker, while `verifyCompare` builds a `Runtime` whose Segments touch
  process-wide focus/hover state and must run on the UI thread. `App::advance` collects the
  worker's result, so the window keeps drawing while the compiler works.
- `raw{ }` is exported but not previewable, by construction — the interpreter cannot run C++.
- There is no undo stack yet; the document is a plain value, so one is a stack of copies.
