# Arstrobench — the benchmark that answers "can this computer run cosmo?".
#
# Times the two engines cosmo and its siblings depend on (arstro_image, arstro_dsp) and
# scores each as 1/seconds. See REQUIREMENTS.md and docs/architecture.md.
#
# Native backend, identical on both hosts: GTK3 (window) + Cairo (the Artboard native
# render adapter) + Fontconfig (registers cosmo's vendored DM Sans / JetBrains Mono at
# startup, see native_main.cpp).
#
#   Linux:            cmake -S . -B build && cmake --build build -j
#   Windows (MSYS2):  in the "MSYS2 MINGW64" shell:
#                       pacman -S --needed mingw-w64-x86_64-{toolchain,cmake,ninja,gtk3,pkgconf}
#                       cmake -S . -B build -G Ninja && cmake --build build
#
# Unlike cosmo this app needs NO GdkPixbuf and NO LibRaw — it never decodes a file; its
# image is generated in code. The only optional dependency is GTK3 itself.

set(ARSTROBENCH_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ARSTRO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CAIRO_ADAPTER ${ARSTRO_ROOT}/core/Artboard/src/adapter/native/CairoTarget.cpp)

# cosmo's Theme.cpp is compiled straight in: R-G-2 says Arstrobench uses cosmo's design
# tokens VERBATIM, and sharing the translation unit is the only way that cannot drift.
# It depends on artboard_core alone — not cosmo_core, GTK or the image engine — so this
# costs nothing and does not couple the two apps' builds.
set(COSMO_THEME ${ARSTRO_ROOT}/apps/cosmo/Theme.cpp)

# ── arstrobench_core: the UI-free measurement layer ──
# No Artboard, no GTK: it builds and runs headless everywhere, which is what lets the
# unit tests drive the real workloads with no display (R-TEST-1).
file(GLOB ARSTROBENCH_CORE_SOURCES CONFIGURE_DEPENDS ${ARSTROBENCH_DIR}/core/*.cpp)
add_library(arstrobench_core STATIC ${ARSTROBENCH_CORE_SOURCES})
target_include_directories(arstrobench_core PUBLIC ${ARSTROBENCH_DIR})
target_link_libraries(arstrobench_core PUBLIC arstro_image arstro_dsp)

find_package(Threads REQUIRED)
target_link_libraries(arstrobench_core PUBLIC Threads::Threads)

# SystemInfo reads the CPU name and the OS version out of the registry (R-SYS-2/3).
# MinGW's default spec already pulls advapi32 in, but relying on a link-order default for
# a symbol we call directly is how a Windows build breaks for a reason nobody can see.
if(WIN32)
  target_link_libraries(arstrobench_core PUBLIC advapi32)
endif()

# ── arstrobench_ui: the platform-free Artboard widgets + the app root ──
# Split from the GUI executable so the unit tests and the shot harness link the SAME
# objects the app does, instead of hand-compiling a second copy that could drift.
add_library(arstrobench_ui STATIC
  ${ARSTROBENCH_DIR}/BenchApp.cpp
  ${ARSTROBENCH_DIR}/widgets/RunButton.cpp
  ${ARSTROBENCH_DIR}/widgets/ScoreCard.cpp
  ${ARSTROBENCH_DIR}/widgets/SystemPanel.cpp
  ${ARSTROBENCH_DIR}/widgets/TotalCard.cpp
  ${COSMO_THEME})
target_include_directories(arstrobench_ui PUBLIC
  ${ARSTROBENCH_DIR}
  ${ARSTRO_ROOT}/core/Artboard/src
  ${ARSTRO_ROOT}/core/Artboard/include)
target_link_libraries(arstrobench_ui PUBLIC artboard_core arstrobench_core)

# ── Unit tests: device-free, no GTK, no display (R-TEST-1) ──
add_executable(arstrobench_tests ${ARSTROBENCH_DIR}/tests/benchTests.cpp)
target_link_libraries(arstrobench_tests PRIVATE arstrobench_ui)
add_test(NAME arstrobench_unit COMMAND arstrobench_tests)

# ── The GUI + the headless shot harness (both need GTK3/Cairo) ──
# pkg-config itself is optional: on native Windows without MSYS2 there is none, and a
# GTK3 GUI cannot be built there anyway, so the executables are skipped gracefully
# rather than hard-failing the whole tree's configure (same policy as cosmo_core).
# The library targets and the unit tests above stay available either way.
find_package(PkgConfig)
if(NOT PkgConfig_FOUND)
  message(STATUS "arstrobench: GUI skipped (no pkg-config — needed for GTK3; on Windows install MSYS2). Core + tests still build.")
else()
  pkg_check_modules(ARSTROBENCH_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  pkg_check_modules(ARSTROBENCH_FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

  add_executable(arstrobench ${ARSTROBENCH_DIR}/native_main.cpp ${CAIRO_ADAPTER})
  target_link_libraries(arstrobench PRIVATE arstrobench_ui
                        PkgConfig::ARSTROBENCH_GTK3 PkgConfig::ARSTROBENCH_FONTCONFIG)
  # cosmo owns the vendored fonts; this app borrows the same files rather than
  # vendoring a second copy that could fall out of step.
  target_compile_definitions(arstrobench PRIVATE
    COSMO_ASSETS_DIR="${ARSTRO_ROOT}/apps/cosmo/assets")

  # ── arstrobench_shots: the real app, the real Cairo adapter, an image surface
  # instead of a window (R-TEST-3) — so the UI can be SEEN with no display. Mirrors
  # cosmo_shots. Cairo comes in via GTK3; gtk_init() is never called.
  add_executable(arstrobench_shots ${ARSTROBENCH_DIR}/tests/renderShots.cpp ${CAIRO_ADAPTER})
  target_link_libraries(arstrobench_shots PRIVATE arstrobench_ui
                        PkgConfig::ARSTROBENCH_GTK3 PkgConfig::ARSTROBENCH_FONTCONFIG)
  target_compile_definitions(arstrobench_shots PRIVATE
    COSMO_ASSETS_DIR="${ARSTRO_ROOT}/apps/cosmo/assets")

  # `--check` is what makes this a TEST rather than a smoke run: it fails a PNG that
  # came out a uniform colour, which is the one failure mode a shot harness has.
  add_test(NAME arstrobench_shots_headless
           COMMAND arstrobench_shots --outdir ${CMAKE_CURRENT_BINARY_DIR}/shots-ctest --check)
endif()
