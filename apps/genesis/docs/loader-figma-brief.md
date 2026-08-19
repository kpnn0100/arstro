# Brief: an Arstro loading animation

Design a **looping loading indicator**. It will be rebuilt as vector code that animates at
runtime — not exported as video, Lottie, or GIF — so the constraints below are hard limits, not
style preferences. A design that breaks them cannot be built.

---

## 1. The house style to stay inside

The existing loader (which you are evolving, not replacing) is built from these ideas. Keep the
family resemblance:

- **A square canvas, read as a 2×2 grid.** Four quadrant centres at 25% and 75% of the box. Motion
  happens *between* those four anchor points.
- **A stroked capsule as the hero.** A rounded rectangle whose corner radius equals half its height
  — so it is a pill, and at minimum length it is a perfect circle. It **stretches** between two
  quadrant centres and **pivots about one of its own ends** to swing to the next.
- **Outlines that draw themselves in.** The capsule's stroke is progressively revealed and hidden,
  and the revealed span travels around the outline.
- **Filled dots that travel** between quadrant centres, growing from nothing and shrinking away.
- **One large translucent circle** that expands and fades out as a closing accent.
- **Monochrome.** A single accent colour on a dark surface. Thin strokes — roughly 10% of a shape's
  own height. Weight comes from stroke width and opacity, never from a second hue.

Free to change: the number of elements, the order of events, the rhythm, which quadrants are used,
whether the capsule or the dots lead.

---

## 2. Hard constraints — what the runtime can draw

**Only these primitives:**

| Primitive | Notes |
| --- | --- |
| **Rectangle** | with a uniform corner radius (one value, all four corners) |
| **Ellipse** | plus three extras: a **pie wedge** (start/end angle in degrees, 0° = 3 o'clock, growing clockwise) and a **hollow centre** as a fraction of the radius — so a ring, a donut segment or a pac-man is one shape |
| **Path** | straight lines and quadratic/cubic Bézier curves only, then optionally closed |
| **Text** | a string with a size |

Every shape has **one flat fill and/or one flat stroke**. Shapes nest, and a parent's opacity and
transform apply to its children as a group.

**Only these properties animate:**

`x` · `y` · `width` · `height` · `opacity` · `rotation` (about a settable pivot point) ·
`scaleX` · `scaleY` · `pivotX` · `pivotY` · `strokeWidth` · `cornerRadius` ·
pie `startAngle` / `endAngle` / `innerRadius` · outline `trimStart` / `trimEnd` / `trimOffset` ·
`fontSize` · `letterSpacing`

**Do NOT use — none of these exist:**

- ❌ **Colour transitions.** Fill and stroke colours are fixed. Fade with **opacity** instead.
- ❌ Gradients of any kind (linear, radial, angular, mesh)
- ❌ Blur, glow, drop shadow, inner shadow, noise, grain
- ❌ Masks, clipping groups, boolean operations (union/subtract/intersect)
- ❌ Blend modes (multiply, screen, overlay…)
- ❌ Dashed or dotted strokes; custom caps/joins
- ❌ Images, image fills, textures
- ❌ Variable stroke width along a path; tapered strokes
- ❌ Motion blur, particles, 3-D, perspective

**Two things that replace the usual tricks:**

- To reveal a stroke progressively, use **outline trim** (`trimStart` / `trimEnd` from 0 to 1), and
  **`trimOffset`** to slide the revealed span around the outline — it wraps, so an arc can cross the
  shape's start point. This is how a stroke "draws itself in".
- To cut a wedge out of a circle, use the **pie angles**, not a mask.

---

## 3. It must be responsive

The loader ships at any size from **24 px to 240 px square**, and must be legible at both ends.

- Design on a **200 × 200** artboard, but **express every dimension and position as a percentage of
  the box** (e.g. "capsule height = 15% of the box", "dot centre at 25%, 75%"), not as fixed pixels.
- Give stroke widths as a percentage too.
- Nothing may depend on a specific pixel size, a specific font, or hairline detail that vanishes
  below 32 px.

---

## 4. It must loop seamlessly

- It runs **until the work finishes** — unknown duration. So the cycle must repeat with **no visible
  seam**: the last frame of the loop has to hand over to the first frame of the loop with matching
  position *and* matching direction of travel.
- Structure it as: an optional short **intro** that plays once, then a **repeating cycle**.
- Also design a short **exit** (how it leaves when loading completes).
- Target cycle length: **1.2 – 2.5 s**. (The current one is 9 s, which is far too slow — please make
  it noticeably quicker.)
- Nothing should ever fully stop mid-cycle and then start again — motion should carry through.

---

## 5. What to deliver

Not a video. I need to rebuild it property-by-property, so:

**a) A layer list.** Every shape, named, in draw order, with its parent, its primitive type, its
fill/stroke, and its resting geometry as % of the box.

**b) A keyframe storyboard.** One frame per significant moment of the cycle — 8 to 12 frames — laid
out left to right, each labelled with its time in ms from the cycle start. The first and last frames
must be identical (that is the loop closing).

**c) A timing table.** This is the most important part. One row per animated property:

| Layer | Property | From | To | Start (ms) | Duration (ms) | Easing |
| --- | --- | --- | --- | --- | --- | --- |
| capsule | width | 15% | 65% | 0 | 400 | ease-in-out |
| capsule | rotation | 0° | −90° | 400 | 500 | ease-in-out |

Easing may be: `linear`, `ease-in` / `ease-out` / `ease-in-out` (in quad, cubic, quart, sine, expo,
back, elastic or bounce flavours), or a custom curve — for a custom curve, say what **speed** the
property should be moving at when it **enters** and **leaves** the segment, and whether it should
match the speed of the segment before or after it. Motion that continues in the same direction
across two consecutive segments should say so; that is what stops it hitting a dead stop.

**d) State which rows belong to the intro, the repeating cycle, and the exit.**

**e) Rotation pivots.** For anything that rotates, say **where the pivot is** (e.g. "the centre of
the capsule's left cap"), not just the angle. Rotating about an end rather than the centre is the
whole character of this loader.

---

## 6. Judge it against these

1. Does it read as **one object moving**, or several unrelated things blinking?
2. Does it still read at **24 px**?
3. Does the cycle **close invisibly**?
4. Does it feel **calm and confident** — the thing you are happy to watch for 30 seconds — rather
   than busy or frantic?
5. Is every effect achievable with **flat fills, strokes, trims, pie angles and transforms alone**?
