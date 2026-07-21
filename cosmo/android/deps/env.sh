# Cross-compile environment for cosmo's Android (arm64-v8a) native dependencies.
# Source this: `source env.sh`. Requires ANDROID NDK; override NDK_HOME to pick one.
#
# Produces static libs (pixman, freetype, cairo, libraw) under deps/prefix so the
# Android CMake build (cosmo/android/CMakeLists.txt) can link them. The desktop build
# is unaffected — this tree is Android-only and git-ignored (see deps/.gitignore).

# --- pick an NDK (prefer an explicit NDK_HOME, else the newest installed) ---
if [ -z "${NDK_HOME:-}" ]; then
  for c in "$HOME/Android/Sdk/ndk"/*; do [ -d "$c" ] && NDK_HOME="$c"; done
fi
export NDK_HOME
export ANDROID_ABI=arm64-v8a
export ANDROID_API=29                       # minSdk (device 4KD1W75QSA is API 29)
export ANDROID_TARGET=aarch64-linux-android
export HOST_TAG=linux-x86_64
export NDK_TC="$NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG"

DEPS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DEPS_DIR
export ANDROID_PREFIX="$DEPS_DIR/prefix"

# cross toolchain binaries (Clang uses per-API wrapper; binutils are the llvm-* set)
export CC="$NDK_TC/bin/${ANDROID_TARGET}${ANDROID_API}-clang"
export CXX="$NDK_TC/bin/${ANDROID_TARGET}${ANDROID_API}-clang++"
export AR="$NDK_TC/bin/llvm-ar"
export RANLIB="$NDK_TC/bin/llvm-ranlib"
export STRIP="$NDK_TC/bin/llvm-strip"
export NM="$NDK_TC/bin/llvm-nm"

# make our cross-built .pc files the ONLY ones meson/pkg-config sees (no host libs)
export PKG_CONFIG_LIBDIR="$ANDROID_PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH="$ANDROID_PREFIX/lib/pkgconfig"
# pkg-config must NOT prepend a sysroot to our absolute prefix paths
unset PKG_CONFIG_SYSROOT_DIR

# prefer a pip-installed modern meson (cairo 1.18 needs meson >= 1.3)
export PATH="$HOME/.local/bin:$PATH"

echo "NDK_HOME=$NDK_HOME  ABI=$ANDROID_ABI  API=$ANDROID_API"
echo "prefix=$ANDROID_PREFIX"
