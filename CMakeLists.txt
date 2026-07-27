# Arstro umbrella build.
#
# Cross-platform CMake for the cosmo photo editor and the libraries it uses.
# Builds on Linux and on Windows (MSYS2 / MinGW-w64, which provides GTK3, Cairo,
# GdkPixbuf, LibRaw and pkg-config) — the same GTK3 + Cairo native backend on both.
#
#   Linux:            cmake -S . -B build && cmake --build build -j
#   Windows (MSYS2):  in the "MSYS2 MINGW64" shell:
#                       pacman -S --needed mingw-w64-x86_64-{toolchain,cmake,ninja,gtk3,libraw,pkgconf}
#                       cmake -S . -B build -G Ninja && cmake --build build
#
# The cosmo GUI needs GTK3 (window + dialogs + GdkPixbuf decode) and Cairo (the
# Artboard native render adapter). RAW decode (LibRaw) is optional and auto-detected.
cmake_minimum_required(VERSION 3.16)
project(arstro LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(ARSTRO_BUILD_COSMO "Build the cosmo photo editor GUI" ON)
option(ARSTRO_BUILD_ANDROID_SHELL "Build the Android-theme touch shell (launcher/android-shell)" ON)
option(ARSTRO_BUILD_DSP "Build the Arstro DSP library + synth/kitchen-sink/piano-demo apps" ON)
option(ARSTRO_BUILD_PIANO_UI "Build the interactive piano-keyboard UI demo (examples/piano; needs GTK3+ALSA, skipped gracefully if absent)" ON)

enable_testing()  # aggregate the subprojects' unit tests so top-level `ctest` sees them

# Portable, dependency-free library targets (artboard_core, arstro_image) reused
# by the app. Each subproject also defines its own unit-test executable.
add_subdirectory(Artboard)
add_subdirectory(ImageProcessing)

# arstro_dsp: the channel-aware DSP/synth library (ESP32DigitalSynth) — physical-
# modeling piano (physical/PianoVoice), oscillators, effects. Also defines the
# arstro_kitchen_sink and arstro_piano_demo executables (selectable/launchable
# targets) and a `demo` convenience target that renders the piano showcase WAV.
if(ARSTRO_BUILD_DSP)
  add_subdirectory(DigitalSignalProcessing)
endif()

# examples/piano: play PianoVoice live off the computer keyboard (Artboard UI +
# ALSA audio thread). Depends on artboard_core + arstro_dsp, so it's added
# after both; the target itself is optional and self-skips without GTK3/ALSA
# (see examples/piano/CMakeLists.txt) rather than failing this configure.
if(ARSTRO_BUILD_PIANO_UI AND ARSTRO_BUILD_DSP)
  add_subdirectory(examples/piano)
endif()

# cosmo: the Figma-exact editor GUI. Its UI-free session/editing/history/decode
# logic lives in cosmo/core (target cosmo_core), added by cosmo/CMakeLists.txt.
if(ARSTRO_BUILD_COSMO)
  add_subdirectory(cosmo)
endif()

# arstro-android-shell: the Android-theme touch shell (launcher, status bar, quick
# settings, notifications, gesture nav) for GNOME/Plasma. Depends only on artboard_core
# + GTK3/Cairo. See launcher/docs/android-theme-plan.md and android-theme-progress.md.
if(ARSTRO_BUILD_ANDROID_SHELL)
  add_subdirectory(launcher/android-shell)
endif()
