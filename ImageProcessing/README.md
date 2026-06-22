# Arstro ImageProcessing Library

A headless image-processing library by Arstro — the image-domain mirror of the
[Arstro DSP library](../DigitalSignalProcessing). It provides a composable, modular
edit engine (namespace `arstro`): **parameters in, pixels out**. It powers the
`cosmo` Lightroom-style photo editor (built on the Artboard UI framework), but the
library itself is UI-free and codec-free, so it compiles natively and to WebAssembly.

## Design in one paragraph

The library mirrors the DSP library's mechanism. A base `ImageProcessor` declares a
property enum and reuses the DSP smoothed-parameter system; concrete processors
override `process(in, out)` (or, for point ops, `processPixel` via `PointProcessor`).
`ImageSource` produces an image (the pipeline start), `ImageBlock` chains processors
(the edit stack), and `EditEngine` is the facade the UI drives. The engine works in
**linear-light sRGB** and decodes/encodes at the ingest/egress boundary, so exposure,
contrast and white balance are physically correct.

## Getting started

```cpp
#include "image_processing.h"   // with src/ on the include path
using namespace arstro;

EditEngine engine;
int slot = engine.addImage(rgba8, width, height);  // gamma-sRGB straight RGBA8 in
engine.setExposure(0.5f);                           // +0.5 EV
engine.setContrast(20.0f);
PreviewBuffer pv = engine.renderPreview();          // engine-owned RGBA8 out
const HistogramData &h = engine.histogram();
```

## Project structure

```
src/        the engine — one directory per module family
            (base, source, tone, color, effect, transform, analysis, video, engine)
            + aggregate header image_processing.h
unittest/   dependency-free MiniTest unit suite
docs/       architecture.puml (class diagram) + design.md
```

## Build & test

```bash
cmake -S . -B build && cmake --build build
(cd build && ctest --output-on-failure)
```

See [`docs/design.md`](docs/design.md) for the full design, the DSP→image mapping,
and the processor roadmap.

## License

MIT License.
