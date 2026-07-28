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
pkg_check_modules(GTK_LAYER_SHELL IMPORTED_TARGET gtk-layer-shell-0)  # optional (see above)

add_executable(arstro-android-shell
  shell/main.cpp
  shell/SurfaceHost.cpp
  shell/FrameClock.cpp
  theme/Fonts.cpp
  # Artboard's native Cairo adapter is NOT part of artboard_core (which is platform-free);
  # each host compiles it in, exactly as cosmo does.
  ${CMAKE_SOURCE_DIR}/Artboard/src/adapter/native/CairoTarget.cpp
)

# Locate assets/ (fonts, later icons/wallpaper) independent of the build dir / CWD, like cosmo.
target_compile_definitions(arstro-android-shell PRIVATE
  ANDROID_SHELL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# artboard_core (from the Artboard subproject, added earlier in the umbrella build)
# exposes its src/ and include/ dirs PUBLIC, so linking it provides <artboard/artboard.h>.
target_link_libraries(arstro-android-shell PRIVATE
  artboard_core
  PkgConfig::GTK3
  PkgConfig::CAIRO
  PkgConfig::FONTCONFIG
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
