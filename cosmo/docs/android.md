# cosmo on Android

The Android build of cosmo — a portrait phone UI (design: `touch-ui-brief.md`) running the
**same** platform-free core and image engine as the desktop app, GPU-accelerated on the device.

## Architecture (what's reused vs new)

- **Reused unchanged:** the ImageProcessing engine, `cosmo/core` (`EditSession`/`History`/
  `PresetLibrary`/`ProjectStore`), Artboard's platform-free core, and the Cairo render adapter
  (`artboard::CairoTarget`). The phone UI drives `EditSession` and polls
  `renderService().tryAcquire(Frame&)` for RGBA preview pixels — identical to desktop `App`.
- **Rendering:** the UI is drawn with `CairoTarget` into a `cairo_image_surface` (ARGB32); the
  host uploads that buffer to a GLES texture and blits it to an `ANativeWindow` (a fullscreen
  quad, BGRA→RGBA swizzle). So on-screen output matches desktop. Fonts (no fontconfig on
  Android) resolve via `CairoTarget::registerFontFile` (`ARTBOARD_CAIRO_FT`).
- **GPU:** the engine's `GlesComputeBackend` (OpenGL ES 3.1 compute, `ARSTRO_GLES_COMPUTE`) is
  the Android sibling of the desktop GL backend — same accepted subset + shader body
  (`compute/GlComputeShared.h`). The UI blit context and the engine's compute context are
  independent EGL contexts (the compute one lives on the RenderService worker thread).
- **Decode:** `AndroidImageDecoder` (`stb_image` for JPEG/PNG/…, LibRaw for RAW).
- **New code:** `cosmo/android/` (host + build) and `cosmo/touch/` (the `arstro::cosmo_touch`
  phone UI Segments — `PhoneApp`, editor, tray, sliders).

## Layout of the source

```
cosmo/android/
  android_main.cpp     NativeActivity host: EGL + ANativeWindow + Cairo blit + touch input + decode
  CMakeLists.txt       NDK build of libcosmo.so (engine + core + Artboard core + phone UI)
  AndroidManifest.xml  pure-native NativeActivity (portrait, GLES 3.1)
  build_apk.sh         NDK cmake -> aapt2 -> zipalign -> apksigner -> adb install/launch
  deps/                cross-compile pixman/freetype/cairo/libraw (arm64) — env.sh + build_deps.sh
  third_party/         stb_image.h
  assets/              sample.jpg + (fonts staged from cosmo/assets at package time)
cosmo/touch/
  PhoneApp.{h,cpp}     the phone app shell + editor screen + tray + touch sliders
```

## Build & deploy

```
# one-time: cross-compile the native deps for arm64 (cairo/pixman/freetype/libraw)
cosmo/android/deps/build_deps.sh all
# build + sign + install + launch on a device
COSMO_ANDROID_DEVICE=<adb-serial> ./build.sh --project cosmo --target android-app
# or directly:  cosmo/android/build_apk.sh --device <adb-serial>
```

Requires the Android SDK (build-tools, platform android-34) + NDK; system `gradle` is not used
(too old) — packaging is done with the SDK build-tools. minSdkVersion 29, arm64-v8a.

## Status (milestones)

- **M0** foundation: NDK toolchain, cross-compiled deps, NativeActivity + EGL + Cairo blit,
  APK pipeline. ✅ (verified on device)
- **M1** GPU: `GlesComputeBackend` (GLES 3.1 compute); GPU==CPU ≤2/255 on host Mesa; live on
  device (`gpuAvailable()==true`). ✅
- **M2** decode: `AndroidImageDecoder` (stb + LibRaw). ✅ (decode + blit verified on device)
- **M3** phone editor: `PhoneApp` — top bar, photo canvas (polls RenderService), animated bottom
  tray, 5-tab tool bar, Basic/Detail section chips + touch sliders wired to `EditParams`, GPU on.
  ✅ (editing an image re-renders live on device; Exposure/Contrast/Temp/Tint hit the GPU).
- **M4–M7** (pending): Curve/Mixer tray, Grade/Xform trays, Mask tray + on-canvas overlay,
  chrome & overlays (filmstrip, breadcrumb, groups + green stacked indicators, sheets/dialogs,
  Home/Loading screens), pinch-zoom, touch-widget unit tests.
