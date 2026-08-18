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
  # cosmo-cc: front end #2 over CosmoService — the same service this window hosts,
  # driven from a shell (R-SVC-1). Its own dir so the GUI's source glob below
  # cannot swallow the CLI's main().
  add_subdirectory(cli)

  file(GLOB COSMO_SOURCES CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
       ${CMAKE_CURRENT_SOURCE_DIR}/widgets/*.cpp)

  set(CAIRO_ADAPTER ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/src/adapter/native/CairoTarget.cpp)

  # ── P0.1 / D-7: the same app sources MINUS the GTK entry point ──
  # linux_main.cpp owns main(), and the glob above was the only list of app sources —
  # so no second main() could link the cosmo UI, and every headless UI harness (a shot
  # renderer, a UI test) was structurally impossible rather than merely unwritten. That
  # is the whole of D-7, and this one REMOVE_ITEM is the whole of its fix. Named and
  # shaped after apps/genesis/CMakeLists.txt's GENESIS_APP_NOMAIN, which has had it all
  # along — the ledger's rule is copy Genesis, do not invent.
  set(COSMO_APP_NOMAIN ${COSMO_SOURCES})
  list(REMOVE_ITEM COSMO_APP_NOMAIN ${CMAKE_CURRENT_SOURCE_DIR}/linux_main.cpp)

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

  # ── P0.2: cosmo_shots — the headless screenshot harness ──────────────────────────
  # The real App, the real Cairo adapter, an image surface instead of a window: every
  # named app state rendered to a PNG with no display, so a UI change can be SEEN before
  # it ships instead of asserted about. Mirrors genesis_shots (tests/renderShots.cpp).
  #
  # It links the SAME libraries the app links, and that is load-bearing rather than tidy:
  # ARSTRO_ENABLE_THREADS and ARSTRO_GL_COMPUTE arrive PUBLICly from arstro_image, so
  # linking it is what keeps this target's defines identical to the ones cosmo_core and
  # the engine were built with. A harness hand-compiled without them is an ODR violation
  # that segfaults in ~EditSession at exit and reads as a teardown bug (decisions log,
  # 2026-08-16 U1.2).
  #
  # GTK3 is here for GdkPixbuf only — NativeImageDecoder and ExportWriter are GdkPixbuf-
  # based and live in the app layer (R-SVC-7). No gtk_init() is ever called, so no display
  # and no X server is needed, exactly as in cli/CMakeLists.txt. Fontconfig is linked so
  # the shots use the app's OWN vendored DM Sans / JetBrains Mono rather than whatever the
  # host's default sans happens to be — a shot in the wrong typeface measures the wrong
  # text widths and would report overflow bugs that do not exist.
  add_executable(cosmo_shots ${COSMO_APP_NOMAIN} ${CAIRO_ADAPTER}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/shots/renderShots.cpp)
  target_include_directories(cosmo_shots PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/ImageProcessing/src)
  target_link_libraries(cosmo_shots PRIVATE artboard_core arstro_image cosmo_core
                        ${CMAKE_DL_LIBS} PkgConfig::GTK3 PkgConfig::COSMO_FONTCONFIG)
  target_compile_definitions(cosmo_shots PRIVATE
    COSMO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  # The fixture-free subset as a ctest case: home (empty, with recents, with the settings
  # modal), the empty editor and the open transition need no display and no images on
  # disk, so they run anywhere the app builds. `--check` is what makes it a TEST rather
  # than a smoke run — it fails the PNG that came out uniform-colour, which is the one
  # failure mode a screenshot harness has (a blank shot is worse than no shot).
  #
  # The project shots are deliberately NOT in it: the editor-with-a-project states need
  # real RAW files, and there are none in the repo (nor should there be — they are ~26 MB
  # each). Run those by hand:
  #   cosmo_shots --outdir /tmp/shots --images <a>.RAF,<b>.RAF
  add_test(NAME cosmo_shots_headless
           COMMAND cosmo_shots --outdir ${CMAKE_CURRENT_BINARY_DIR}/shots-ctest --check)
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
