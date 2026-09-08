# Interstellar — professional video editor (colour blending & edit)

> Status: **concept / specification — CLI-first.** No code yet. Conformance rung **0** (spec only)
> on `arstro.rule` §5's ladder. This README is the one-page brief; the normative documents are in
> [`docs/`](docs/) and [`REQUIREMENTS.md`](REQUIREMENTS.md).

> **AMENDED (the project-in-project / mixer direction), 2026-09-08.** The first version of this
> brief put the grade **on the timeline clip** and animated it with per-clip `keyframe` nodes and a
> `grade-dissolve` transition. That is now withdrawn, in favour of the model in
> [`REQUIREMENTS.md`](REQUIREMENTS.md): **colour is authored in an embedded, live Cosmo project**
> (the *rack*), **the timeline only cuts**, and **animation is an automation clip on a named
> parameter**, exactly as a DAW automates a mixer parameter. Why the change is an improvement and
> not a preference is argued in [`docs/design.md`](docs/design.md) §2; what the old model could
> express and the new one must therefore still express is listed in §2.6 of that file. The
> superseded text is kept in §9 below so nobody re-proposes it by accident.

## 1. What it is

Interstellar is a non-linear video editor whose distinguishing focus is **colour blending** — not
just per-clip grading, but the way colour is *composited, modulated and driven over time*. It does
not own a colour engine: it **hosts Cosmo**. A Cosmo project sits inside every Interstellar project
as a live, writable child — the *grade rack* — and Interstellar adds only what *time* and *cut*
require.

Three sentences that fix the whole architecture:

1. **Cosmo grades; Interstellar cuts.** Every source's colour is a node in the embedded Cosmo
   project, edited through Cosmo's own panels, with Cosmo's grouping and Cosmo's stacking. The
   timeline holds cuts, and a clip carries no grade of its own.
2. **One authority per fact.** A source's colour lives in exactly one place — the Cosmo project.
   Interstellar's colour UI writes *into* it, so there is no import, no re-sync, and no second copy
   to drift.
3. **Every parameter is addressable, automatable and bindable.** `gr1.basic.exposure`,
   `gr1.opacity`, `clp_a.geom.crop.w` — one dotted address space, one registry, and any address can
   be driven by an **automation clip** or by an **expression** over other addresses.

## 2. The workflow it is built around

```
   Cosmo                                 Interstellar
   ─────                                 ────────────
   open the video sources AND the         import the .cmp  →  it becomes the project's
   behind-the-scenes stills in one            embedded rack (as=rack, branch=main)
   project; group them (gr1 "day
   interiors", gr2 "night"); grade    ⇄   cut on the timeline; place clips; animate any
   each source, stack looks on the        rack parameter with an automation clip; bind
   groups                                 one parameter to another with a calculation

                       every colour edit made in Interstellar is a commit
                       on the SAME Cosmo project — open it in Cosmo again
                       and the edits are there
```

The user's own statement of it, which the requirements are traceable to:

> *"Open a set of video sources directly in cosmo along with behind-the-scene photos, group and
> edit it, then import the project into interstellar — and interstellar will know that we embed the
> cosmo project. All edits in interstellar can apply back to cosmo. Like project in project."*
>
> *"For edit colour, make it like cosmo: each source is edited separately, with the grouping
> mechanism. The timeline view is only to cut. Just like a mixer in a DAW — when we want to animate
> a param of a filter of a group we create an automation clip for that. All params can have an
> automation clip (position, crop, opacity). Multiple params can use the same automation; a param
> can be bound to another param with a calculation, like genesis. Each object has its own name to
> bind: I have group 1 named gr1, gr1 will have filters and their params, so I can bind
> `gr1.opacity` to `gr1.basic.exposure`."*

## 3. How it is put together

Four layers, each depending only on those below it — the same shape as Cosmo
([`docs/architecture.md`](docs/architecture.md) has the full picture and the module map):

```
 host          window · GTK/Cairo · file dialogs · FFmpeg decode+encode · audio out · sockets
 ──────────────────────────────── seam ────────────────────────────────
 front ends    GUI · interstellar-cc · control socket · shots/tests      (peers, not layers)
 ──────────────────────────────── seam ────────────────────────────────
 interstellar_core   InterstellarService: Command in, AppModel + Event out
                     Timeline · Composite · ParamRegistry · Automation · Binding · CosmoEmbed
 ──────────────────────────────── seam ────────────────────────────────
 cosmo_core          a REAL CosmoService instance — the grade authority, driven by Commands
 engines             ImageProcessing (EditEngine/RenderService, per frame) · Gene (expressions)
```

The embedded Cosmo is not a library call and not a copied parameter struct: it is a
**`cosmo::CosmoService` running in-process**, dispatched with `cosmo::Command`s and observed
through its `AppModel` and `Event` stream. That is what makes "project in project" structural.

## 4. Colour blending — the focus, restated in the new model

1. **Per-source grading, with Cosmo's stacking.** A source is a Cosmo node; a group carries offsets
   that compose down onto its children (`composeParams`). This is the "each source edited
   separately, with grouping" requirement, and it is free — Cosmo already does it.
2. **Layer compositing.** Stacked video tracks blend with per-clip **blend mode + opacity**,
   evaluated after each layer's frame has been graded, so a composite is a composite of *graded*
   frames.
3. **Modulation over time.** Because every parameter has an address, *any* of them can be driven by
   an automation clip or an expression — an exposure ramp across a shot, a crop that pushes in, an
   opacity tied to a grade value. This subsumes the old "keyframe a grade" and "dissolve grade A
   into grade B" as two ordinary cases of one mechanism.

Non-goals for v1, kept explicit: GPU real-time playback at full resolution, effects beyond
colour/composite/transform, node-graph compositing, and multi-user editing. The focus is
**colour + cut**.

## 5. Documents

| question | document |
| --- | --- |
| What was asked for, and what is its status? | [`REQUIREMENTS.md`](REQUIREMENTS.md) — intent, `R-<AREA>-<n>` |
| What does the code do today? | [`docs/requirements.md`](docs/requirements.md) — as-built, `DR-<AREA>-<n>` (empty: no code yet) |
| Where does each module live, and what are the seams? | [`docs/architecture.md`](docs/architecture.md) + [`docs/architecture.puml`](docs/architecture.puml) |
| What are the classes, and their real signatures? | [`docs/detailed_design.md`](docs/detailed_design.md) |
| Why is it shaped this way, and what was rejected? | [`docs/design.md`](docs/design.md) |
| What exactly is in an `.isp` file? | [`docs/project-format.md`](docs/project-format.md) |
| How do automation and bindings evaluate? | [`docs/binding.md`](docs/binding.md) |
| What should the UI look and feel like? | [`docs/ui-brief.md`](docs/ui-brief.md) + `.claude/skills/arstro.design.rule` |
| What is built, and what is next? | [`docs/PROGRESS.md`](docs/PROGRESS.md) — the ledger |
| What is the build order and the gates? | [`docs/plan.md`](docs/plan.md) |
| Is this a known bug? | [`docs/DEFECTS.md`](docs/DEFECTS.md) |
| How do I work on this at all? | [`docs/DEVELOPING.md`](docs/DEVELOPING.md) |

Suite context: [`../../docs/vision.md`](../../docs/vision.md) ·
[`../../docs/shared-core.md`](../../docs/shared-core.md) (Nebula) ·
[`../cosmo/docs/`](../cosmo/docs/) (the reference app).

## 6. CLI surface, at a glance

CLI-first, and the GUI may do nothing the CLI cannot (`arstro.rule` §1). The full grammar is in
[`docs/project-format.md`](docs/project-format.md) §8; the shape:

```
interstellar-cc new cut.isp --fps 24 --res 3840x2160
interstellar-cc rack import cut.isp japan18.cmp          # embed a Cosmo project as the rack
interstellar-cc set  cut.isp gr1.basic.exposure=0.2      # writes THROUGH to the Cosmo project
interstellar-cc clip add cut.isp --track v0 --src gr1/DSC01.MOV --in 12.4 --out 16.6 --at 0
interstellar-cc auto new cut.isp ac_push --dur 2.0 --points 0=0,1=1 --ease easeInOut
interstellar-cc auto link cut.isp ac_push -> gr1.basic.exposure --at 4.0 --from 0 --to 0.8
interstellar-cc bind cut.isp gr1.opacity = clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)
interstellar-cc render cut.isp --out master.mov --range 0:12
interstellar-cc api --json                               # the generated API document
```

## 7. Engine reuse — what already exists and what does not

| need | status today |
| --- | --- |
| colour pipeline, per frame | **exists** — `EditEngine`'s 17-stage pipeline + `RenderService` on its own worker; its header already says a video editor may reuse it |
| the parameter set | **exists** — `arstro::EditParams`, whose own comment names "a future video editor" as a target front end |
| grouping, stacking, history, project text | **exists** — `cosmo::EditSession` + `CosmoService`, UI-free |
| expression language | **exists, in the wrong place** — `genesis::gene` is inside `genesis_core`; it must be promoted to a shared library and extended to dotted paths (R-BIND-2) |
| per-frame application helper | **a 49-line stub** — `core/ImageProcessing/src/video/VideoProcessor.h` applies a processor to a frame sequence and owns no time model; Interstellar's evaluator does not use it (see [`docs/design.md`](docs/design.md) §7) |
| video decode / encode | **does not exist** — a host-layer `IFrameSource`/`IFrameWriter` over FFmpeg (R-PLAY-1, R-RENDER-2) |
| Nebula (text project, VCS, embeds, resource pool) | **does not exist** — specified only; Interstellar needs the same minimal subset Solaris does ([`../solaris/docs/prerequisites.md`](../solaris/docs/prerequisites.md) §B1) |
| audio | **out of scope for v1** beyond playing an embedded Solaris mixdown (R-SCOPE-5) |

## 8. Roadmap

Eleven phases, gates and dependencies in [`docs/plan.md`](docs/plan.md):

**P0** freeze the contracts · **P1** model + `.isp` + CLI skeleton · **P2** frame-source seam +
single-frame render · **P3** the Cosmo embed as grade authority · **P4** timeline cut + composite ·
**P5** parameter registry + automation clips · **P6** bindings · **P7** scrub + proxy playback ·
**P8** offline master render · **P9** Nebula VCS, embed propagation, Solaris audio · **P10** the
Artboard UI.

## 9. Superseded design (kept so it is not re-proposed)

<details>
<summary>The original clip-grade + keyframe + grade-dissolve model — <b>none of this is in
force.</b></summary>

The first brief specified these node types: `track` (`kind = video | audio | adjustment`), `clip`
(carrying its own non-destructive `grade` stack and a `blend`), `grade` (an `EditParams` value
attachable to a clip, a track or an adjustment layer), `transition` (`kind = dissolve | wipe |
grade-dissolve`, easing `EditParams` A → B across a cut), `keyframe` (`(node, field, time, value,
easing)`, merged by time), and `embed` (`as=look` for a Cosmo project, `as=audio` for Solaris).
Colour blending was described as three layers: per-clip blend modes, grade interpolation across
time via keyframes and `grade-dissolve`, and a shared look library.

**Why it was withdrawn.** (a) A grade on a clip means a source used in three shots has three
colours to keep in step by hand, which is exactly the drift the suite exists to remove; (b)
`as=look` made the Cosmo project a read-only input, so a colour fix during the edit had to be made
twice; (c) `keyframe` and `grade-dissolve` are two mechanisms for one job, neither of which can
express "drive this parameter from that one", and both of which are special cases of an automation
clip on an addressed parameter.

**What the replacement must still be able to express**, since the old model could: a grade
changing over the length of a shot (→ an automation clip on the source's parameters, scoped to the
clip's time), a look dissolving across a cut (→ two clips of two source variants with an opacity
crossfade, or one automation clip spanning the cut), and an adjustment track (→ a rack group whose
children are the sources beneath it, since Cosmo's group stacking already *is* an adjustment
layer).
</details>
