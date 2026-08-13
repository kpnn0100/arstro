# Genesis

**An animation designer for Arstro Artboard whose deliverable is source code.**

Pick an abstract control to inherit from, draw the component, bind its geometry to
expressions, wire animations to the base class's events — Genesis writes a `.h`/`.cpp` pair
that compiles against `artboard` and nothing else.

```
genesis                       # the editor
genesis samples/RingLoader.genesis
genesis-cc --new VisualLoop CoolVisualLoop
genesis-cc CoolVisualLoop.genesis --verify
```

## The one bet: Genesis ships no runtime

Nothing in the generated file mentions Genesis. No library, no registry, no init call, no
JSON parsed at startup. A component is a normal class you can read, review as a diff, and
hand-edit — and it works in every Artboard adapter (Cairo, Canvas2D/WASM, the headless test
target) because it *is* Artboard code.

## Five nouns, and no sixth

1. **Component** — a name, an authorable **base**, a design size, and **params**.
2. **Shape** — rect / circle / path / label, nested into a tree.
3. **Binding** — *every numeric and colour field is an expression, not a number.* Type `40`
   and it is one; type `min(w, h) * 0.5` and it still is. This is where responsiveness comes
   from.
4. **Signal** — something the base tells you happened: `loopStart`, `pressDown`,
   `valueChanged`, `checkedChanged`.
5. **Reaction** — *when* `<signal>`, *animate* `<field>` *to* `<expr>` over `<ms>` with
   `<easing>` — chained with steps, so "then" is literally the previous step's completion.

Reactions belong to the **object**: select one and the panel shows its reactions and nothing
else, so a track just names a field (`opacity`). A signal is still a component-level event, so
several objects can react to the same one and all of them run. Need to reach a sibling? Qualify
the target — `halo.opacity`.

There is no global timeline. A UI component is not a movie; it is a set of responses to
events that interrupt each other. The event graph is the source of truth, and the scrubber
is per reaction.

## Opening it

Genesis opens on a launcher: your recent components first, then a card per base. Picking
either crosses into the editor. A launch splash plays while startup work happens behind it,
naming each step — the first thing on screen is never a blank frame.

Passing a file on the command line skips straight to the editor.

## The bases

| Base | For | Signals |
| --- | --- | --- |
| `VisualLoop` | spinners, busy pulses, loading screens | `loopStart` `cycle` `loopEnd` |
| `ProgressIndicator` | bars, rings, meters | `valueChanged` `complete` `indeterminate` `determinate` |
| `Button` | press/click controls | `pressDown` `release` `cancel` `clicked` |
| `Slider` | ranged controls | `dragStart` `valueChanged` `dragEnd` |
| `Checkbox` | toggles | `checkedChanged` |

Every base also has `attach`, `resize`, `hoverEnter`, `hoverExit`, `focusGained`,
`focusLost`, and publishes read-only values as `base.*` (`base.phase`, `base.display`,
`base.hover`, `base.checked`, `base.norm`, …). `genesis-cc --bases` prints the full contract.

## Gene, the binding language

One type family (`double` and `Color`), no statements, no loops, no user functions.

```
minSide * 0.7                 # responsive geometry
(w - self.w) / 2              # centre me in my parent
w * base.display              # a progress bar IS the base's smoothed value
mix(surface, accent, base.hover * 0.5)     # hover cross-fade, as a formula
0.25 + 0.75 * max(0, sin((base.phase - spread) * TAU))    # a pulse, with no reaction at all
turns(1)                      # one full rotation, in radians
raw{ myHelper(w) }            # verbatim C++ when you need the escape hatch
```

Names: `w` `h` `minSide` `maxSide` `aspect` `PI` `TAU`, any param, `self.<field>`,
`<shape>.<field>`, `base.<read>`, `theme.<role>`.
Functions: `min max abs clamp lerp floor ceil round sign sqrt pow mod sin cos tan atan2 exp
log deg rad turns pct rgb rgba fade mix`.

Division by zero is `0`, not NaN — a preview never silently draws nothing, and the emitted
C++ reproduces that exactly.

## Sector: a circle can be a pie, a ring, or a pac-man

Circles carry `arcStart` and `arcEnd` **in degrees** (0 = right, growing clockwise) and
`arcInner`, a hollow centre as a fraction of the radius. The shape kept is the wedge between
the two rays from the centre:

```
arcStart =  30,  arcEnd = 330               # a pac-man: a 60-degree mouth at 0
arcStart =   0,  arcEnd =  90               # a quarter pie
arcInner = 0.76                             # hollow: a ring segment instead of a pie
arcEnd   = -90 + 360 * base.display         # a ring gauge that follows the value
```

`samples/PacmanLoader.genesis` chomps by animating the two angles against each other;
`samples/DonutGauge.genesis` is a ring gauge.

This is a different tool from trim, and the difference matters: **arc** chooses which part of
the *disk* the shape is — a fillable region cut by rays from the centre. **Trim** chooses how
much of that shape's *outline* is drawn. They compose: a trimmed pie is a pie whose edge
draws itself in.

## Trim: any shape can become a partial one

Every rect, circle, and path has `trimStart`, `trimEnd`, and `trimOffset` — in fractions of
the outline's length, and animatable like anything else.

```
trimEnd    = sweep / 360      # a circle drawn to N degrees IS an arc
trimEnd    animate 0 -> 1     # the shape draws itself in
trimOffset animate 0 -> 1     # the trimmed span travels around the outline, wrapping
```

`samples/ArcSpinner.genesis` is the classic arc spinner built from it: the arc's length
breathes while the whole ring spins, so it reads as one object rather than two. (That one is
a stroked circle whose outline is partly drawn — for a *filled* wedge, use the sector fields
above.)

## Verify: the preview is the code

The editor interprets; the export compiles. Two implementations of one semantics drift, and
a design tool that lies is worthless. So **Verify** emits the code, compiles it, drives both
the compiled class and the interpreter through the same signal script, renders both into an
`artboard::RecordingTarget`, and diffs the op streams:

```
$ genesis-cc samples/TrackSlider.genesis --verify
verified: 9 frames, 423 ops, preview == compiled
```

Without a C++ toolchain it reports **unavailable** — never a pass.

## genesis-cc

```
genesis-cc <file.genesis> [-o <dir>] [--verify] [--check] [--print] [--stale]
genesis-cc --new <Base> <Name> [-o <dir>]
genesis-cc --bases
```

`--stale` exits non-zero when the generated files on disk differ from what would be emitted,
so a build can assert its checked-in sources are current. Emission is deterministic: the same
document always produces byte-identical output.

## Samples

`samples/` holds one verified component per base, plus `PulseDots` — a loop authored with
**no reactions at all**, purely as a binding on `base.phase`, to show that the animation can
be a formula.

## Keyboard

`Ctrl+N` new · `Ctrl+O` open · `Ctrl+S` save · `Ctrl+E` export · `Ctrl+R` verify ·
`Ctrl+Z` undo · `Ctrl+Shift+Z` / `Ctrl+Y` redo · `Esc` closes a dialog.

Expression fields are real text fields: click to place the caret, drag or shift-click to
select, double-click to take a word, `Ctrl+←/→` to move by words, `Ctrl+A` select all,
`Ctrl+C/X/V` copy/cut/paste against the system clipboard, `Ctrl+Backspace`/`Ctrl+Delete` to
remove a word.

## Editing

- **Inspector** — the component's name/namespace/design size, the selected shape's id (rename
  carries every reference with it) and fields, a label's text, a path's commands, the params
  (add by clicking `number` / `color` / `text`, remove with the ×), and a **Problems** list of
  every validator diagnostic.
- **Reactions** — per track: `target`, `from` (blank = wherever it is now), `to`, `ms`,
  `delay`, `easing`, a repeat chip that cycles ×1 → ×2 → ×3 → ∞, a yoyo toggle, and a delete.
  The scrubber replays the selected reaction to any point in its own timeline.
- **Duplicate** (or `Ctrl+D`) copies an object and its children as `<id>_copy` — with its
  reactions, so the copy animates itself rather than sharing the original's motion.
- Every edit is undoable; rapid typing collapses into one undo step.

## Building

Genesis builds with the Arstro umbrella:

```
cmake -S . -B build && cmake --build build -j
./build/genesis/genesis
```

`genesis_core`, `genesis-cc` and the tests need only `artboard_core`. The editor additionally
needs GTK3 + Cairo + Fontconfig, and self-skips if they are absent.

## Docs

- [docs/requirements.md](docs/requirements.md) — the contract (G-1…G-14, the R-G design rules)
- [docs/architecture.md](docs/architecture.md) — the shape of the system and why
- [docs/detailed_design.md](docs/detailed_design.md) — how each piece works
- [docs/brief.md](docs/brief.md) — the original product brief
