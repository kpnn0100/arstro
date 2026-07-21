# cosmo Android native library (libcosmo.so) — built with the NDK CMake toolchain
# (see build_apk.sh). Reuses the platform-free Artboard core + the Cairo render adapter
# (artboard::CairoTarget), rendering into a Cairo image surface that android_main.cpp
# blits to the ANativeWindow via GLES. Depends on the arm64 static libs cross-compiled
# by deps/build_deps.sh (cairo/pixman/freetype). Engine + phone UI are added in later
# milestones (see cosmo/android notes).
cmake_minimum_required(VERSION 3.18)
project(cosmo_android CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Repo roots (this file lives at cosmo/android/)
set(COSMO_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(ROOT_DIR    ${COSMO_DIR}/..)
set(AB_DIR      ${ROOT_DIR}/Artboard)
set(IP_DIR      ${ROOT_DIR}/ImageProcessing)
set(DEPS_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/deps/prefix)
set(GLUE_DIR    ${ANDROID_NDK}/sources/android/native_app_glue)

if(NOT EXISTS ${DEPS_PREFIX}/lib/libcairo.a)
  message(FATAL_ERROR "Missing cross-compiled deps at ${DEPS_PREFIX}. Run cosmo/android/deps/build_deps.sh all first.")
endif()

# Artboard platform-free core (same set build.sh compiles into artboard_core).
file(GLOB_RECURSE AB_CORE
  ${AB_DIR}/src/core/*.cpp   ${AB_DIR}/src/anim/*.cpp  ${AB_DIR}/src/render/*.cpp
  ${AB_DIR}/src/scene/*.cpp  ${AB_DIR}/src/input/*.cpp ${AB_DIR}/src/ui/*.cpp)

# ImageProcessing engine (platform-free). Includes GlesComputeBackend.cpp (active under
# ARSTRO_GLES_COMPUTE) and GlComputeBackend.cpp (empty without ARSTRO_GL_COMPUTE).
file(GLOB_RECURSE IP_SRC ${IP_DIR}/src/*.cpp)

# cosmo UI-free core (Artboard-independent): session/history/presets/projects. Decode is
# Android-only (AndroidImageDecoder); NativeImageDecoder (GdkPixbuf) is excluded.
set(COSMO_CORE
  ${COSMO_DIR}/core/EditSession.cpp
  ${COSMO_DIR}/core/History.cpp
  ${COSMO_DIR}/core/PresetLibrary.cpp
  ${COSMO_DIR}/core/ProjectStore.cpp
  ${COSMO_DIR}/core/decode/AndroidImageDecoder.cpp)

# Phone UI (arstro::cosmo_v2 touch widgets) — added incrementally per milestone.
file(GLOB PHONE_UI ${CMAKE_CURRENT_SOURCE_DIR}/../touch/*.cpp)

add_library(cosmo SHARED
  android_main.cpp
  ${PHONE_UI}
  ${COSMO_DIR}/Theme.cpp              # cosmo_v2 palette/font/radius tokens (reused by touch UI)
  ${COSMO_DIR}/widgets/Icons.cpp      # cosmo_v2::icon:: line glyphs (panelLeft etc.)
  ${COSMO_CORE}
  ${IP_SRC}
  ${AB_DIR}/src/adapter/native/CairoTarget.cpp
  ${GLUE_DIR}/android_native_app_glue.c
  ${AB_CORE})

target_include_directories(cosmo PRIVATE
  ${AB_DIR}/src ${AB_DIR}/include
  ${AB_DIR}/src/adapter/native
  ${IP_DIR}/src
  ${COSMO_DIR}                        # core/decode/... and touch/... includes
  ${COSMO_DIR}/core
  ${CMAKE_CURRENT_SOURCE_DIR}/../touch
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party   # stb_image.h
  ${GLUE_DIR}
  ${DEPS_PREFIX}/include
  ${DEPS_PREFIX}/include/cairo
  ${DEPS_PREFIX}/include/pixman-1
  ${DEPS_PREFIX}/include/freetype2)

# ARTBOARD_CAIRO_FT: cairo-ft font faces (no fontconfig on Android).
# COSMO_HAVE_LIBRAW: RAW decode via the cross-built LibRaw.
# ARSTRO_ENABLE_THREADS: RenderService worker thread (matches the desktop build).
# ARSTRO_GLES_COMPUTE: the engine's OpenGL ES 3.1 GPU compute backend.
target_compile_definitions(cosmo PRIVATE
  ARTBOARD_CAIRO_FT COSMO_HAVE_LIBRAW ARSTRO_ENABLE_THREADS ARSTRO_GLES_COMPUTE)

# Static deps. Order matters (cairo -> pixman/freetype). libraw needs zlib (-lz,
# provided by Android) for its default USE_ZLIB build.
target_link_libraries(cosmo
  ${DEPS_PREFIX}/lib/libcairo.a
  ${DEPS_PREFIX}/lib/libpixman-1.a
  ${DEPS_PREFIX}/lib/libfreetype.a
  ${DEPS_PREFIX}/lib/libraw.a
  z EGL GLESv3 android log m)

# Keep the NativeActivity entry the glue defines (nothing references it directly).
target_link_options(cosmo PRIVATE -u ANativeActivity_onCreate)
