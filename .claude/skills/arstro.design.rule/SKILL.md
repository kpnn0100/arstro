---
name: arstro.design.rule
description: The design law for every Arstro front end — cosmo, pulsar, genesis, arstrobench, the launcher shell, and the coming interstellar and solaris UIs. Carries the token contract, the non-negotiable motion rule, the six R-rules (smooth, responsive, contained, window-responsive, text fits, reachable), the layout and widget conventions, the twenty gotchas already paid for, the every-state checklist, and how to verify by looking. Invoked FIRST by every design skill; supersedes arstro.design.desktop. Invoke directly for "add a panel/screen/dialog", "restyle this", "make it consistent with cosmo", "audit this app's design", "why does this snap", or when starting a UI for an app that has none.
---

# arstro.design.rule

**The law for every pixel an Arstro app draws.** Invoked first by every design skill, so one family
of apps looks and behaves like one family — and so a rule about motion or overflow lives in one file
rather than four.

> **Invoke `arstro.rule` first**, then this, then the app's own design skill. `arstro.rule` owns the
> process (requirements, V-model, docs, ledger, commit); this file owns the pixels. **Follow all
> three; where this file and an app's skill overlap, this one wins on RULES and the app's wins on
> its own VALUES** — an app may choose its accent; it may not choose whether things animate.

**This supersedes `arstro.design.desktop`**, whose R1–R6 are hoisted here intact. Where an older
skill still says "arstro.design.desktop is the law", read it as this file.

**Reference implementation: `apps/cosmo`.** Where this file states a value without qualification it
is cosmo's, and it is the family default.

## Who this binds, and what each owes

| app | today | first thing to fix |
|---|---|---|
| **cosmo** | the reference — tokens, motion, layout, widget catalog | keep it the reference; adopt genesis's `motion::` and text measurement |
| **arstrobench** | **aliases** cosmo's token namespaces and compiles `cosmo/Theme.cpp` | nothing — this is the model |
| **genesis** | best on motion tokens and text measurement; **worst on tokens and fonts** — a forked palette copy, a different font family, Fontconfig | alias cosmo's namespaces; embed the fonts |
| **cosmo/touch** | metric divergence **authorised** by R-TOUCH; but re-declares every colour locally | use the tokens it already includes |
| **launcher** shell | its own language by design (a phone shell) | the motion and state rules still bind it |
| **pulsar** | pre-system: own theme factory, no fonts named, fixed 1180×800 | name the fonts; adopt the tokens |
| **interstellar · solaris** | no UI yet; briefs already say *"nothing ever snaps"* and use cosmo's palette | start here, not from a blank Theme.h |

---

## 1. THE NON-NEGOTIABLE RULE — nothing changes in one frame

**No property a user can see may change suddenly. Ever. Every visible change is an animation.**

Not a quality bar to trade against a deadline, and not satisfied by animating the things that
obviously move. It is the first thing to check when you write code and the last before you commit.

**What counts as a visible property — deliberately wider than it looks:**

- position, size, opacity, colour, radius, rotation, scale;
- show / hide — a fade or a size-to-zero tween, **never** a `visible` flip;
- scroll and zoom offsets, panel open/close, list insert/remove, grid reflow;
- **the coordinate system itself** — UI scale, logical size, the transform the tree is drawn through;
- **anything derived from a setting, a window resize, or a value arriving over a socket.** A
  property does not become exempt because a preference changed it rather than a click;
- **anything derived from another animated value** — recompute it every frame from the *eased*
  value, never once from the target.

**The one exemption: direct manipulation.** A drag follows the pointer exactly, with no easing —
*the pointer is the animation*. But a value **derived** from that drag still eases.

### How

`AnimatedProperty` (`animateTo(target, ms, easing, nowMs)` then `update(nowMs)`), or `Property` —
`Segment::x/y/width/height/opacity/rotation/scale*` are all `Property`, and `Segment::advance()`
updates every one, so animating a child's geometry is `child->width.animateTo(...)` with no extra
state. `Spring` for a follower. `HoverFade` for many regions in one Segment.

**`set()` snaps and cancels**, and the *first* placement of anything must `set` rather than
animate — there is nowhere to travel from.

### A setter has no clock

Only `advance(nowMs)` has one. A setter records **intent**; the next `advance` starts the tween.
The three idioms, all in cosmo:

- boolean: `mXWanted` / `mXApplied`, compared in `advance`;
- scalar: `mScrollTarget` / `mScrollLastTarget`;
- a setter that genuinely must animate (a modal's `show()`): cache the frame clock as `mLastMs` in
  `advance` and use it.

**Do not gate `advance()` on `isOpen()`.** `isOpen()` is already false during the closing fade, so
gating freezes the close mid-fade — and `show()` then has no clock to start from.

### How to actually check it — reading the code does not catch this

The failure mode is a change that is *correct at rest*, so a still frame and a passing test both
look fine.

1. **Render mid-transition**, not only at rest. Two frames, half a tween apart, that differ.
2. **Assert the LIVE value differs from the TARGET mid-tween.** The only assertion that can tell an
   eased implementation from a snapping one. Expose the live value for the test — a private eased
   value is an unverifiable one.
3. **Never settle and then look.** Settling 400 ms is long enough for a 180 ms fade to *finish*, so
   the assertion reads 1.0 and passes a snap. Pump one frame at a time; keep the **first non-zero**.

**Precedent, so this reads as experience and not decoration.** A grid whose column count came from
the window width re-laid every card in one frame. The UI-scale setting changed the whole shell's
transform and logical size instantly — *the largest visible change in the app, in one frame* —
written in the same session that wrote this rule. A progress bar took a service's five uneven stage
fractions raw and read as three stalls. Each was written by someone who had just read the rule,
because each felt like "configuration" or "just data". **There is no such category, and all three
*read* correctly** — which is why compliance is established by comparing two frames, never by
reading.

---

## 2. The token contract — one structure, per-app values

**Every app pulls colour, radius, font and spacing from a token namespace. No raw hex and no
off-ladder pixel value in a widget. Ever.**

An app may choose its own *values*. It may not choose its own *structure*, and it must not **copy**
the file: `arstrobench` **aliases** cosmo's namespaces and compiles `cosmo/Theme.cpp` — that costs
nothing, because `Theme` depends only on `artboard_core`. `genesis` forked it and has already
drifted. **A forked token file is a divergence with a delay fuse.**

**Tokens are `inline` functions, never constants.** A `const Color` at namespace scope is a
static-init-order hazard across translation units and cannot be `constexpr`. A function is one
instruction after inlining and is safe to call from any static initializer.

### 2.1 Colour — one accent

```
background 0x141414   foreground 0xDBDBDB   card 0x1C1C1C     popover 0x222222
primary    0x4F7EF7 ← THE single accent     primaryForeground 0xFFFFFF
secondary  0x252525   secondaryForeground 0xAAAAAA
muted      0x191919   mutedForeground 0x636363
destructive 0xE5534B  success 0x3FB950      input 0x252525    switchBackground 0x444444
border     rgba(255,255,255,0.072)   ← an alpha, not a grey
ring       rgba(79,126,247,0.5)
inputLight 0xF4F4F5 / inputLightText 0x18181B   ← a deliberate light island
```

**Per-surface literals**, so no widget re-hardcodes a hex: `canvasBg 0x0A0A0A` ·
`histogramBg 0x0F0F0F` · `leftRailBg 0x161616` · `filmstripBg 0x121212` · `folderChipBg 0x1A1A1A` ·
`segmentedBg 0x111111` · `curvePlotBg 0x0D0D0D`. Plus `white()`, `whiteAlpha(a)`,
`primaryAlpha(a)`, `successAlpha(a)`.

**One interaction token:** `hoverWash(t) = whiteAlpha(0.07 * t)` for self-drawn regions;
`artboard::hoverBox()` for child-Segment controls. **There is no third mechanism.**

**"One accent" is a rule about ambiguity, not a vow of monochrome.** `destructive` and `success` are
semantic and are not accents. And **pulsar's chromatic system is correct**, not a violation: a synth
has three simultaneous modulation sources to tell apart, so it assigns meaning-only hues — signal
cyan for the audio path, amber for envelopes, violet for LFOs, rose for macros, over a neutral
chassis. A photo editor has one selection and needs one accent. **Domain adaptation is allowed;
decoration is not. Write down which one you are doing.**

**A colour used by more than one widget belongs in the token file.** Cosmo currently fails this: the
stacked-reach green `0x4CB573` and the channel colours `0xE65252/0x61CC6B/0x6B94F5` are cited by the
requirements as system tokens and live as scattered literals. That is a defect, not a style.

### 2.2 Radius — named values, not a scale

`hairline() = 1.0` (segmented pickers) · `control() = 2.0` (buttons, chips, panels, cards, cells) ·
`pill() = 9999.0` (slider track and thumb, toggles, scroll thumbs). Deliberately not a uniform ramp.
Genesis adds `panel() = 4.0`; pulsar uses 12 for its hardware chassis. **Add a NAMED rung if you
need one — a bare number in a widget is the bug.**

### 2.3 Type — weight is a family name, and the fonts are in the binary

```
font::sans()         "Roboto"
font::sansMedium()   "Roboto Medium"
font::sansSemiBold() "Roboto SemiBold"
font::mono()         "JetBrains Mono"
font::monoMedium()   "JetBrains Mono Medium"
```

**Weight is a family name, never a number or an enum** — a static weight is a distinct font file,
and the Cairo weight enum has only two values. The whole text API is
`drawText(text, x, y, sizePx, fontFamily, letterSpacingPx)`.

**Fonts are compiled INTO the binary.** Cosmo used to register vendored TTFs with Fontconfig from a
path baked in at build time, and abandoned it: the path and Fontconfig's family resolution are both
things that differ between machines, and *the app's own type is not a thing that may differ between
machines*. **Genesis still loads from `exeDir()/assets/fonts` via Fontconfig and treats missing
fonts as non-fatal — meaning it silently falls back to a generic sans. That is the mistake cosmo
already documented; do not copy it.**

**The ramp** (cosmo's, measured across every `drawText`):

| px | weight | role |
|---|---|---|
| 7–8 | Medium / SemiBold | filmstrip counts, badges |
| **9** | **SemiBold, tracked `+0.13 × px`** | **section headers**, rail headers, uppercase micro-labels |
| 9 | Regular | filmstrip cell name; version (mono) |
| **10** | Regular sans + **mono for the number** | **the workhorse row** — slider label and its readout, tab labels, action-bar labels |
| 11 | Regular / Medium / mono | breadcrumb, card name, filename (mono), settings rows |
| 12 | Regular / Medium | dialog body and buttons, project name, history labels |
| 13 | SemiBold / Medium | wordmark, dialog section titles, loading status |
| 14 | SemiBold | **modal title — the largest thing in the editor** |
| 46 | SemiBold | home wordmark |

**`mono` is for numerics and filenames only.** A number in a proportional face jitters as it
changes; that is the entire reason the mono family is vendored.

**Tracking has two directions and both are formulas, never per-site tweaks:** uppercase micro-labels
track **out** (`+0.13 × px` for section headers, `+0.12` histogram, `+0.06` settings rows); the
wordmark tracks **in** (`−0.03 × px`).

**Baseline, universal:** `baselineY = centerY + sizePx * 0.35`. (Genesis uses `0.36` — a silent
1%-of-size fork. One must win; it is `0.35`.)

### 2.4 Spacing — a ladder, and an off-ladder number needs a comment

Cosmo's base unit is **3.25 px** (a 13 px root, 0.25rem). The rungs in use:
`1.5 = 4.875 · 2 = 6.5 · 2.5 = 8.125 · 3 = 9.75` **(the dominant panel padding)** `· 3.5 = 11.375
· 4 = 13 · 6 = 19.5 · 7 = 22.75 · 9 = 29.25`.

**Modals and launchers deliberately use a coarser 8/16/32 rhythm.** Dense editor chrome is on the
ladder; a dialog is not. An app may adopt a different base — the touch shell and the solaris brief
both specify an 8 px grid, because a finger is not a mouse. **What is not optional is that there IS
a ladder, and that a value off it carries a comment saying why.**

Genesis names it as tokens (`metrics::gap()`, `metrics::pad()`) — better than cosmo's scattered
literals, and the direction to move in.

### 2.5 Motion — the durations, and the tokens you should prefer

**Artboard already ships a named motion scale** (`MotionTokens.h`: short/medium/long durations,
spring omegas) and five named easings. **Cosmo ignores them and uses hand-picked literals; genesis
uses them. Genesis is the newer and better-behaved answer — new work uses the named tokens.** The
table below is what cosmo actually does, and it is what a value must match if you hand-pick one.

| ms | for |
|---|---|
| **120** | hover in and out (the framework default), modal **close** |
| 150 | modal **open** |
| 160 | photo cross-dissolve (**`Linear`** — a dissolve must be a linear alpha ramp), menu grow |
| **180** | **the canonical default** — eased scroll everywhere, cross-fade, scrim, reference lines |
| 190–200 | menu highlight, rail width, tab indicator, selection ring, progress ease |
| **220** | segmented highlight; **a value catching up to a source it does not control** |
| 260 | UI-scale zoom, home-grid reflow, shell cross-fade |
| 300–520 | screen transitions; **520 is the longest tween in the app** (the open reveal) |

**Default easing `EaseOutCubic`.** `EaseInOutCubic` for a scrim. `Linear` **only** for a
cross-dissolve. Touch closes with `EaseInCubic` — slower, asymmetric, deliberate.

**Asymmetric durations are legitimate and sometimes required.** A progress bar fades in at 120 and
out at 300 so its fill finishes while it disappears: the last thing a user should see of a completed
operation is a full bar, not one vanishing at 70%.

**Do not confuse a hold with a duration.** A 950 ms "complete" hold, a 260 ms minimum visible time,
a 1400 ms pulse period — these are timings, not tweens, and they do not ease.

### 2.6 Reduced motion — honoured at the primitive, and unreachable in every app

`reducedMotion()` is honoured inside `animateTo`, `Spring::advance` and `HoverFade`, so **it is free
if you animate through the primitives** — and any hand-rolled per-frame ramp must branch on it
itself.

**The gap: no shipping app ever calls `setReducedMotion`.** The path is real and tested; the switch
is unreachable by a user. **An app must read the OS or its own setting once at startup and call
it** — an accessibility feature nobody can turn on is not a feature.

---

## 3. The six rules

### R1 — Smooth
Every visible change eases through the primitives, honouring `reducedMotion()`. Tab highlights
travel; content swaps cross-fade or slide; every tappable gives an animated press response; panels
open, close and drag-snap eased. **§1 is the full statement; this is its name.**

### R2 — Responsive to input
**A visible response this frame.** Heavy work goes off the UI thread behind an eased loading state,
and the affordance that started it shows it is working — a button that started a job is disabled,
with the work reported beside it, not left looking pressable to no effect.

### R3 — Contained
Every component lives in a container or a clearly clipped self-drawn layout. **Siblings snap, never
stack.** Overlays are drawn in the overlay pass, not by nudging a sibling.

### R4 — Window-responsive
Geometry comes from the current width and height and from *measured* content — never a hardcoded
window size. **Verified at two sizes**, one small. "It fits on my monitor" is not a layout claim.
**A column gives way before the content does:** derive a rail's effective state every layout from
"wanted AND there is room", so it folds itself when the canvas would drop below its floor and
unfolds when the room returns — through the *same* eased property the toggle drives.

### R5 — Text fits
Measured, sized-to-fit, or ellipsized against the space *actually left*. Clipping is the backstop,
not the plan.

**Use the real primitive.** `IRenderTarget::measureText` exists and Cairo answers with real metrics.
Cosmo's `estimateTextWidth` (`len × px × 0.6`) is **font-independent by construction and therefore
wrong for any actual font** — it detached the wordmark's accent dot the moment the typeface changed.
Its header still claims the HAL has no measurement primitive; that comment is stale.

**The pattern to standardise on is genesis's:** the host publishes the live render target once per
frame, a `textWidth()` helper uses real metrics when there is one and the estimate otherwise, and an
`ellipsize()` guarantees nothing spills. **And measurement is only valid during a render** —
measuring from an input handler uses a destroyed context. Defer it.

### R6 — Reachable
Anything that can overflow **clips *and* scrolls**: clamped both ends, indicator shown, wheel and
drag agreeing, an unscrollable list bubbling the wheel to its parent. **A clip with no scroll is the
bug** — it silently deletes content, and *a panel the user can see is cut off but cannot reach is
worse than one that never showed the content at all.*

Four properties, all required:
- measure viewport **and** content height **every layout**, so a resize or a font change cannot
  leave a stale limit;
- **measure the same rectangle the rows are laid out in** — one top and one bottom, shared by the
  measure, the row placement, the visibility test, the paint clip and the scrollbar. Two copies
  drift, and the symptom is a last row that is unreachable at any offset;
- hide the widgets of scrolled-out rows so they neither draw nor take input;
- draw the bar only when there is something to scroll — a permanent track claims there is more when
  there is not.

**A wheel notch is not one pixel.** Scale it per target (a filmstrip notch is one cell; a panel notch
is ~10 px). Taking the raw delta as pixels reads as broken.

---

## 4. Layout

### The engine exists; cosmo does not use it

Artboard ships `Row`, `Column`, `LinearLayout` and `ScrollView`. **Pulsar uses them. Cosmo uses none
of them** — every container hand-writes `void layout()`. Genesis also hand-writes, and rolls its own
list scroll.

**Neither choice is wrong, but the choice must be deliberate and written down.** `Row`/`Column` are
right for a fixed-size instrument panel; hand-written arithmetic is right where a container reads
several animated siblings to place one flexible child. What is *not* acceptable is a new container
re-deriving the same arithmetic because nobody said which way this app goes.

### The `layout()` contract

Not virtual, not an override. Called **every frame**, after `advance()` and before `render()`, so
geometry stays correct while an animated property is mid-tween.

1. **A pure function of current animated values → child geometry.** Read `width.value()`, siblings'
   `.value()`, the eased scroll offset — **the live value, never the target.** A panel whose width
   eases must have everything beside it laid out from the live width, or the seam tears.
2. **Idempotent and cheap.** It runs 60×/s. No allocation.
3. **It must not start an animation.** `set()`, never `animateTo()` — and `set` is safe here only
   because it early-returns on an unchanged value.
4. **It must not rebuild children.** They are built once in the constructor; `Segment` has no
   `removeChild`.
5. **It may — and must — set `child->visible` to cull**, because clipping does not cull hit-testing.
6. **Fixed columns first, the flexible one last**, with `std::max(0.0, remainder)`.

### Resize, and the logical coordinate space

A logical unit space, with the scale divided out at exactly **two** points — the size, and the input
entry points — so every widget below that line hit-tests in the same space it laid itself out in.

**State a minimum and derive it, don't guess it:** the canvas's floor plus the fixed columns gives
the window minimum, and the host applies it. **Reflow animates** (R4 + §1): per-item animated
geometry eased over ~260 ms, with the chrome and the content drawn from the *same* eased values.

### Scrolling — the five co-located parts

`clipToBounds = true` in the ctor · `scrollBy` moves only a plain target, clamped · `advance` eases
toward it and re-`layout()`s **only while moving** · `layout()` positions from `-scroll.value()` and
sets `visible` per row · paint skips off-screen content with the same test.

Overlay content clips itself by hand (`save/clipRect/restore`) — see G1.

---

## 5. Widget conventions

**One class per file, filename == class name. Every file opens with a block comment saying what it
is, which design it maps to, and why the decision was made.** Not optional — it is where the next
person learns why the obvious alternative was rejected.

```cpp
class MyWidget : public artboard::Segment
{
public:
    static constexpr double kHeight = 39.0;    // its own fixed metrics
    MyWidget();                                // builds children ONCE
    void setValue(double v);                   // programmatic — NEVER fires the callback
    artboard::Rect thumbRect() const;          // geometry a test or shot must aim at
    void layout();                             // not virtual, not an override
    std::function<void(double)> onChange;      // public field — the upward seam
protected:
    void onPaint(artboard::IRenderTarget &t) const override;
    void onOverlay(artboard::IRenderTarget &t) const override;   // only to escape a clip
    bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
    bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }
    void advance(double nowMs) override;       // only if it owns motion
};
```

- **A setter never fires its own callback**, or a programmatic refresh from the model becomes an
  edit and the two push each other in a loop. Where both are wanted, provide `setX` (animates,
  fires) and `setXImmediate` (snaps, silent).
- **Callbacks are public `std::function`s named `onVerb`** — the only upward seam. Never a listener
  interface, never a virtual. An unwired one is silent, not an error: `if (onX) onX(...)`.
- **A callback that must report success returns `bool`**, so a two-hop channel cannot drop silently.
- **Style is caller-supplied, not hard-coded**, so one button class serves menu items, chips and
  pickers.
- **Publish the geometry a test or a shot must aim at.** A test that hard-codes where it thinks a
  control is keeps passing after the control moves.
- **Reuse before you build**, and **a genuinely reusable control belongs in Artboard** (via
  `implement_artboard`, with docs and tests) — not grown inside one app where others cannot share it.

### Hover — two mechanisms, and when each

- **One clickable Segment** → the framework's animated `hoverAmount()` with `hoverBox()`; icons
  brighten; press is a plain bool merged as `max(hover, pressed)`.
- **Many regions in one Segment** → `HoverFade`, which ramps the hovered id toward 1 and **every
  other toward 0 simultaneously**, so the pointer *cross-fades* between regions instead of jumping.
  **Clear it when the widget loses hover**, or a region stays lit.
- **There is no third mechanism.** Disabled is free: set `enabled = false` and read the animated
  disabled amount.

### Gestures

- Return `true` **only** if you consumed it; otherwise fall through to the base.
- `Down` **captures**; `Move`/`Drag`/`Up` go to the capture — so a drag that leaves the widget stays
  with it. **`Click`/`DoubleClick`/`RightClick` are hit-tested fresh**, so a widget that wants a
  click must be hittable at the *release* point. `Scroll` **bubbles**.
- **A drag must never teleport.** On `Down` store `grab = anchor − pointer`; on `Drag` add it back.
- **A gesture in flight outranks the model.** Any `set*()` that re-seeds from the model must
  early-return while a drag is live — the model re-seeds every few tens of ms and will otherwise
  flatten the very node being dragged.
- **A modal swallows keys** — one that lets them through is a modal in name only. Escape cancels,
  Enter confirms, and **the destructive action is deliberately unbound**.
- **Decorative children set `inputTransparent = true`.**

### Paint — `onPaint` vs `onOverlay`

`onPaint` runs **before children** and is **not** covered by `clipToBounds`. `onOverlay` is a
second, full-tree, unclipped pass.

- **`onPaint`** = your own background and chrome, which sit *under* your children.
- **`onOverlay`** = anything that must sit on top of your children or escape a clip — popups,
  dropdowns, scrims, selection rings, every modal. `raise()` to become last among siblings.
- **Opacity is the show/hide mechanism, never `visible`** — and `opacity <= 1e-3` also stops
  hit-testing, which is what makes it safe.
- **Fade a modal by multiplying every colour** through a local helper; a scrim, a card and text are
  separate draw ops and a single opacity will not cover them.
- **Close the modal before running the action** — copy the callback out, `beginClose()`, then invoke.
  An action may open another modal, and two dialogs each believing they own the input is the bug.

---

## 6. The gotchas already paid for

Each cost real time once. Cheap to obey, invisible until they bite.

1. **`clipToBounds` clips the CHILD SUBTREE ONLY** — `onPaint` runs before the clip is installed,
   deliberately, so a widget can draw a ring or shadow outside its box. **A clipped container that
   paints must clip itself.** And clipping never culls hit-testing, so a scrolled-out row must be
   culled twice: skipped in paint *and* `visible = false` in layout.
2. **Overriding `hitTestSelf` to `return true` swallows the whole app.** Four panels once did, and
   the topmost ate every click. Test the point, or do not override. A real modal may capture — as
   `mOpen && !mClosing`, never a bare `true`.
3. **An inactive overlay must be click-through**, not merely invisible.
4. **`Segment` has no `removeChild`.** Pool children and toggle visibility. Rebuild only on a
   *structure* change — and then restore focus and caret, or a text field loses focus every
   keystroke.
5. **`onPaint` draws under your children** — see §5.
6. **`layout()` runs every frame**: no allocation, no side effects, no `animateTo`.
7. **`estimateTextWidth` is font-independent and therefore wrong.** Use `measureText`; measure only
   during a render.
8. **A forgiving pick radius must not become a teleport** — keep the grab offset.
9. **A gesture in flight outranks the model.**
10. **A zoom is not a content change** — successive frames of a geometric transition must not
    cross-dissolve; set both views instead.
11. **`opacity <= 1e-3` disables hit-testing.** A faded control is not merely invisible.
12. **A rounded rect degenerates below `2 × radius`** — a progress fill shorter than its bar height
    reads as a dot that pops. Draw nothing: that is the honest picture of "not started".
13. **A section header is a ROW** — label, then a rule that takes what is left, then the trailing
    control's **box** (not its glyph). One constant read by both the layout and the paint. Two
    numbers drift, and the symptom is a hairline drawn through an icon.
14. **Announce before you mutate.** Subscribers run synchronously, and a view is entitled to clear
    itself when it hears "a project is opening". Emitting after the mutation cost 18 decoded images
    attaching to nothing.
15. **Two copies of one fact always drift.** Derive the second from the first — a viewport's top and
    bottom, a seam's position, a group's tick state.
16. **A dropdown must be placed against the root, capped and scrolled**, or it opens downward at
    full height and runs off the window.
17. **A column's contents fit inside the column** — size off `width − 2·pad`, not `width`.
18. **Mutually recursive geometry overflows the stack** — if a card and its chips ask each other how
    big they are, split out the axis that does not depend on the other.
19. **A UI-scale change is a visible change and must ease** — and every geometric read must go
    through the eased value, re-derived per frame. Deriving once zooms the transform and leaves the
    layout behind.
20. **On touch, a drawn handle is covered by the fingertip.** Select the *nearest* handle rather
    than requiring a hit, so a miss adjusts something instead of nothing.

---

## 7. Every state, including the two nobody draws

A surface is not finished when the full, happy, populated version looks right. Draw **and shoot**:

`idle · hover · pressed · active · disabled · **empty** · **loading** · error/refused`

**Empty and loading are the two that get skipped, and they are the two that make an app feel
broken.** A list empty because nothing matched and a list empty because it has not loaded look
identical. A mask that covers nothing because nobody ran the detection looks exactly like one that
ran and found nothing. **Those need different sentences, because they ask the user for different
things.** Say which it is, in words, next to the control.

---

## 8. Verify by looking — mandatory

**A design change is not done until you have seen a rendered frame.** Render headlessly to PNG and
check the actual image:

- **Layout** — everything contained, aligned, nothing overlapping a sibling or clipped at the edge.
- **Text fit** — nothing past its box; long values ellipsize.
- **Responsive** — at least two window sizes; side panels hold, the centre flexes.
- **Motion** — **mid-transition and at rest** (§1).
- **States** — every one in §7.
- **Contrast** — legible, roughly WCAG AA.

Then run the real app on real data — the only thing that catches a wiring mistake between host and
service.

**The three instruments, in this order:** the **UI tree dump** (which node is at fault — a shot only
tells you *that* it is wrong), **headless PNG shots**, and **`RecordingTarget`** to assert the actual
draw-op stream when the question is geometric rather than visual.

**Add a named shot for every new state.** Shots are product code, not scaffolding: a state with no
shot is a state nobody will look at again.

---

## 9. The consistency audit

When asked to make an app consistent, check in this order and report findings as **defects with
`file:line` and the rule broken**, not as impressions. An audit that produces a paragraph of
opinions produces nothing.

1. **Tokens** — does it *alias* the token header, or has it forked one? Grep widgets for raw hex and
   for off-ladder pixel values.
2. **Accents** — more than one, excluding `destructive`/`success`? Is it a domain adaptation with a
   written reason, or decoration?
3. **Radii** — any value that is not a named rung.
4. **Fonts** — are the families named at every `drawText`? Are they embedded, or resolved at runtime?
5. **Durations** — any number not in §2.5 without a comment. Does the app use the framework's named
   motion tokens?
6. **Motion** — for each visible property that changes, is there an `animateTo`? Is the live value
   readable by a test? Grep for direct assignment to `visible`.
7. **Overflow** — every scrollable surface clamped both ends, wheel bubbling, one viewport rect.
8. **Text** — anything centred or right-aligned using the estimate where it should measure.
9. **Reduced motion** — does anything call `setReducedMotion`?
10. **Doc drift** — do the durations in the design docs match the code? (Cosmo's currently do not:
    the doc says one figure and easing for the photo dissolve, the code says another.)

---

## 10. Definition of done

- [ ] **Nothing a user can see changes in one frame** — including the coordinate system, anything a
      setting changed, and anything derived from another eased value. Checked by comparing two
      frames half a tween apart, **not** by reading the code.
- [ ] Tokens only: one accent, named radii, the type ramp, the spacing ladder. No raw hex.
- [ ] Fonts named at every draw, and embedded rather than resolved at runtime.
- [ ] R1–R6 hold, and R6 was checked by actually scrolling to the end.
- [ ] Every state drawn, **empty and loading included**, each with a shot.
- [ ] Verified at **two window sizes**, **mid-transition and at rest** — you looked at the PNGs.
- [ ] A live eased value is exposed wherever a test must tell a tween from a snap.
- [ ] A reusable control went to Artboard rather than growing inside one app.
- [ ] Requirement, docs and ledger in sync, and committed — `arstro.rule` §3, §6, §7.
