# arstro-shell — the Arstro touch stack: a shell that owns the screen, an SDK apps are written
# against, and the apps themselves (apps/launcher/shell/REQUIREMENTS.md).
#
#   arstro_shell_core   the service: commands in, model + events out. No display, no toolkit.
#   arstro_kit          tokens, fonts, icons and touch controls shared by the shell and every app.
#   arstro_app          the app SDK: an app is an Artboard tree; this puts it on the shell's screen.
#   arstro-shell        the host: SDL3 window or headless, compositor, app socket.
#   arstro-shell-cc     the shell with no screen: the API document, scripts, a control client.
#   arstro-pm           pack, install, list and remove app bundles.
#
# POSIX today (a unix socket and shared memory carry the app protocol). The seam for Windows is
# protocol/Channel.* — see docs/architecture.md.

find_package(PkgConfig REQUIRED)
pkg_check_modules(SHELL_CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(SHELL_FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(SHELL_SDL3 IMPORTED_TARGET sdl3)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(SHELL_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COSMO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo)
set(ARTBOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/Artboard)

# ── the service ──────────────────────────────────────────────────────────────────────────────
add_library(arstro_shell_core STATIC
  core/Command.cpp core/Navigator.cpp core/ShellService.cpp core/AppRegistry.cpp)
target_include_directories(arstro_shell_core PUBLIC ${SHELL_DIR})

# ── the kit ──────────────────────────────────────────────────────────────────────────────────
# Icons: kit/icons-src/*.svg -> one generated header of path tables (not committed).
set(KIT_ICON_HEADER ${CMAKE_CURRENT_BINARY_DIR}/gen/GeneratedIcons.h)
file(GLOB KIT_ICON_SVGS CONFIGURE_DEPENDS ${SHELL_DIR}/kit/icons-src/*.svg)
add_custom_command(OUTPUT ${KIT_ICON_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen
  COMMAND ${Python3_EXECUTABLE} ${SHELL_DIR}/kit/icons-svg-to-header.py ${SHELL_DIR}/kit/icons-src ${KIT_ICON_HEADER}
  DEPENDS ${SHELL_DIR}/kit/icons-svg-to-header.py ${KIT_ICON_SVGS}
  COMMENT "arstro kit: generating icon path tables" VERBATIM)
add_custom_target(arstro_kit_icons DEPENDS ${KIT_ICON_HEADER})

# Fonts: the family's faces, compiled into the binary by cosmo's own generator (its R-FONT-1), so
# the touch stack and the desktop apps cannot end up drawing different type.
set(KIT_FONT_DIR ${COSMO_DIR}/assets/fonts)
set(KIT_FONT_LIST
    "Roboto|${KIT_FONT_DIR}/Roboto/Roboto-Regular.ttf"
    "Roboto Medium|${KIT_FONT_DIR}/Roboto/Roboto-Medium.ttf"
    "Roboto SemiBold|${KIT_FONT_DIR}/Roboto/Roboto-SemiBold.ttf"
    "JetBrains Mono|${KIT_FONT_DIR}/JetBrainsMono/JetBrainsMono-Regular.ttf")
set(KIT_EMBEDDED_FONTS ${CMAKE_CURRENT_BINARY_DIR}/gen/EmbeddedFonts.generated.cpp)
add_custom_command(OUTPUT ${KIT_EMBEDDED_FONTS}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen
  COMMAND ${CMAKE_COMMAND} "-DFONTS=${KIT_FONT_LIST}" -DOUT=${KIT_EMBEDDED_FONTS} -P ${COSMO_DIR}/cmake/embed_fonts.cmake
  DEPENDS ${COSMO_DIR}/cmake/embed_fonts.cmake
          ${KIT_FONT_DIR}/Roboto/Roboto-Regular.ttf ${KIT_FONT_DIR}/Roboto/Roboto-Medium.ttf
          ${KIT_FONT_DIR}/Roboto/Roboto-SemiBold.ttf ${KIT_FONT_DIR}/JetBrainsMono/JetBrainsMono-Regular.ttf
  VERBATIM COMMENT "arstro kit: embedding the typeface")

add_library(arstro_kit STATIC
  kit/Fonts.cpp
  ${COSMO_DIR}/EmbeddedFonts.cpp
  ${KIT_EMBEDDED_FONTS}
  ${ARTBOARD_DIR}/src/adapter/native/CairoTarget.cpp)
add_dependencies(arstro_kit arstro_kit_icons)
# ARTBOARD_CAIRO_FT is PUBLIC: everything that includes CairoTarget.h must see the same class.
target_compile_definitions(arstro_kit PUBLIC ARTBOARD_CAIRO_FT)
target_include_directories(arstro_kit PUBLIC ${SHELL_DIR} ${SHELL_DIR}/kit ${CMAKE_CURRENT_BINARY_DIR}/gen ${COSMO_DIR})
target_link_libraries(arstro_kit PUBLIC artboard_core PkgConfig::SHELL_CAIRO PkgConfig::SHELL_FREETYPE)

# ── the app SDK ──────────────────────────────────────────────────────────────────────────────
add_library(arstro_app STATIC sdk/App.cpp protocol/Channel.cpp)
target_include_directories(arstro_app PUBLIC ${SHELL_DIR}/sdk)
target_link_libraries(arstro_app PUBLIC arstro_kit)

# ── the shell without a screen ───────────────────────────────────────────────────────────────
add_executable(arstro-shell-cc cli/main.cpp protocol/Channel.cpp)
target_link_libraries(arstro-shell-cc PRIVATE arstro_shell_core)

add_executable(arstro-pm pm/main.cpp)
target_link_libraries(arstro-pm PRIVATE arstro_shell_core)

# ── apps ─────────────────────────────────────────────────────────────────────────────────────
# Each app is built straight into a bundle directory under <build>/bundles, laid out exactly as an
# installed one, so `arstro-shell --apps <build>/bundles` runs the tree as built.
set(SHELL_BUNDLES ${CMAKE_CURRENT_BINARY_DIR}/bundles)
add_executable(arstro-settings apps/settings/main.cpp apps/settings/Controls.h)
target_link_libraries(arstro-settings PRIVATE arstro_app)
set_target_properties(arstro-settings PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${SHELL_BUNDLES}/arstro.settings/bin)
add_custom_command(TARGET arstro-settings POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  ${SHELL_DIR}/apps/settings/manifest.arstro ${SHELL_BUNDLES}/arstro.settings/manifest.arstro VERBATIM)
add_executable(arstro-clock apps/clock/main.cpp)
target_link_libraries(arstro-clock PRIVATE arstro_app)
set_target_properties(arstro-clock PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${SHELL_BUNDLES}/arstro.clock/bin)
add_custom_command(TARGET arstro-clock POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  ${SHELL_DIR}/apps/clock/manifest.arstro ${SHELL_BUNDLES}/arstro.clock/manifest.arstro VERBATIM)

# ── the host ─────────────────────────────────────────────────────────────────────────────────
if(SHELL_SDL3_FOUND)
  add_executable(arstro-shell host/main.cpp host/ShellHost.cpp host/Scene.cpp host/Views.cpp protocol/Channel.cpp)
  target_link_libraries(arstro-shell PRIVATE arstro_shell_core arstro_kit PkgConfig::SHELL_SDL3)
  add_dependencies(arstro-shell arstro-settings arstro-clock)
else()
  message(WARNING "arstro-shell: SDL3 not found — the host is skipped; the service, CLI, SDK and apps still build.")
endif()

# ── tests ────────────────────────────────────────────────────────────────────────────────────
add_executable(arstro_shell_core_tests tests/coreTests.cpp)
target_link_libraries(arstro_shell_core_tests PRIVATE arstro_shell_core)
add_test(NAME arstro_shell_core COMMAND arstro_shell_core_tests)
# The committed API document must equal what the tables generate (arstro.rule §5).
add_test(NAME arstro_shell_api_doc COMMAND arstro_shell_core_tests --api-check ${SHELL_DIR}/docs)

add_executable(arstro_shell_scene_tests tests/sceneTests.cpp host/Scene.cpp)
target_link_libraries(arstro_shell_scene_tests PRIVATE arstro_shell_core arstro_kit)
add_test(NAME arstro_shell_scene COMMAND arstro_shell_scene_tests)

add_test(NAME arstro_shell_pm
  COMMAND ${SHELL_DIR}/tests/pm.sh $<TARGET_FILE:arstro-pm> ${SHELL_BUNDLES} ${CMAKE_CURRENT_BINARY_DIR}/pmtest)

if(SHELL_SDL3_FOUND)
  # The real host, headless, with real app processes attached: launch, gestures, lifecycle.
  add_test(NAME arstro_shell_live
    COMMAND ${SHELL_DIR}/tests/live.sh $<TARGET_FILE:arstro-shell> ${SHELL_BUNDLES} ${CMAKE_CURRENT_BINARY_DIR}/live)
endif()
