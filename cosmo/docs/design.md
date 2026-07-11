# cosmo — Design Intent

This is the short "why" behind the structure. The "what" is in
[`requirements.md`](requirements.md); the "how" is in [`architecture.md`](architecture.md) and
[`detailed_design.md`](detailed_design.md).

## Why four layers
The editing logic is worth more than any one GUI. Keeping the group tree, history, presets, and
persistence in a UI-free `cosmo_core` (no Artboard types) and pixels in a headless engine means
the same brain can be driven by this GTK app, by a headless test, or by a future front end. The
app layer stays thin: presentation + wiring. The host layer is the only place OS code is allowed,
so the rest of the app is portable to any Artboard adapter.

## Why params-in / pixels-out
An edit is data (`EditParams`), not a mutation of pixels. That one decision buys non-destructive
editing, trivial undo (swap params), presets (copy a subset of fields), before/after (render with
and without), and full-res export (re-render) — all for free, with no special-casing.

## Why the render service is a worker thread
A photo edit at preview resolution can take tens of milliseconds; a project open decodes many
images. Neither may block a 60 fps UI. So rendering runs on a `RenderService` worker (requests
coalesced to the latest), decode runs on a `LoadJob` worker, and the UI only ever *polls* finished
results. The cost is eventual-consistency (a frame lands a tick later); the payoff is a UI that
never janks. The trade-off is deliberate: correctness of the animation loop over immediacy of a
single edit.

## Why the open/return is a three-phase transition
Opening a project is the one unavoidably slow moment. Rather than freeze, the transition hides the
latency inside motion: an **intro** that is pure animation (no I/O — the decode is deferred to
`onLoadingReady` precisely so part 1 can't hitch), a **loading** beat that shows real progress, and
a **reveal** that dissolves the loading screen as the editor materializes on the *same* dark
backdrop with a single cross-faded wordmark, so nothing flashes or doubles. The return mirrors it
so leaving a project is equally smooth, and the launcher is rebuilt behind the shown loading screen
rather than as a click-time freeze.

## Why everything animates
Inherited from Artboard's taste rules: a single-frame jump reads as a glitch. Every visible
property change goes through an eased `AnimatedProperty`, and `App::render` re-runs layout +
advance every frame so animated geometry reflows continuously. Reduced-motion collapses each
animation to its end state, so accessibility is a switch, not a rewrite.

## Why bezier control points for the mixer curve
The mixer is edited as a smooth curve with draggable tangent handles, but the engine consumes a
LUT. Persisting the *sampled* LUT loses the handles (reopening gives coarse corner points). So the
model stores the **control points** and a single shared `curve::sample()` flattens them for both
the on-screen drawing and the engine LUT — the drawn curve and the render can never diverge, and a
reopened project restores the exact editable curve.

## Why history lives in the project file
Undo history is part of an image's working state, not a scratch buffer. Persisting the full
branching tree (per image, in the `.cmp`) means closing and reopening a project restores not just
the current look but the ability to walk back through — and branch off — every prior step.

## Why two hover mechanisms
Child-`Segment` controls (buttons, pills) get the framework's per-Segment `hoverAmount()`.
Multi-region self-drawn widgets (menus, breadcrumb, filmstrip cells, tabs, tree rows, cards) paint
many clickable regions in one Segment, so they can't use per-Segment hover; they track a hovered
region id and drive it through the shared `HoverFade` helper, which cross-fades (old region fades
out as the new fades in) rather than snapping the highlight between siblings.

## Consistency locks
One accent, one radius scale, one type ramp, one wordmark renderer — all from `Theme`. A widget
that invents its own blue or radius is a bug. The wordmark in particular is drawn by one function
(`App::drawWordmark`) everywhere it appears, so the home, top-bar, and transition copies are
pixel-identical and can cross-fade into one another.
