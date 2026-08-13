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
- `apps/`: the Arstro applications — `cosmo`, `genesis`, `pulsar`, `launcher`, and the
  spec-stage `solaris` and `interstellar`.
- `examples/`: demo apps and platform glue, kept small on purpose.
- `build.sh`: umbrella build entry point.

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

### 1.3 Build script integration

Add the new app to `build.sh` in the target case statements.

For a web app:

- Add a `PROJECT` case under `linux-web-server`.
- Set `EXPORT`, `OUTNAME`, `APP_DIR`, and `APP_SRC`.
- Add DSP sources and includes only if the app uses DSP.

For a Linux app:

- Add a `PROJECT` case under `linux-native-app`.
- Compile `linux_main.cpp`, `<AppName>App.cpp`, and the native adapter source.
- Link the required toolkit libraries through `pkg-config`.

## 2. Build and run apps

List available projects and targets:

```bash
./build.sh --list
```

### 2.1 Web target

The web target builds WebAssembly through Emscripten and the Canvas2D adapter.

Load Emscripten into the current shell first:

```bash
source ~/emsdk/emsdk_env.sh
```

Build a web app:

```bash
./build.sh --project ui-demo --target linux-web-server
```

Serve the output directory:

```bash
cd examples/ui_demo/web
python3 -m http.server 8000
```

Then open `http://localhost:8000`.

Notes:

- `build.sh` now selects a Python 3.10+ interpreter for Emscripten automatically.
- Web output is written to `examples/<app>/web/`.

### 2.2 Linux native desktop target

The Linux desktop target is currently implemented for `ui-demo` using GTK3 and Cairo.

Build it with:

```bash
./build.sh --project ui-demo --target linux-native-app
```

Run the built application with:

```bash
./examples/ui_demo/build/ui_demo_linux
```

This target reuses the same `UiDemoApp` logic as the web build.

### 2.3 Native smoke target

Use `native-example` when you want a quick non-windowed validation run:

```bash
./build.sh --project ui-demo --target native-example
```

This compiles the app and runs a small executable check.

### 2.4 Repository tests

Run all repository tests known to the umbrella script with:

```bash
./build.sh --target native-test
```

You can also run Artboard tests directly:

```bash
cd Artboard
cmake -S . -B build
cmake --build build
./build/artboard_tests
```

### 2.1 Cross-platform build with CMake (Linux, Windows, macOS)

The umbrella `CMakeLists.txt` builds the **cosmo** photo editor and the library unit
tests with plain CMake — the same GTK3 + Cairo native backend on every platform (no
`build.sh` / bash needed). GTK3 pulls in Cairo and GdkPixbuf; LibRaw (RAW decode) is
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
- If `pkg-config` cannot find `gtk+-3.0`, ensure you are in the MinGW64 shell (not the
  plain MSYS shell) so `PKG_CONFIG_PATH` points at the MinGW packages.
- The **web** (Emscripten/WASM) target still ships via `build.sh --target linux-web-server`;
  CMake here covers the native desktop build.

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
- `build.sh` has explicit project support for each target you intend to ship.
- The web target serves `web/index.html` and `web/app.js` from `examples/<app>/web/`.
- The Linux target links only the libraries it actually uses.
- The umbrella repo commit includes the updated submodule pointer when Artboard or DSP changed.

## 5. Current known target coverage

- `scope`: web build via DSP + Artboard.
- `studio`: web build via DSP + Artboard.
- `ui-demo`: web build, Linux GTK3+Cairo desktop build, and native smoke build.