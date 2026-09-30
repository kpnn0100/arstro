# Arstro Development Guide

This guide covers three common workflows in the umbrella repository:

- Creating a new app that uses Artboard and optionally DigitalSignalProcessing.
- Building and running an existing app on the supported targets.
- Upgrading the Artboard and DigitalSignalProcessing submodules safely.

## Repository layout

- `core/`: the Arstro core libraries — the reusable engines every app links against.
  - `core/Artboard/`: rendering, animation, input, UI framework, and adapters.
  - `core/DigitalSignalProcessing/`: DSP engine and supporting audio processing modules.
  - `core/ImageProcessing/`: the image/video edit engine (tone, colour, masks, compute backends).
- `apps/`: the Arstro applications — `cosmo`, `genesis`, `pulsar`, `launcher`, `arstrobench`, and the
  spec-stage `solaris` and `interstellar`.
- `examples/`: demo apps and platform glue, kept small on purpose.
- `cmk`: CLI over the umbrella CMake build — list targets, build or run one.

## 1. Create a new app

Use `examples/ui_demo/` as the reference for a pure Artboard app and `examples/studio/` as the
reference for an app that combines Artboard with DSP.

### 1.1 Suggested file layout

Create a new directory under `examples/<app-name>/` with these files:

- `<AppName>App.h`: app-facing class declaration.
- `<AppName>App.cpp`: platform-free app logic.
- `web_main.cpp`: web adapter entry point.
- `linux_main.cpp`: Linux native entry point if the app needs a desktop build.
- `web/index.html` and `web/app.js`: browser shell for the WASM target.

Typical examples:

- `examples/ui_demo/UiDemoApp.cpp` for a single cross-platform UI implementation.
- `examples/ui_demo/web_main.cpp` for a web entry that forwards events into the same app code.
- `examples/ui_demo/linux_main.cpp` for a GTK3+Cairo Linux entry that reuses the same app code.

### 1.2 Platform-free app rules

Keep the application logic in the app class, not in the platform entry point.

The shared app class should:

- Depend on `core/Artboard/include/artboard/artboard.h`.
- Depend on `core/DigitalSignalProcessing/src/...` only if audio/DSP is needed.
- Expose `render(...)` for drawing.
- Expose pointer and keyboard input methods when interactive.
- Avoid direct OS, browser, or toolkit APIs.

The platform entry point should only:

- Create the window or browser module.
- Translate native input into the app input methods.
- Create the appropriate Artboard render target.
- Drive the app frame loop.

### 1.3 Build integration

Give the app a `CMakeLists.txt` that defines its target(s), and `add_subdirectory` it from
the umbrella `CMakeLists.txt` behind an `ARSTRO_BUILD_<APP>` option. Link `artboard_core`
(and `arstro_dsp` / `arstro_image` only if used); find toolkit libraries through
`pkg-config` and self-skip the GUI target when they are absent rather than failing the
configure. Once added, the target shows up in `./cmk list` with no further wiring.

## 2. Build and run apps

List every target CMake knows about, then build or run one by name:

```bash
./cmk list                 # executables + libraries (--all adds utility targets)
./cmk list shots           # filter: substring or glob
./cmk build cosmo          # build one target (several names, or none = all)
./cmk run cosmo -- a.jpg   # build an executable, then run it; args after --
./cmk test                 # build everything, then ctest --output-on-failure
./cmk configure -DARSTRO_BUILD_COSMO=OFF   # pass cache options
./cmk --debug run genesis  # Debug tree in build-debug/
```

A misspelt target name gets close-match suggestions. `-B DIR` picks another build tree
and `-j N` caps parallelism (default: all cores).

### 2.1 Cross-platform build with CMake (Linux, Windows, macOS)

The umbrella `CMakeLists.txt` builds the **cosmo** photo editor and the library unit
tests with plain CMake — the same GTK3 + Cairo native backend on every platform (no
bash needed). GTK3 pulls in Cairo and GdkPixbuf; LibRaw (RAW decode) is
optional and auto-detected via `pkg-config`.

```bash
cmake -S . -B build            # configure (prints whether RAW/LibRaw was found)
cmake --build build -j         # -> build/cosmo/cosmo, plus the library test exes
ctest --test-dir build         # run the Artboard + ImageProcessing unit tests
```

**Windows** — build in the *MSYS2 MinGW64* shell (GTK is a first-class MinGW package):

```bash
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
                   mingw-w64-x86_64-ninja mingw-w64-x86_64-gtk3 \
                   mingw-w64-x86_64-libraw mingw-w64-x86_64-pkgconf
cmake -S . -B build -G Ninja
cmake --build build            # -> build\cosmo\cosmo.exe
```

Notes:
- Options: `-DARSTRO_BUILD_COSMO=OFF` builds just the libraries + tests (no GTK needed).
  `-DARSTRO_BUILD_ARSTROBENCH=OFF` drops the benchmark app (`build/apps/arstrobench/arstrobench`),
  which builds on the same GTK3 + Cairo stack as cosmo and needs no LibRaw or GdkPixbuf.
- If `pkg-config` cannot find `gtk+-3.0`, ensure you are in the MinGW64 shell (not the
  plain MSYS shell) so `PKG_CONFIG_PATH` points at the MinGW packages.
- There is no web (Emscripten/WASM) build any more; the retired `build.sh` was its only
  driver. The `web_main.cpp` entry points remain for when it is ported to CMake.

## 3. Upgrade Artboard and DigitalSignalProcessing

Both dependencies are git submodules. Upgrade them from the umbrella repository root.

### 3.1 Inspect current submodule state

```bash
git submodule status
```

### 3.2 Update a submodule to the latest remote branch tip

For Artboard:

```bash
cd Artboard
git fetch origin
git checkout <branch>
git pull --ff-only
cd ..
git add Artboard
```

For DigitalSignalProcessing:

```bash
cd DigitalSignalProcessing
git fetch origin
git checkout <branch>
git pull --ff-only
cd ..
git add DigitalSignalProcessing
```

Then verify builds and commit the updated submodule pointer in the umbrella repo.

### 3.3 Update submodules after cloning or switching branches

```bash
git submodule update --init --recursive
```

### 3.4 Upgrade workflow when you have local submodule changes

If a submodule has local commits:

1. Commit and push inside the submodule first.
2. Return to the umbrella repo.
3. Stage the submodule directory to record the new submodule commit pointer.
4. Commit the umbrella repo.

If a submodule has uncommitted dirty files, the umbrella repo cannot capture those file contents.
You must either commit inside the submodule or clean that working tree first.

## 4. Cross-platform app checklist

Before calling an app complete, verify these points:

- One shared app class drives both web and native builds.
- Platform entry points only translate input and create render targets.
- The app's CMake target appears in `./cmk list` and builds with `./cmk build <target>`.
- The Linux target links only the libraries it actually uses.
- The umbrella repo commit includes the updated submodule pointer when Artboard or DSP changed.

## 5. Current known target coverage

`./cmk list` is the authoritative list. `scope`, `studio`, `synth`, `ui-demo` and `pulsar`
were built only by the retired `build.sh` (web and hand-rolled native builds) and have no
CMake target yet.
