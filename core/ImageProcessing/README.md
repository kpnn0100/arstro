# Arstro ImageProcessing Library

A headless image-processing library by Arstro — the image-domain mirror of the
[Arstro DSP library](../DigitalSignalProcessing). It provides a composable, modular
edit engine (namespace `arstro`): **parameters in, pixels out**. It powers the
`cosmo` professional photo editor (built on the Artboard UI framework), but the
library itself is UI-free and codec-free, so it compiles natively and to WebAssembly.

Inside the [Arstro](https://github.com/kpnn0100/arstro) umbrella this repo lives at
`arstro/core/ImageProcessing/`, alongside the other core libraries (`core/Artboard/`,
`core/DigitalSignalProcessing/`); the applications that consume them live under
`arstro/apps/`. It also stands alone — nothing here depends on the umbrella.

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

Builds natively on Linux and Windows (MSVC or MinGW) — no code changes needed either
way, `CMakeLists.txt` picks the right toolchain bits per OS:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
(cd build; ctest -C Release --output-on-failure)
```

The GPU compute backend (`ARSTRO_IMG_GL_COMPUTE`, on by default) accelerates
exposure/contrast/white-balance point ops via OpenGL 4.3 compute shaders on both:
surfaceless EGL on Linux, native WGL (`opengl32`, no extra install) on Windows. Any
edit outside that ported subset — and any host with no GL 4.3 device — falls back to
the CPU reference path automatically; nothing to configure.

See [`docs/design.md`](docs/design.md) for the full design, the DSP→image mapping,
and the processor roadmap.

## License

GNU Lesser General Public License v2.1 ([LICENSE](LICENSE)).
