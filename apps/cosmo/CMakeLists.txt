# cosmo — the Figma-exact photo-editor GUI, built on Artboard + its UI-free
# editing logic in core/ (cosmo_core).
#
# Native backend: GTK3 (window, file dialogs) + Cairo (the Artboard native
# render adapter) + Fontconfig (registers the vendored DM Sans / JetBrains Mono
# files at startup, see linux_main.cpp). Native-only; there is no web build.

# UI-free session/editing/history/decode logic lives in core/ (target cosmo_core).
# Optional itself — needs pkg-config + GTK3, see core/CMakeLists.txt — so the
# `cosmo` app below only builds when cosmo_core actually exists (e.g. skipped on
# native Windows without MSYS2), rather than hard-failing this whole subtree.
add_subdirectory(core)

if(TARGET cosmo_core)
  file(GLOB COSMO_SOURCES CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
       ${CMAKE_CURRENT_SOURCE_DIR}/widgets/*.cpp)

  set(CAIRO_ADAPTER ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/src/adapter/native/CairoTarget.cpp)

  add_executable(cosmo ${COSMO_SOURCES} ${CAIRO_ADAPTER})

  target_include_directories(cosmo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/ImageProcessing/src)

  target_link_libraries(cosmo PRIVATE artboard_core arstro_image cosmo_core)

  # OmpPin.cpp resolves omp_set_num_threads dynamically (dlsym / GetProcAddress) rather
  # than linking OpenMP, so the app carries no OpenMP dependency and the pin is a no-op
  # where LibRaw was built without it. Empty on platforms whose libc already has dlsym.
  target_link_libraries(cosmo PRIVATE ${CMAKE_DL_LIBS})

  # ── GTK3 (window, dialogs, Cairo, GdkPixbuf) via pkg-config ── find_package(PkgConfig)
  # must be re-run in THIS scope even though core/CMakeLists.txt already did it:
  # its result variables (e.g. PKG_CONFIG_VERSION, which pkg_check_modules' internal
  # caching check depends on) are directory-scoped, not cache variables, so they
  # don't inherit back up from a child add_subdirectory() scope to this parent one
  # — only the module's macros (pkg_check_modules itself) are global.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  target_link_libraries(cosmo PRIVATE PkgConfig::GTK3)

  # ── Fontconfig: registers the vendored fonts as app-private fonts at startup ──
  # Own prefix (not "FONTCONFIG") so this doesn't collide with launcher/android-
  # shell's independent fontconfig check when both are configured in the same run.
  pkg_check_modules(COSMO_FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
  target_link_libraries(cosmo PRIVATE PkgConfig::COSMO_FONTCONFIG)

  # Baked into the binary so it can find assets/fonts relative to itself
  # regardless of the working directory it's launched from.
  target_compile_definitions(cosmo PRIVATE
    COSMO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
else()
  message(STATUS "cosmo: app skipped (needs cosmo_core; see core/CMakeLists.txt)")
endif()

# ── Widget unit tests: curve/mixer point-editor hit-testing ──
# Device-free (no GTK/Cairo, no cosmo_core): the editors are Artboard Segments,
# so the tests drive their gesture entry point against artboard_core and the few
# self-contained widget TUs they need. Verifies the anchor pick radius
# (metrics::anchorHitRadius) and nearest-within-radius resolution. Plain assert().
add_executable(cosmo_widget_tests
  widgets/tests/widgetTests.cpp
  widgets/HueCurveEditor.cpp
  widgets/CurvePanel.cpp
  widgets/SegmentedControl.cpp
  widgets/PillButton.cpp
  widgets/IconButton.cpp
  widgets/ExportDialog.cpp
  widgets/Filmstrip.cpp
  widgets/HomeScreen.cpp
  widgets/ProjectCard.cpp
  widgets/SplashScreen.cpp
  Theme.cpp
  widgets/Icons.cpp)
target_include_directories(cosmo_widget_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../core/ImageProcessing/src)
target_link_libraries(cosmo_widget_tests PRIVATE artboard_core)
add_test(NAME cosmo_widget COMMAND cosmo_widget_tests)
