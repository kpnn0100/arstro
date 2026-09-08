# Interstellar — UI Design Brief

> **The law is `.claude/skills/arstro.design.rule`** — invoked first, and **not restated here.**
> The palette, the radii, the type ramp, the spacing ladder, the durations, the six R-rules and the
> twenty gotchas live there. This file specifies only what is *about Interstellar*: its four
> workspaces, the widgets it adds, and the interactions that have no precedent in Cosmo.
>
> Requirements: `R-UI-*` in [`../REQUIREMENTS.md`](../REQUIREMENTS.md). Planned, not built (P10).

**Three sentences that decide everything else.** Interstellar looks like Cosmo, because it *is*
Cosmo's tokens, Cosmo's widgets and Cosmo's rhythm plus a time axis. It is a **desktop** app with a
pointer and a keyboard — there is no touch shell in v1, and the metric exemption R-TOUCH grants
Cosmo does not apply. And the monitor is the one thing on screen that never moves, because it is
the picture, and everything else is a way of changing it.

---

## 1. The shell

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│ TopBar  interstellar.   « project name »        [ Grade | Cut | Mix | Deliver ]   ⋯ │  29.25
├──────────────────┬──────────────────────────────────────────┬───────────────────────┤
│                  │                                          │                       │
│  left column     │             MONITOR                      │    right column       │
│  (per workspace) │       the composited frame at the        │   (per workspace)     │
│                  │            playhead — ONE widget,        │                       │
│                  │            every workspace (R-UI-2)      │                       │
│                  ├──────────────────────────────────────────┤                       │
│                  │  Transport   ◀◀ ▶ ▶▶   00:00:04:07       │                       │
├──────────────────┴──────────────────────────────────────────┴───────────────────────┤
│  bottom deck  (per workspace: filmstrip · timeline · lane stack · render queue)     │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

- **The workspace switcher is a `SegmentedControl`** — Cosmo's, with its 220 ms travelling
  highlight. Four labels, sized to their text.
- **Switching workspace cross-fades the three regions around the monitor**, over 180 ms, and does
  **not** touch the project, the playhead or the monitor (R-UI-1). A workspace switch that reloads
  anything is a defect.
- **The monitor is outside the cross-fading host.** One widget, one state, one picture (design.md
  §6).
- **Minimum window is derived, not guessed**: the monitor's floor (16:9 at 480 px wide) plus the two
  fixed columns plus the deck's minimum. The host applies it, and a column **gives way before the
  content does** — derive each column's effective open state every layout from "wanted AND there is
  room", eased through the same property the toggle drives (`arstro.design.rule` R4).

---

## 2. Grade — the rack

**This workspace is Cosmo, near enough to be the same room.** That is the requirement (*"for edit
colour, make it like cosmo"*) and it is also the cheapest thing in the whole UI, because the widgets
already exist (R-UI-6).

```
 left:   RackTree          the Cosmo group tree — groups, sources, bypass, drag to regroup
 right:  HistogramWidget   ┐
         EditStackTabs     │  Cosmo's OWN widget classes, linked and driven by Interstellar
           ParamPanel      │  (Basic/Detail · Mask · Mixer/Curve · Grade · Xform)
           MixerPanel      │
           CurvePanel      │
           GradePanel      │
           XformPanel      ┘
         RackActionBar     Add source · Duplicate (variant) · Group · Reference frame
 deck:   SourceBin         rack sources as cells — Cosmo's Filmstrip rhythm, 86 px
```

What is **new** here, and only these three things:

1. **A video source shows its reference frame plus a frame selector.** A small scrubber under the
   cell picks which frame is graded (R-COSMO-7). Changing it re-decodes and changes no parameter —
   so it must not look like an edit: no history entry, no dirty flag, and the panel values do not
   move.
2. **`opacity` on a rack object** — a slider on the group/source header, beside `bypass`. It is the
   *weight* of that node's own offsets on its descendants (design.md §4), so its label is
   **"Grade weight"**, not "Opacity": two things called opacity in one app is a real risk and the
   label is where it is cheapest to fix.
3. **A "used by" count per source** — how many clips reference it. Zero is not an error (R-COSMO-10:
   the behind-the-scenes stills are reference material), so it reads as a quiet count, never a
   warning badge.

---

## 3. Cut — the timeline

**Cuts and nothing else** (R-UI-3). No colour, no curves, no automation lanes.

```
 left:   SourceBin         rack sources, draggable to a track
 right:  ClipInspector     the selected clip: src · in/out/at · speed · fit · blend · opacity · geom
 deck:   TimelineView      ruler · video tracks (top over bottom) · audio track · markers · playhead
```

### 3.1 Interactions, and the traps in each

| gesture | behaviour | the trap |
|---|---|---|
| drag a clip | moves in time, snapping to the playhead, clip edges and markers | **never teleport**: store `grab = clipStart − pointerTime` on `Down` and add it back on `Drag` |
| drag a clip edge | trims; the neighbour is untouched | the *snapped* value is what the command carries — the service must never receive an unsnapped value and re-derive it |
| drag between two clips | rolls the edit point | roll needs both clips, so the pick target is the seam, and the seam's width is one constant read by both the hit test and the paint (gotcha 13) |
| `S` at the playhead | splits the clip under it | new ids, never reused (R-FMT-5) |
| wheel | scrolls horizontally; `Ctrl`+wheel zooms | **a notch is not a pixel** — one notch is ~10 px of scroll or one zoom step, and the zoom **eases** |
| drag a source in from the bin | creates a clip at the drop point | the drop preview is an eased ghost, and it must be drawn in the overlay pass to escape the track's clip |

### 3.2 The zoom is the hardest thing in this widget

`pixelsPerSecond` is an **animated** property. Every geometric read — clip rects, the ruler's tick
spacing, the playhead's x, the visibility cull, the snap threshold in seconds — goes through the
**live eased value, re-derived every frame**. Deriving once zooms the ruler and leaves the clips
behind; that is gotcha 19, and the UI-scale defect it comes from was written by someone who had just
read the rule. **Compliance is checked by comparing two frames half a tween apart**, never by
reading the code.

### 3.3 States

Empty ("drag a source here", with the source bin highlighted), loading (ruler drawn, tracks greyed,
no clips), and a clip whose source is **offline** (a marked placeholder — the project stays
openable, R-RACK-5).

---

## 4. Mix — the mixer

**The workspace the user described**: objects in columns, their parameters as rows, automation lanes
in time, bindings shown on the parameter they drive.

```
 left:   ObjectList        gr1 · gr2 · s_day01 · v0 · clp_a …   (expand → its parameters)
 right:  BindingInspector  every binding on the selected object: target ← expression ← deps
 deck:   LaneStack         one lane per automated address; links drawn as clips in time
```

### 4.1 The mixer strip, and why it is a list rather than a wall of faders

A DAW's mixer is a wall of channel strips because a channel has a fixed, small parameter set (gain,
pan, sends). A rack group has **dozens** of addressable parameters and a clip has a dozen more, so a
fixed strip cannot show them. So: **a list of objects, each expanding to its parameters**, with the
*automated* and *bound* ones surfaced first and the rest behind a "show all" — because in practice a
user automates five parameters and leaves two hundred alone.

Each parameter row is a `SliderRow` (Cosmo's) plus two affordances:

- **an automation button** — creates a lane for this address, or reveals it. Disabled, with the
  reason beside it, when the registry says the address is not automatable (R-AUTO-6). *A control
  that cannot work must look like it cannot work* — `enabled = false` and the animated disabled
  amount, never a button that silently does nothing.
- **a binding chip** — absent when there is no binding; when there is one, the row's value readout
  becomes the **resolved** value and the chip shows the expression, click-to-edit. A bound parameter
  whose slider still looks draggable is a lie, so the slider goes to its disabled treatment and the
  chip is the control.

### 4.2 The lane stack

- **It shares the timeline's time axis by deriving from the same animated `pixelsPerSecond` and
  scroll offset** — not by keeping its own copy. Two copies of one fact drift and the symptom is a
  lane three pixels out of step with the cut above it (gotcha 15).
- **A lane expands to show its curve**, and the expansion **eases its height** (34 → 96) rather than
  swapping between two heights.
- **A link is a clip**: draggable, its ends trimmable (which time-scales the shape), copyable, and
  showing the shape's **name** — because the name is what tells you this shape also drives something
  else.
- **A shape used by more than one link says so** (`ac_push · 2 links`), and selecting one link
  highlights its siblings. This is the affordance that makes the shared-automation model
  discoverable; without it, editing a shape and changing a second parameter feels like a bug.
- **The boundary discontinuity is drawn** — a step marker where a link's first value differs from
  the static value with no fade (R-AUTO-5). A lint finding nobody can see is a lint finding that
  does not work.
- **It clips *and* scrolls**: clamped both ends, one viewport rectangle shared by the measure, the
  row placement, the visibility test, the paint clip and the scrollbar, the wheel bubbling when
  there is nothing to scroll (R6).

### 4.3 The expression editor

The one genuinely new control in the app (R-UI-5).

- **Completion over the registry**, filtered as you type, showing each candidate's unit and range.
  The address space is too large to type from memory.
- **Validation inline, as you type** — the same `BindingGraph::set` the command would call, so a
  cycle is refused *while you are looking at it* and names both ends.
- **The resolved value beside the field**, live, updating with the playhead. This is what turns an
  expression from a guess into a reading.
- **Errors are sentences, not codes**: `cycle: gr1.opacity → gr1.basic.exposure → gr1.opacity`;
  `no such address: gr1.basic.exposer — did you mean exposure?`
- It is a modal, and it follows the modal skeleton exactly: an `AnimatedProperty mAppear`, a scrim,
  `beginClose()`, modal-capture `hitTestSelf` as `mOpen && !mClosing`, click-outside cancel, painted
  in `onOverlay`, **the callback copied out and the modal closed before the action runs**, Escape
  cancels, Enter confirms.

---

## 5. Deliver — render and export

```
 left:   RenderQueue       queued/active/finished jobs, each cancellable
 right:  OutputSpecPanel   format · codec · resolution · range · branch
 deck:   LintReport        boundary steps, broken bindings, offline sources, unreferenced clips
```

- **The lint report is the reason this workspace is not a modal.** It is a list the user works
  through, each row jumping to the offending object in the workspace that owns it.
- **Progress is a bar plus a rate plus an ETA**, and the bar **fades out over 300 ms while its fill
  finishes** — the last thing a user should see of a completed render is a full bar, not one
  vanishing at 94%. Asymmetric durations are legitimate and this is the canonical case.
- **A rounded fill shorter than its own height reads as a dot that pops**, so below `2 × radius` draw
  nothing: that is the honest picture of "not started" (gotcha 12).
- **A render that is running disables the controls that would invalidate it**, with the reason
  beside them — not silently ignoring the click (R2).

---

## 6. The transport

Persistent, under the monitor, in every workspace.

- **The scrubber is direct manipulation** — it follows the pointer exactly, with no easing. That is
  the one motion exemption, and *the pointer is the animation*. Every value **derived** from the
  playhead still eases.
- **The timecode readout is `font::mono()`**, because a proportional face jitters as the number
  changes, which is the entire reason the mono family is vendored.
- **The proxy level and the dropped-frame count are readouts, not warnings** — a small mono figure
  that appears when either is non-default. Under load the app must *say* it is showing a coarse
  frame (R-NFR-4), and a red badge would make a normal, correct degradation look like a fault.
- **Play/pause is one button whose glyph cross-fades**, never swaps.

---

## 7. Every state, including the two nobody draws

`idle · hover · pressed · active · disabled · empty · loading · error/refused`, drawn **and shot**
(R-UI-7). The Interstellar-specific ones, because these are the ones that will be skipped:

| surface | empty | loading | refused |
|---|---|---|---|
| Monitor | "no clip at the playhead" — **not** a black frame, which reads as a bug | "decoding" + the level being waited for | "source offline" + the path |
| TimelineView | "drag a source here" | ruler drawn, tracks greyed | — |
| LaneStack | "no automation on gr1 — pick a parameter to automate" | — | "this parameter cannot be automated: `blend` is a mode, not a value" |
| SourceBin | "the rack is empty — add footage" | Cosmo's per-entry spinner cells | "decode failed" per cell |
| BindingInspector | "no bindings" | — | the cycle, with both ends named |
| RenderQueue | "nothing queued" | — | "the rack is pinned at 8f2c1ab" |

**Empty and loading are the two that make an app feel broken.** A lane list empty because nothing is
automated and one empty because the project has not finished loading look identical, and they ask
the user for different things. Say which, in words, next to the control.

---

## 8. Verification — how this brief is checked

Not by reading it. `interstellar_shots` renders every named state through `artboard::CairoTarget`
into a PNG with no display, and `interstellar_ui_tests` asserts on the assembled app
(`arstro.design.rule` §8, R-UI-7):

- **Two window sizes minimum**, one small (1440×900 and 1024×640): side columns hold, the monitor
  flexes, nothing collapses, the deck keeps its minimum.
- **Mid-transition and at rest.** Two frames half a tween apart that *differ* — for the workspace
  cross-fade, the timeline zoom, the lane expansion, the column fold and the progress bar. **Never
  settle and then look**: settling 400 ms is long enough for a 180 ms fade to finish, and the
  assertion then reads 1.0 and passes a snap.
- **A live eased value exposed wherever a test must tell a tween from a snap** —
  `TimelineView::pixelsPerSecond()` returns the live value for exactly this reason. A private eased
  value is an unverifiable one.
- **Nothing overlaps a sibling; nothing is clipped at the window edge; every list row is reachable
  at some scroll offset; every text fits its box.**
- **A shot per new state, committed.** Shots are product code, not scaffolding: a state with no shot
  is a state nobody will look at again.

Two known gaps in the tooling, recorded rather than assumed away, because Cosmo's skills spent a
whole investigation each discovering them: **`cosmo_shots` has no `--size` and no `--script`**, and
there is **no headless UI-tree dump** (`ui dump` is a socket command and needs a live window).
Interstellar's harness should have all three from the start — `--size`, `--script` and a headless
tree dump — because they are cheap when the harness is new and expensive to retrofit.
