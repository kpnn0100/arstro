# Interstellar — automation, bindings, and how a frame's numbers are decided

Normative for **R-AUTO**, **R-BIND** and **R-EVAL**. This is the document to read before touching
anything that produces a parameter value, because the *order* below is a contract: two front ends, a
scrub and a master render must all produce the same numbers, and any other order produces a
different picture.

Contents: [1. The three producers](#1-three-producers-one-value) ·
[2. Automation clips](#2-automation-clips) · [3. Links](#3-links-applying-a-shape-to-an-address) ·
[4. Bindings](#4-bindings) · [5. The language](#5-the-language-gene) ·
[6. The frame pipeline](#6-the-frame-pipeline) · [7. Failure](#7-what-happens-when-it-goes-wrong) ·
[8. Proving it](#8-proving-it)

---

## 1. Three producers, one value

Every address (`docs/project-format.md` §4) gets its value at time *t* from exactly **one** of:

| producer | when | stored as |
|---|---|---|
| **static** | always, unless something below overrides it | the parameter's own field, in the `.isp` or in the rack's `.cmp` |
| **automation** | while an `autolink` on that address covers *t* | `#autoclip` (the shape) + `#autolink` (the application) |
| **binding** | always, if the address has one | `#bind` (an expression) |

**The model refuses configurations where two apply.** An address with both a binding and a link is
rejected at edit time, naming both (R-BIND-5); two links overlapping on one address are rejected
(R-AUTO-4). This is not fastidiousness: a value with two producers is a value whose picture depends
on evaluation order, and a renderer whose output depends on evaluation order cannot be tested.

Where a user genuinely wants both — a curve *and* a calculation — the binding reads the shape's
output explicitly:

```
#autoclip id=ac_push name=ac_push dur=2 ...
#bind target=clp_b.geom.scale expr="1 + ac_push.value * 0.08"
```

`ac_push.value` is the shape's output at *t*, and now there is one producer (the binding) reading one
input (the shape). Which drives which is written down instead of implied.

---

## 2. Automation clips

**An automation clip is a shape with a name, not a property of a parameter.** That single decision
is what makes "multi param can use the same automation" possible, and it is the difference between
this design and a conventional keyframe track.

```
#autoclip id=ac_push name=ac_push dur=2.000 interp=bezier
  0.000 = 0.0  ease=easeInOut
  0.750 = 0.9  ease=easeOutCubic
  2.000 = 1.0  ease=linear
```

- **Local time.** Breakpoint times are in the shape's own time, `0 … dur`. A link may place and
  time-scale it; the shape itself has no position on the timeline.
- **Own unit space.** Values are the shape's, conventionally 0..1. The link maps them into the
  target's unit (§3), which is what lets one shape drive an EV and a scale factor at once.
- **Easing is per-segment**, naming the ease *out of* that point into the next. The last point's
  ease is unused and is written `linear` for canonical form.
- **`interp`** picks the segment family: `linear` · `bezier` (the eased default) · `hold` (step at
  the point) · `step` (step at the midpoint).
- **Sampling is exact, not table-driven.** `value(localT)` finds the bracketing pair and evaluates
  its easing analytically, so the same shape gives the same number at 24 fps, at 48 fps, in a proxy
  and in a master. A resampled lookup table would make the frame rate a parameter of the picture.
- **Outside `[0, dur]`** the shape clamps to its first/last value. A link's own placement decides
  when the shape applies at all; clamping is only about the sampling function's domain.

### 2.1 Why a clip and not an envelope

A DAW offers both: a per-parameter envelope drawn across the whole song, and a *clip* of automation
that can be named, moved, copied and reused. The user asked for the second, and the second is also
the better fit here:

- an envelope belongs to one parameter, so nothing can be shared;
- a clip is an object, so it has a bind name, appears in the registry, can be read by an
  expression (`ac_push.value`), and can be linked to five parameters at once;
- a cut gets re-timed constantly, and a *scoped* clip (§3.3) moves with its shot, which an
  absolute-time envelope cannot.

---

## 3. Links: applying a shape to an address

```
#autolink id=al_ex name=al_ex clip=ac_push target=gr1.basic.exposure
  at=2.000 dur=2.000 from=0.0 to=0.8 mode=absolute fadeIn=0 fadeOut=0
```

### 3.1 Time mapping

```
localT = (t - at) * (autoclip.dur / dur)          ; dur defaults to the shape's own dur
active = 0 <= (t - at) < dur
```

`dur` on the link time-scales the shape: a 2-second shape linked with `dur=0.5` plays four times as
fast. The shape is not copied and not resampled — only the argument changes.

### 3.2 Value mapping

```
shapeV = autoclip.value(localT)                    ; in the shape's unit space
mapped = from + (to - from) * shapeV               ; or: shapeV * scale + offset
```

`from`/`to` is the form a human writes ("this ramps exposure from 0 to +0.8 EV"); `scale`/`offset`
is the form a script writes. Exactly one pair may be present.

Then `mode` decides how `mapped` meets the address's **static** value `s`:

| mode | result | for |
|---|---|---|
| `absolute` | `mapped` | the ordinary case: the shape *is* the value |
| `add` | `s + mapped` | modulate around whatever the panel is set to |
| `multiply` | `s * mapped` | a gain-shaped modulation (opacity, scale) |

`add` and `multiply` compose onto the **static** value, never onto each other — because two links on
one address are already refused (R-AUTO-4), so there is never a second one to compose with. That is
deliberate: "stacked modulation" is expressible as a binding over two shapes, where the stacking
order is written down.

### 3.3 Scope

`scope=<clipId>` reinterprets `at` in the **clip's local time**, so the link's position is relative
to the shot. Move the clip and the ramp moves with it; re-time the clip and the ramp re-times. An
unscoped link is in absolute timeline time, which is right for a project-wide fade and wrong for
"this shot pushes in".

### 3.4 The boundary, and the lint that exists because of it

Outside every link, an address is its static value (R-AUTO-5). So entering a link whose first mapped
value differs from the static value is a **step in the rendered image**:

```
static exposure = 0.0
link at 4.0, from 0.5 → 0.8            ; at t = 4.0 the picture jumps 0.5 EV
```

Two mechanisms, and both are needed:

- **`fadeIn` / `fadeOut`** (frames, default 0) ramp from the static value into the shape's first
  value and back out. A `fadeIn` of 6 at 24 fps is a quarter-second lead-in.
- **The lint.** With `fadeIn = 0` and a first value that differs from the static value by more than
  the parameter's own epsilon, `render --lint` names the link, the address, the time and the size of
  the step, and the lane draws the discontinuity. A hard change the user asked for is legitimate; one
  they did not notice is a defect they will blame on the renderer.

Default 0 is the honest default: it does exactly what the numbers say. The lint is what stops that
honesty from being a trap.

---

## 4. Bindings

```
#bind target=gr1.opacity expr="clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)"
```

The user's own example. Read as: *the weight with which gr1's grade applies to its children follows
gr1's exposure, half-scaled, biased to 0.5, clamped to legal range.*

### 4.1 What an expression may read

| reads | meaning |
|---|---|
| any address | its value **after step 2** of §6 — i.e. post-automation, pre-composition |
| `ac_x.value` | an automation clip's output at *t* (§1) |
| `t`, `frame`, `fps`, `dur` | the timeline clock |
| `project.playhead`, `project.width`, `project.height` | project values |
| `clp_a.local`, `clp_a.progress` | a clip's local time and its 0..1 progress — the two things every shot-relative expression needs |
| numeric literals, the function set (§5) | — |

**Reads are of OWN values, not stacked ones** (R-BIND-6). `gr1.basic.exposure` is gr1's own
parameter, before the rack composes it with its ancestors. Reading an effective value is reserved as
`…​.eff` and is **not** in v1: a group whose own value depends on its descendants' effective values
is a dependency that is a cycle in *meaning* even where the graph is acyclic, and the resulting
picture is impossible to reason about.

### 4.2 The graph

- Dependencies are **derived from the expression**, never stored (`Gene::collectMembers` already
  does this). A stored dependency list is a second copy of a fact (R-G-3).
- The graph must be a **DAG**. `bind` refuses a cycle at set time and prints both ends of it:
  `refused: cycle gr1.opacity → gr1.basic.exposure → gr1.opacity`.
- Evaluation is in **topological order**, computed once per project revision, not per frame.
- A binding target may itself be read by another binding — that is the normal case, and it is what
  the topological order is for.

### 4.3 Compilation

The expression is parsed once into a Gene AST, constant-folded, and its dependency set collected —
at *edit* time, so a bad expression is refused while the user is looking at it rather than during a
twelve-thousand-frame render. Per frame, evaluation is an AST walk over a `Scope` whose two lookup
functions resolve an address and a built-in. No re-parsing, no string work, no allocation in the
per-frame path.

---

## 5. The language: Gene

**Genesis already has this language, and it is not re-invented here.** `genesis::gene` is a
parse-once AST with a constant folder, an interpreter, a C++ emitter and dependency collection —
which is exactly this problem, already solved and already tested. R-BIND-2 requires it be
**promoted to a shared, UI-free library and aliased from `genesis_core`**, not copied: this suite
has already paid for one forked file (genesis's palette copy), and `arstrobench` shows the right
pattern — alias the namespace, compile the source.

### 5.1 Functions (Gene's set, unchanged)

```
abs acos asin atan atan2 ceil clamp cos deg dist div exp fade floor hypot lerp log max min mix
mod pct pow rad remap round sign sin smoothstep snap sqrt step tan turns wrap
```

Operators: `+ - * / %`, comparisons, `&& ||`, `!`, and the ternary `c ? a : b`. Booleans are
doubles (0/1). There are no statements, no loops and no user-defined functions — an expression is an
expression.

### 5.2 The two extensions Interstellar needs

1. **Dotted paths of arbitrary depth.** Gene's `Member` node is exactly two levels
   (`object.field`); `gr1.basic.exposure` is three and `s1.mask.0.adjust.exposure` is five. The
   parser must produce a **path node** with a segment list. Two-level members remain the special
   case, so Genesis is unaffected — this is a superset, not a change.
2. **A time scope.** `t`, `frame`, `fps`, `dur` join `w`, `h`, `minSide`, `pi` as built-in
   identifiers.

### 5.3 What Interstellar does **not** expose

- **`raw{ … }`**, Gene's verbatim C++ escape. Genesis needs it because its deliverable is source
  code; Interstellar has no code generation, and an escape hatch that cannot be evaluated would make
  a project unrenderable.
- **The colour type.** A colour-valued parameter is bound component-wise (`…​.r`, `…​.g`, `…​.b`),
  so arithmetic on a colour cannot arise.
- **`emitCpp`.** Nothing here compiles to C++. (It stays in the shared library for Genesis.)

---

## 6. The frame pipeline

The normative order (R-EVAL-2). Every stage is a pure function of the stages above it.

```
 ┌ 1  RESOLVE TIME ────────────────────────────────────────────────────────────┐
 │    frame index from t and fps; per clip: active?, local time, source frame  │
 │    index via speed (nearest-neighbour, R-CUT-6)                            │
 ├ 2  AUTOMATION ─────────────────────────────────────────────────────────────┤
 │    for each autolink active at t:  localT → shape value → map → mode       │
 │    → the address's automated value                                         │
 ├ 3  BINDINGS ───────────────────────────────────────────────────────────────┤
 │    topological order over the binding DAG; every read sees step 2's        │
 │    values; unresolvable → static value + one report (R-BIND-8)             │
 ├ 4  RACK COMPOSITION ───────────────────────────────────────────────────────┤
 │    per rack node: own params (steps 2–3 applied) → composeParams up its    │
 │    ancestors, weighted by each ancestor's opacity, skipping bypassed ones  │
 │    — this is COSMO's own function, unchanged                               │
 ├ 5  PER-LAYER COLOUR ───────────────────────────────────────────────────────┤
 │    EditEngine renders each active clip's source frame with its rack        │
 │    node's effective params → a graded RGBA frame  (the 17-stage pipeline)  │
 ├ 6  PER-LAYER GEOMETRY ─────────────────────────────────────────────────────┤
 │    geom.crop → fit → scale/rotate about anchor → translate, into the       │
 │    output raster                                                           │
 ├ 7  COMPOSITE ──────────────────────────────────────────────────────────────┤
 │    tracks bottom → top; per-clip blend + opacity; transitions resolved as  │
 │    a two-layer mix                                                         │
 ├ 8  OUTPUT ─────────────────────────────────────────────────────────────────┤
 │    colour-space encode → frame cache / writer / monitor                    │
 └────────────────────────────────────────────────────────────────────────────┘
```

Three things the order settles, each of which would otherwise be a defect nobody could name:

- **Automation before bindings** — so a binding may read an automated value. The reverse would make
  `expr="1 + ac_push.value * 0.08"` read a stale number.
- **Both before composition** — so a group's offsets are composed from values that are already
  final. Composing first and animating after would mean an automated group parameter reached its
  children through a value that had already been folded.
- **Colour before geometry** — so a crop or a scale never changes the *colour* of a pixel by
  changing which pixels the colour stages saw. Cosmo's own pipeline puts geometry first for the
  opposite reason (its crop defines the framed image the histogram describes); the two are
  consistent because Cosmo's crop is the source's *framing* (step 5's input) and Interstellar's is a
  *reframe* of the graded result (step 6). That is why the two crops have different addresses
  (R-COMP-3).

### 6.1 Caching, and the key that makes it safe

A cached frame is keyed by `(layer, source frame index, resolved-parameter hash, proxy level)`. The
hash covers **every value step 4 produced for that layer** — so changing any static parameter, any
breakpoint, any mapping or any expression that feeds that layer changes the key, and a stale frame
cannot be shown (R-PLAY-3/5). A key that hashed only the parameter *struct* would miss a change to a
shape a link maps into it, which is the exact bug this key shape exists to prevent.

---

## 7. What happens when it goes wrong

| situation | behaviour |
|---|---|
| an expression references a deleted object | the parameter falls back to its **static** value, the binding is marked `broken` in the model, and **one** event is emitted per render — not per frame (R-BIND-8) |
| an expression divides by zero / produces a non-finite value | same fallback, same report. A NaN reaching the engine is D-48's territory and the frame would be silently wrong |
| a link's shape was deleted | the link is `orphaned`; the address is static; refused at edit time, so this only arises from a hand-edited file |
| a cycle appears via a rename | impossible: a rename rewrites every referencing expression atomically (R-PARAM-2) and re-validates the graph before committing |
| an address resolves to a read-only parameter | refused at edit time, naming it |
| the rack is pinned | any colour command is refused with "the rack is pinned at `<commit>`" (R-COSMO-5) |

**A master render never dies of a stale expression.** Twelve thousand frames in, a typo three weeks
old must degrade one parameter and say so, not abort the delivery.

---

## 8. Proving it

Every claim above is checkable with no display and no video codec:

1. **`eval <address> --at <t>`** prints the resolved value; `--explain` prints each input and the
   intermediate. This is the unit of evidence for automation and bindings, and it is why R-AUTO-9
   requires it.
2. **A resolved-value table test** — a committed `.isp` with a shape, two links and three bindings;
   assert the value of a dozen addresses at a dozen times against a committed table. It catches an
   order change (§6), a mapping error and a cycle regression in one file.
3. **A golden-frame test** — the same project rendered to a PNG sequence and compared against
   committed goldens within a stated tolerance (R-RENDER-3). This is what catches "the numbers are
   right and the picture is wrong".
4. **The `--lint` output is asserted too**, because a warning nobody has ever seen printed is a
   warning that does not work (the R-CPU-4 lesson: a log line nobody had ever run was taken as
   proof for months).
