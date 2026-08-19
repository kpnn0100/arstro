# Arstro ImageProcessing — Design Overview

> Design surface for the Arstro ImageProcessing library — the image-domain mirror
> of the DSP library (`DigitalSignalProcessing/`). Kept in sync with the code by
> the same V-model discipline (doc → design → code → unit test). The library is a
> **headless edit engine**: parameters in, pixels out. The UI (the `cosmo` app,
> built on Artboard) only pushes parameters in and displays the output.

## What this is

A modular image-processing library (namespace `arstro`) that mirrors the DSP
library's mechanism — a base processing node with a smoothed-parameter system, an
`update()` hook, and a composition primitive — applied to 2D images. The buildable,
tested surface is declared in [`../src/image_processing.h`](../src/image_processing.h).
The core is **platform-free and codec-free** (it compiles under Emscripten); file
decoding (LibRaw / stb_image / the browser) lives in the app layer behind the
`FileImageSource` seam.

## How the DSP mirror maps

| DSP (`DigitalSignalProcessing`) | Image (`ImageProcessing`) | Why it changes |
|---|---|---|
| `Sample` (double scalar) | `Pixel` = 32-bit `float` | a full-res RGBA image is huge; single-pass ops don't accumulate error → float. |
| `process(Sample in, int channel)` per sample | `process(const Image&, Image&)` whole buffer | images are random-access 2D; a "channel" is an R/G/B component of one pixel, **not** a separate pass. |
| `SignalProcessor` property system | reused verbatim (`ParameterSet`/`SmoothedParameter`) | only the process signature changes. Smoothing is OFF by default (instant edits). |
| `SignalGenerator` | `ImageSource` (`generate() : Image`) | produces a buffer; no envelope/notes. |
| `Block` | `ImageBlock` (one ordered chain = the edit stack) | ping-pongs two scratch Images; stages may resize (Crop/Rotate). |
| `AudioConfig` | `ImageConfig` (working space, preview cap, clamp) | one config authority (DIP). |

## Layers

| Layer | Home (under `src/`) | Responsibility |
|-------|------|----------------|
| Config | `base/ImageConfig` | Working colour space, interactive preview cap, output clamp. |
| Buffer | `base/Image`, `base/Pixel` | The pixel buffer + scalar type that flow through the pipeline. |
| Colour | `base/ColorSpace` | sRGB ↔ linear, luminance (and, later, HSL / Kelvin). |
| Core | `base/ImageProcessor` (+ `PointProcessor`) | `process(in,out)` + the property system. |
| Params | `base/SmoothedParameter`, `ParameterSet` | Parameter storage/ramps (mirrored from DSP). |
| Composite | `base/ImageBlock` | The ordered edit stack (serial chain). |
| Sources | `base/ImageSource` → `source/*` | Pipeline starting points (file / solid / noise). |
| Modules | `tone/`, `color/`, `effect/`, `transform/` | Concrete processors. |
| Analysis | `analysis/Histogram` | A sink: tonal distribution of the output (not a processor). |
| Accel | `base/Parallel` | Row-parallel `parallelFor` across CPU cores (native, `-pthread`); serial fallback on the single-threaded web build. |
| Video | `video/VideoProcessor` | Per-frame application wrapper over an ImageProcessor. |
| Params | `engine/EditParams` (+ `EditParamsIO`, `EditParamsCompose`) | The UI-independent edit description (plain data): every control's value. `EditParamsIO` serializes it to/from text for session/sidecar files. `composeParams(base, over)` stacks one param set on top of another (additive scalars; tone/mixer curves add in Y; masks concatenate; crop stays per-item) — the math behind cosmo's group settings / any adjustment-layer stack. Built by any front end, handed to the engine. |
| Engine | `engine/EditEngine` | The facade: slots, flat + whole-`EditParams` API, preview/full render, histogram, and `renderImage(img, params, maxEdge)` — the seam a video editor reuses per frame. |
| Service | `engine/RenderService` | Runs an EditEngine on its OWN worker thread; the UI submits `(slot, EditParams)` and polls completed frames, never blocking. Synchronous fallback when threads are off. |
| Compute | `compute/ComputeBackend` (+ `compute/GlComputeBackend`) | Optional GPU-accelerator **seam**: `IComputeBackend` (an accelerator that renders the pipeline for a `(linear Image, EditParams)` — chains + masks + encode + histogram taps) + the per-platform `createComputeAccelerator()` factory. `EditEngine::renderInto` prefers it when the user opts in *and* it is available; the CPU pipeline is the reference and the guaranteed fallback (see cosmo R-GPU). First concrete backend: `GlComputeBackend` — OpenGL 4.3 compute over surfaceless EGL (Linux/AMD; llvmpipe software fallback), accelerating the per-pixel colour/tone point ops and declining the rest to CPU (`ARSTRO_GL_COMPUTE`; the seam header stays platform-free so the web build is CPU-only). Android sibling: `GlesComputeBackend` — the same algorithm over an OpenGL **ES 3.1** context (`ARSTRO_GLES_COMPUTE`), selected by the same factory; the accepted subset and shader body are shared with the desktop backend via `compute/GlComputeShared.h` (only the EGL context + `#version` header differ) so the two never drift. Because an available backend may still **decline** a job (`process` returning false, e.g. any edit outside `glcompute::computeSupports`), `activeBackendName()` states the backend the next render will *try* and `lastRenderAccelerated()` states whether the last one actually *ran* there — a caller reporting which backend produced a result (a benchmark, a diagnostics panel) cannot otherwise tell the two apart. |

See [`architecture.puml`](architecture.puml) for the class diagram.

## Threading & reuse

- **Core is UI-free and reusable.** `EditParams` (data) + `EditEngine` (pipeline) +
  `RenderService` (threading) have zero Artboard/UI dependency. A video editor decodes a
  frame, builds an `EditParams`, and calls `EditEngine::renderImage(frame, params, maxEdge)`
  (or drives a `RenderService`) — the same core, no photo-app coupling.
- **Two cores.** `RenderService` puts the engine on a worker thread; the UI thread only
  submits parameter snapshots and polls finished frames. Renders coalesce to the latest
  request, so dragging a slider never queues a backlog.
- **Hardware acceleration.** Inside a render, the hot per-pixel loops (every point op, the
  sRGB egress encode, the preview downscale) run row-parallel across all CPU cores via
  `par::parallelFor`. Parallel output is byte-identical to serial (rows are independent).

## Key design rules

- **Linear working space (correctness).** The engine decodes sRGB → linear once at
  ingest and encodes linear → sRGB once at egress (display + histogram). Exposure,
  Contrast, White Balance, blur/dehaze, and downscale are only physically correct in
  linear light — `+1 EV` exactly doubles linear values (the headline unit test). The
  ToneCurve and Histogram are display-referred (encoded). This decode/encode boundary
  is the single highest-risk correctness item.
- **Headless engine / param layering.** `EditEngine` exposes a flat setter per edit
  control and returns RGBA8 pixels + a histogram. The UI never touches pixels or the
  processors — exactly the DSP → Pulsar split.
- **Pipeline order is a hard contract.** `Crop → Rotate → LensCorrection →
  NoiseReduction → Exposure → Contrast → ToneRegions → WhiteBalance → ToneCurve →
  Texture → Clarity → Vibrance → ColorMixer → ColorGrading → Dehaze → Sharpen →
  Grain`. Geometry first so spatial effects and the histogram describe the framed
  image; noise reduction precedes the tone ops that would amplify it; sharpening and
  grain come last so they are neither blurred nor rotated. The engine runs this order
  as three segments so it can **tap** the image entering the tone curve (a luminance
  histogram) and entering the colour mixer (a saturation-weighted hue distribution) —
  the UI draws these behind the curve/mixer editors so each control's background shows
  the data it operates on, not the final output.
- **Preview vs full-res.** Interactive edits run on a fitted, area-averaged preview
  proxy (`renderPreview`); export runs the full-res path (`renderFull`). Resolution-
  independent params (gain-based ops, normalized crop) match between the two.
- **Smoothing is its own concern (SRP).** `ImageProcessor` decides *when* a ramp
  advances; `ParameterSet`/`SmoothedParameter` know *how*. Disabled by default for
  stills; `VideoProcessor::enableSmoothing(true)` re-arms it for temporal interpolation.

## Build & test

```bash
cmake -S . -B build && cmake --build build
(cd build && ctest --output-on-failure)      # runs the MiniTest unit suite
```

Unit tests live in `unittest/` (dependency-free `MiniTest.h`). Every reachable core
source line is exercised; the only gcov residual is the compiler-generated deleting-
destructor (`D0`) variant of the abstract base `ImageProcessor`, which is uncallable.

## Processor catalog

| Family | Processors |
|--------|------------|
| `tone/` | Exposure (±5 EV, `2^EV`), Contrast (slope around 0.18 pivot), ToneRegions (highlights/shadows/whites/blacks via luminance masks), ToneCurve (four 1024-entry LUTs — an RGB **master** curve applied to all channels, then an independent **per-channel** R/G/B curve; `out_c = chan_c(master(x_c))`; log/linear domain shared. Control points are **bezier** `CurvePoint`s — corners by default, smooth (Alt-dragged handles) where set — flattened by the shared `curve::sample()`, the same model and sampler as the colour mixer) |
| `color/` | WhiteBalance (temp/tint as luminance-preserving gains), Vibrance (+saturation, sat-weighted), ColorMixer (3 cyclic per-hue curves: hue-shift/sat/lum over the input hue, wrapping at 360 so it never bands), ColorGrading (3-way wheels + hue-range remap) |
| `effect/` | Dehaze (dark-channel prior, ± adds/removes haze), Grain (smooth two-octave deterministic value noise — quintic fade, so `size` sets grain scale without blocky upscaling), Texture (fine-radius local contrast), Clarity (large-radius midtone local contrast, midtone-masked) |
| `detail/` | Sharpen (unsharp mask on perceptual luma, amount/radius/edge-masking), NoiseReduction (Gaussian chroma blur for colour speckle + edge-preserving bilateral on luma) |
| `transform/` | Crop (normalized rect), Rotate (90° steps + arbitrary straighten, bilinear), LensCorrection (radial distortion + chromatic-aberration + vignette, one resample pass) |
| `base/` (shared) | `spatial::` separable Gaussian + luminance plane helpers, used by the detail/presence processors so none re-implements a blur |
| `engine/MaskStack` | local adjustments: each `MaskParams` (radial / linear / brush) builds a 0..1 coverage plane in normalised framed coords, renders an adjusted copy through the same processors, and blends it over the base. Applied post-pipeline in `renderInto`, so it rides preview, full-res, and the video seam alike. |

## Status (milestones)

- **M1 (done):** base layer, `ImageBlock`, `EditEngine`, Histogram, sources, video wrapper.
- **M4a (done):** the full processor catalog above + every `EditEngine` setter, per-slot
  parameter state, the canonical pipeline, and HSL/Kelvin colour math. 45 unit tests; every
  reachable core line covered.
- **M4b (planned):** the matching cosmo UI panels + custom widgets (curve editor, colour
  wheels, crop overlay).
