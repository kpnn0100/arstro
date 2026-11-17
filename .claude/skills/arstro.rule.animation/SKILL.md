---
name: arstro.rule.animation
description: The animation law for everything Arstro draws that moves — logo and splash animations, app open/close, loading loops, screen and panel transitions, in every app (cosmo, interstellar, solaris, pulsar, genesis, the launcher shell) and on every brand page. Three rules set by the founder — never stretch an object, every move has a strong speed slope in and out, nothing disappears suddenly (a leaving object fades WHILE it moves, never after it stops) — plus the legal way to show speed (light streaks), the loop rule, the easing table (Artboard `Easing::` names and their cubic-bezier twins), and the checklist. Invoke BEFORE designing or changing any animation, alongside arstro.rule.design; invoke directly for "animate this", "add a splash", "make it appear/disappear", "add a loading animation", "why does this motion feel wrong".
---

# arstro.rule.animation

**How things move in Arstro.** `arstro.rule.design` §1 says *that* every visible change must be an
animation. This file says *what a correct animation is*. Follow both; on the shape of a motion,
this file wins.

The three rules were set by the founder on 2026-11-17 while judging the Arstro logo animations
(`docs/brand/arstro-identity.html`, section 08 — the reference implementation; play it before
designing a new motion). They are stated in the founder's terms first, then made checkable.

## Rule 1 — Never stretch an object

> "never stretch object"

**A shape keeps its proportions for its whole life.**

- **Allowed:** translate, rotate, opacity, colour, and **uniform** scale (x and y by the same
  factor, e.g. the star growing from 30% to 100% as it appears).
- **Forbidden:** non-uniform scale (`scaleX` ≠ `scaleY`), skew, squash-and-stretch, and any
  per-frame change of one dimension of a logo, icon, glyph or image.
- **To show speed or thrust, add something; do not deform something.** The legal device is the
  **light streak** (below).

**A fixed clip is not a stretch.** An object may move behind an edge that stays still — the
Solaris sun rises from behind its horizon. The clip never moves or scales, and the object behind
it only translates.

This rule is about *objects* — marks, icons, glyphs, pictures. A layout container changing its
width (a panel opening, a rail collapsing) is a layout change governed by `arstro.rule.design`; its
*contents* still may not be stretched to follow it.

### Light streaks — the legal way to show speed

- A streak is its own element: a thin rounded bar, 2.5–4 units wide on a 96 grid.
- It is a gradient that fades to transparent at its far end (a thrust streak) or at both ends
  (a passing streak), so it has no hard edge.
- **A streak has a fixed length.** It fades in, travels, and fades out. It is never grown or
  shrunk — that would be stretching.
- A thrust streak travels with the object, in the object's accent colour. **It starts at the
  object's tail edge and runs back from it — it is never drawn under the object**, or it would
  show through as the object fades. Passing
  streaks travel the opposite way, beside the object, in white.

## Rule 2 — Speed has a slope, strong going in and strong going out

> "all need to have slope for speed with high ingoing and outgoing"

**Nothing that can be seen starts at full speed or stops at full speed. The speed ramps up
strongly and ramps down strongly.**

- **No linear motion.** `Linear` is allowed only for a pure cross-dissolve of opacity
  (`arstro.rule.design` §2.5) — never for position, scale or rotation.
- **Rest to rest** (visible at both ends): strong ease-in-out.
- **Arriving** (invisible at the start, at rest at the end): ease-out. The object may be moving
  when it first becomes visible *because it is fading in at that moment*; it must slow into rest.
- **Leaving** (at rest at the start, invisible at the end): ease-in. It must start slowly from
  rest; it may still be moving when it reaches zero opacity — Rule 3 requires exactly that.
- **"Strong" is a floor, not a mood.** Cubic or steeper. `EaseInOutSine` and the quad curves are
  too gentle for a move the eye follows; keep them for tiny repeating motions (a shake of a unit
  or two).
- A small overshoot (`EaseOutBack`) is allowed on an arrival when it reads as a settle. It is
  motion past the target, not a change of shape, so Rule 1 is not involved.

| Case | Artboard | CSS twin (brand pages) |
|---|---|---|
| Rest → rest | `Easing::EaseInOutCubic` (or `EaseInOutQuart`) | `cubic-bezier(.7, 0, .3, 1)` |
| Arriving | `Easing::EaseOutCubic` | `cubic-bezier(.2, .8, .2, 1)` |
| Arriving with a settle | `Easing::EaseOutBack` | `cubic-bezier(.34, 1.56, .64, 1)` |
| Leaving | `Easing::EaseInCubic` | `cubic-bezier(.6, 0, .9, .4)` |
| Tiny loop (≤ 2 units) | `Easing::EaseInOutSine` | `ease-in-out` |

## Rule 3 — Nothing disappears suddenly

> "don't suddenly disappear … fade out while doing that, not after stopped"

**A leaving object fades while it is still moving, and reaches zero opacity at or before the
moment its move ends.**

- **Never pop.** Opacity never jumps to 0 (or from 0). Every fade is eased and lasts at least
  120 ms.
- **Never "move, stop, then fade".** If an object travels and vanishes, the fade overlaps the
  travel — start it by about 40% of the move and finish it with the move. An object must never be
  seen standing still, waiting to fade.
- **Never remove a moving object at full opacity** (flying off and being cut by a clip edge or
  deleted). It fades out before it would reach any edge.
- **A fade never accelerates.** Opacity eases in AND out (`EaseInOutSine` / `ease-in-out`), even
  inside an exit whose position eases in. An `ease-in` fade ends at its steepest: measured on the
  app logos, it dropped 0.10 of alpha in one 8 ms step — a pop, with a curve on it. `ease-in` is
  for position only.
- The same holds for every part of a group: a streak, a trail, a label each fade on their own
  eased curve inside the group's exit. Parts may finish at different times; none may pop.
- Appearing is the mirror: fade in while arriving, never appear and then start moving.

## Loops

- A loop **starts and ends in the same pose**, so it can be entered and left without a jump.
- **Change state only at a loop boundary, or ease out of the loop first** — those are the only
  two ways. Waiting for the boundary is the default. Ease out (every part home in 240 ms, strong
  in-out) when waiting would cost the user time: an opening splash must not hold a launch for up to
  a whole loop period. In code: `brand::Leave::AtBoundary` / `brand::Leave::EaseOut`.
- **Latch the boundary when you are asked to leave.** Recomputed every frame it is always "the
  next one" and is never reached — the first implementation never left its loop.
- Everything in a loop that appears also disappears inside the loop — streaks are at zero opacity
  at the boundary.
- A loop obeys Rules 1–3 like any other motion: no stretching "breath", no linear drift.
- **A loop that shows flight moves on one axis.** The Arstro mark shakes up and down only, never
  sideways, because the ship flies upward.

## Reduced motion

When the system asks for reduced motion: no travel, no streaks, no loops. Things fade in and fade
out over about 150 ms. A fade is still required — Rule 3 does not switch off. In Artboard this is
free when you animate through the primitives (`arstro.rule.design` §2.6).

## The Arstro mark — the reference timings

| Animation | Length | Shape of it |
|---|---|---|
| Show up | 1000 ms | Closed white star fades in, grows uniformly and turns into place; the cut opens; ship kicks up and settles; trail takes its colour; name slides in. |
| Disappear | 720 ms | Ship dips 3 units (wind-up), then flies up with ease-in. A thrust streak fades in under the trail. Trail, then ship, fade **during** the climb; the ship reaches zero as the move ends. |
| Working loop | 1560 ms | Ship shakes ±1.6 units vertically (520 ms). Thrust streak glows 0 → 90% → 0 (780 ms). Three white streaks pass downward one after another. |

Exact keyframes: the timing table in `docs/brand/arstro-identity.html`.

### The app marks

Every app mark has the same three animations with the same lengths for show up (1000 ms) and
disappear (720 ms), so the suite opens and closes at one tempo. Keyframes, storyboards and timing
tables: `docs/brand/app-logos.html`.

| Mark | Show up | Disappear | Working loop |
|---|---|---|---|
| Cosmo — lens and moon | The ring fades in, growing evenly from 60%; the moon swings 150° along its orbit into place with a light streak behind it. | The moon pulls back 7°, then swings 147° away, fading as it goes; the ring shrinks evenly to 70% and fades with it. | 1600 ms: the moon makes one full orbit (slow, fast, slow) with a light streak that peaks half way; the ring stays still. |
| Interstellar — Warp | The ship flies in from the left and slows to rest; its three streaks slide in after it, one by one. | Ship and streaks pull back 3 units, then the ship jumps right; the streaks follow a shorter way and fade first. | 1200 ms: ship shakes ±1.5 units vertically; each streak drifts back and returns, out of step; two white streaks pass right to left. |
| Solaris — Synth sun | The sun rises 40 units from behind the fixed horizon; the three stripes fade in while settling, top first. | The sun lifts 2 units, then sinks behind the horizon, fading as it sinks; the stripes fade while drifting down, bottom first. | 1600 ms: the sun rises 2 units and settles on an 800 ms beat; a ripple runs down the stripes (each dips 1.5 units and dims in turn). |

A new app's animation tells that mark's own story with the same grammar: the main body arrives
first, the accent parts follow in sequence, and leaving is the arrival's opposite — never its
rewind.

## In the apps — the reference implementation, and how it is checked

`apps/cosmo/widgets/BrandMark.{h,cpp}` is this law in C++, for the three app logos:

- **`poseAt(mark, phase, ms)` is a pure function of time.** No clock, no target, no state. That is
  what makes the law testable: a test stands at a millisecond and measures. Build every new
  animation this way — a tween you can only watch is a tween you cannot check.
- **Rule 1 by construction:** a `Pose` has translations, one rotation and one uniform scale. There
  is no field a stretch could be written in.
- **The easings are the same cubic-beziers as the brand pages**, solved numerically — one curve in
  two places, not two that look alike.
- **`cosmo_widget_tests` measures the law** (`test_brand_marks_obey_the_animation_law`): the exit
  ends with nothing drawn; the body is partly transparent *and* in two different places late in
  the exit; no 8 ms step of opacity exceeds 0.06; the flight starts slowly; the loop is identical
  after one period; the ship never moves sideways. Copy these assertions for a new mark.
- **Then look**: render the poses to a PNG. The tests passed while the splash still cut the logo's
  exit off, because the splash returned early once its own fade was done. Measuring finds the
  curve; only a frame finds the thing that is not drawn at all.

## Checklist — before an animation is done

1. **Stretch:** search the animation for a non-uniform scale or a changing width/height on an
   object. There must be none.
2. **Slope:** name the easing of every moving property. None is `Linear`. Anything that starts or
   ends at visible rest eases at that end.
3. **Exit:** scrub to the last 30% of every exit. The object is still moving and already fading.
   At the final frame it is at zero opacity, not removed at partial opacity.
4. **Pop:** step frame by frame across every state change. No property jumps.
5. **Loop:** the first and last frame of the loop are identical; state changes happen there.
6. **Reduced motion:** it fades, and nothing else moves.
7. **Look at it.** Play it at full speed and at quarter speed. A storyboard of the key poses
   belongs next to the timing table, as in the identity page.

## Updating this rule

These rules are the founder's taste, recorded. Change them only on the founder's word, quote the
new wording here, and update the reference page in the same commit (`arstro.rule` §8).
