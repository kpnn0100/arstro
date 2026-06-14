# Arstro

Umbrella project tying together the Arstro stack:

- **[DigitalSignalProcessing](DigitalSignalProcessing/)** (submodule) — the Arstro **DSP** library
  + synth engine.
- **[Artboard](Artboard/)** (submodule) — the Arstro **UI stack**: a platform-free 2D drawing
  framework (shapes, paths, text, animation) over a thin device adapter (a graphics HAL).
- **[examples/](examples/)** — apps that link DSP + Artboard, plus a pure Artboard UI demo.

An Arstro **app is platform-free**: it depends only on the Arstro stack and renders through an
adapter, so it adapts to any screen/OS like a HAL.

See [DEVELOPMENT.md](DEVELOPMENT.md) for the app creation workflow, build targets, and submodule
upgrade steps.

## Build

The build picks the **adapter from the target**; the **host is auto-detected**.

```bash
./build.sh --list
./build.sh --project scope --target linux-web-server   # WASM + Canvas2D (needs emcc)
./build.sh --project ui-demo --target native-example   # native smoke build + run for the UI demo
./build.sh --project ui-demo --target linux-native-app # GTK3 + Cairo Linux desktop app
./build.sh --target native-test                         # build + run every repo's tests
```

For the web target: `source ~/emsdk/emsdk_env.sh` first (Emscripten), then
`./build.sh`, then `(cd examples/scope/web && python3 -m http.server 8000)`.

## Repos / submodules

| Path | Remote |
|------|--------|
| (this) | https://github.com/kpnn0100/arstro.git |
| `DigitalSignalProcessing/` | https://github.com/kpnn0100/DigitalSignalProcessing.git |
| `Artboard/` | https://github.com/kpnn0100/artboard.git |

Clone with submodules: `git clone --recurse-submodules <arstro-url>` (or
`git submodule update --init --recursive` after a plain clone).
