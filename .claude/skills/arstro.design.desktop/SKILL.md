---
name: arstro.design.desktop
description: Use when building, restyling, reviewing, or fixing the UI of ANY Arstro DESKTOP app — the ones built on the Artboard framework and rendered natively (Cairo): cosmo, pulsar, studio, synth, ui-demo, and future desktop apps. Enforces ONE consistent desktop design language across all of them (each app may pick its own palette/colours, but the design-token structure, motion, layout, hover, responsiveness, and typography rules are IDENTICAL), using cosmo as the reference implementation. Invoke for tasks like "add a panel/dialog/screen", "restyle this widget", "make X responsive to the window", "add a loading state", "audit the app's design vs the system", "the transition snaps / text overflows / this looks inconsistent with cosmo". NOT for the Artboard library itself (use implement_artboard) or non-UI app logic.
---

# arstro.design.desktop

The single design system for **Arstro desktop apps**. Every app (cosmo, pulsar, studio,
synth, …) is a different product with its **own palette**, but they must feel like **one family**:
same motion, same layout discipline, same hover language, same typography rhythm, same
responsiveness. **cosmo is the reference implementation** — when unsure how something should look
or behave, match how cosmo does it.

This skill governs the **app layer** (an app's `widgets/`, `App.cpp`, `Theme.h`, `REQUIREMENTS.md`).
It sits on top of, and never replaces, `implement_artboard`: the reusable controls, the animation
primitives, and the render/input HAL live in **Artboard** and are changed with `implement_artboard`.
Apps **compose** those pieces under the rules below.

**Where things live.** The umbrella splits into `arstro/core/` (the libraries — `core/Artboard/`,
`core/DigitalSignalProcessing/`, `core/ImageProcessing/`) and `arstro/apps/` (the applications —
`apps/cosmo`, `apps/genesis`, `apps/pulsar`, `apps/launcher`, plus spec-stage `apps/solaris` and
`apps/interstellar`); the small demos stay at `arstro/examples/` (`examples/studio`,
`examples/synth`, `examples/ui_demo`, `examples/scope`, `examples/piano`). So the apps this skill
governs are `apps/cosmo`, `apps/pulsar`, `apps/genesis`, `apps/launcher/android-shell` and the
`examples/*` demos — and an app includes Artboard via a relative path into `core/Artboard/`
(e.g. from `apps/cosmo/widgets/`: `../../../core/Artboard/include/artboard/artboard.h`).

## 0. Orient (what "the Arstro desktop look" is)

- **Built on Artboard.** A desktop app builds a tree of `artboard::Segment`s (controls) + layout
  containers and renders them through the native `CairoTarget` adapter. It never rasterizes itself
  and never calls the OS toolkit for drawing — it emits Artboard primitives. Input arrives as
  platform-neutral gestures (the host feeds `RawPointer`/keys; the `GestureRecognizer` +
  `Segment` tree route them).
- **One dark, near-black, single-accent language.** Dark neutral surfaces, **ONE** accent colour
  used only for interactive/selected state, a restrained neutral grey ramp otherwise, small literal
  corner radii, and a single type ramp (a UI sans + a numeric mono). cosmo's is a blue accent on
  `#141414`; another app may be a warm accent on its own near-black — **the colours may differ, the
  structure may not.**
- **Reference files to read before you start** (in the app you're touching):
  - `Theme.h` — the app's design tokens (`palette::`, `radius::`, `font::`). Read cosmo's
    (`apps/cosmo/Theme.h`) as the canonical shape.
  - `REQUIREMENTS.md` — the app's requirements, including its **global design rules** (cosmo calls
    them `R-G-1…R-G-3`; see §2). Read them first, every time — they are the app's contract.
  - the `widgets/` a similar screen already uses, so a new one matches existing rhythm.

## 1. The design-token contract (consistency lock across apps)

Every app declares the **same token namespaces** in its `Theme.h`, with the **same semantic roles**.
This is what makes different-coloured apps still read as one system. Copy cosmo's structure:

- **`palette::`** — semantic colour roles, not raw hex at call sites:
  `background`, `foreground`, `card`, `popover`, `primary` (**the single accent**),
  `primaryForeground`, `secondary`/`secondaryForeground`, `muted`/`mutedForeground`,
  `destructive`, `border`, `input`, `ring`, plus `whiteAlpha(a)` / `primaryAlpha(a)` and a
  canonical **`hoverWash(t)`** (see R-G-3). Per-surface literals (a stage bg, a rail bg) are also
  named here so no widget re-hardcodes a hex.
- **`radius::`** — a small, named scale (`hairline`, `control`, `pill`). **ONE** radius scale per
  app; a widget that invents its own radius is a bug.
- **`font::`** — one sans ramp (`sans`, `sansMedium`, `sansSemiBold`) + one mono for numerics /
  filenames. Weights are distinct family names (Artboard FR-22), registered by the host at startup.

**Rules:** ONE accent, ONE radius scale, ONE type ramp per app. **Never** write a hex or a magic
size in a widget — cite a `palette::`/`radius::`/`font::` token (or add one). A control that invents
its own blue, radius, or font weight breaks the family.

## 2. The non-negotiable rules

These are the standing rules for every desktop app. The first five are the product requirements;
the rest are the transferable taste from `implement_artboard` §2A, adapted to the app layer. cosmo
already encodes the spirit of these as `R-G-1…R-G-3` in `apps/cosmo/REQUIREMENTS.md` — mirror that rule
block into any app that lacks it.

### R1 — Every transition is smooth; nothing snaps (ease-in-out)
No component may suddenly change a property that affects the UI — **position, size, opacity/show-hide
(fade, never a `visible` flip), colour, corner radius, scroll/zoom offset, panel open/close, list
insert/remove** — in a single frame. Every such change is driven through an Artboard animation
primitive and **eased**, never linear:
- `Property` / `AnimatedProperty` for one-shot transitions (use `Easing::EaseInOut*` /
  `EaseOut*` — short, ≈120–220 ms).
- `Spring` (critically damped, framerate-independent) for followers that glide toward a moving
  target (a value display, a scroll offset, a highlight that tracks the pointer).
- Honor `artboard::reducedMotion()`: it collapses all motion to the final state instantly (the
  primitives already do this — don't re-introduce motion that ignores it).
- **Motion must be motivated** (hierarchy / feedback / state-transition / reveal) and **actually
  driven every frame** (the host must call `root->advance(nowMs)` per frame). If you spec a
  transition, prove it moves across frames; never ship a half-built tween that snaps. If you can't
  drive it in the available scope, ship the clean static end-state instead.
- Data is not motion: a counter or a committed value (e.g. a mask's numeric params) updates as
  data; only its *rendered* position/size/colour eases.

**R1a — the transitions that MUST animate (no exceptions):**
- **Tab / segmented-control selection** — the highlight must *travel*, never teleport. Either the
  highlight box / underline **slides** to the newly-selected item (animate its x/width via
  `Property`/`Spring`), or the selected item **cross-fades** from the old highlight colour to the
  new. Repainting the active pill/underline at the new spot in one frame is a bug.
- **Tab / section content swap** — when the active tab or section changes, the incoming content
  **slides in** (short horizontal translate, ≈160–220 ms, eased) and/or cross-fades; never an
  instant content replace.
- **Every button / tappable** — gives an immediate **animated press response** on press-down (a
  background wash that fades, or a subtle scale), not merely a static pressed colour; a released or
  cancelled press eases back.
- **Panels / sheets / drawers** — open, close, and **drag-snap** through an eased tween, and a
  fixed bar (e.g. a bottom tab/nav bar) **stays put while the attached sheet slides behind it** —
  the chrome does not ride up and down with the sheet.

### R2 — Every action responds immediately; if it can't, show loading
A click/keypress must produce a visible response **this frame** (press feedback, selection, the
panel starting to open). If the real work can't finish in-frame (decode, file I/O, export, a long
compute):
- **Never freeze the UI thread.** Do the heavy work off the UI thread and apply results
  incrementally on the UI thread (cosmo's `R-LOADING` decodes on a background `decodeWorker` and the
  UI polls finished results per frame).
- **Show a loading state immediately**: a determinate **progress bar** when you know the fraction
  (feed real progress, ease the bar), an indeterminate **spinner/shimmer** when you don't, or a
  **skeleton** of the coming layout. The loading UI itself obeys R1 (fades in, never pops) and R5
  (its text fits). Keep a short minimum visible time so it never just flashes.
- The affordance that started the action shows it's working (disabled/active state) — never dead.

### R3 — Every component lives in a container (Row / Column / ScrollView)
No free-floating, hand-placed widget. Compose with layout containers so spacing, alignment, and
auto-sizing are consistent and survive resize:
- `artboard::Row` / `Column` position their visible children along one axis with `spacing` +
  `padding` and **auto-size to content** — use them for toolbars, forms, lists, control stacks.
- Content that can exceed its viewport goes in a **`ScrollView`** (clipped viewport + draggable
  thumb), not an unbounded overflowing column.
- **Snap, don't stack** (implement_artboard §2): siblings align edge-to-edge / into a shared grid;
  overlap is a bug unless it's a deliberate overlay (modal / dropdown / tooltip / drag-ghost drawn
  in the overlay pass). Give each item its own slot; derive each position from the previous item's
  extent or a grid, never from a value two items can share.
- A self-drawn multi-region widget (a menu, a grid of cards) still owns a clear internal layout
  and clips its content to its bounds.

### R4 — The UI is responsive to window/screen size (best effort: no hardcoded sizes)
The app must look right at any window size and reflow on resize (`App::setSize` → `layout()` each
change). Derive geometry from the current window `mW`/`mH` and from measured content — **not** from
baked-in pixel constants:
- Flex the primary content; let it absorb the slack (cosmo fixes the side panels and flexes the
  centre stage: `centre.width = mW − leftRail − rightColumn`). Fixed chrome (a top bar height, a
  rail width) is acceptable **only** where the design is genuinely fixed — and it comes from a named
  constant, not a literal sprinkled around.
- Prefer containers' auto-size and fractions/`min`/`max` over absolute sizes. A grid uses
  `auto-fill minmax(min, 1fr)`-style column math (see cosmo's home grid), not a fixed column count.
- Clamp to sane minimums so nothing collapses or overflows the window at small sizes.
- **Verify at ≥2 window sizes** (see §4). A layout that only works at one size fails R4.

### R5 — Text never overflows its container; it fits
Every string is laid out to fit the box it's in:
- **Measure** with `estimateTextWidth(text, sizePx)` (`widgets/TextMetrics.h`) before you place or
  size text.
- Either **size the container to the text** (auto-size row/label) **or**, when the box is fixed,
  **truncate with an ellipsis** ("Long name…") computed from the measured width, and/or reduce the
  size within the type ramp — never let a string run past its container edge or into a neighbour.
- Clip text to its container as a backstop (`clipToBounds` / the container's clip), but clipping is
  the safety net, not the plan — a mid-glyph clip with no ellipsis is a bug.
- Reserve room for the longest realistic value (counts, sizes, dates, filenames), and right-size
  dynamic labels each frame.

### Adapted taste rules (from implement_artboard §2A / Artboard FR-24)
- **Everything interactive hovers** (R-G-3 / Artboard FR-24). Every button and every clickable
  region shows an **animated** hover treatment under the pointer, never a hard flip. Child-`Segment`
  controls key off the framework's animated `hoverAmount()` via `artboard::hoverBox()`; self-drawn
  multi-region widgets wash the hovered region with `palette::hoverWash(amount)`. Eased (R1),
  collapses under `reducedMotion()`, identical treatment app-wide (consistency lock).
- **Draw every state, not just the happy path**: idle / hover / pressed / active / disabled, plus
  **empty** and **loading** where a panel can have no data yet. A control created `visible=true`
  before its first data push piles up at (0,0) — default children hidden/positioned for the empty
  state.
- **Contrast** (aim WCAG AA: ≈4.5:1 body, ≈3:1 large/against a fill). No low-contrast label-on-fill;
  add a scrim or pick a legible pair.
- **Deferred action outlasts the gesture that cancels it** (implement_artboard §2A): if a control
  defers an action so a later gesture can pre-empt it (click-to-jump vs double-click-reset), the
  guard must be `>=` the double-click window (default 300 ms) and the next press cancels the pending
  action.
- **Anti-slop**: align to a grid, respect whitespace, keep labels terse and real, don't stack
  decorative dividers/dots or duplicate the same affordance twice.
- **Figma/spec is the truth** where one exists (R-G-2): match spacing/type/colour/radii pulled from
  `Theme`, don't approximate.

## 3. Build on Artboard — don't reinvent it

Reach for the framework before hand-rolling:
- Controls: `Button`, `Slider`, `Knob`, `ComboBox`, `TabView`, `ScrollView`, `TextBox`,
  `ToggleSwitch`, `Checkbox`, `ProgressBar`, `LineGraph`, `ImageView`, and `Row`/`Column`.
- Motion + interaction: `AnimatedProperty`/`Property`/`Spring`, `reducedMotion()`, `Easing`,
  `Segment::hoverAmount()`/`isHovered()`/`isHoverWithin()`, `snapTo()`, the overlay pass
  (`renderOverlay`/`onOverlay`) for popups/dropdowns/tooltips, `Observable<T>` for one-source-of-
  truth state so two views can't drift.
- Paint helpers: `drawRoundedRect`, `applyPaint`, `artboard::hoverBox`/`lerpColor`/`brighten`,
  gradients, `drawText` (with the themed family + tracking, FR-22).

If a genuinely reusable control or primitive is **missing**, add it to **Artboard** via
`implement_artboard` (with its docs + 100%-covered tests), then compose it here — do **not** grow a
one-off control inside the app that other apps can't share.

## 4. The loop (how to change an app's UI)

1. **Read the app's `REQUIREMENTS.md` first** (its global R-G rules + the feature's requirement). If
   the app has no global design-rule block, add one mirroring cosmo's `R-G-1…R-G-3` before coding,
   and state the new/changed screen's contract (what it shows, its states, how it reflows).
2. **Design-consistency check** — before writing code, confirm the change will: pull colours/radii/
   type from `Theme` tokens (§1); animate every visible change (R1); keep every element in a
   container (R3); reflow with the window (R4); fit its text (R5); hover where interactive; and
   **match how cosmo does the equivalent thing.** Resolve any conflict with an existing rule in
   `REQUIREMENTS.md` first.
3. **Implement** on Artboard (§3), tokens only (§1), containers only (§3), animated (R1),
   responsive (R4), text-fitted (R5).
4. **Verify it renders and reflows** (§5) — this is mandatory; a design change is not done until you
   have seen it.
5. **Definition of done** (§6) — if any box is unchecked, it's not done.
6. **Commit** the app change (each app is committed in its own repo/dir per that repo's convention;
   an Artboard change committed separately via `implement_artboard`). Keep the app's
   `REQUIREMENTS.md` in sync with what shipped.

## 5. Verify it — you must see it

Apps render through Cairo, so you can **headless-render a real frame to a PNG** and inspect it (no
display needed): create a `cairo_image_surface` + `cairo_t`, wrap it in an `artboard::CairoTarget`,
build the app / screen, drive it to the state under test (feed pointer gestures, call `advance()`
across frames to settle animations, push fake data/progress), then `cairo_surface_write_to_png`.
Compile the app's sources (minus its `main`/host entry) + `CairoTarget.cpp` + the Artboard core.

Check, on the rendered frame(s):
- **Layout**: everything in a container, aligned, nothing overlapping a sibling, nothing clipped at
  the window edge.
- **Text fit**: no string runs past its box; long values ellipsize.
- **Responsive**: render at **≥2 window sizes** (e.g. 1280×800 and 1024×640) — the layout reflows,
  side panels hold, centre flexes, nothing collapses.
- **Motion**: sample an animated frame mid-transition and at rest — the property is interpolating,
  not jumping; and it settles.
- **States**: render idle / hover / loading / empty variants, not just the full happy path.
- **Contrast**: text/icons legible on their surface.

Also build the app natively (its `build.sh --project <app> --target linux-native-app`) and, when
feasible, run it, to confirm real input + resize behave.

## 6. Definition of done — the checklist

- [ ] The app's `REQUIREMENTS.md` was read first and states the screen's contract + states; a global
      design-rule block (R-G-1…R-G-3 equivalent) exists.
- [ ] **R1 Smooth:** every visible property change (position/size/show-hide-via-fade/colour/radius/
      scroll-zoom/open-close/list insert-remove) goes through `Property`/`AnimatedProperty`/`Spring`,
      is **eased** (not linear), and collapses under `reducedMotion()`. No single-frame pop/jump.
- [ ] **R2 Responsive-to-input:** every action responds this frame; heavy work runs off the UI
      thread with an immediate, eased **loading** state (progress / spinner / skeleton) and never
      freezes.
- [ ] **R3 Contained:** every component sits in a `Row`/`Column`/`ScrollView` (or a self-drawn
      widget with a clear clipped internal layout); siblings snap, don't stack; overlays only in the
      overlay pass.
- [ ] **R4 Window-responsive:** geometry derives from `mW`/`mH` + measured content; primary content
      flexes; no stray hardcoded sizes (fixed chrome comes from named constants); verified at ≥2
      window sizes.
- [ ] **R5 Text fits:** every string measured (`estimateTextWidth`) and sized-to-fit or ellipsized;
      no overflow; clipping is only the backstop.
- [ ] **Hover:** every interactive element has an animated hover treatment (`hoverAmount()` /
      `hoverBox()` / `hoverWash()`), eased, reduced-motion-safe.
- [ ] **Tokens:** all colours/radii/fonts come from `palette::`/`radius::`/`font::` — ONE accent,
      ONE radius scale, ONE type ramp; no raw hex/px in widgets.
- [ ] **Every state drawn** (idle/hover/pressed/active/disabled + empty + loading), not just the
      happy path; contrast ≈ WCAG AA.
- [ ] **Built on Artboard** — reused its controls/primitives; any missing reusable piece was added
      to Artboard via `implement_artboard`, not one-off in the app.
- [ ] **Consistent with cosmo** — the equivalent pattern matches the reference app's behaviour/rhythm
      (different colours are fine; different structure/motion/spacing rhythm is not).
- [ ] **Seen it:** headless render(s) at ≥2 sizes + relevant states inspected; app builds natively.
- [ ] Branding stays `arstro`/the app's name; the app change is committed and `REQUIREMENTS.md` is
      in sync.
