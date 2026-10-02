# interstellar_app — Interstellar's FRONT END: a platform-free Artboard Segment tree (App + widgets),
# drawn from AppModel and reporting intent as text command lines through AppHooks. See NOTES.md.
#
# Usable two ways:
#   * standalone   cmake -S apps/interstellar/app -B <dir>     (pulls Artboard + ImageProcessing in)
#   * add_subdirectory(app) from a parent that already defines artboard_core / arstro_image.
#
# It deliberately links NEITHER cosmo_core NOR interstellar_core: the service does not exist in
# this build, which is the point of the hooks (R-G-4 by construction). The reused cosmo widgets are
# COMPILED from apps/cosmo/widgets/*.cpp (R-UI-5: linked, not copied), together with cosmo's
# Theme.cpp (the arstrobench pattern: the token namespaces are aliased, the theme factory is
# compiled) and cosmo's embedded-font machinery, so the shots draw in the app's OWN type.
cmake_minimum_required(VERSION 3.14)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(interstellar_app LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
  endif()
  enable_testing()
endif()

set(IS_APP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(IS_REPO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(IS_COSMO_DIR ${IS_REPO_DIR}/apps/cosmo)

# ── dependencies, only when a parent has not already defined them ──
if(NOT TARGET artboard_core)
  add_subdirectory(${IS_REPO_DIR}/core/Artboard ${CMAKE_CURRENT_BINARY_DIR}/artboard EXCLUDE_FROM_ALL)
endif()
# arstro_image: EditParams (the model's grade params), EditParamsIO's curve spelling, and
# Histogram::toLog behind the reused HistogramWidget. Linked PUBLIC so its PUBLIC defines
# (ARSTRO_ENABLE_THREADS, ARSTRO_GL_COMPUTE) match whatever else in the final binary uses EditParams.
if(NOT TARGET arstro_image)
  add_subdirectory(${IS_REPO_DIR}/core/ImageProcessing ${CMAKE_CURRENT_BINARY_DIR}/arstro_image EXCLUDE_FROM_ALL)
endif()

# Standalone, the two dependency projects register their OWN suites (core, unit, volume) whose
# executables are EXCLUDE_FROM_ALL here — they belong to other owners and are not this build's
# evidence. CTestCustom.cmake tells ctest to skip them, so `ctest` in this build dir runs exactly
# the front end's suites and reports green or red on those alone.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  file(WRITE ${CMAKE_BINARY_DIR}/CTestCustom.cmake "set(CTEST_CUSTOM_TESTS_IGNORE core unit volume)\n")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(IS_CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(IS_FREETYPE REQUIRED IMPORTED_TARGET freetype2)

# ── the typeface, compiled in exactly as cosmo does it (R-FONT-1) ──
set(IS_FONT_DIR ${IS_COSMO_DIR}/assets/fonts)
set(IS_FONT_FILES
    ${IS_FONT_DIR}/Roboto/Roboto-Regular.ttf
    ${IS_FONT_DIR}/Roboto/Roboto-Medium.ttf
    ${IS_FONT_DIR}/Roboto/Roboto-SemiBold.ttf
    ${IS_FONT_DIR}/JetBrainsMono/JetBrainsMono-Regular.ttf
    ${IS_FONT_DIR}/JetBrainsMono/JetBrainsMono-Medium.ttf)
set(IS_FONT_LIST
    "Roboto|${IS_FONT_DIR}/Roboto/Roboto-Regular.ttf"
    "Roboto Medium|${IS_FONT_DIR}/Roboto/Roboto-Medium.ttf"
    "Roboto SemiBold|${IS_FONT_DIR}/Roboto/Roboto-SemiBold.ttf"
    "JetBrains Mono|${IS_FONT_DIR}/JetBrainsMono/JetBrainsMono-Regular.ttf"
    "JetBrains Mono Medium|${IS_FONT_DIR}/JetBrainsMono/JetBrainsMono-Medium.ttf")
set(IS_EMBEDDED_FONTS ${CMAKE_CURRENT_BINARY_DIR}/InterstellarEmbeddedFonts.generated.cpp)
add_custom_command(OUTPUT ${IS_EMBEDDED_FONTS}
  COMMAND ${CMAKE_COMMAND} "-DFONTS=${IS_FONT_LIST}" -DOUT=${IS_EMBEDDED_FONTS}
          -P ${IS_COSMO_DIR}/cmake/embed_fonts.cmake
  DEPENDS ${IS_FONT_FILES} ${IS_COSMO_DIR}/cmake/embed_fonts.cmake
  VERBATIM
  COMMENT "interstellar_app: embedding cosmo's typeface into the binary (R-FONT-1)")

# ── the cosmo widgets Interstellar reuses as libraries (R-UI-5) — compiled, never copied ──
set(IS_COSMO_SOURCES
    ${IS_COSMO_DIR}/Theme.cpp
    ${IS_COSMO_DIR}/EmbeddedFonts.cpp
    ${IS_COSMO_DIR}/widgets/EditStackTabs.cpp
    ${IS_COSMO_DIR}/widgets/ParamPanel.cpp
    ${IS_COSMO_DIR}/widgets/SliderRow.cpp
    ${IS_COSMO_DIR}/widgets/MixerPanel.cpp
    ${IS_COSMO_DIR}/widgets/HueCurveEditor.cpp
    ${IS_COSMO_DIR}/widgets/CurvePanel.cpp
    ${IS_COSMO_DIR}/widgets/GradePanel.cpp
    ${IS_COSMO_DIR}/widgets/XformPanel.cpp
    ${IS_COSMO_DIR}/widgets/HistogramWidget.cpp
    ${IS_COSMO_DIR}/widgets/Filmstrip.cpp
    ${IS_COSMO_DIR}/widgets/SegmentedControl.cpp
    ${IS_COSMO_DIR}/widgets/PillButton.cpp
    ${IS_COSMO_DIR}/widgets/IconButton.cpp
    ${IS_COSMO_DIR}/widgets/ConfirmDialog.cpp
    ${IS_COSMO_DIR}/widgets/MenuStrip.cpp
    ${IS_COSMO_DIR}/widgets/ContextMenu.cpp
    ${IS_COSMO_DIR}/widgets/SettingsDialog.cpp
    ${IS_COSMO_DIR}/widgets/Icons.cpp
    ${IS_COSMO_DIR}/widgets/WidgetLog.cpp)

file(GLOB IS_APP_SOURCES CONFIGURE_DEPENDS ${IS_APP_DIR}/*.cpp ${IS_APP_DIR}/widgets/*.cpp)

add_library(interstellar_app STATIC
  ${IS_APP_SOURCES}
  ${IS_COSMO_SOURCES}
  ${IS_REPO_DIR}/core/Artboard/src/adapter/native/CairoTarget.cpp
  ${IS_EMBEDDED_FONTS})
target_include_directories(interstellar_app PUBLIC
  ${IS_APP_DIR}
  ${IS_COSMO_DIR}
  ${IS_REPO_DIR}/apps/interstellar/core
  ${IS_REPO_DIR}/core/Artboard/src
  ${IS_REPO_DIR}/core/Artboard/include
  ${IS_REPO_DIR}/core/ImageProcessing/src)
target_link_libraries(interstellar_app PUBLIC artboard_core arstro_image
                      PkgConfig::IS_CAIRO PkgConfig::IS_FREETYPE)
# ARTBOARD_CAIRO_FT: CairoTarget's text path is "a registered (embedded) face wins" (Artboard
# FR-22a). PUBLIC, because CairoTarget.h's registerFontMemory only exists under it.
target_compile_definitions(interstellar_app PUBLIC ARTBOARD_CAIRO_FT)

# ── the headless shot harness: one PNG per named state ──
add_executable(interstellar_app_shots ${IS_APP_DIR}/tests/shots/renderShots.cpp)
target_include_directories(interstellar_app_shots PRIVATE ${IS_APP_DIR}/tests)
target_link_libraries(interstellar_app_shots PRIVATE interstellar_app)
add_test(NAME interstellar_app_shots
         COMMAND interstellar_app_shots --outdir ${CMAKE_CURRENT_BINARY_DIR}/shots-ctest --check)

# ── the assembled-app UI tests over a FAKE AppHooks ──
add_executable(interstellar_app_ui_tests ${IS_APP_DIR}/tests/ui/uiTests.cpp)
target_include_directories(interstellar_app_ui_tests PRIVATE ${IS_APP_DIR}/tests)
target_link_libraries(interstellar_app_ui_tests PRIVATE interstellar_app)
add_test(NAME interstellar_app_ui COMMAND interstellar_app_ui_tests)
