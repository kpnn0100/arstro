#!/bin/bash
#
#  Build, package, sign, install and launch the cosmo Android APK (arm64-v8a, native).
#  Uses the NDK CMake toolchain for libcosmo.so and the SDK build-tools (aapt2/zipalign/
#  apksigner) to assemble the APK — the system `gradle` (4.4.1) is too old for modern AGP.
#
#  Usage:  ./build_apk.sh [--device SERIAL] [--no-install]
#
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/deps/env.sh"

DEVICE="${COSMO_ANDROID_DEVICE:-}"     # default: first adb device
NO_INSTALL="${COSMO_ANDROID_NO_INSTALL:-0}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --device) DEVICE="$2"; shift 2;;
    --no-install) NO_INSTALL=1; shift;;
    *) echo "unknown arg: $1" >&2; exit 1;;
  esac
done

SDK="$HOME/Android/Sdk"
BT="$SDK/build-tools/$(ls "$SDK/build-tools" | sort -V | tail -1)"
ANDROID_JAR="$SDK/platforms/android-34/android.jar"
MIN_SDK=29; TARGET_SDK=34
AAPT2="$BT/aapt2"; ZIPALIGN="$BT/zipalign"; APKSIGNER="$BT/apksigner"
OUT="$HERE/build-apk"; STAGE="$OUT/stage"

# 0) ensure cross-compiled native deps exist
[ -f "$ANDROID_PREFIX/lib/libcairo.a" ] || "$HERE/deps/build_deps.sh" all

# 1) build libcosmo.so (arm64-v8a) via the NDK CMake toolchain
echo "== cmake/ndk build =="
cmake -S "$HERE" -B "$OUT/cmake" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ANDROID_ABI" -DANDROID_PLATFORM="android-$ANDROID_API" \
  -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$OUT/cmake" -j"$(nproc)"

# 2) stage APK contents: native lib + font assets
rm -rf "$STAGE"; mkdir -p "$STAGE/lib/$ANDROID_ABI" "$STAGE/assets/fonts"
cp "$OUT/cmake/libcosmo.so" "$STAGE/lib/$ANDROID_ABI/"
FDIR="$HERE/../assets/fonts"
cp "$FDIR/DMSans/DMSans-Regular.ttf"        "$STAGE/assets/fonts/"
cp "$FDIR/DMSans/DMSans-Medium.ttf"         "$STAGE/assets/fonts/"
cp "$FDIR/DMSans/DMSans-SemiBold.ttf"       "$STAGE/assets/fonts/"
cp "$FDIR/JetBrainsMono/JetBrainsMono-Regular.ttf" "$STAGE/assets/fonts/"
cp "$FDIR/JetBrainsMono/JetBrainsMono-Medium.ttf"  "$STAGE/assets/fonts/"
# any other bundled assets (sample image, etc.)
[ -d "$HERE/assets" ] && cp -r "$HERE/assets/." "$STAGE/assets/"

# 3) link a base APK (manifest + assets; no compiled resources needed)
echo "== aapt2 link =="
"$AAPT2" link -I "$ANDROID_JAR" \
  --manifest "$HERE/AndroidManifest.xml" \
  --min-sdk-version "$MIN_SDK" --target-sdk-version "$TARGET_SDK" \
  -A "$STAGE/assets" \
  -o "$OUT/base-unaligned.apk"

# 4) add the native lib into the APK
( cd "$STAGE" && zip -qXr "$OUT/base-unaligned.apk" lib )

# 5) align + sign (debug keystore, auto-created)
KS="$HERE/debug.keystore"
if [ ! -f "$KS" ]; then
  keytool -genkeypair -keystore "$KS" -storepass android -keypass android \
    -alias androiddebugkey -dname "CN=Android Debug,O=Android,C=US" \
    -keyalg RSA -keysize 2048 -validity 10000 >/dev/null 2>&1
fi
"$ZIPALIGN" -f 4 "$OUT/base-unaligned.apk" "$OUT/cosmo.apk"
"$APKSIGNER" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android "$OUT/cosmo.apk"
echo "built $OUT/cosmo.apk"

# 6) install + launch
if [ "$NO_INSTALL" = "0" ]; then
  ADBSEL=(); [ -n "$DEVICE" ] && ADBSEL=(-s "$DEVICE")
  echo "== install =="
  adb "${ADBSEL[@]}" install -r "$OUT/cosmo.apk"
  adb "${ADBSEL[@]}" shell am start -n com.arstro.cosmo/android.app.NativeActivity
  echo "launched com.arstro.cosmo on ${DEVICE:-default device}"
fi
