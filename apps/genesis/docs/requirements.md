# Genesis — Software Requirements

## 1. Purpose

Genesis shall let a designer author an animated UI component for the Arstro **Artboard**
framework visually, and shall deliver that component as **C++ source code** that compiles
against `artboard` alone.

## 2. Scope

Covered:

- A component document: a name, an authorable **base class**, a design size, **params**, a
  tree of **shapes**, and **reactions**.
- **Gene**, an expression language in which every numeric and colour field is written.
- A **code emitter** producing a `.h`/`.cpp` pair with no dependency on Genesis.
- A **live preview** that runs the authored component as the real base class.
- A **verifier** that proves the preview and the emitted code draw the same thing.
- A desktop editor (GTK3 + Cairo) and a headless compiler (`genesis-cc`).

Not covered: general vector illustration, application layout, runtime document loading,
general-purpose scripting, cross-component orchestration (screen/shared-element transitions).

## 3. Functional Requirements

### G-1 No runtime

The generated `.h`/`.cpp` shall include `<artboard/artboard.h>` and the C++ standard library
and **nothing else**. No Genesis header, library, registry, initialisation call, or document
file shall be required to build or run a generated component. A generated file shall be
valid to read, review, diff, and hand-edit as ordinary source.

### G-2 The document model

A component shall consist of exactly:

- **name**, **base**, **namespace**, **design size**;
- **params** — typed knobs (`number` with an optional range, `color`, `text`) that become a
  public setter/getter pair on the generated class;
- **shapes** — a tree of `rect` / `circle` / `path` / `label`, each with a parent;
- **reactions** — `signal → [step, …]`, each step a set of tracks that start together.

No sixth concept shall be introduced. Field names, types, defaults, which shape kinds they
apply to, whether they are animatable, and which Artboard `Property` they drive shall be
declared in **one table** consumed by the inspector, the emitter, the runtime, and the
validator, so a new field is a table row rather than five parallel edits.

### G-3 Gene: every field is an expression

Every numeric and colour field shall hold a Gene expression, never a bare number. A literal
(`40`) is a valid expression, so the rule costs the author nothing and buys responsiveness
by construction.

- **Types.** Two: `double` and `Color`. Booleans are doubles. Arithmetic on a colour is a
  type error.
- **Scope.** `w`/`h` (the component's live size), `minSide`/`maxSide`/`aspect`, the constants
  above; any param by name; `self.<field>`; **`parent.<field>`** — the object that holds this
  one, so a child sizes and places itself relative to its container without naming it;
  `<shape>.<field>`; `base.<name>` (what the base class publishes); `theme.<role>`.
  - For a nested object `parent` is its parent shape, and every field is readable.
  - For a top-level object `parent` is the COMPONENT, which publishes `w` and `h`. Reading any
    other field there is an error, reported as such rather than silently returning zero.
  - `parent` joins the binding graph like any other reference, so a cycle through it is caught
    (G-7) instead of looping at run time.
- **Functions.** Arithmetic and geometry: `min max abs clamp lerp floor ceil round sign sqrt
  pow div mod hypot dist snap wrap remap step smoothstep`. Trigonometry and angles:
  `sin cos tan asin acos atan atan2 deg rad turns pct`. Exponentials: `exp log`. Colour:
  `rgb rgba fade mix`. And no others — there are no user-defined functions, statements, or
  loops.
  - `div(a, b)` is the TRUNCATED quotient and pairs with `mod`: `div(a,b) * b + mod(a,b) == a`
    for every `b != 0`.
  - `snap(v, step)` rounds to the nearest multiple of `step` — a grid, in one call.
  - `wrap(v, lo, hi)` brings a value into `[lo, hi)` by whole periods, which is what an angle
    or a looping phase needs.
  - `remap(v, inLo, inHi, outLo, outHi)` rescales between two ranges; `step` and `smoothstep`
    are the usual threshold and eased threshold.
- **Constants.** `pi`, `tau`, `e` (and the older spellings `PI`, `TAU`).
- **Totality.** Every function is defined for every input a designer can type: division,
  modulo and `div` by zero yield `0`; `sqrt`/`log` of a non-positive value yield `0`;
  `asin`/`acos` clamp their argument to `[-1, 1]`; `snap` with a zero step returns the value;
  `wrap` and `remap` over an empty range return the low end. A preview shall never produce a
  NaN and silently draw nothing, and the emitted C++ shall reproduce each definition exactly —
  the verifier (G-9) is what proves it did.
- **Escape hatch.** `raw{ … }` passes C++ through verbatim to the emitter. It cannot be
  previewed; the author is told so rather than shown a wrong result.

### G-4 Authorable bases

Genesis shall support authoring against `VisualLoop`, `ProgressIndicator`, `Button`,
`Slider`, and `Checkbox`. Each base's signals, its `base.*` reads, its summary, and what the
author is responsible for drawing shall be held as **data** (`BaseCatalog`), so adding a base
is a table entry plus the Artboard class — never a new branch in the emitter or the runtime.

An authored component supplies the component's **whole appearance**: the base contributes
behaviour only (Artboard FR-41), and the authored shapes take no input (the base owns it).

### G-5 Reactions

A reaction is `signal → steps`. A step's tracks start together; step *N+1* begins when step
*N* completes. A track is `(target, from, to, durationMs, delayMs, easing, repeat, yoyo)` —
a literal `artboard::Tween` plus a target — where `from` (empty = the field's current value),
`to`, `durationMs` and `delayMs` are Gene expressions, so a `speed` param genuinely re-times
a component.

Inside those four expressions — and only there — the name **`current`** reads the target
field's value at the moment the reaction fires. `to = current + 10` therefore means "ten more
than wherever it is", which is what makes a reaction relative rather than absolute: firing it
repeatedly steps the value, and interrupting mid-flight resumes from where it actually got to.
(`from` left blank already means `current`; writing it is the same thing.) `current` is not in
scope in a shape's field bindings, where there is no target to speak of.

- A track with `repeat == -1` never completes and therefore never chains; a step whose tracks
  all repeat forever ends the chain.
- Every reaction carries a **cancellation policy** — `restart` (default), `ignoreIfRunning`,
  or `queue` — because interruption is what event-driven motion gets wrong.
- Only a field the base table calls **animatable** may be a track target — a property of the
  field, not a per-shape mark the author has to set (G-21).

### G-6a A binding follows the value it reads, including while it animates

`self.w` means "my width". While a reaction is animating that width, it means the ANIMATED
width — so a binding that reads it must follow. Given `x = w / 4 * 3 - self.w / 2` on an object
whose `w` is being animated, `x` shall track `w` frame by frame, not hold the value it had when
the animation started.

Two things follow, and both must hold in the interpreter and in the generated code alike:

- **A field's value is its live value once motion owns it.** When a reaction has driven a
  field, the value other bindings read for it is the animated property's current value, not the
  result of its own binding expression. Until then the binding expression is its value, because
  that is its resting state (G-6).
- **Layout re-runs per frame when it reads something that changes per frame.** A binding that
  transitively depends on an animated field — or on `base.*` — is re-evaluated every frame; one
  that depends on neither is still only re-evaluated on resize, so the common case costs
  nothing. The dependency graph already exists for ordering and cycle detection (§4), so this
  is a reachability question over it rather than a new mechanism.

A field that motion owns is still never re-asserted by layout (G-6): it is read from, not
written to.

### G-6 Bind-versus-animate

A field's binding is its resting value; a reaction's target is its motion. Layout shall
assert a bound value on build and on resize, but **not** once a reaction has driven that
field, so a resize cannot stomp an animation's result. A resize eases; the first sizing
snaps (placing a component into a layout is not a change the user should see animate). A
binding that reads `base.*` shall be re-evaluated every frame; one that does not shall be
re-evaluated only when the size changes.

### G-18 Reactions belong to an object

A reaction shall be owned by a **shape**, not by the document. Selecting an object shows that
object's reactions and nothing else, and the reactions panel names whose they are.

- Because the owner is implied, a track's target is a **bare field name** (`opacity`). A
  qualified `other.opacity` still reaches a different object, so a reaction can drive its
  siblings; the two forms differ only in whether a name is given.
- A signal remains a **component-level event**: several objects may each react to the same
  one, and they all run. The generated hook for a signal starts every object that handles it,
  and each keeps its own cancellation state, token and pending count.
- Renaming an object rewrites only qualified targets; bare ones already mean "my own field"
  and follow for free.
- A document written with a top-level reaction list shall still load: each reaction migrates
  to the object its first track drives, which is the object it was always about.

### G-19 Duplicating an object

`duplicateShape(id)` shall copy an object **and its whole subtree**, naming the copies
`<id>_copy`, then `<id>_copy2`, `<id>_copy3` — the same numbering ids already use. Parent links
inside the subtree follow the copies, and so do reaction targets that point inside it; a target
pointing OUTSIDE is left alone, so a copy still drives whatever external object the original
drove. The result is independent: it animates itself rather than sharing the original's motion.
The editor offers it as **Duplicate** beside Delete and as `Ctrl+D`, and selects the copy.

### G-17 Sector: a circle can be a pie, a ring, or a pac-man

A circle shall carry `arcStart` and `arcEnd` — animatable fields **in degrees**, 0 pointing
right and growing clockwise — plus `arcInner`, a hollow centre as a fraction of the radius.
The kept region is the wedge between the two rays from the centre, so `30 -> 330` leaves a
60-degree mouth: a pac-man. `arcInner` above 0 makes it a ring segment instead of a pie, and
a full sweep with `arcInner` makes a donut. An end at or before the start wraps forward one
turn, so `330 -> 30` is a 60-degree wedge rather than nothing.

This is **not** the same field as `trimStart`/`trimEnd` (G-16), and the two must not be
conflated: `arc*` chooses which part of the DISK the shape is (a fillable region, cut by rays
from the centre); `trim*` chooses how much of that shape's OUTLINE is drawn (a stroke that
draws itself in). They compose — a trimmed pie is a pie whose edge draws itself in.

### G-16 Trim: any shape can become a partial one

A rect, circle, or path shall carry `trimStart`, `trimEnd`, and `trimOffset` — animatable
fields, in **fractions of the outline's length**, so a trimmed circle is an arc (the fraction
is simply `angle / 360`), a trimmed rounded rect is a border that draws itself in, and a
trimmed path is a line that grows. `trimOffset` wraps, so an arc can sit across the outline's
seam. These map directly onto Artboard's `Trim` (FR-42); Genesis adds no geometry of its own.

### G-7 Validation

The document shall be validated on every edit. **Errors** block export: unknown base,
unknown/duplicate/missing shape id, unparsable expression, unknown name or field, a binding
**cycle**, a reaction on a signal the base lacks, a track targeting an unknown shape, a
non-animatable field, a colour field, or a field not marked animatable, an unknown easing, a
malformed path command, a param shadowing a built-in. **Warnings** do not block: an expected
signal nothing reacts to, an empty reaction or step, a component that draws nothing, a
`raw{ }` that cannot be previewed. Every diagnostic names where it is.

### G-8 Live preview

The editor shall run the authored component as a real `artboard::Segment` tree whose root
**is** the authored base class, so base behaviour (cycle signals, the progress spring, press
handling) is the real thing. The preview shall be resizable independently of the window,
because a component that only works at its design size is broken and this is the only way to
see it.

### G-9 Verification — the preview is the code

Genesis shall be able to prove that its preview and its output agree:

1. emit the `.h`/`.cpp`;
2. compile them with a generated harness against `artboard_core`;
3. drive the compiled class **and** the interpreter through one signal script;
4. render both into an `artboard::RecordingTarget` at each sampled frame;
5. diff the op streams field-wise.

A divergence shall be reported with its frame, op index, and field. Where no compiler or
built `artboard_core` is available the result shall be reported as **unavailable** — an
unavailable check shall never be reported as a pass. A verify step shall mean the same thing
on both sides (a driver that differs between them is a defect in the verifier, not a finding).

### G-9a The verification plan must look after it resizes

Every default plan resizes (G-9), but a resize eases: the frame immediately after it still shows
the old geometry. A plan that samples only that frame resizes without ever observing the result,
so a component that has stopped responding to its size passes. The default plan shall therefore
carry a sample **after the resize transition has settled**, not only at its first frame.

This is not hypothetical: it is what hid a one-sided implementation of G-22's hand-back — the
interpreter released the field, the generated class did not, and the two agreed at every sampled
frame anyway.

### G-10 Determinism

The same document shall always emit byte-identical output, and saving shall always produce a
byte-identical file, so a build can regenerate components and a stale generated file is a CI
failure rather than a mystery (`genesis-cc --stale`).

### G-11 Generated code quality

The emitted file is the product. It shall: fold constant subexpressions; emit the shortest
literal that reads back exactly; emit the minimum parentheses C++ needs; omit a zero-divisor
guard where the divisor is a non-zero literal; carry the authored expression as a trailing
comment on each binding; and name members after the authored ids. The test is not "does it
compile" but "would a person have written this".

### G-12 The headless compiler

`genesis-cc` shall provide, with no display: generate, `--check` (validate only), `--print`,
`--verify`, `--stale`, `--new <Base> <Name>`, and `--bases`.

### G-15 Launcher, launch moment, and recents

The editor shall open on a **home screen**, not on a blank document: a fixed sidebar
(wordmark, tagline, an Open action, the version) and a grid of **recent components** followed
by one card per authorable base, so "carry on with something" and "start something new" are
the same gesture in the same place. Recents persist across sessions under the user's config
directory; an entry whose file has vanished is shown as missing rather than silently dropped.

The application shall show a **launch splash** in its own undecorated window while it does its
startup work: the wordmark rises and scales in, the tagline follows, three dots pulse until
there is something to name, and a 2px accent bar reports REAL progress with the step named.
The main window is not shown until the intro has played and the work behind it is done, so the
first thing on screen is never a blank frame. Opening a file from the command line skips
straight to the editor.

Home and editor are one window with two screens that **cross-fade** as groups (never cut), and
whichever is faded out takes no input.

### G-13 The editor

The editor shall present: the shape tree with add/delete; the live preview with a
base-appropriate transport and a reduced-motion switch; the inspector (every field as an
editable expression, and the params with live controls); the reactions
panel (signal, cancellation policy, steps, tracks, a per-reaction scrubber, and Fire); and
New/Open/Save/Export/Verify. Editing a field shall update the preview as it is typed.

The reactions panel shall **shed columns** rather than let them collide as the window
narrows: `delay` first, then `from`, then the repeat/yoyo/delete chips, then `easing`
narrows — in reverse order of how often each is edited. Rows that fall outside the visible
area shall be **reachable by scrolling** (G-20), never silently dropped, and the widgets of a
row that scrolled out shall be hidden so they neither draw nor take input.

There shall be **no global timeline**: a component is a set of responses to events, not a
linear movie, so the event graph is the source of truth and the scrubber is per reaction.

### G-14 A new project is an example

Creating a component shall produce a working, idiomatic component for its base — bound
responsively, with reactions on the signals that base expects — because the fastest way to
learn the tool is to open something that already moves and take it apart.

### G-20 Every list the document can outgrow scrolls

A panel whose content grows with the document — the object list, the property list, the
reaction list, and the track/step list — shall scroll, because all four grow without bound
while their boxes do not, and a panel the user can see is cut off but cannot reach is worse
than one that never showed the content at all.

Each such list shall:

- measure its **viewport and content height every layout**, so a window resize, a document
  edit, or a font change cannot leave a stale limit behind;
- measure **the same rectangle its rows are laid out in**. A list box has one top and one
  bottom, and `measure()`, the row placement, the row-visible test, the paint clip, and the bar
  shall all read them from one place. A viewport measured even a few pixels taller than the box
  the rows may occupy sets a limit that stops short, and the final row becomes unreachable at
  *every* offset — which is how a second step's tracks could be neither seen nor scrolled to;
- clamp its offset to `[0, max(0, content − viewport)]`, so neither end runs away;
- accept **both** the mouse wheel and a drag on its body, landing at the same offset for the
  same distance;
- **bubble** a wheel it cannot act on (nothing out of view) to its ancestors rather than
  swallow it, so an outer scrollable region still responds;
- **show that it can scroll** — a thin bar, sized `viewport/content`, drawn only while there
  is something out of view, so "there is more" never has to be discovered by accident;
- hide the widgets of rows scrolled out of view, and drop no row to make things fit. A row of
  editable fields is shown **whole or not at all** — half a text field is not editable, so there
  is nothing to gain from drawing one — which makes reachability the load-bearing property: at
  full scroll the last row shall land flush inside the box.

The panel shall also not be capped so tightly that a large window cannot show more: the
reactions panel takes a third of the window height, so a taller screen buys more track rows.

### G-21 A field is animated because a track animates it

There shall be **no animate mark**. A field is animated exactly when some track targets it, and
its resting value is the expression in the inspector — nothing else to set, and nothing that can
disagree.

The mark was a second source of truth for a fact the reactions already stated, and it could
contradict them in both directions: a field marked animated with no track (a `Property` that
never moves, and a whole-frame layout re-run to read it), or a track on an unmarked field (which
the validator had to reject with "must be marked animated", a diagnostic that only ever meant
"you forgot the checkbox"). Both disappear when the answer is derived:

- `Document::animatedFields(shapeId)` shall answer it by scanning every reaction of every
  shape — a track on object A may target `B.opacity`, so this is a document-level question, not
  a shape-level one — and shall return the fields in field-table order, because the emitter's
  output must be byte-stable.
- The answer shall be computed **after** `all` is expanded (G-22), since `all` is what makes a
  whole shape animated.
- Every consumer — the emitter's own-flags, the interpreter's `owned` map, the live-reachability
  pass of G-6a — shall read that one function.
- A document carrying the old `"animated"` list shall still load; the list is ignored, because
  the tracks in the same file already say what moves. A field it marked with no track was, in
  every sense that reaches the screen, not animated.

### G-22 `original`, and `all` for a whole object

Two names, so a reaction can put an object back the way it was authored.

**`original`** — valid in a track's `from`/`to` alongside `current` (G-6), and nowhere else —
means the target field's **own binding**: the expression sitting in the inspector, or the field's
default when nothing is bound. Where `current` is "wherever it is now", `original` is "whatever
the binding says". It is the binding, not a value read from it once, and that has two parts:

- **It is evaluated, not remembered.** The expression is evaluated in the same live scope the
  binding itself reads (G-6a), so an `original x` of `(w - self.w) / 2` recentres against the
  *current* width, at a size the component was never authored at. A field's binding may not
  itself contain `original` or `current`, so there is no recursion.
- **A track that ends at `original` gives the field back to its binding.** When such a track
  completes, the field stops being owned by motion and layout drives it again — so a later resize
  or param change re-evaluates the binding, exactly as it did before the reaction ever ran.
  Without this, `to = original` would only ever mean "the number the binding happened to give at
  fire time", and the object would look right for one frame and then stop being responsive.

  This refines G-6 rather than contradicting it. G-6 stops a resize from *stomping* a running or
  finished animation; here the author has explicitly said "this field belongs to its binding
  again", so handing it back is the stated intent rather than an accident.

  It applies when the track **comes to rest at `to`**: the `to` is the bare name `original`
  (`original + 4` ends somewhere the binding does not describe and keeps the field), the track
  completes at all (a `repeat == -1` track never does), and its final cycle is forward — a yoyo
  with an odd repeat count rests back at `from`. That is the same condition `artboard::Tween`
  uses to choose its resting endpoint, and it shall be decided by **one** predicate that the
  interpreter and the emitter both call, since they must release the field at the same moment or
  they diverge on the next resize.

**`all`** — a target of `all` (or `other.all`) expands to one track per animatable field of that
object, in field-table order, sharing the same `from`/`to`/`ms`/`delay`/`easing`/`repeat`/`yoyo`.
`from = current, to = original` on `all` is the whole point: one row that returns an entire
object to its authored state.

- Expansion shall happen in **one** place (`Document::expandSteps`) that the interpreter, the
  emitter, and the validator all call, so the compiled class and the preview cannot disagree —
  the Verifier compares them op for op.
- The editor keeps showing the **authored** row: one `all`, not the fields it stands for.
- A shape with no animatable field for its kind expands to nothing rather than erroring.

### G-23 A track can be dragged into another step

Ordering motion is a rearrangement, so it shall be done by rearranging. Each track row carries a
**grip** at its left edge; dragging it moves the track, and the panel shows an insertion line
where it would land.

- The grip is the only draggable part of the row, because every other part is a text field whose
  own drag selects text. It is never shed as the panel narrows (G-13) — it is the only way to
  reorder.
- Dropping into a step inserts at the line's position; dropping in the empty space **below the
  last row** puts the track in a **new final step**, which is how one turns simultaneous tracks
  into sequential ones.
- A step left with no tracks is removed, since a step is defined by the tracks that start
  together and an empty one has no duration to chain from.
- Dragging is direct manipulation and therefore exempt from R-G-1: the pointer is the animation.
- A drop that would not move the track is a no-op, not a document change (it must not land in
  the undo history).

## 4. Design rules (the app's UI)

Genesis is an Arstro desktop app and follows that design system; these are its `R-G` rules.

### R-G-1 Nothing snaps

Every visible change — position, size, show/hide (fade), colour, radius, scroll, panel
open/close — goes through `Property`/`AnimatedProperty`/`Spring`, is eased, and collapses
under `artboard::reducedMotion()`. Direct manipulation (dragging the preview's resize
handle) is exempt: the pointer is the animation.

### R-G-2 Tokens only, and the tokens are cosmo's

Every colour, radius, type size, and layout constant comes from `app/Theme.h`
(`palette::`/`radius::`/`type::`/`metrics::`). ONE accent, ONE radius scale, ONE type ramp.
A raw hex or magic size in a widget is a bug.

The token VALUES are cosmo's — the same near-black ramp, the same blue accent, the same small
literal radii, the same DM Sans + JetBrains Mono ramp — so the two apps read as one family
rather than as two products that happen to share a framework. `genesis::themeColor` publishes
the same palette to Gene as `theme.*`, so a colour an author types and a colour the app draws
are the same colour, and a starter component follows the palette in force instead of baking a
literal.

### R-G-3 One hover language

Every interactive element has an animated hover treatment: child-Segment controls via
`artboard::hoverBox()`, self-drawn regions via `palette::hoverWash(amount)` driven by
`ui::RowHover`.

### R-G-4 Responsive

Geometry derives from the live window size and measured content. The rails shrink to a
usable minimum before the canvas is squeezed; the canvas absorbs the slack. Verified at two
or more window sizes.

### R-G-5 Text fits, and no two strings ever overlap

Every string is measured against the live render target (`ui::textWidth`) and either sized to
fit or ellipsized (`ui::ellipsize`). Any panel that scrolls also **clips**, and hides the
widgets of rows that scrolled out — a clipped row still records its draw calls and its child
Segments would still take input. Clipping without scrolling is a bug, not a layout: see G-20.

This is enforced, not assumed: a test renders the real app at six window sizes, on both
screens, replays the op stream (tracking the transform AND the clip stack), and asserts that
no two drawn strings share pixels and that none is drawn outside the window. A modal is the
one permitted overlay, so its own content is checked in isolation.

### R-G-6 Every state is drawn

Idle, hover, pressed, disabled, **empty**, **loading**, and **error** — each panel draws its
empty state with a sentence saying what to do, and the preview draws its error state with the
diagnostic rather than a blank frame.

## 5. Non-functional Requirements

### NG-1 Headless core

`genesis_core` shall contain no UI and no display dependency: document, language, emitter,
runtime, and verifier are all testable without a window.

### NG-2 Dependency floor

`genesis_core` shall depend only on `artboard_core` and the C++17 standard library — no JSON
library, no scripting engine. The editor adds only GTK3 + Cairo + Fontconfig.

### NG-3 Testability

Every drawing claim shall be asserted through `artboard::RecordingTarget`, and the editor
shall be renderable headlessly to a PNG for design review.

### NG-4 SOLID

Adding a base, a shape kind, a field, or an easing shall be data, not a new code path.

## 6. Verification Outline

- `genesis_tests` covers the document model, Gene (parse/fold/interpret/emit), the emitter,
  the runtime, and the verifier's comparison, asserting through `RecordingTarget`.
- `genesis_shots` renders the editor headlessly at two or more window sizes and in its empty,
  error, modal, and per-base states for design review.
- `genesis-cc --verify` proves preview/codegen agreement for every sample component.
