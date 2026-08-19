# Arstrobench — Architecture

Arstrobench is a thin GUI over two measured workloads. The split that matters is the same one
cosmo makes: **a UI-free measurement core that knows nothing about drawing, and an Artboard UI
that knows nothing about how a benchmark is timed.**

```
                    ┌──────────────────────────────────────────┐
  native_main.cpp   │ GTK3 window + Cairo surface + Fontconfig  │  host (platform)
  (Linux, MSYS2)    └───────────────────┬──────────────────────┘
                                        │ render(target, nowMs) / pointer(...)
                    ┌───────────────────▼──────────────────────┐
                    │ BenchApp            (artboard::Segment)   │  UI (platform-free)
                    │  ├ RunButton                              │
                    │  ├ GpuToggle       (ToggleSwitch)         │
                    │  ├ ScoreCard × 2   (ProgressIndicator)    │
                    │  ├ TotalCard                              │
                    │  └ SystemPanel                            │
                    └───────────────────┬──────────────────────┘
                                        │ polls (no blocking)
                    ┌───────────────────▼──────────────────────┐
                    │ BenchmarkRunner     (worker std::thread)  │  core (UI-free)
                    │  ├ ImageWorkload ── arstro::EditEngine    │
                    │  ├ DspWorkload   ── MixChain (DspChain.h) │
                    │  └ SystemInfo                             │
                    └──────────────────────────────────────────┘
```

## Layers

**`core/` — UI-free, no Artboard, no GTK.** Everything here compiles and runs headless and is
what the unit tests drive directly.

- `Workload.h` — `WorkloadResult { seconds, score, checksum, detail, ok }` and the one place
  `score = 1 / seconds` is computed (R-SCORE-1). Both workloads return this type, so the UI has a
  single shape to render and no knowledge of which engine produced it.
- `ImageWorkload` — generates the dummy image (R-IMG-1), builds the all-stages-engaged
  `EditParams` (R-IMG-2), and times `EditEngine::renderImage()` (R-IMG-3).
- `DspWorkload` — builds the 9-voice bank and renders **one buffer per voice** (untimed), then
  times the mix over 192000 samples per channel (R-DSP-1/2/3).
- `DspChain.h` — the measured mix, as its own file because it is the substance of the signal
  score rather than a detail of the workload that drives it: `VoiceStrip` (per voice),
  `MultibandCompressor` (4 bands), `ParametricEq` (6 sections), `ReverbSection` (early + tail),
  and `MixChain` which owns them and the pre-allocated scratch. `Cascade<Filter>` is the one
  shared piece — N one-pole sections in series, since the library ships 6 dB/octave filters and
  a crossover needs a steeper slope than that to be worth the name. Shelves and peaks are built
  the way an EQ builds them (filter a copy, sum it back at a gain), so a band can boost as well
  as cut. Nothing here is added to `arstro_dsp`: a benchmark composes the library, it does not
  reshape it.
- `SystemInfo` — chip / RAM / OS, per-platform behind one `query()` (R-SYS-1/2/3).
- `BenchmarkRunner` — owns the worker thread and publishes a `Snapshot` under a mutex. The UI
  polls it once per frame; nothing on the render thread ever blocks (R-G-4).

**Widgets — `artboard::Segment` subclasses, platform-free.** They draw only through
`IRenderTarget` primitives and animate only through `Property` / `ProgressIndicator`, so
`RecordingTarget` can assert them (R-TEST-1/2).

- `ScoreCard : artboard::ProgressIndicator` — a card *is* a progress readout, so it inherits the
  indeterminate sweep and the spring-smoothed display level rather than re-implementing them.
- `RunButton : artboard::Segment` — click + eased hover + eased press wash.
- `GpuToggle` — a caption plus Artboard's own `ToggleSwitch` (styled from cosmo's theme), which
  is why it owns no track/thumb drawing of its own. It carries the unavailable state, because
  "this machine has no accelerator" is a property of the control, not of the app.
- `SystemPanel`, `TotalCard` — self-drawn read-only panels.

**`BenchApp`** — composes the tree, owns the `BenchmarkRunner`, and on each frame moves the
runner's snapshot into the widgets. It is the only class that knows both sides.

**`native_main.cpp`** — the one platform file: GTK3 window → `artboard::CairoTarget`, a 16 ms
tick, pointer events into `GestureRecognizer`, and Fontconfig registration of cosmo's vendored
DM Sans / JetBrains Mono. Identical on Linux and on Windows under MSYS2/MinGW-w64, which is why
there is one host file and not two.

## Why no Artboard change was needed

Arstrobench adds no drawing capability: every card, meter, chip and label is paths, text and the
existing `pushLayer` opacity group. The `IRenderTarget` HAL is untouched, so no adapter needed
updating (`implement_artboard` §3).

## The one change made outside this app

`EditEngine::lastRenderAccelerated()` was added to `arstro_image` — a read-only accessor over a
flag `renderInto` already computed. `activeBackendName()` states which backend the engine *will
try*; nothing exposed whether the accelerator actually *took* the job, and an accelerator is free
to decline an edit it does not implement. Without the accessor the GPU toggle (R-IMG-4a) could
only report intent, which on today's OpenGL backend would mean labelling a CPU number "GPU". No
behaviour changed; one bool is now observable.

## Theme reuse (R-G-2)

`Theme.h` here is a *namespace alias* onto `arstro::cosmo_v2`'s tokens, and the build compiles
`apps/cosmo/Theme.cpp` into this target. `apps/cosmo/Theme.{h,cpp}` depends only on
`artboard_core` — not on `cosmo_core`, GTK or the image engine — so reusing it costs nothing and
guarantees the two apps cannot drift. Arstrobench adds exactly two derived tokens (the per-workload
accents), both built from `palette::primary()` / `palette::success()`.

## Threading

The worker thread touches only the core. `BenchmarkRunner` publishes through
`std::atomic<Stage>` (for the cheap per-frame read) plus a `std::mutex`-guarded `Snapshot` (for
the results). The GUI thread never joins mid-run; the destructor joins.
`arstro_image` parallelises internally over its own pool, which is intentional — cosmo's real
responsiveness depends on that pool, so the score should reflect it.
