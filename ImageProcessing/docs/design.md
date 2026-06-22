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
| Video | `video/VideoProcessor` | Per-frame application wrapper over an ImageProcessor. |
| Engine | `engine/EditEngine` | The facade: slots, flat parameter API, preview/full render, histogram. |

See [`architecture.puml`](architecture.puml) for the class diagram.

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
- **Pipeline order is a hard contract.** `Crop → Rotate → Exposure → Contrast →
  ToneRegions → WhiteBalance → ToneCurve → Vibrance → ColorMixer → ColorGrading →
  Dehaze → Grain`. Geometry first so spatial effects and the histogram describe the
  framed image and grain isn't rotated.
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
| `tone/` | Exposure (±5 EV, `2^EV`), Contrast (slope around 0.18 pivot), ToneRegions (highlights/shadows/whites/blacks via luminance masks), ToneCurve (1024-entry LUT, log/linear domain) |
| `color/` | WhiteBalance (temp/tint as luminance-preserving gains), Vibrance (+saturation, sat-weighted), ColorMixer (8 HSL bands), ColorGrading (3-way wheels + hue-range remap) |
| `effect/` | Dehaze (dark-channel prior, ± adds/removes haze), Grain (deterministic monochrome value noise) |
| `transform/` | Crop (normalized rect), Rotate (90° steps + arbitrary straighten, bilinear) |

## Status (milestones)

- **M1 (done):** base layer, `ImageBlock`, `EditEngine`, Histogram, sources, video wrapper.
- **M4a (done):** the full processor catalog above + every `EditEngine` setter, per-slot
  parameter state, the canonical pipeline, and HSL/Kelvin colour math. 45 unit tests; every
  reachable core line covered.
- **M4b (planned):** the matching cosmo UI panels + custom widgets (curve editor, colour
  wheels, crop overlay).
