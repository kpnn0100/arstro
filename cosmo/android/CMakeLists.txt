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
set(DEPS_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/deps/prefix)
set(GLUE_DIR    ${ANDROID_NDK}/sources/android/native_app_glue)

if(NOT EXISTS ${DEPS_PREFIX}/lib/libcairo.a)
  message(FATAL_ERROR "Missing cross-compiled deps at ${DEPS_PREFIX}. Run cosmo/android/deps/build_deps.sh all first.")
endif()

# Artboard platform-free core (same set build.sh compiles into artboard_core).
file(GLOB_RECURSE AB_CORE
  ${AB_DIR}/src/core/*.cpp   ${AB_DIR}/src/anim/*.cpp  ${AB_DIR}/src/render/*.cpp
  ${AB_DIR}/src/scene/*.cpp  ${AB_DIR}/src/input/*.cpp ${AB_DIR}/src/ui/*.cpp)

add_library(cosmo SHARED
  android_main.cpp
  ${AB_DIR}/src/adapter/native/CairoTarget.cpp
  ${GLUE_DIR}/android_native_app_glue.c
  ${AB_CORE})

target_include_directories(cosmo PRIVATE
  ${AB_DIR}/src ${AB_DIR}/include
  ${AB_DIR}/src/adapter/native
  ${GLUE_DIR}
  ${DEPS_PREFIX}/include
  ${DEPS_PREFIX}/include/cairo
  ${DEPS_PREFIX}/include/pixman-1
  ${DEPS_PREFIX}/include/freetype2)

# ARTBOARD_CAIRO_FT switches CairoTarget to cairo-ft font faces (no fontconfig on Android).
target_compile_definitions(cosmo PRIVATE ARTBOARD_CAIRO_FT)

# Static graphics deps (order matters: cairo -> pixman/freetype).
target_link_libraries(cosmo
  ${DEPS_PREFIX}/lib/libcairo.a
  ${DEPS_PREFIX}/lib/libpixman-1.a
  ${DEPS_PREFIX}/lib/libfreetype.a
  EGL GLESv3 android log m)

# Keep the NativeActivity entry the glue defines (nothing references it directly).
target_link_options(cosmo PRIVATE -u ANativeActivity_onCreate)
