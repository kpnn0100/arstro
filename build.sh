#!/bin/bash
#
#  Arstro umbrella build. Selects a PROJECT and a TARGET device; the HOST is
#  auto-detected. The target chooses the Artboard adapter (web target -> Canvas2D
#  via WebAssembly).
#
#  Usage:
#    ./build.sh [--project scope] [--target linux-web-server]
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
    --list)    echo "projects: scope, studio"; echo "targets:  linux-web-server, native-test"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 1;;
  esac
done

HOST_OS=$(uname -s); HOST_ARCH=$(uname -m)
ROOT="$(cd "$(dirname "$0")" && pwd)"
DSP="$ROOT/DigitalSignalProcessing"; AB="$ROOT/Artboard"
echo "host: ${HOST_OS}/${HOST_ARCH}   project: ${PROJECT}   target: ${TARGET}"

dsp_src() { find "$DSP/src" -name '*.cpp' -not -path '*/spatial/*' -not -path '*/util/*'; }
ab_core() { find "$AB/src/core" "$AB/src/anim" "$AB/src/render" "$AB/src/scene" "$AB/src/input" -name '*.cpp'; }

case "$TARGET" in
  linux-web-server)
    if ! command -v emcc >/dev/null 2>&1; then
      echo "error: emcc not found. Install Emscripten and 'source ~/emsdk/emsdk_env.sh'." >&2
      exit 1
    fi
    case "$PROJECT" in
      scope)  EXPORT=createScopeModule;  OUTNAME=scope ;;
      studio) EXPORT=createStudioModule; OUTNAME=studio ;;
      *) echo "unknown project '$PROJECT' for web target" >&2; exit 1;;
    esac
    OUT="$ROOT/examples/$PROJECT/web"
    emcc -O2 -std=c++17 --bind \
      "$ROOT/examples/$PROJECT/web_main.cpp" "$ROOT/examples/$PROJECT/${PROJECT^}App.cpp" \
      "$AB/src/adapter/web/Canvas2DTarget.cpp" $(ab_core) $(dsp_src) \
      -I"$AB/src" -I"$AB/include" -I"$DSP/src" \
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
  *) echo "unknown target '$TARGET'" >&2; exit 1;;
esac
