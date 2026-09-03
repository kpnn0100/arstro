---
name: arstro.cosmo.design.implement
description: Use to implement or resume ANY front-end work in the cosmo photo editor — widgets, panels, dialogs, screens, layout, theme tokens, motion, hover/press states, the shell chrome, and the touch/phone UI. Carries cosmo's design language inline (palette, radii, fonts, spacing ladder, type ramp, durations) plus the widget catalog and the conventions a new widget must follow, so you can start building immediately. Runs the V-model with both requirement tiers in sync, verifies by rendering real frames headlessly to PNG, records progress in a committed ledger, and commits. Invoke for "add a panel/dialog/screen", "restyle this widget", "the layout breaks when I resize", "this snaps instead of animating", "continue cosmo UI", "/arstro.cosmo.design.implement". NOT for the engine, session, or load path — that is arstro.cosmo.core.implement.
---

# arstro.cosmo.design.implement

> **Invoke `arstro.rule` first.** It carries the rules this skill used to restate: the
> core/front-end split, requirements-first and the conflict rule, the V-model doc-sync loop, the
> agent-drivable surface and its API document, and the ledger/defect/commit conventions. **Follow
> both; where they overlap, this file's checklist is the one to satisfy** — except on the
> architecture, requirement and agent-drivability laws, where `arstro.rule` wins.

The **resume-driven V-model workflow for everything the user can see in cosmo.** All state lives in
committed files, so a session on any machine can pick up exactly where the last one stopped.


**You are the only skill that changes product code for a defect.** `arstro.cosmo.design.debug` never fixes anything: it reproduces, files and *recommends*, and the user then invokes this skill to land it. So when you pick a defect up, the diagnosis, the measurement, the cause with `file:line`, the recommended change and the test that should guard it are already written down in `docs/DEFECTS.md` — **read the entry before re-deriving any of it.** You are free to disagree with the recommendation, and should say so in the commit if you do, but a recommendation you silently ignore usually means the entry knows something you have not read yet.
**You own:** `apps/cosmo/widgets/` · `apps/cosmo/App.{h,cpp}` (screen composition, layout, gesture
routing) · `apps/cosmo/Theme.{h,cpp}` · `apps/cosmo/touch/` · `apps/cosmo/assets/` · the headless UI
harness (`cosmo_shots`, `cosmo_ui_tests`).

**You do not own:** `apps/cosmo/core/`, `core/ImageProcessing/`, `ExportWriter`, `Log`, the load
orchestration. Those are **`arstro.cosmo.core.implement`**. A task spanning both is two commits: core
first, then design.

**You sit on top of `arstro.design.rule`**, the design law for every Arstro front end (the token
contract, the non-negotiable motion rule, R1–R6, the layout and widget conventions, the twenty
gotchas). **Invoke it before this file.** That skill is the law; this one is cosmo's concrete
dialect of it, plus cosmo's process. When they appear to disagree, `arstro.design.rule` wins on
*rules* and this file wins on *cosmo's values*. (It replaces the retired `arstro.design.desktop`.)
A genuinely reusable control belongs in Artboard via **`implement_artboard`**, not in `widgets/`.

---

## THE NON-NEGOTIABLE RULE — nothing changes in one frame

**No property a user can see may change suddenly. Ever. Every visible change is an animation.**

This is not a quality bar to trade against a deadline, and it is not satisfied by animating the
things that obviously move. It is the first thing to check when you write code and the first thing to
check before you commit, and a change that breaks it is not finished, however correct it is otherwise.

**What counts as a visible property** — and the list is deliberately wider than it looks:

- position, size, opacity, colour, radius, rotation, scale
- show / hide — a fade or a size-to-zero tween, **never** a `visible` flip
- scroll and zoom offsets, panel open/close, list insert/remove, grid reflow
- **the coordinate system itself**: a change of UI scale, of logical size, of the transform the whole
  tree is drawn through
- **anything derived from a setting, a window resize, or a value arriving over the socket** — a
  property does not become exempt because a preference changed it rather than a click
- **anything derived from another animated value** — if it is computed from something that eases, it
  must be recomputed every frame from the *eased* value, not once from the target

**How, in cosmo:** `AnimatedProperty` / `Property::animateTo(target, ms, easing, nowMs)` / `Spring`,
driven from `advance(nowMs)`, which **always** chains to `Segment::advance(nowMs)`. Never assign the
visible value directly. A setter with no `nowMs` cannot start a tween — record a pending target and
let `advance()` start it. Default easing `EaseOutCubic`; cosmo's durations are in §1.6.
`reducedMotion()` is free if you animate through the primitives; hand-rolled interpolation must check
it. Deriving a value every frame from an eased source is the pattern, not an optimisation to skip.

**How to actually check it, because reading the code does not catch this.** The failure mode is a
change that is *correct at rest*, so a still frame and a passing test both look fine:

1. Render **mid-transition**, not only at rest (§8). Two frames, half a tween apart, that differ.
2. Where a test can see it, assert the *live* value differs from the *target* mid-tween — the
   `HomeScreen` reflow test does exactly this (`cardLive` vs `cardTarget`), and it is the only kind
   of assertion that can tell an eased implementation from a snapping one.

**Precedent, so this reads as experience and not as decoration.** R-G-1a exists because a grid whose
column count came from the window width re-laid every card in one frame when the width crossed a
threshold. R-SCALE-2a exists because the screen-scale setting — added in the same session that wrote
this rule — changed the whole shell's transform and logical size **instantly**, and it took the user
pointing at it. Both were written by someone who had just read R-G-1 and still shipped a snap,
because both changes felt like "configuration", not like motion. There is no such category.

The requirement is **R-G-1** (with **R-G-1a**, **R-SCALE-2a**) in `apps/cosmo/REQUIREMENTS.md`. It
outranks every preference in this file.

---

## 0. Orient — read these, in this order, every single invocation

| # | File | Why |
|---|---|---|
| 1 | `apps/cosmo/docs/PROGRESS.md` | The ledger. **NEXT** says what to do. |
| 2 | `apps/cosmo/docs/DEFECTS.md` | Open defects — yours may already be filed. |
| 3 | `apps/cosmo/REQUIREMENTS.md` | Intent, `R-<area>-<n>`. **The global design rules are `R-G-1…R-G-3`.** |
| 4 | `apps/cosmo/docs/requirements.md` | As-built, `DR-<AREA>-<n>`, with `file:line`. |
| 5 | `apps/cosmo/Theme.h` | The tokens. Never guess a value that lives here. |
| 6 | `apps/cosmo/docs/detailed_design.md` §4–§8 | Per-widget design: constants, callbacks, behaviour. |
| 7 | `apps/cosmo/docs/touch-ui-brief.md` | Only when touching `touch/PhoneApp`. |
| 8 | The `widgets/` file a similar surface already uses | So the new one matches existing rhythm. |

Reconcile a stale ledger to reality first, and say so.

---

## 1. The design language — cosmo's actual values

Everything is in `namespace arstro::cosmo_v2`. `Theme.h` is **inline free functions, not constants** —
you call `palette::card()`, not `palette::card`.

### 1.1 Palette (`Theme.h`) — one accent, no exceptions

```
background 0x141414   foreground 0xDBDBDB   card 0x1C1C1C     popover 0x222222
primary    0x4F7EF7 <- THE single accent    primaryForeground 0xFFFFFF
secondary  0x252525   secondaryForeground 0xAAAAAA
muted      0x191919   mutedForeground 0x636363
destructive 0xE5534B  success 0x3FB950      input 0x252525    switchBackground 0x444444
border     rgba(255,255,255,0.072)          ring rgba(79,126,247,0.5)
inputLight 0xF4F4F5 / inputLightText 0x18181B   (the inverted entry field)
```

Per-surface literals — use these instead of re-hardcoding a hex:
`canvasBg 0x0A0A0A` (photo stage) · `histogramBg 0x0F0F0F` · `leftRailBg 0x161616` (rail, breadcrumb,
home sidebar) · `filmstripBg 0x121212` · `folderChipBg 0x1A1A1A` · `segmentedBg 0x111111` ·
`curvePlotBg 0x0D0D0D`. Plus `white()`, `whiteAlpha(a)`, `primaryAlpha(a)`, `successAlpha(a)`.

**The canonical hover wash:** `palette::hoverWash(t) = whiteAlpha(0.07 * t)`. Self-drawn regions use it;
child-`Segment` controls use `artboard::hoverBox()` instead. **There is no third mechanism.**

### 1.2 Radii (`radius::`) — three literal values, from Figma

`hairline() = 1.0` (mixer/curve/grade segmented pickers) · `control() = 2.0` (buttons, chips, panels,
filmstrip cells) · `pill() = 9999.0` (slider track/thumb, before/after pill). Not a uniform scale by
design. A widget that invents a fourth radius is a bug.

### 1.3 Fonts (`font::`) — weight is a family name, not a flag

`sans() = "Roboto"` · `sansMedium() = "Roboto Medium"` · `sansSemiBold() = "Roboto SemiBold"` ·
`mono() = "JetBrains Mono"` · `monoMedium() = "JetBrains Mono Medium"`.

**Compiled INTO the binary** (`EmbeddedFonts.{h,cpp}`, `registerEmbeddedFonts()`, R-FONT-1) — no
font file beside the exe, no Fontconfig, no system-installed family, and the same glyphs on Linux
and Windows. Cosmo *used* to register vendored TTFs with Fontconfig from a path baked in at build
time and abandoned it: the path and Fontconfig's family resolution both differ between machines, and
the app's own type is not a thing that may differ between machines. Adding a face means adding the
TTF, its OFL, the `COSMO_FONT_LIST` entry and a `font::` accessor together.

Usage split: `sans` for body/labels · `sansMedium` for tab labels, project name, emphasised buttons ·
`sansSemiBold` for the wordmark, 9px uppercase section headers, "Recent Projects" · **`mono` for
numerics and filenames only** (slider readouts, `(i/n)`, version string).

### 1.4 The spacing ladder — 1 unit = 3.25px

The Figma root is 13px, so one Tailwind unit is 0.25rem = **3.25px**. Every literal sits on this ladder:
`1 = 3.25` · `1.5 = 4.875` · `2 = 6.5` · `2.5 = 8.125` · `3 = 9.75` (the near-universal `kPadX`) ·
`3.5 = 11.375` · `4 = 13.0` · `6 = 19.5`. A number off the ladder needs a reason in a comment.
(`HomeScreen` deliberately uses a coarser 8px scale: `kPad=32, kGap=16, kMinCard=220, kActionH=34`.)

### 1.5 Type ramp (derived from use — there is no named token yet)

`9px SemiBold uppercase, tracking 0.13*px` section headers · `10px` slider label + readout, tab labels,
buttons, pill segments · `11px` default `th.label`, filename, home links · `12px Medium` centred project
name, home action labels · `13px SemiBold` wordmark, "Recent Projects" · `46px SemiBold` home wordmark.

**Baseline convention, universal:** `baselineY = centerY + sizePx * 0.35`.
**Wordmark lock (R-G-2a):** letter-spacing `-0.03 * sizePx`; `"cosmo"` in `foreground()`, `"."` in
`primary()`, the dot placed at `x + estimateTextWidth("cosmo", size)`.

### 1.6 Motion — durations cosmo actually uses

`120ms` hover (framework default `interaction::kHoverMs`) · `160` menu dropdown · `180` **eased scroll
everywhere**, tab page cross-fade, bypass scrim · `190` menu highlight slide · `200` left-rail collapse,
tab indicator slide, filmstrip ring slide · `220` segmented-control highlight slide · `300/460/480/520`
touch press wash and open-transition phases · `900` splash intro.

**Default easing is `Easing::EaseOutCubic`** for nearly everything; `EaseInOutCubic` for the bypass
scrim. Artboard also ships `artboard::motion::kDuration*` and spring omegas — cosmo does not use them
today; if you introduce them, do it as a deliberate, documented sweep, not one widget at a time.

### 1.7 Layout — there is no layout engine

Each container exposes **`void layout()`** (not virtual, not an override) that sets children's
`x/y/width/height` arithmetically, and `App::renderEditor` calls it **every frame** so geometry stays
correct while an animated Property is mid-tween. Therefore `layout()` must be cheap, allocation-free,
and idempotent. Artboard's `Row`/`Column`/`snapTo` exist but cosmo does not use them — follow the local
convention unless you are deliberately migrating.

The desktop shell:

```
TopBar                                        kHeight = 29.25, full width
LeftRail 196 -> 0 (animated) | CenterStage (flex) | RightColumn kWidth = 324
                             |  PhotoCanvas       |  HistogramWidget  94.325
                             |  Breadcrumb  22.75 |  EditStackTabs (tabHeight 27)
                             |  Filmstrip   86    |  ActionBar        39 (pinned)
```

`CenterStage` width = `mW - mLeftRail->width.value() - RightColumn::kWidth` — it reads the **animated**
rail width, so the edit area reflows in step. Screens are `App::Screen{Home, Loading, Editor}`; `Home`
is a standalone Segment outside `mRoot`, `Loading` is immediate-mode and swallows input, `Editor` is the
`mRoot` tree. **Child order == draw order == reverse hit-test order** — modals are added last, and
`MenuStrip` calls `mTopBar->raise()` when it opens.

### 1.8 The touch shell is a separate UI, not a responsive desktop

`touch/PhoneApp.{h,cpp}` (`namespace arstro::cosmo_touch`) drives the **same** `cosmo::EditSession`
through a single-column shell with its own coarser metrics (`kRowH=48, kToolBar=56, kTopBar=48 …`) and
its own **local palette copy** that diverges deliberately (`MUTED = 0x8a8a8a`, brighter for arm's-length
reading). It reuses `cosmo_v2::font`, `sharedTheme().slider` and `cosmo_v2::icon::`. Do not "unify" the
palettes — the divergence is the design. Do keep the two in step on *structure*: a new desktop panel
that has no phone equivalent is a ledger task, not an oversight to leave silent.

---

## 2. The loop (every invocation)

1. **Orient** (§0).
2. **Scope one task**, mark it `[~]` in the ledger.
3. **Requirements first** (§3) — including the design-consistency check *before* any code.
4. **Design docs** (§4) — `detailed_design.md` + `architecture.puml` before the code.
5. **Implement** (§5), tokens only, containers only, animated, responsive, text-fitted.
6. **Make it shell-reachable** (§6): a `cosmo_shots` shot and a `cosmo_ui_tests` assertion.
7. **Log it** (§7): UI events at debug level, so a user's "it looks wrong" becomes a readable trace.
8. **Verify by looking** (§8) — a design change is not done until you have rendered it.
9. **Sync check + Definition of done** (§9, §11).
10. **Ledger + defects, then commit** (§10).

---

## 3. Requirements first — and the conflict rule

Same two tiers as the core skill: `REQUIREMENTS.md` (`R-<area>-<n>`, intent, status in the heading) and
`docs/requirements.md` (`DR-<AREA>-<n>`, as-built, `file:line` anchors, R-tag in the title).

> - **Requirement exists and agrees** → build it, then refresh its `DR-` entry.
> - **Requirement conflicts** with what is asked → resolve it **in the document** first: amend the `R-`
>   entry in place, marked `**AMENDED (…)**` with one line of why. Never ship code that contradicts a
>   written requirement, and never leave two requirements that disagree.
> - **No requirement covers it** → **write it before any code.** A new screen or panel's requirement
>   must state: what it shows, its states (idle / hover / pressed / active / disabled / **empty** /
>   **loading**), how it reflows, and what it does when its content overflows.

**The design-consistency check, before writing code.** Confirm the change will: pull every colour,
radius and font from `Theme` (§1.1–1.3); animate every visible change (R1); sit in a container or a
clearly clipped self-drawn layout (R3); reflow from `mW`/`mH` and measured content (R4); fit its text
(R5); clip **and** scroll anything that can overflow (R6); hover wherever it is interactive; and match
how cosmo already does the equivalent thing. Any conflict with `R-G-1…R-G-3` gets resolved in
`REQUIREMENTS.md` first.

---

## 4. Design docs before code

- `docs/detailed_design.md` §4–§8 — a `### N.M ClassName` subsection per widget with its real constants
  (`kHeight=86`, `kCardW=560`), struct definitions, callback signatures, animated properties and their
  durations, and gesture behaviour. Insert with a letter suffix rather than renumbering.
- `docs/architecture.puml` — every new widget class gets a box, with the R-tag in a trailing comment.
- `docs/architecture.md` — only when the shell structure, the screen state machine, or the Segment tree
  changes; then update the ASCII tree and the module map row too.
- `docs/touch-ui-brief.md` — when the phone UI changes.

---

## 5. Implement — the conventions a new widget must follow

**File layout.** `apps/cosmo/widgets/<Name>.{h,cpp}`, one class per file, filename == class name.
`#pragma once`. Namespace `arstro { namespace cosmo_v2 { … } }` (two levels, un-indented braces).
`using namespace artboard;` at namespace scope in the `.cpp`. Header-only when the thing is stateless
(`SectionHeader.h`, `TextMetrics.h`, `RoundedRectExt.h`) or a tiny self-contained helper (`HoverFade.h`,
`StackPanel.h`, `Starfield.h`). Private tuning constants go in an anonymous namespace at the top of the
`.cpp`; geometry callers need goes in the header as `static constexpr double kFoo`. Artboard comes in as
`#include "../../../core/Artboard/include/artboard/artboard.h"`; include a narrow header only for pure
drawing helpers that must not pull in the framework. **Every file opens with a block comment saying what
it is, which Figma element it maps to, and why the design decision was made** — that is not optional in
this codebase.

**Declaration shape.**

```cpp
class MyWidget : public artboard::Segment
{
public:
    static constexpr double kHeight = 39.0;    // public geometry callers need
    MyWidget();
    std::function<void()> onClick;             // public std::function = the upward seam
    void setValue(double v);                   // setters NEVER fire callbacks
    void layout();                             // app-layer convention: not virtual, not an override
    void advance(double nowMs) override;       // only if you animate
protected:
    void onPaint(artboard::IRenderTarget &t) const override;
    void onOverlay(artboard::IRenderTarget &t) const override;   // only for popups/scrims/rings
    bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
    bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }
private:
    // mCamelCase members
};
```

**Painting.** `onPaint` is `const`, arrives with this segment's world transform installed — draw in
local coords from `(0,0)`, sizing off `width.value()`/`height.value()`. **`onPaint` runs *before*
children**, so anything that must sit above them (selection rings, scrims, dropdowns) goes in
`onOverlay`. Hairline dividers are a 1px stroked path in `palette::border()`. Text is
`t.setFill(c); t.drawText(s, x, centerY + px*0.35, px, font::sans(), tracking)`. Per-corner radii come
from `drawRoundedRectCorners` (Artboard's `drawRoundedRect` is uniform). Cull off-screen rows in
scrollable panels.

**Gestures.** Handle what you consume, `return true`, and **always fall through** to
`Segment::handleGesture(g, local)`. Multi-region widgets must handle `Move` to update hover. Override
`hitTestSelf` for self-drawn widgets; modals override it as `return mOpen && !mClosing;` to capture
everything. Set `inputTransparent = true` on decorative children and `clipToBounds = true` on anything
that scrolls. Null-check every callback.

**Hover and press — the two-mechanism rule.**
- *One clickable Segment* → the framework's animated `hoverAmount()` with `artboard::hoverBox(base,
  emphasis, hv)`; icons use `brighten(base, 0.4*hv)`; press is a plain `bool mPressed` merged as
  `max(hv, mPressed ? 1.0 : 0.0)`.
- *Many regions in one Segment* → `HoverFade`: in `advance()`, `if (!isHovered()) mHover.clear();
  mHover.advance(nowMs);` — on `Move`, `mHover.setHovered(regionAt(local))` — in `onPaint`,
  `palette::hoverWash(mHover.amount(i))`. Give regions flat integer ids via a static `hoverId(kind, i)`
  mapper (see `HomeScreen.h`).
- *Disabled* is free: set `enabled = false` and read `disabledAmount()`, or use `artboard::dimBox`.

**Animation.** Animate `Segment::x/y/width/height/opacity` via `animateTo(target, ms, easing, nowMs)`;
own scalars via `AnimatedProperty` updated in `advance()`; followers via `Spring`. **Prefer animating
`opacity` over flipping `visible`.** The `advance()` contract:

```cpp
void MyWidget::advance(double nowMs)
{
    if (mTarget != mLastTarget) { mScroll.animateTo(mTarget, 180.0, Easing::EaseOutCubic, nowMs); mLastTarget = mTarget; }
    const bool moving = mScroll.isAnimating();
    mScroll.update(nowMs);
    if (moving) layout();          // reposition children at the animated value
    Segment::advance(nowMs);       // ALWAYS chain to the base
}
```

A setter with no `nowMs` cannot start a tween — record a pending target and let `advance()` start it.
`reducedMotion()` is free if you animate through the primitives; hand-rolled interpolation must check it.

**Reuse before you build.** Controls: `SliderRow` (the develop-parameter control), `PillButton`,
`IconButton`, `SegmentedControl`, `HoverFade`, `StackPanel`, `ParamPanel` (any grouped-slider panel).
Helpers: `drawSectionHeader`, `estimateTextWidth`, `drawRoundedRectCorners`, `icon::*` (15 lucide
glyphs), `UnitConversions`, `drawProjectCardChrome`. Artboard: `Button`, `Slider`, `Checkbox`, `TextBox`,
`ComboBox`, `TabView`, `ScrollView`, `ImageView`, `ProgressBar`, `Knob`, `ToggleSwitch`, `LineGraph`.
Every modal shares one skeleton: `AnimatedProperty mAppear{0.0}`, a scrim, `beginClose()`, modal-capture
`hitTestSelf`, click-outside cancel, painted in `onOverlay` — copy `ConfirmDialog`.

**Gotchas that have already cost this codebase time.**
1. `Segment` has **no `removeChild`** — pool children and toggle `visible` (`Filmstrip`, `HomeScreen`).
2. `onPaint` draws *under* your children; use `onOverlay` for anything above them.
3. `estimateTextWidth` is an estimate (`len * px * 0.6`). The real `t.measureText()` exists on
   `IRenderTarget` and is accurate on Cairo — the touch UI already uses it. Prefer it wherever
   mis-centering would be visible; the `TextMetrics.h` comment claiming there is no measurement
   primitive is stale.
4. `layout()` runs every frame — keep it pure arithmetic, no allocation.
5. Four panels once overrode `hitTestSelf` to `return true` and the topmost swallowed every click. Test
   the point, or do not override.
6. Clipping without scrolling is R6's exact failure mode: measure the box you lay out in, define it once,
   clamp both ends, draw the indicator, and let an unscrollable list bubble the wheel.

---

## 6. Shell-reachable UI — no blind spots

**Rule: every screen and every state you build must be renderable and assertable from a shell, with no
display and no clicking.** Genesis already does all of this (`apps/genesis/tests/renderShots.cpp`,
`uiTests.cpp`, and the `GENESIS_APP_NOMAIN` list in its CMakeLists) — copy it.

The one structural blocker: `apps/cosmo/CMakeLists.txt` globs `*.cpp` **including `linux_main.cpp`**, so
no second `main()` can link the cosmo UI. Add `COSMO_APP_NOMAIN` via
`list(REMOVE_ITEM … linux_main.cpp)` first. Then:

- **`cosmo_shots --outdir <DIR> [--only <substr>] [--images A,B] [--check] [--list]`** — builds a real `App`,
  drives it to a named state, renders through `artboard::CairoTarget` into a PNG, writes it, prints
  `wrote <name> (WxH)`. `App` is already platform-free (`App(w,h)`, `setSize`, `render(target, nowMs)`,
  `pointer(...)`, `wheel(...)`, `key(...)`), and `showHome/showEditor/beginOpenTransition/
  setLoadProgress/finishOpenTransition` are all public — every screen and transition phase is reachable.
  Use the settle helper so transitions are deterministic:
  `for (double t = 0; t < ms; t += 16.0) { now += 16.0; app.advance(now); }`.
- **Every shot is a named state, not just the happy path.** Home, editor, **mid-transition**, empty,
  loading, error, modal open, hover, pressed, an over-crowded panel in a small window (to prove the
  scrollbar appears), and **at least two window sizes** (e.g. 1440x900 and 1024x640).
- **`cosmo_ui_tests`** — headless assertions over the *assembled* app, not isolated widgets: nothing
  overlaps a sibling, nothing is clipped at the window edge, every list row is reachable at some scroll
  offset, text fits its box, the layout reflows at a second size. Registered with `add_test`, plain
  `assert()` in the local style. (`cosmo_widget_tests` today only covers isolated hit-test geometry —
  it stays, but it is not enough.)
- **`ui dump [--json] [--root editor|home|splash] [--visible] [--depth N]`** — the single best UI
  debugging tool: the Segment tree as text, each node's class, world rect, visible/enabled/opacity,
  scroll offset and hover state. **It is a `Command` over the control socket, not a flag** — run
  `cosmo --control /tmp/c.sock`, then `cosmo-cc attach /tmp/c.sock` and send it; the reply is framed
  by `[evt] ui.begin` / `[evt] ui.end`. When the user says "the panel is empty", this tells you
  whether the rows are missing, off-screen, or transparent.
  **It needs a live window.** `cosmo-cc run` answers "no view attached", because there is no view.
  **Two real gaps, both worth filing the moment they cost you an investigation:** `cosmo_shots` has
  **no `--size`** and **no `--script`**, so you cannot ask it for an arbitrary window geometry or
  for the state your own click sequence produced; and there is **no headless tree dump** at all. Use
  `cosmo_ui_tests` for geometry questions without a display, add a *named* shot for a new state, and
  reproduce live over the socket for anything else.
- **`--script`** replays scripted input at a fixed 16 ms tick; the grammar is in
  `arstro.cosmo.core.implement` §5.3 and the same file must run headless and live.

---

## 7. Debug mode — make the UI narrate itself

Today `App.cpp` and every widget log **nothing**, so a user's "the slider does nothing" is unanswerable
from the log. That is the single biggest observability gap in the app. Under `--debug` /
`COSMO_LOG_CATEGORIES=ui,input`, the UI must log:

- **input**: every gesture that lands, with type, world point, and the widget that consumed it (and
  loudly, at debug, when nothing consumed it — "click at 320,540 consumed by nobody" solves half of all
  dead-control reports).
- **ui**: every screen transition with phase and ms; every panel/tab/segment change with old and new;
  every selection change with node ids; every modal open/close; every scroll with offset, viewport,
  content, and whether it clamped; every value commit with the param name and the value sent to the
  session.
- **layout**: on `--log-frames` or a `ui dump`, the tree with rects — not every frame by default.
- **motion**: when an animation starts and settles, with target, duration and easing, so "it snapped"
  can be confirmed or refuted from text.

Keep the line shapes stable once written down — the debug skills assert on them.

---

## 8. Verify by looking — mandatory

A design change is not done until you have **seen a rendered frame**. Render through `cosmo_shots` (or,
until it exists, a throwaway Cairo harness compiled from the app sources minus `linux_main.cpp` plus
`CairoTarget.cpp` plus `artboard_core`) and check on the actual PNGs:

- **Layout** — everything in a container, aligned, nothing overlapping a sibling, nothing clipped at the
  window edge.
- **Text fit** — no string past its box; long values ellipsize.
- **Responsive** — at least two window sizes: side panels hold, centre flexes, nothing collapses.
- **Motion** — sample mid-transition *and* at rest: the property is interpolating, and it settles.
- **States** — idle, hover, pressed, active, disabled, **empty**, **loading** — not just the full one.
- **Contrast** — legible on its surface, roughly WCAG AA.

Then build and run `cosmo.exe` on real images to confirm real input and resize behave.

---

## 9. Sync check — if any of these lags, you are not done

- [ ] `REQUIREMENTS.md` — the `R-` entry exists, conflict-checked, heading status current; `R-G-1…R-G-3`
      still hold for what you built.
- [ ] `docs/requirements.md` — the `DR-` entry matches as-built, cites the R-tag, `file:line` anchors live.
- [ ] `docs/detailed_design.md` — the widget's subsection matches its real constants and callbacks.
- [ ] `docs/architecture.puml` — every new widget class has a box.
- [ ] `docs/architecture.md` — shell/tree/module-map updated if the structure changed.
- [ ] `docs/touch-ui-brief.md` — updated if the phone UI changed.
- [ ] `PARITY.md` — row updated if this closed or descoped one.
- [ ] `cosmo_shots` has a shot for every new state; `cosmo_ui_tests` asserts the new layout.

---

## 10. Ledger, defects, commit

Update `apps/cosmo/docs/PROGRESS.md` (tick `[x]` or `[!]` with the reason, rewrite **NEXT**, note the
commit, log any decision) and `apps/cosmo/docs/DEFECTS.md` (close what you fixed, file what you found)
**in the same commit as the work**. Then commit to `main`:

```
cosmo: <lowercase sentence naming the user-visible effect> (R-AREA-n, DR-AREA-n)

<What it looked like, why, what changed, and which shot proves it.>

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
```

Artboard changes are a **submodule** commit plus an umbrella pointer bump — and they go through
`implement_artboard`, not this skill. Do not push unless asked; remind the user at session end.

---

## 11. Definition of done

- [ ] **THE NON-NEGOTIABLE RULE:** nothing a user can see changes in one frame — including the
      coordinate system, anything a setting changed, and anything derived from another eased value.
      Checked by comparing two mid-transition frames, not by reading the code.
- [ ] Requirement read first, written or amended, conflict-checked — **before** the code.
- [ ] **R1 Smooth:** every visible change eased through `Property`/`AnimatedProperty`/`Spring`, honouring
      `reducedMotion()`. No single-frame pop. Tab highlights travel; content swaps cross-fade or slide;
      every tappable gives an animated press response; panels open, close and drag-snap eased.
- [ ] **R2 Responsive to input:** a visible response this frame; heavy work off the UI thread behind an
      eased loading state; the affordance that started it shows it is working.
- [ ] **R3 Contained:** in a container or a clearly clipped self-drawn layout; siblings snap, never stack;
      overlays only in the overlay pass.
- [ ] **R4 Window-responsive:** geometry from `mW`/`mH` and measured content; verified at 2+ sizes.
- [ ] **R5 Text fits:** measured, sized-to-fit or ellipsized; clipping is the backstop, not the plan.
- [ ] **R6 Reachable:** anything that can overflow clips **and** scrolls, clamped both ends, indicator
      shown, wheel and drag agree, an unscrollable list bubbles the wheel.
- [ ] **Tokens only:** one accent, one radius scale, one type ramp; no raw hex or off-ladder px in a widget.
- [ ] **Every state drawn**, including empty and loading; contrast ≈ WCAG AA.
- [ ] Built on Artboard; a genuinely reusable control went to Artboard via `implement_artboard`.
- [ ] **You looked at it** — shots rendered at 2+ sizes, mid-transition and at rest (§8).
- [ ] UI debug logging covers the new surface (§7); `ui dump` over the socket shows it correctly.
- [ ] Docs and puml in sync (§9); `PROGRESS.md` and `DEFECTS.md` updated; committed to `main`.
