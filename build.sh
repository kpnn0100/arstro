#!/bin/bash
#
#  Arstro umbrella build. Selects a PROJECT and a TARGET device; the HOST is
#  auto-detected. The target chooses the Artboard adapter (web target -> Canvas2D
#  via WebAssembly).
#
#  Usage:
#    ./build.sh [--project scope] [--target linux-web-server]
#    ./build.sh --project ui-demo --target native-example
#    ./build.sh --project ui-demo --target linux-native-app
#    ./build.sh --project cosmo --target linux-native-app   # Figma-exact editor (native-only)
#    ./build.sh --target native-test     # build + run every repo's tests
#    ./build.sh --list
#
set -e
PROJECT=scope
TARGET=linux-web-server
while [[ $# -gt 0 ]]; do
  case "$1" in
    --project) PROJECT="$2"; shift 2;;
    --target)  TARGET="$2";  shift 2;;
    --list)    echo "projects: scope, studio, synth, piano, pulsar, cosmo, ui-demo"; echo "targets:  linux-web-server, native-test, native-example, linux-native-app, android-app"; echo "note:     cosmo runs native (linux-native-app), Android (android-app), no web build; piano is native-only too"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 1;;
  esac
done

HOST_OS=$(uname -s); HOST_ARCH=$(uname -m)
ROOT="$(cd "$(dirname "$0")" && pwd)"
DSP="$ROOT/DigitalSignalProcessing"; AB="$ROOT/Artboard"; IP="$ROOT/ImageProcessing"
echo "host: ${HOST_OS}/${HOST_ARCH}   project: ${PROJECT}   target: ${TARGET}"

dsp_src=()
while IFS= read -r -d '' file; do
  dsp_src+=("$file")
done < <(find "$DSP/src" -name '*.cpp' -not -path '*/spatial/*' -not -path '*/util/*' -print0)

ab_core=()
while IFS= read -r -d '' file; do
  ab_core+=("$file")
done < <(find "$AB/src/core" "$AB/src/anim" "$AB/src/render" "$AB/src/scene" "$AB/src/input" "$AB/src/ui" -name '*.cpp' -print0)

# ImageProcessing engine core (platform-free / WASM-friendly — no codecs).
ip_src=()
while IFS= read -r -d '' file; do
  ip_src+=("$file")
done < <(find "$IP/src" -name '*.cpp' -print0)

case "$TARGET" in
  linux-web-server)
    EM_PYTHON=""
    for candidate in python3.12 python3.11 python3.10 python3; do
      if command -v "$candidate" >/dev/null 2>&1; then
        py_path="$(command -v "$candidate")"
        py_ver="$($py_path -c 'import sys; print(f"{sys.version_info[0]}.{sys.version_info[1]}")')"
        py_major="${py_ver%%.*}"
        py_minor="${py_ver#*.}"
        if [[ "$py_major" -gt 3 || ( "$py_major" -eq 3 && "$py_minor" -ge 10 ) ]]; then
          EM_PYTHON="$py_path"
          break
        fi
      fi
    done
    if [[ -z "$EM_PYTHON" ]]; then
      echo "error: no Python 3.10+ interpreter found for Emscripten." >&2
      exit 1
    fi
    export EMSDK_PYTHON="$EM_PYTHON"
    export PATH="$(dirname "$EM_PYTHON"):$PATH"

    if ! command -v emcc >/dev/null 2>&1; then
      echo "error: emcc not found. Install Emscripten and 'source ~/emsdk/emsdk_env.sh'." >&2
      exit 1
    fi
    echo "emscripten python: $EM_PYTHON"
    case "$PROJECT" in
      scope)
        EXPORT=createScopeModule;  OUTNAME=scope
        APP_DIR="$ROOT/examples/scope"
        APP_SRC=("$APP_DIR/web_main.cpp" "$APP_DIR/ScopeApp.cpp")
        EXTRA_SRC=("${dsp_src[@]}")
        EXTRA_INC=(-I"$DSP/src")
        ;;
      studio)
        EXPORT=createStudioModule; OUTNAME=studio
        APP_DIR="$ROOT/examples/studio"
        APP_SRC=("$APP_DIR/web_main.cpp" "$APP_DIR/StudioApp.cpp")
        EXTRA_SRC=("${dsp_src[@]}")
        EXTRA_INC=(-I"$DSP/src")
        ;;
      synth)
        EXPORT=createSynthModule; OUTNAME=synth
        APP_DIR="$ROOT/examples/synth"
        APP_SRC=("$APP_DIR/web_main.cpp" "$APP_DIR/SynthApp.cpp")
        EXTRA_SRC=("${dsp_src[@]}")
        EXTRA_INC=(-I"$DSP/src")
        ;;
      pulsar)
        EXPORT=createPulsarModule; OUTNAME=pulsar
        APP_DIR="$ROOT/pulsar"
        APP_SRC=("$APP_DIR/web_main.cpp" "$APP_DIR/PulsarApp.cpp" "$APP_DIR/OscillatorPanel.cpp" "$APP_DIR/WaveDisplay.cpp" "$APP_DIR/MuteButton.cpp" "$APP_DIR/Stepper.cpp" "$APP_DIR/Chrome.cpp" "$APP_DIR/SubOscPanel.cpp" "$APP_DIR/FilterPanel.cpp" "$APP_DIR/EnvPanel.cpp" "$APP_DIR/LfoPanel.cpp" "$APP_DIR/LfoCurve.cpp" "$APP_DIR/MacroPanel.cpp" "$APP_DIR/Keyboard.cpp" "$APP_DIR/ModSourceBadge.cpp" "$APP_DIR/GainPanel.cpp")
        EXTRA_SRC=()
        EXTRA_INC=()
        ;;
      ui-demo)
        EXPORT=createUiDemoModule; OUTNAME=ui_demo
        APP_DIR="$ROOT/examples/ui_demo"
        APP_SRC=("$APP_DIR/web_main.cpp" "$APP_DIR/UiDemoApp.cpp")
        EXTRA_SRC=()
        EXTRA_INC=()
        ;;
      *) echo "unknown project '$PROJECT' for web target" >&2; exit 1;;
    esac
    OUT="$APP_DIR/web"
    emcc -O2 -std=c++17 --bind \
      "${APP_SRC[@]}" "$AB/src/adapter/web/Canvas2DTarget.cpp" "${ab_core[@]}" "${EXTRA_SRC[@]}" \
      -I"$AB/src" -I"$AB/include" "${EXTRA_INC[@]}" \
      -sMODULARIZE=1 -sEXPORT_NAME=$EXPORT \
      -sEXPORTED_RUNTIME_METHODS=HEAPF32,HEAPU8 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web \
      -o "$OUT/$OUTNAME.js"
    echo "built $OUT/$OUTNAME.js + $OUTNAME.wasm"
    echo "run:   (cd '$OUT' && python3 -m http.server 8000)   then open http://localhost:8000"
    ;;
  native-test)
    for repo in "$DSP" "$AB" "$IP"; do
      echo "== $repo =="
      cmake -S "$repo" -B "$repo/build" >/dev/null
      cmake --build "$repo/build" -j >/dev/null
      (cd "$repo/build" && ctest --output-on-failure)
    done
    ;;
  native-example)
    case "$PROJECT" in
      ui-demo)
        OUTDIR="$ROOT/examples/ui_demo/build"
        mkdir -p "$OUTDIR"
        c++ -std=c++17 -O2 \
          "$ROOT/examples/ui_demo/native_main.cpp" "$ROOT/examples/ui_demo/UiDemoApp.cpp" \
          "${ab_core[@]}" \
          -I"$AB/src" -I"$AB/include" \
          -o "$OUTDIR/ui_demo_native"
        "$OUTDIR/ui_demo_native"
        ;;
      *)
        echo "native-example currently supports only project 'ui-demo'" >&2
        exit 1
        ;;
    esac
    ;;
  linux-native-app)
    case "$PROJECT" in
      ui-demo)
        if ! pkg-config --exists gtk+-3.0; then
          echo "error: gtk+-3.0 development files not found. Install GTK3 dev packages." >&2
          exit 1
        fi
        OUTDIR="$ROOT/examples/ui_demo/build"
        mkdir -p "$OUTDIR"
        read -r -a GTK_CFLAGS <<< "$(pkg-config --cflags gtk+-3.0)"
        read -r -a GTK_LIBS <<< "$(pkg-config --libs gtk+-3.0)"
        c++ -std=c++17 -O2 \
          "$ROOT/examples/ui_demo/linux_main.cpp" "$ROOT/examples/ui_demo/UiDemoApp.cpp" \
          "$AB/src/adapter/native/CairoTarget.cpp" \
          "${ab_core[@]}" \
          -I"$AB/src" -I"$AB/include" \
          "${GTK_CFLAGS[@]}" "${GTK_LIBS[@]}" \
          -o "$OUTDIR/ui_demo_linux"
        echo "built $OUTDIR/ui_demo_linux"
        ;;
      synth)
        for dep in gtk+-3.0 alsa x11; do
          if ! pkg-config --exists "$dep"; then
            echo "error: $dep development files not found." >&2
            exit 1
          fi
        done
        OUTDIR="$ROOT/examples/synth/build"
        mkdir -p "$OUTDIR"
        read -r -a SYNTH_CFLAGS <<< "$(pkg-config --cflags gtk+-3.0 alsa x11)"
        read -r -a SYNTH_LIBS <<< "$(pkg-config --libs gtk+-3.0 alsa x11)"
        c++ -std=c++17 -O2 \
          "$ROOT/examples/synth/linux_main.cpp" "$ROOT/examples/synth/SynthApp.cpp" \
          "$AB/src/adapter/native/CairoTarget.cpp" \
          "${ab_core[@]}" "${dsp_src[@]}" \
          -I"$AB/src" -I"$AB/include" -I"$DSP/src" \
          "${SYNTH_CFLAGS[@]}" "${SYNTH_LIBS[@]}" -lpthread \
          -o "$OUTDIR/synth_linux"
        echo "built $OUTDIR/synth_linux"
        echo "run:   $OUTDIR/synth_linux"
        ;;
      piano)
        for dep in gtk+-3.0 alsa x11; do
          if ! pkg-config --exists "$dep"; then
            echo "error: $dep development files not found." >&2
            exit 1
          fi
        done
        OUTDIR="$ROOT/examples/piano/build"
        mkdir -p "$OUTDIR"
        read -r -a PIANO_CFLAGS <<< "$(pkg-config --cflags gtk+-3.0 alsa x11)"
        read -r -a PIANO_LIBS <<< "$(pkg-config --libs gtk+-3.0 alsa x11)"
        c++ -std=c++17 -O2 \
          "$ROOT/examples/piano/linux_main.cpp" "$ROOT/examples/piano/PianoApp.cpp" \
          "$DSP/apps/piano_demo/PianoEngine.cpp" \
          "$AB/src/adapter/native/CairoTarget.cpp" \
          "${ab_core[@]}" "${dsp_src[@]}" \
          -I"$AB/src" -I"$AB/include" -I"$DSP/src" \
          "${PIANO_CFLAGS[@]}" "${PIANO_LIBS[@]}" -lpthread \
          -o "$OUTDIR/piano_linux"
        echo "built $OUTDIR/piano_linux"
        echo "run:   $OUTDIR/piano_linux"
        ;;
      pulsar)
        if ! pkg-config --exists gtk+-3.0; then
          echo "error: gtk+-3.0 development files not found." >&2
          exit 1
        fi
        OUTDIR="$ROOT/pulsar/build"
        mkdir -p "$OUTDIR"
        read -r -a PULSAR_CFLAGS <<< "$(pkg-config --cflags gtk+-3.0)"
        read -r -a PULSAR_LIBS <<< "$(pkg-config --libs gtk+-3.0)"
        c++ -std=c++17 -O2 \
          "$ROOT/pulsar/linux_main.cpp" "$ROOT/pulsar/PulsarApp.cpp" \
          "$ROOT/pulsar/OscillatorPanel.cpp" "$ROOT/pulsar/WaveDisplay.cpp" "$ROOT/pulsar/MuteButton.cpp" "$ROOT/pulsar/Stepper.cpp" \
          "$ROOT/pulsar/Chrome.cpp" "$ROOT/pulsar/SubOscPanel.cpp" "$ROOT/pulsar/FilterPanel.cpp" \
          "$ROOT/pulsar/EnvPanel.cpp" "$ROOT/pulsar/LfoPanel.cpp" "$ROOT/pulsar/LfoCurve.cpp" "$ROOT/pulsar/MacroPanel.cpp" "$ROOT/pulsar/Keyboard.cpp" "$ROOT/pulsar/ModSourceBadge.cpp" "$ROOT/pulsar/GainPanel.cpp" \
          "$AB/src/adapter/native/CairoTarget.cpp" \
          "${ab_core[@]}" \
          -I"$AB/src" -I"$AB/include" \
          "${PULSAR_CFLAGS[@]}" "${PULSAR_LIBS[@]}" \
          -o "$OUTDIR/pulsar_linux"
        echo "built $OUTDIR/pulsar_linux"
        echo "run:   $OUTDIR/pulsar_linux"
        ;;
      cosmo)
        # Figma-exact photo editor. Native-only. UI (cosmo/*.cpp + widgets/) +
        # the UI-free logic in cosmo/core (session/history/presets/decoder), and
        # registers vendored DM Sans / JetBrains Mono via Fontconfig at start.
        for dep in gtk+-3.0 fontconfig; do
          if ! pkg-config --exists "$dep"; then
            echo "error: $dep development files not found." >&2
            exit 1
          fi
        done
        OUTDIR="$ROOT/cosmo/build"
        mkdir -p "$OUTDIR"
        read -r -a COSMO_CFLAGS <<< "$(pkg-config --cflags gtk+-3.0 fontconfig)"
        read -r -a COSMO_LIBS <<< "$(pkg-config --libs gtk+-3.0 fontconfig)"
        # RAW decoding is optional. Prefer a vendored LibRaw source tree under
        # ImageProcessing/lib/LibRaw (built on demand into a static lib); else fall
        # back to a system LibRaw via pkg-config; else build without RAW.
        RAW_DEF=""; RAW_CFLAGS=(); RAW_LIBS=()
        LIBRAW_DIR="$IP/lib/LibRaw"
        if [ -f "$LIBRAW_DIR/libraw/libraw.h" ]; then
          if [ ! -f "$LIBRAW_DIR/lib/libraw.a" ]; then
            echo "cosmo: building vendored LibRaw static lib (one-time)…"
            (cd "$LIBRAW_DIR" && make -f Makefile.dist lib/libraw.a -j"$(nproc)") >/dev/null
          fi
          RAW_DEF="-DCOSMO_HAVE_LIBRAW"
          RAW_CFLAGS=(-I"$LIBRAW_DIR")
          RAW_LIBS=("$LIBRAW_DIR/lib/libraw.a" -lz)   # default LibRaw build uses zlib
          echo "cosmo: RAW enabled (vendored LibRaw)"
        elif pkg-config --exists libraw; then
          RAW_DEF="-DCOSMO_HAVE_LIBRAW"
          read -r -a RAW_CFLAGS <<< "$(pkg-config --cflags libraw)"
          read -r -a RAW_LIBS <<< "$(pkg-config --libs libraw)"
          echo "cosmo: RAW enabled (system LibRaw)"
        else
          echo "cosmo: RAW disabled (LibRaw not found) — JPEG/PNG/TIFF via GdkPixbuf only"
        fi
        # Every cosmo .cpp (app + core + decoder) EXCEPT the core unit test's main().
        cosmo_src=()
        while IFS= read -r -d '' f; do cosmo_src+=("$f"); done \
          < <(find "$ROOT/cosmo" -name '*.cpp' ! -path '*/tests/*' ! -path '*/build/*' \
                ! -path '*/android/*' ! -path '*/touch/*' ! -name 'AndroidImageDecoder.cpp' -print0)
        # ARSTRO_ENABLE_THREADS + RAW_DEF on EVERY TU so the RenderService layout
        # matches; COSMO_SOURCE_DIR lets the binary find assets/fonts by path.
        # GPU compute backend (R-GPU): OpenGL 4.3 compute over surfaceless EGL. Link
        # EGL/GL and define ARSTRO_GL_COMPUTE so compute/GlComputeBackend.cpp builds;
        # the CPU pipeline stays the reference + fallback if no GPU is present.
        GL_DEF="-DARSTRO_GL_COMPUTE"; GL_LIBS=(-lEGL -lGL)
        c++ -std=c++17 -O2 -pthread -DARSTRO_ENABLE_THREADS $RAW_DEF $GL_DEF \
          -DCOSMO_SOURCE_DIR="\"$ROOT/cosmo\"" \
          "${cosmo_src[@]}" \
          "$AB/src/adapter/native/CairoTarget.cpp" \
          "${ab_core[@]}" "${ip_src[@]}" \
          -I"$AB/src" -I"$AB/include" -I"$IP/src" -I"$ROOT/cosmo" \
          "${COSMO_CFLAGS[@]}" "${COSMO_LIBS[@]}" "${RAW_CFLAGS[@]}" "${RAW_LIBS[@]}" "${GL_LIBS[@]}" \
          -o "$OUTDIR/cosmo_linux"
        echo "built $OUTDIR/cosmo_linux"
        echo "run:   $OUTDIR/cosmo_linux [image files...]"
        ;;
      *)
        echo "linux-native-app currently supports projects 'ui-demo', 'synth', 'pulsar' and 'cosmo'" >&2
        exit 1
        ;;
    esac
    ;;
  android-app)
    # Android (arm64-v8a) native app. cosmo only. Cross-compiles the graphics deps
    # (cairo/pixman/freetype/libraw) once, builds libcosmo.so with the NDK CMake
    # toolchain, packages/signs an APK with the SDK build-tools, and installs it.
    # The system gradle (4.4.1) is too old for modern AGP, so packaging is manual.
    case "$PROJECT" in
      cosmo)
        # Pick the install target with COSMO_ANDROID_DEVICE=<adb-serial> (or --no-install
        # via COSMO_ANDROID_NO_INSTALL=1). build_apk.sh has finer-grained flags.
        exec "$ROOT/cosmo/android/build_apk.sh"
        ;;
      *) echo "android-app currently supports only project 'cosmo'" >&2; exit 1;;
    esac
    ;;
  *) echo "unknown target '$TARGET'" >&2; exit 1;;
esac
