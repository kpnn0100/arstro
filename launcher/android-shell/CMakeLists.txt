# arstro-android-shell — the Android-theme touch shell host.
#
# GTK3 + Cairo (Artboard's native CairoTarget) multi-surface host, matching the
# proven cosmo host. Added to the umbrella build via add_subdirectory from the root
# CMakeLists (guarded by ARSTRO_BUILD_ANDROID_SHELL).
#
# gtk-layer-shell is OPTIONAL at M1.1 (a plain window) and REQUIRED from M1.2 for the
# real layer-shell surfaces (status bar, shade panels, edge strips). When it is absent
# the build still succeeds (plain-window mode) but warns, and HAVE_GTK_LAYER_SHELL is
# left undefined so the code can #ifdef the layer-shell paths.

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)  # GDBus (system services, M3.1)
pkg_check_modules(GTK_LAYER_SHELL IMPORTED_TARGET gtk-layer-shell-0)  # optional (see above)

add_executable(arstro-android-shell
  shell/main.cpp
  shell/SurfaceHost.cpp
  shell/FrameClock.cpp
  theme/Fonts.cpp
  system/DbusSystemServices.cpp
  notifyd/NotifyService.cpp
  compositor/KWinBridge.cpp
  compositor/GnomeBridge.cpp
  # Artboard's native Cairo adapter is NOT part of artboard_core (which is platform-free);
  # each host compiles it in, exactly as cosmo does.
  ${CMAKE_SOURCE_DIR}/Artboard/src/adapter/native/CairoTarget.cpp
)

# Locate assets/ (fonts, later icons/wallpaper) independent of the build dir / CWD, like cosmo.
target_compile_definitions(arstro-android-shell PRIVATE
  ANDROID_SHELL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# ── Icon codegen (M2.3): assets/icons-src/*.svg -> generated cubic-path header ──
# A build step runs the Python codegen; the header lands in the build tree (not committed).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(ICON_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons-src)
set(ICON_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons-svg-to-header.py)
set(ICON_GEN_HEADER ${CMAKE_CURRENT_BINARY_DIR}/gen/theme/icons/GeneratedIcons.h)
file(GLOB ICON_SVGS CONFIGURE_DEPENDS ${ICON_SRC_DIR}/*.svg)
add_custom_command(
  OUTPUT ${ICON_GEN_HEADER}
  COMMAND ${Python3_EXECUTABLE} ${ICON_SCRIPT} ${ICON_SRC_DIR} ${ICON_GEN_HEADER}
  DEPENDS ${ICON_SCRIPT} ${ICON_SVGS}
  COMMENT "Generating Android-theme icon path tables from SVGs"
  VERBATIM)
add_custom_target(android-shell-icons DEPENDS ${ICON_GEN_HEADER})
add_dependencies(arstro-android-shell android-shell-icons)
target_include_directories(arstro-android-shell PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)

# artboard_core (from the Artboard subproject, added earlier in the umbrella build)
# exposes its src/ and include/ dirs PUBLIC, so linking it provides <artboard/artboard.h>.
target_link_libraries(arstro-android-shell PRIVATE
  artboard_core
  PkgConfig::GTK3
  PkgConfig::CAIRO
  PkgConfig::FONTCONFIG
  PkgConfig::GIO
)
target_include_directories(arstro-android-shell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(GTK_LAYER_SHELL_FOUND)
  target_link_libraries(arstro-android-shell PRIVATE PkgConfig::GTK_LAYER_SHELL)
  target_compile_definitions(arstro-android-shell PRIVATE HAVE_GTK_LAYER_SHELL=1)
  message(STATUS "arstro-android-shell: gtk-layer-shell found — real layer surfaces enabled")
else()
  message(WARNING "arstro-android-shell: gtk-layer-shell NOT found — building in plain-window mode "
                  "(fine for M1.1). Install libgtk-layer-shell-dev / gtk-layer-shell for M1.2+ surfaces.")
endif()

# ── Install + packaging (M9) ──
include(GNUInstallDirs)
install(TARGETS arstro-android-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/arstro-android-shell/assets
        PATTERN "icons-src" EXCLUDE PATTERN "*.py" EXCLUDE)
install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES packaging/sessions/android-shell-plasma.desktop
              packaging/sessions/android-shell-gnome.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions)
install(PROGRAMS packaging/android-shell-plasma-session.sh
        DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME arstro-android-shell-plasma-session)
install(FILES packaging/systemd/arstro-android-shell.service
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user)
install(DIRECTORY kwin-script/ DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/scripts/arstro-android-shell/contents/code
        FILES_MATCHING PATTERN "*.js")
install(FILES kwin-script/metadata.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/scripts/arstro-android-shell)
install(DIRECTORY gnome-extension/ DESTINATION ${CMAKE_INSTALL_DATADIR}/gnome-shell/extensions/android-shell@arstro)

# CPack (DEB + RPM) — enabled only when building android-shell standalone as the top project.
if(CMAKE_PROJECT_NAME STREQUAL "arstro-android-shell" OR ARSTRO_ANDROID_SHELL_PACKAGING)
  set(CPACK_PACKAGE_NAME "arstro-android-shell")
  set(CPACK_PACKAGE_VERSION "0.1.0")
  set(CPACK_PACKAGE_CONTACT "arstro <noreply@arstro>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Android-theme touch shell for GNOME/Plasma")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgtk-3-0, libcairo2, libglib2.0-0, fontconfig")
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gtk-layer-shell, kwin-wayland | gnome-shell")
  set(CPACK_RPM_PACKAGE_REQUIRES "gtk3, cairo, glib2, fontconfig")
  set(CPACK_GENERATOR "DEB")
  include(CPack)
endif()
