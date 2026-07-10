# cosmo — the Figma-exact photo-editor GUI, built on Artboard + its UI-free
# editing logic in core/ (cosmo_core).
#
# Native backend: GTK3 (window, file dialogs) + Cairo (the Artboard native
# render adapter) + Fontconfig (registers the vendored DM Sans / JetBrains Mono
# files at startup, see linux_main.cpp). Native-only; there is no web build.

# UI-free session/editing/history/decode logic lives in core/ (target cosmo_core).
add_subdirectory(core)

file(GLOB COSMO_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/widgets/*.cpp)

set(CAIRO_ADAPTER ${CMAKE_CURRENT_SOURCE_DIR}/../Artboard/src/adapter/native/CairoTarget.cpp)

add_executable(cosmo ${COSMO_SOURCES} ${CAIRO_ADAPTER})

target_include_directories(cosmo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../Artboard/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../Artboard/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../ImageProcessing/src)

target_link_libraries(cosmo PRIVATE artboard_core arstro_image cosmo_core)

# ── GTK3 (window, dialogs, Cairo, GdkPixbuf) via pkg-config ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
target_link_libraries(cosmo PRIVATE PkgConfig::GTK3)

# ── Fontconfig: registers the vendored fonts as app-private fonts at startup ──
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
target_link_libraries(cosmo PRIVATE PkgConfig::FONTCONFIG)

# Baked into the binary so it can find assets/fonts relative to itself
# regardless of the working directory it's launched from.
target_compile_definitions(cosmo PRIVATE
  COSMO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
