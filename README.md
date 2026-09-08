# Arstro

**Top technical for art.**

Arstro is an indie project founded by **Nam Doan** ([@kpnn0100](https://github.com/kpnn0100)).
It builds the technical foundation that creative software stands on — a 2D rendering and UI
framework, a DSP engine, and an image-processing engine — and then builds real creative
applications on top of them.

The name is *art* + *astro*: using art to reach for the star — the dream — and getting there by
precise engineering, the way you reach a star in a spaceship. Ambition is the destination;
precision is the vehicle. The apps are named for what's out there: cosmo, pulsar, genesis,
solaris, interstellar.

## The core libraries

The three libraries under [`core/`](core/) are the point of the project. Each is headless,
dependency-light, and usable on its own.

| Library | What it is |
|---|---|
| [`core/Artboard`](core/Artboard/) | A **platform-free 2D drawing and UI framework**. Your app builds a scene of drawables and a tree of interactive segments against one abstract surface; a thin adapter renders it to a real device. The core contains no platform code — it is a HAL for graphics and input. Adapters exist for web (Canvas2D), native (Cairo), and test (a recording target). |
| [`core/DigitalSignalProcessing`](core/DigitalSignalProcessing/) | The **DSP / synth engine** (namespace `arstro`). Composable signal processors, generators, filters, effects and reverb, all channel-aware and driven by one shared audio config. Includes a physical-modelling piano. |
| [`core/ImageProcessing`](core/ImageProcessing/) | The **image edit engine** — the image-domain mirror of the DSP library. Parameters in, pixels out: tone, colour, masks, detail, transforms, with CPU and GPU compute backends. UI-free and codec-free, so it compiles natively and to WebAssembly. |

An Arstro **app is platform-free**: it depends only on these libraries and renders through an
adapter, so it adapts to any screen or OS the same way a driver adapts to hardware.

## The applications

| App | What it is | State |
|---|---|---|
| [`apps/cosmo`](apps/cosmo/) | Professional photo editor | **Building** — Linux desktop, plus an Android build path |
| [`apps/genesis`](apps/genesis/) | Animation designer whose deliverable is source code — draw a control, bind its geometry, and it emits a `.h`/`.cpp` pair that compiles against Artboard | **Building** — editor, CLI and core |
| [`apps/pulsar`](apps/pulsar/) | Synthesiser UI over the DSP engine | **Building** — Linux desktop |
| [`apps/launcher`](apps/launcher/) | An Android-style touch shell for GNOME and KDE Plasma, drawn entirely with Artboard | **Building** — milestones code-complete, live-desktop verification pending |
| [`apps/arstrobench`](apps/arstrobench/) | Benchmark that answers "can this computer run cosmo?" — times the image pipeline and the signal pipeline, scores each as `1/seconds`, and reports the chip, RAM and OS it measured | **Building** — Linux and Windows desktop |
| [`apps/solaris`](apps/solaris/) | Digital audio workstation | **Spec only** — no code yet |
| [`apps/interstellar`](apps/interstellar/) | Professional video editor: cuts and composites, and **hosts a live Cosmo project as its colour authority** — colour is graded in the rack, the timeline only cuts, and every parameter is addressable, automatable and bindable to a calculation | **Spec only** — no code yet; [fully specified](apps/interstellar/docs/) |

[`examples/`](examples/) holds small demos — `scope`, `studio`, `synth`, `piano`, `ui_demo` —
kept deliberately minimal as references for building an app on the stack.

The long-term plan is a creative suite that shares one project model, version control and
resource pool, so a project made in one app can be embedded live into another. See
[docs/vision.md](docs/vision.md) for the overview and [docs/shared-core.md](docs/shared-core.md)
for the shared **Nebula** core.

## Layout

```
arstro/
├── core/          the libraries — Artboard, DigitalSignalProcessing, ImageProcessing
├── apps/          the applications — cosmo, genesis, pulsar, launcher, arstrobench, solaris, interstellar
├── examples/      small demos built on the stack
├── docs/          suite vision and shared-core design
└── build.sh       umbrella build entry point
```

## Build

Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/kpnn0100/arstro.git
# or, after a plain clone:
git submodule update --init --recursive
```

Everything at once, via CMake:

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build            # all libraries' and apps' tests
```

Or per project via `build.sh`, which picks the Artboard adapter from the target and
auto-detects the host:

```bash
./build.sh --list
./build.sh --target native-test                          # build + run every repo's tests
./build.sh --project cosmo  --target linux-native-app    # GTK3 + Cairo desktop app
./build.sh --project pulsar --target linux-native-app
./build.sh --project scope  --target linux-web-server    # WASM + Canvas2D (needs emcc)
```

For the web target, `source ~/emsdk/emsdk_env.sh` first, then build and serve:
`(cd examples/scope/web && python3 -m http.server 8000)`.

See [DEVELOPMENT.md](DEVELOPMENT.md) for the app-creation workflow, build targets, and
submodule upgrade steps.

## Repos

Each core library is developed in its own repository and can be used without the rest of Arstro.

| Path | Remote | Linked as |
|------|--------|-----------|
| (this) | https://github.com/kpnn0100/arstro.git | — |
| `core/Artboard/` | https://github.com/kpnn0100/artboard.git | submodule |
| `core/DigitalSignalProcessing/` | https://github.com/kpnn0100/DigitalSignalProcessing.git | submodule |
| `core/ImageProcessing/` | https://github.com/kpnn0100/ImageProcessing.git | vendored — its sources are committed into this repo directly, so a plain clone already has it |

## Licence

GNU Lesser General Public License v2.1 ([LICENSE](LICENSE)) © 2025 Nam Doan
([@kpnn0100](https://github.com/kpnn0100)).

The core libraries may be linked into closed-source applications; changes to the libraries
themselves must be published under the same terms. Artboard and DigitalSignalProcessing were
previously published under the MIT License — those earlier releases remain available under MIT.

Third-party code keeps its own licence: `core/ImageProcessing/lib/LibRaw` is vendored LibRaw,
dual-licensed CDDL / LGPL-2.1.
