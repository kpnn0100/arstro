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
    --list)    echo "projects: scope, studio, synth, ui-demo"; echo "targets:  linux-web-server, native-test, native-example, linux-native-app"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 1;;
  esac
done

HOST_OS=$(uname -s); HOST_ARCH=$(uname -m)
ROOT="$(cd "$(dirname "$0")" && pwd)"
DSP="$ROOT/DigitalSignalProcessing"; AB="$ROOT/Artboard"
echo "host: ${HOST_OS}/${HOST_ARCH}   project: ${PROJECT}   target: ${TARGET}"

dsp_src=()
while IFS= read -r -d '' file; do
  dsp_src+=("$file")
done < <(find "$DSP/src" -name '*.cpp' -not -path '*/spatial/*' -not -path '*/util/*' -print0)

ab_core=()
while IFS= read -r -d '' file; do
  ab_core+=("$file")
done < <(find "$AB/src/core" "$AB/src/anim" "$AB/src/render" "$AB/src/scene" "$AB/src/input" "$AB/src/ui" -name '*.cpp' -print0)

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
      -sEXPORTED_RUNTIME_METHODS=HEAPF32 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web \
      -o "$OUT/$OUTNAME.js"
    echo "built $OUT/$OUTNAME.js + $OUTNAME.wasm"
    echo "run:   (cd '$OUT' && python3 -m http.server 8000)   then open http://localhost:8000"
    ;;
  native-test)
    for repo in "$DSP" "$AB"; do
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
        for dep in gtk+-3.0 alsa; do
          if ! pkg-config --exists "$dep"; then
            echo "error: $dep development files not found." >&2
            exit 1
          fi
        done
        OUTDIR="$ROOT/examples/synth/build"
        mkdir -p "$OUTDIR"
        read -r -a SYNTH_CFLAGS <<< "$(pkg-config --cflags gtk+-3.0 alsa)"
        read -r -a SYNTH_LIBS <<< "$(pkg-config --libs gtk+-3.0 alsa)"
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
      *)
        echo "linux-native-app currently supports projects 'ui-demo' and 'synth'" >&2
        exit 1
        ;;
    esac
    ;;
  *) echo "unknown target '$TARGET'" >&2; exit 1;;
esac
