# Genesis — Software Detailed Design

## 1. `Json`

An insertion-ordered JSON value. Objects keep a `vector<pair<string, Json>>` rather than a
map, so `dump()` is byte-stable and a saved document diffs cleanly — the reason the format is
text at all. Numbers print as the shortest exact form (integral values without a fraction),
never in scientific notation for the magnitudes a design document holds. The parser tolerates
`//` line comments so a hand-edited sample can carry notes, and reports failures as
`line N: message`. Missing keys and out-of-range indices return a shared null, so
`doc["a"]["b"]` never throws.

## 2. `gene`

### 2.1 AST and parsing

`Node` is a tagged union over `Number`, `ColorLit`, `Ident`, `Member`, `Unary`, `Binary`,
`Ternary`, `Call`, `Raw`. The recursive-descent parser mirrors C++ precedence exactly
(`?:` < `||` < `&&` < equality < relational < additive < multiplicative < unary < postfix),
which is what later lets the emitter drop parentheses safely. Call arity and function names
are checked at parse time, so an unknown function is reported where it was typed.

`raw{ … }` captures a balanced-brace body and trims it.

### 2.2 `Value`, and totality

A `Value` is a `double` or a `Color`. `evalNumber` rejects a colour and vice versa, so a type
error is caught rather than coerced. Division/modulo by zero and `sqrt`/`log` of a
non-positive value yield `0` — a preview must never produce a NaN and silently draw nothing
(G-3). The emitter reproduces each of these definitions, which is why they are listed
together here: they are a contract between the two consumers, not local conveniences.

### 2.3 `fold`

Post-order; a node whose children are all literals is evaluated against an empty scope and
replaced. Folding runs before both consumers, so the interpreter does less work per frame and
the emitted line reads as a constant where it is one.

### 2.4 `emitCpp`

Two precedence functions, deliberately distinct:

- `precedenceOf(n)` — the precedence of the OPERATOR, used when emitting children;
- `emittedPrecedence(n)` — the precedence of the TEXT this node emits. Comparisons, logical
  operators, and a guarded division emit their own parentheses, so as an operand they are
  atomic. `operand()` wraps only when `emittedPrecedence(child) < parentPrecedence`.

`num()` prints the shortest literal that reads back as exactly the same double (trying
`%.6g` through `%.17g`), so `0.7` never appears as `0.69999999999999996` while staying
bit-exact for the verifier. A `/` or `%` whose divisor is a non-zero literal is emitted
without the zero guard.

## 3. `Document`

### 3.1 The field table

`fieldDefs()` is the single source: `{name, type, defaultExpr, kinds mask, animatable,
segmentProperty, doc}`. `segmentProperty` is the `artboard::Segment` `Property` a field
drives (`w → width`, `opacity → opacity`, …), or `""` for a style value the generated class
must hold in its own `Property` and push into the style each frame. The inspector, the
emitter, the runtime and the validator all read it, so a new field is one row.

`Shape::effectiveField` returns the authored expression or the table default, so an unset
field is not a special case anywhere downstream.

### 3.2 `removeShape`

Collects the shape and every descendant, drops them, then drops any track targeting one of
them and any step left empty — an orphaned track would emit code referring to a deleted
member. `addShape` de-duplicates the id and **returns the id it assigned**, because the
caller needs to know.

### 3.3 `validate`

Runs in one pass over params, shapes (fields, animated flags, path commands), the binding
graph, and reactions; see G-7 for the list. The cycle check builds the `(shape.field)` graph,
including `self.` resolved to the owning shape, and runs a colour-marked DFS per node,
reporting the shape that closes a cycle once.

### 3.4 `starter`

Per base, an idiomatic working component with a design size that suits it (G-14): a spinning
ring (VisualLoop), a track + `base.display`-bound fill that pops on completion
(ProgressIndicator), a body that cross-fades on hover and squashes with a wash on press
(Button), a rail/fill/thumb that pops on grab (Slider), a box with a path tick that fades in
(Checkbox).

## 4. `CppEmitter`

### 4.1 Structure of the emitted class

```
ctor          params ← defaults, size ← design size, drawsBuiltInVisuals = false,
              buildTree(), layout(0.0), applyStyles()
buildTree()   one Segment per shape, inputTransparent, parented
layout(ms)    every binding as a const local in dependency order, then bindProp per field
applyStyles() the (possibly animating) style Properties → each node's style
setX(v)       per param: assign, re-layout, re-apply
playSigStepN  one method per step of each reaction
onHook()      the base's signal hook → start the reaction
advance(now)  resize check → layout, tick style Properties, applyStyles, base::advance
```

### 4.2 `bindProp`

```
if (owned) return;                         // a reaction owns this field now
if (ms > 0) { p.animateTo(v, ms, …); return; }   // an edge always RETARGETS
if (p.isAnimating()) return;               // a per-frame refresh must not cut an ease short
p.set(v);
```

The three clauses are in that order for a reason, and `Runtime::bindProp` is byte-for-byte
the same logic. `owned` is a per-`(shape, field)` bool set when a track first drives it.

### 4.3 Reaction emission

Per step: count the **finite** tracks into a pending counter; give each finite track an
`onComplete` that ignores stale tokens (`token != mTokSig`), decrements, and on zero either
plays the next step or clears `running` (and, under `queue`, restarts). Infinite tracks
(`repeat == -1`) get no callback at all; a step with none finite ends the chain and says so
in a comment. `mSeq` is a monotonic token so a restarted reaction ignores its own in-flight
callbacks.

### 4.4 Name resolution

`layoutNames(owner)` resolves `self.f`/`shape.f` to the local `<shape>_<field>`;
`liveNames(owner)` resolves them to the live `Property::value()`. `base.f` resolves through
`BaseDef::reads`, and `theme.f` to a `Color` literal — so the generated file carries no theme
lookup and no Genesis dependency.

## 5. `Runtime`

The root is a `Host<Base>` template specialised per base (`LoopHost`, `ProgressHost`,
`ButtonHost`, `SliderHost`, `CheckboxHost`) that overrides the base's signal hooks and
forwards them to `hostSignal(name)`, and overrides `advance` to call `hostAdvance` before the
base's own. The root really is the base class, so `dynamic_cast` is enough to drive it
(`loopStart`, `setProgress`, `setPressed`, `toggleChecked`, …).

Per shape it holds the `Segment`, a `Property` per style field, a `Color` per colour field,
and the `owned` flags. `propertyFor(shape, field)` maps a field name to either the Segment's
own `Property` or the style one — the single place the field table's `segmentProperty` is
turned into a pointer.

Authored shapes are created `inputTransparent` and the root has `drawsBuiltInVisuals = false`
(Artboard FR-41): the authored shapes are the whole appearance, and the base owns the input.

`toggleChecked()` exists because a verify step must mean the same thing on both sides:
`setChecked(bool)` is a no-op when already there, which the harness's unconditional click was
not.

## 6. `Verifier`

`kSerializerSource` is the op-stream serializer **as source text**, compiled into the
harness; `opsToText` is the identical function compiled into `genesis_core`. One text, two
compilations, so the format cannot drift between the sides.

The plan's events and samples are merged into one ordered timeline, so both sides see the
component in the same state at the same frames. The harness is compiled `-O0
-ffp-contract=off`: the compiled arithmetic must follow the same order the interpreter uses,
or a fused multiply-add would surface as a false mismatch.

`verifyCompile` / `verifyCompare` split the work by thread-safety, not by convenience: the
first touches no Artboard object and is safe on a worker; the second constructs Segments,
which register in the process-wide focus and hover slots, and so must run on the UI thread.
`verify()` is the two in order, which is what `genesis-cc` uses.

Comparison is token-wise with a relative tolerance of `1e-9`, so a last-bit difference is not
reported as a semantic one while any real divergence is. The scan stops after 40 differences —
enough to diagnose, and the rest would be noise.

## 7. The editor

### 7.1 `App`

Owns `Document`, `Runtime`, and the panels. `documentChanged()` is the only edit funnel:
re-validate → rebuild the runtime → resize it to the preview frame → `restartPreview()` →
refresh every panel → report the diagnostic count → re-layout.

`render()` publishes the live target to `ui::setMeasureTarget` for the frame, renders the
tree, then runs the overlay pass, then clears the target.

### 7.2 Panels

- **Chrome** — identity, status, and the five actions. The Verify button reads "Verifying…"
  and disables while a check runs (R2: never dead).
- **ShapeTree** — a depth-first flattening of the tree with per-depth indent and a kind
  glyph; the footer (title, delete, four add buttons) is one block measured from the bottom,
  so nothing can land on a coordinate another element also uses.
- **CanvasView** — draws the stage and the frame, renders the previewed component inside the
  frame in `render()` (it is content, not chrome), and draws the resize handle and the
  selection outline in `onOverlay` (they sit on top of the component). The selection outline
  is a polygon through the four transformed corners, so it stays correct for a rotated or
  scaled shape. Dragging the handle is direct manipulation and therefore exempt from R-G-1.
- **Inspector** — one row per field of the selected shape: name, an `artboard::TextBox`
  holding the expression, and an animate toggle. Focused fields commit every frame, so the
  preview tracks typing.
- **ReactionsPanel** — the reaction list, the signal and cancellation dropdowns, the track
  rows (target / to / ms / easing), a step header on its own row, and a per-reaction
  scrubber.
- **Modal** — New / Open / Save As / report, drawn in the normal pass (see architecture §6).

### 7.3 `RowHover`

A self-drawn multi-region widget paints many clickable rows in one Segment, so the
framework's per-Segment `hoverAmount()` cannot tell them apart. `RowHover` gives each row its
own eased amount, so moving between rows cross-fades instead of the highlight jumping — one
implementation, so every Genesis panel hovers identically.
