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
                    │  ├ ScoreCard × 2   (ProgressIndicator)    │
                    │  ├ TotalCard                              │
                    │  └ SystemPanel                            │
                    └───────────────────┬──────────────────────┘
                                        │ polls (no blocking)
                    ┌───────────────────▼──────────────────────┐
                    │ BenchmarkRunner     (worker std::thread)  │  core (UI-free)
                    │  ├ ImageWorkload ── arstro::EditEngine    │
                    │  ├ DspWorkload   ── arstro::Voice/effects │
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
- `DspWorkload` — builds the 9-voice bank and renders it (untimed), then times
  Compressor → HighPass → LowPass → Reverb over 48000 samples per channel (R-DSP-1/2/3).
- `SystemInfo` — chip / RAM / OS, per-platform behind one `query()` (R-SYS-1/2/3).
- `BenchmarkRunner` — owns the worker thread and publishes a `Snapshot` under a mutex. The UI
  polls it once per frame; nothing on the render thread ever blocks (R-G-4).

**Widgets — `artboard::Segment` subclasses, platform-free.** They draw only through
`IRenderTarget` primitives and animate only through `Property` / `ProgressIndicator`, so
`RecordingTarget` can assert them (R-TEST-1/2).

- `ScoreCard : artboard::ProgressIndicator` — a card *is* a progress readout, so it inherits the
  indeterminate sweep and the spring-smoothed display level rather than re-implementing them.
- `RunButton : artboard::Segment` — click + eased hover + eased press wash.
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
