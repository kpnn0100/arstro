# cosmo_v2 — the Figma-exact editor GUI, sharing cosmo_core's editing logic.
#
# Same native backend as cosmo: GTK3 (window, file dialogs) + Cairo (the
# Artboard native render adapter) + Fontconfig (registers the vendored DM Sans
# / JetBrains Mono files at startup, see linux_main.cpp). The web build stays
# in build.sh, mirroring cosmo's split.

file(GLOB COSMO_V2_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/panels/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/widgets/*.cpp)
list(FILTER COSMO_V2_SOURCES EXCLUDE REGEX "web_main\\.cpp$")

set(CAIRO_ADAPTER ${CMAKE_CURRENT_SOURCE_DIR}/../Artboard/src/adapter/native/CairoTarget.cpp)

add_executable(cosmo_v2 ${COSMO_V2_SOURCES} ${CAIRO_ADAPTER})

target_include_directories(cosmo_v2 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../Artboard/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../Artboard/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../ImageProcessing/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../cosmo_core)

target_link_libraries(cosmo_v2 PRIVATE artboard_core arstro_image cosmo_core)

# ── GTK3 (window, dialogs, Cairo, GdkPixbuf) via pkg-config ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
target_link_libraries(cosmo_v2 PRIVATE PkgConfig::GTK3)

# ── Fontconfig: registers the vendored fonts as app-private fonts at startup ──
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
target_link_libraries(cosmo_v2 PRIVATE PkgConfig::FONTCONFIG)

# Baked into the binary so it can find assets/fonts relative to itself
# regardless of the working directory it's launched from.
target_compile_definitions(cosmo_v2 PRIVATE
  COSMO_V2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
