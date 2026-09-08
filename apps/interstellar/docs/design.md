# Interstellar — Design Rationale

**Why the code is shaped this way, and what was rejected.** Per `arstro.rule` §3.1 this file is
updated when a decision is *reversed* — and the violation it guards against is a sentence stating a
decision left standing beside its replacement. Every heading below is a decision; every one names
the alternative it beat and the evidence.

Contents: [1. Host Cosmo](#1-host-cosmo-rather-than-share-its-parameter-struct) ·
[2. Colour off the timeline](#2-colour-comes-off-the-timeline) ·
[3. Automation clips](#3-automation-clips-rather-than-keyframes) ·
[4. One address space](#4-one-address-space-for-everything) ·
[5. Gene](#5-gene-rather-than-a-new-expression-language) ·
[6. Small decisions with a reason](#6-small-decisions-that-still-need-a-reason) ·
[7. What was rejected outright](#7-rejected-outright) ·
[8. Known risks](#8-known-risks-stated-before-they-bite)

---

## 1. Host Cosmo, rather than share its parameter struct

**Decision.** `interstellar_core` depends on `cosmo_core` and constructs a real
`cosmo::CosmoService`. Colour edits are `cosmo::Command`s. The `.cmp` is the colour authority; the
`.isp` contains no colour at all.

**The three alternatives, and why each loses.**

| alternative | what it gives | why it loses |
|---|---|---|
| **share `EditParams` only** — Interstellar stores its own `EditParams` per source and links against ImageProcessing, not Cosmo | the least coupling; Interstellar owns its data | it re-implements grouping, stacking, history, bypass, presets and the project writer — every one of which already exists and is tested. And "apply back to cosmo" becomes an export/import with a merge, which is the drift the suite exists to remove |
| **embed read-only (`as=look`)**, the original brief | simple, one-directional, no write path | a colour fix during the edit has to be made twice, and the second one is the one that gets forgotten. The user's requirement is explicit: *"all edits in interstellar can apply back to cosmo"* |
| **run Cosmo as a separate process** and talk over the control socket | hard isolation; Cosmo unchanged | two processes for one project; the per-frame render crosses the boundary tens of times a second; and the pixels are the thing you cannot cheaply send. A colour authority that lives 33 MB away from the compositor is not an authority, it is a service call |

**What hosting actually costs.** `CosmoService` is a service over an `EditSession` over a
`RenderService`. No window, no GTK, no fonts, no Artboard. `cosmo-cc` already proves it runs
headless — it opens projects, decodes, renders and exports with no display. So the cost is a static
library and an injected decoder, and the benefit is that grouping, stacking, history, presets,
bypass, the 17-stage pipeline, the pyramid levels, the LRU pixel pools and the project writer are
all *already done and already have requirements*.

**And it makes the hardest guarantee in the project cheap.** R-RENDER-5 asks that a frame grabbed
from the cut be byte-identical to the same frame graded in Cosmo. With a hosted service that is
true by construction — it is the same engine, the same params, the same code path. With a copied
`EditParams` it would be a claim to be re-established after every change to either app.

**The trap this avoids, named.** Cosmo's own gotcha 15 is *"two copies of one fact always drift"*.
An `EditParams` in the `.isp` and an `EditParams` in the `.cmp` are two copies of one fact, and the
suite's whole premise is that they must not be.

---

## 2. Colour comes off the timeline

**Decision.** A clip carries no colour. It references a rack node, and the rack node's effective
parameters are the colour. A `grade` field on a clip is a validation **error**, not an ignored key.

**Why.** Three reasons, in increasing order of how much they cost:

1. **A source used in three shots would have three colours.** In the clip-grade model, cutting one
   shot into three pieces triples the number of places its look lives, and keeping them in step is
   manual. In the rack model, the three clips reference one node and there is nothing to keep in
   step.
2. **It is what the user asked for**, and the reason they asked is the workflow: the look is locked
   on stills and frames *before* the cut exists. A model where colour lives on clips cannot express
   "the look is locked" at all, because there are no clips yet.
3. **It makes the grade UI Cosmo's UI.** If colour is on a rack node, then the panels that edit it
   are Cosmo's panels editing Cosmo's node, and "for edit colour, make it like cosmo" is not a
   styling exercise — it is the same widgets over the same service.

**The obvious objection, and its answer.** *"But sometimes one shot really does need a different
look from another use of the same source."* Then duplicate the rack node (R-RACK-3): two nodes on
one file, two sets of parameters, two clips. That is one command, it is explicit, and it keeps
"colour lives in one place per look" true. A per-clip override would keep it true only until someone
used it.

**Why rejection rather than tolerance.** Nebula preserves unknown keys, which is right for forward
compatibility. But a `grade` on a clip is not an unknown key from the future — it is a user's edit
that this model cannot honour, and silently ignoring it means silently discarding their work. The
same distinction runs through the validator: **structural errors refuse; numeric corruption repairs
and reports** (Cosmo's D-36 — one stray `nan` must not make a project unopenable).

---

## 3. Automation clips, rather than keyframes

**Decision.** Animation is `#autoclip` (a named, reusable shape) + `#autolink` (its application to
an address, with a time placement, a value mapping and a mode). Not `keyframe (node, field, time,
value)`.

**Why the shape/link split is the whole point.** The user's requirement was *"multi param can use
the same automation"*. A keyframe belongs to one field by construction, so sharing is impossible. A
shape belongs to nobody, so one shape drives an EV ramp and a scale push together — and moving one
breakpoint moves both. That is not a convenience: it is the difference between "these two things
move together" being *true* and being *maintained*.

**Why it also beats a per-parameter envelope.** A DAW offers both, and the clip is the better fit
here for three reasons: a clip is an object so it has a bind name and can be read by an expression
(`ac_push.value`); a clip can be copied and reused where an envelope is welded to its parameter; and
a **scoped** clip travels with its shot through a re-cut, which an absolute-time envelope cannot.
A cut gets re-timed constantly and that third reason is the one that will matter daily.

**What the old model could express, and where it went** — because a replacement that loses a
capability is a regression, not a simplification:

| old mechanism | new expression of it |
|---|---|
| `keyframe` on a clip's grade field | an `autolink` scoped to the clip, targeting the source's address |
| `transition kind=grade-dissolve` | one `autolink` spanning the cut, or two source variants with an opacity crossfade |
| `track kind=adjustment` | a rack **group** — Cosmo's group stacking already *is* an adjustment layer |
| a shared look library | unchanged: presets and LUTs are pool assets, and a Cosmo preset *is* an Interstellar preset |

**The cost, paid explicitly.** A boundary between "inside a link" and "outside a link" is a
potential step in the picture, which a continuous keyframe track does not have. That is why
`fadeIn`/`fadeOut` exist and why the boundary lint exists (R-AUTO-5). Choosing the more expressive
model and then paying for its one sharp edge is the right trade; not noticing the edge would not be.

**One authority per parameter, enforced rather than resolved.** A parameter could have been allowed
both a binding and a link, with a documented precedence. It is refused instead, because a
precedence rule is a rule someone has to remember, and the alternative — read the shape from inside
the expression — writes the precedence down in the project where it can be seen.

---

## 4. One address space for everything

**Decision.** `<object>[.<filter>].<param>[.<component>]`, one generated registry, and `set` routes
by the registry's `owner` field.

**Why one space.** The user's example (`gr1.opacity` bound to `gr1.basic.exposure`) requires that a
rack group's Interstellar-owned parameter and its Cosmo-owned parameter be *addressable the same
way*. Once that is true, the same strings serve `set`, the CLI, the API document, the expression
language, the automation lane list and the UI panel builder — six consumers, one vocabulary. The
alternative is a Cosmo vocabulary plus an Interstellar vocabulary plus a translation layer, and the
translation layer is where the typos live.

**Why generated, not hand-written.** Cosmo's `--help` is the experiment already run: command names
come from `commandNames()` and are always right; the argument hints beside them are hand-maintained
and missing for 8 of 30. With several hundred addresses, a hand-written registry would be wrong
within a week, and an agent reading the API document would build scripts on parameters that do not
exist — which is worse than no document, because it is a document that lies.

**Why Cosmo's spelling wins.** Leaf names are `EditParamsIO`'s keys. So a `.apf` preset, a `.cmp`
file, a `cosmo-cc set` line and an Interstellar expression all spell `exposure` identically, and
nobody has to learn two names for one number (R-PARAM-4).

**The one hard case, decided.** *Two crops.* A source has one **framing** (`s1.xform.crop`, authored
in Cosmo, part of the look, static) and a shot has a **reframe** (`clp_a.geom.crop`, Interstellar's,
animatable, per clip). Collapsing them into one address would mean a source used in three shots can
only have one reframe, which defeats the requirement that crop be automatable. Giving them the same
*name* in two scopes would be worse than giving them different ones, so: `xform.crop` is the
framing, `geom.crop` is the reframe, and [`binding.md`](binding.md) §6 explains why they also sit on
opposite sides of the colour stage.

**And a second, smaller one: `gr1.opacity`.** The user's example binds it, so it must exist on a
rack *group*. Cosmo has `bypass` — a boolean — and nothing continuous. So `opacity` on a rack object
is defined as **the weight with which that node's own parameter offsets apply to its descendants**:
a continuous `bypass`, owned by Interstellar, composed in step 4. It is not a pixel opacity — pixel
opacity is a clip/track parameter — and calling both "opacity" is a real risk that the two-scope
naming (rack object vs timeline object) is what keeps manageable.

---

## 5. Gene, rather than a new expression language

**Decision.** Promote `genesis::gene` to `core/Gene`, alias it from `genesis_core`, extend it with
dotted paths and a time scope. Do not write a second expression language, and do not copy the file.

**Why Gene.** It is already: parsed once into an AST, constant-folded, interpretable, and able to
collect its own dependencies (`collectMembers` — which is exactly the cycle-detection input this
design needs). Its function set is the right one. Its number formatter already solves "shortest
literal that reads back bit-exact", which the canonical serializer needs anyway. It is tested by a
verifier that diffs an interpreter against an emitter, which is a stronger correctness argument than
a new implementation would start with.

**Why promote rather than copy.** `arstro.design.rule` §2 states the precedent as a scar: genesis
forked Cosmo's palette and it has already drifted, while `arstrobench` **aliases** Cosmo's
namespaces and compiles `cosmo/Theme.cpp` and has cost nothing. A forked expression language would
be the same mistake with much worse failure modes — two parsers accepting slightly different
grammars, silently.

**The extensions, and why they are safe.** Gene's `Member` node is exactly two levels
(`object.field`); Interstellar needs `gr1.basic.exposure` (three) and
`s1.mask.0.adjust.exposure` (five). A path node with a segment list is a **superset**: the
two-segment case remains what Genesis already emits, so Genesis is unaffected. The time scope (`t`,
`frame`, `fps`, `dur`) is additive identifiers, which is the same shape as its existing `w`, `h`,
`minSide`.

**What is deliberately not exposed.** `raw{}` (Interstellar has no code generation, and an
unevaluatable escape would make a project unrenderable), the colour type (bound component-wise
instead, so colour arithmetic cannot arise), and `emitCpp` (nothing here compiles to C++). Each is
an exposure Genesis needs and Interstellar does not, and an unused escape hatch is an untested one.

---

## 6. Small decisions that still need a reason

- **Frames are the authority, not seconds.** Solaris made beats authoritative for the same reason:
  a cut that lands between frames is a bug, and two edits must never compare unequal over a
  rounding difference. Times are written as decimal seconds *on a frame boundary* and re-derived as
  an index on read (R-CUT-5).
- **A dissolve eases linearly.** An eased alpha ramp reads as a luminance bump in the middle of the
  transition, because two frames at 50% do not sum to one frame at 100%. Cosmo's photo dissolve is
  `Linear` for exactly this reason, and it is the only place in either app where `Linear` is right.
- **The monitor lives outside the workspace host.** One widget showing one frame in all four
  workspaces. Putting it inside the cross-fading host would make it two widgets with two states,
  and a frame that looks different in two workspaces is a defect (R-UI-2).
- **A proxy-level change is not a content change.** Two resolutions of the same picture must not
  cross-dissolve — that reads as a double exposure. Set both views (design-rule gotcha 10, "a zoom
  is not a content change").
- **Read-ahead is bounded by bytes, not frames.** A 4K RGBA frame is 33 MB; "twenty frames" is
  660 MB. Every cap in this design is in bytes for the same reason Cosmo's pixel pools are (R-MEM).
- **`RenderJob::step()` renders one frame per call.** Cosmo's batch exporter does one image per
  main-loop step (R-EXPORT-6) so that progress is honest and a cancel is prompt; a loop would
  deliver its progress events in a burst at the end.
- **Lanes derive the time axis from the timeline's animated value**, rather than keeping their own.
  Two copies of one fact drift, and the symptom would be a lane three pixels out of step with the
  cut above it (gotcha 15).
- **The service owns the playhead; the view eases to it.** Presentation is still a real category
  (R-SVC-4). A drag sets it directly — direct manipulation is the one motion exemption — but
  everything *derived* from it still eases.
- **`interstellar_core` gets its own `ThreadBudget` reference, not its own budget.** See §8's first
  risk: three consumers, one number.

---

## 7. Rejected outright

Recorded so they are not re-proposed as improvements.

- **A node graph for compositing.** Powerful, and the wrong shape for this product: the focus is
  colour + cut, and a node graph makes the *simple* case — grade a source, cut it, fade it — harder
  than a rack and a timeline do. Explicitly out of scope (R-SCOPE-6).
- **Baking automation into per-frame parameter values on save.** It would make rendering trivial and
  editing impossible: the shapes are the thing the user edits, and a baked project cannot be
  re-timed. `auto flatten` exists as an explicit, destructive command that says what it destroyed.
- **A GPU-first pipeline.** ImageProcessing has a GPU backend behind an opt-in seam, and using it is
  a later optimisation with a measurement, not an architecture. A design that requires a GPU to be
  correct cannot be tested in CI.
- **Storing each binding's dependency list in the project.** It is derivable from the expression,
  and a stored copy would drift the first time an expression was edited by hand (R-G-3).
- **A separate "look library" concept.** Presets and LUTs are pool assets shared with Cosmo. A
  Cosmo preset *is* an Interstellar preset, and inventing a second library would create the second
  authority this whole design is built to avoid.
- **Letting the GUI reach the timeline directly, "just for the drag".** That is the exact shape of
  Cosmo's remaining `R-SVC` debt (105 direct session calls against 7 dispatches). A drag sends a
  command per commit and eases locally between them.
- **Two thread budgets, one per service.** This is D-11, verbatim, and it produced 17 threads on a
  16-core box.
- **A new `core/VideoProcessing` shared library.** `vision.md`'s module table proposed one, and
  `ImageProcessing/src/video/VideoProcessor.h` is a 49-line stub in that direction — it applies an
  `ImageProcessor` to a frame sequence and advances a frame index. It is not what is needed: it owns
  **no time model**, no timeline, no automation and no compositor, so the interesting 95% would live
  above it anyway. And there is **no second consumer**, which is the only thing that justifies a
  shared library — promoting one consumer's code is how a library acquires an API shaped by exactly
  one caller. So the evaluator and the compositor live in `interstellar_core`, the stub stays where
  it is as an ImageProcessing-level convenience, and the day something else needs a timeline is the
  day this is revisited. (`vision.md`'s table is amended to say so.)

---

## 8. Known risks, stated before they bite

1. **Three consumers of one CPU budget.** Cosmo had two and got it wrong twice. Interstellar has
   rack decode, colour render, video decode and a render job — four. The mitigation is structural
   (one owner, injected everywhere, division published in the model) and the evidence is a
   randomized concurrency test with a stall deadline, not an argument.
2. **Per-frame colour render throughput is unknown.** The engine drops every stage at its default
   and picks a pyramid level against a measured budget, so the *mechanism* to degrade exists. What
   is not known is whether a 1280-edge graded frame fits a frame interval on a laptop. P2's gate is
   a measurement, not a demo.
3. **Nebula does not exist, and two apps need it.** If it slips, P1 ships with a serializer and
   branch pointers only; merge, rebase and embed propagation wait for P9. The plan is sequenced that
   way deliberately so the schedule risk is contained to one phase.
4. **The address space is a single point of failure.** Everything reads the registry. That is the
   design's biggest bet (architecture §10.3) — and its consolation is that if it is wrong, it is
   wrong in one file.
5. **Hosting Cosmo couples Interstellar to Cosmo's release cadence.** A change to
   `cosmo::Command`'s enum or `AppModel`'s shape is a change to Interstellar's build. Mitigation:
   Interstellar depends only on the *service* surface, which is the one part of Cosmo that R-SVC is
   deliberately stabilising — and the equivalence test in both apps is what will catch a break.

   **This risk is smaller than it looks, because the surface is already the right shape.** Checked
   against the source rather than assumed: `CosmoService`'s constructor is
   `explicit CosmoService(ThreadBudget &budget)` — it already takes the budget **by reference**,
   which is R-COSMO-8's central requirement satisfied before it was written down. It already has
   `setDecoderFactory`, `setWorkerInit` (the per-thread OpenMP pin, D-12), `setImageWriter`,
   `subscribe(EventSink)`, `applySettings`, `dispatch`, `dispatchText`, `pump`, `model` and
   `takeFrame`, and as of its S4c step it **owns** its `EditSession` rather than borrowing one. So
   `RackEmbed` needs no new Cosmo API for any of that.

   **One genuine gap remains**: the engine's pixel-memory caps are set on `RenderService`
   (`setMemoryCaps`), reachable only through the explicitly transitional `session()` accessor — the
   one Cosmo's own ledger counts as lines still to delete. Interstellar should not add a use to that
   count, so the P0.2 item is narrowed to *"plumb the caps through `CosmoService`"*, which is a
   one-method change in Cosmo's own commit.
6. **Two `RenderService` workers may exist** (Cosmo's, for the rack's stills; Interstellar's, per
   layer) and the budget must cover both. If that turns out to double-count, the fix is one render
   worker shared through the rack seam — which is why `sessionForRender()` is named rather than
   hidden.
7. **R-COSMO-7 is work in another app.** Interstellar cannot start P3 until Cosmo can open a video
   source as a reference frame. That is a cross-app dependency on `arstro.cosmo.core.implement`, it
   is listed as a P0 prerequisite, and pretending it is Interstellar's own work would hide the
   dependency rather than remove it.
