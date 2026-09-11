# Interstellar — the video editor: cuts and composites, and HOSTS a live Cosmo project as its
# colour authority (R-COSMO). See docs/architecture.md.
#
# Layering, strictly downward: interstellar_core (UI-free) -> the app (Artboard) -> the host.
add_subdirectory(core)

# interstellar-cc: front end #1 over InterstellarService — the whole application without a
# window (R-CLI-1). Its own directory so the GUI's source glob cannot swallow the CLI's main().
add_subdirectory(cli)

# ── The GUI + its two headless harnesses ────────────────────────────────────────────────
# Theme.h aliases cosmo's token namespaces and this build compiles cosmo/Theme.cpp — the
# arstrobench pattern, which costs nothing and cannot drift (arstro.design.rule §2). That is
# what gates the GUI on cosmo's presence rather than on GTK directly.
if(TARGET cosmo_core)
  set(INTERSTELLAR_UI_SOURCES
      ${CMAKE_CURRENT_SOURCE_DIR}/App.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/widgets/Monitor.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/widgets/TimelineView.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/widgets/LaneStack.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/widgets/Transport.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/widgets/WorkspaceBar.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/../cosmo/Theme.cpp)
  set(CAIRO_ADAPTER ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/src/adapter/native/CairoTarget.cpp)

  find_package(PkgConfig REQUIRED)
  pkg_check_modules(INTERSTELLAR_CAIRO REQUIRED IMPORTED_TARGET cairo)
  pkg_check_modules(INTERSTELLAR_FREETYPE REQUIRED IMPORTED_TARGET freetype2)

  # interstellar_shots — every named state to a PNG with no display. It has --size, --script and
  # --tree from the first commit: all three are cheap while a harness is new, and cosmo's absence
  # of them cost an agent a whole investigation each (see its skill's §6 "two real gaps").
  add_executable(interstellar_shots ${INTERSTELLAR_UI_SOURCES} ${CAIRO_ADAPTER}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/shots/renderShots.cpp)
  target_include_directories(interstellar_shots PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/include)
  target_link_libraries(interstellar_shots PRIVATE interstellar_core artboard_core
                        PkgConfig::INTERSTELLAR_CAIRO PkgConfig::INTERSTELLAR_FREETYPE)

  # interstellar_ui_tests — assertions over the ASSEMBLED app. cosmo_widget_tests-style isolated
  # widget tests cannot see R-G-1's compliance clause: it needs App's clock.
  add_executable(interstellar_ui_tests ${INTERSTELLAR_UI_SOURCES}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/uiTests.cpp)
  target_include_directories(interstellar_ui_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/Artboard/include)
  target_link_libraries(interstellar_ui_tests PRIVATE interstellar_core artboard_core)
  add_test(NAME interstellar_ui COMMAND interstellar_ui_tests)

  # The fixture-free shot set as a ctest case. `--check` is what makes it a TEST rather than a
  # smoke run: it fails a shot that came out uniform-colour, which is the one failure mode a
  # screenshot harness has.
  add_test(NAME interstellar_shots_headless
           COMMAND interstellar_shots --outdir ${CMAKE_CURRENT_BINARY_DIR}/shots-ctest --check)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shots-ctest)
else()
  message(STATUS "interstellar: GUI + shots skipped (Theme aliases cosmo's tokens; needs cosmo_core)")
endif()
