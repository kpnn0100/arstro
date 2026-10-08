# solaris_app — Solaris's FRONT END: a platform-free Artboard Segment tree drawn from AppModel,
# reporting intent as text command lines through AppHooks. It links NO service (R-G-4): the host and
# the tests bind the hooks. Cosmo's widgets are COMPILED from apps/cosmo (R-UI-4, linked not copied),
# Interstellar's header-only helpers are INCLUDED from apps/interstellar/app/widgets, and cosmo's
# typeface is embedded the way cosmo does it (design rule §2.3).
find_package(PkgConfig REQUIRED)
pkg_check_modules(SL_CAIRO IMPORTED_TARGET cairo)
pkg_check_modules(SL_FREETYPE IMPORTED_TARGET freetype2)
if(NOT SL_CAIRO_FOUND OR NOT SL_FREETYPE_FOUND OR NOT TARGET artboard_core)
  message(STATUS "solaris_app: skipped (needs Artboard, Cairo and FreeType)")
  return()
endif()

set(SL_APP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SL_REPO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(SL_COSMO_DIR ${SL_REPO_DIR}/apps/cosmo)

set(SL_FONT_DIR ${SL_COSMO_DIR}/assets/fonts)
set(SL_FONT_FILES
    ${SL_FONT_DIR}/Roboto/Roboto-Regular.ttf
    ${SL_FONT_DIR}/Roboto/Roboto-Medium.ttf
    ${SL_FONT_DIR}/Roboto/Roboto-SemiBold.ttf
    ${SL_FONT_DIR}/JetBrainsMono/JetBrainsMono-Regular.ttf
    ${SL_FONT_DIR}/JetBrainsMono/JetBrainsMono-Medium.ttf)
set(SL_FONT_LIST
    "Roboto|${SL_FONT_DIR}/Roboto/Roboto-Regular.ttf"
    "Roboto Medium|${SL_FONT_DIR}/Roboto/Roboto-Medium.ttf"
    "Roboto SemiBold|${SL_FONT_DIR}/Roboto/Roboto-SemiBold.ttf"
    "JetBrains Mono|${SL_FONT_DIR}/JetBrainsMono/JetBrainsMono-Regular.ttf"
    "JetBrains Mono Medium|${SL_FONT_DIR}/JetBrainsMono/JetBrainsMono-Medium.ttf")
set(SL_EMBEDDED_FONTS ${CMAKE_CURRENT_BINARY_DIR}/SolarisEmbeddedFonts.generated.cpp)
add_custom_command(OUTPUT ${SL_EMBEDDED_FONTS}
  COMMAND ${CMAKE_COMMAND} "-DFONTS=${SL_FONT_LIST}" -DOUT=${SL_EMBEDDED_FONTS}
          -P ${SL_COSMO_DIR}/cmake/embed_fonts.cmake
  DEPENDS ${SL_FONT_FILES} ${SL_COSMO_DIR}/cmake/embed_fonts.cmake
  VERBATIM
  COMMENT "solaris_app: embedding cosmo's typeface into the binary")

set(SL_COSMO_SOURCES
    ${SL_COSMO_DIR}/Theme.cpp
    ${SL_COSMO_DIR}/EmbeddedFonts.cpp
    ${SL_COSMO_DIR}/widgets/ConfirmDialog.cpp
    ${SL_COSMO_DIR}/widgets/ContextMenu.cpp
    ${SL_COSMO_DIR}/widgets/Icons.cpp
    ${SL_COSMO_DIR}/widgets/SliderRow.cpp)

file(GLOB SL_APP_SOURCES CONFIGURE_DEPENDS ${SL_APP_DIR}/*.cpp ${SL_APP_DIR}/widgets/*.cpp)
add_library(solaris_app STATIC
  ${SL_APP_SOURCES}
  ${SL_COSMO_SOURCES}
  ${SL_REPO_DIR}/core/Artboard/src/adapter/native/CairoTarget.cpp
  ${SL_EMBEDDED_FONTS})
target_include_directories(solaris_app PUBLIC
  ${SL_APP_DIR}
  ${SL_COSMO_DIR}
  ${SL_APP_DIR}/../core/service
  ${SL_APP_DIR}/../engine
  ${SL_REPO_DIR}/core/Artboard/src
  ${SL_REPO_DIR}/core/Artboard/include)
target_link_libraries(solaris_app PUBLIC artboard_core PkgConfig::SL_CAIRO PkgConfig::SL_FREETYPE)
target_compile_definitions(solaris_app PUBLIC ARTBOARD_CAIRO_FT)

# the shots and the UI tests run the REAL service behind the hooks (tests/Rig.h)
add_executable(solaris_app_shots tests/shots/renderShots.cpp)
target_link_libraries(solaris_app_shots PRIVATE solaris_app solaris_core)
add_test(NAME solaris_app_shots COMMAND solaris_app_shots --outdir ${CMAKE_CURRENT_BINARY_DIR}/shots-ctest --check)

add_executable(solaris_app_ui_tests tests/ui/uiTests.cpp)
target_link_libraries(solaris_app_ui_tests PRIVATE solaris_app solaris_core)
add_test(NAME solaris_app_ui COMMAND solaris_app_ui_tests)
