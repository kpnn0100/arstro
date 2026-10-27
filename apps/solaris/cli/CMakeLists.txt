# solaris-cc — argv/stdout only; every verb is the service's grammar, but the two faces of Faces.h
# (attach: a terminal on a running window; ntwb: the service on the Arstro Remote bridge).
add_executable(solaris-cc main.cpp
  Attach.cpp        # attach: a terminal on a running window (R-SVC-5) — builds no service
  NtwbAdapter.cpp   # ntwb: Solaris as an Arstro Remote app (R-SVC-7)
  NtwbMain.cpp)
target_link_libraries(solaris-cc PRIVATE solaris_host arstro_ntwb)
# the web face's pictures (videoFrame / videoThumb): JPEG through GdkPixbuf, as interstellar-cc and cosmo-cc encode
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SOLARIS_CC_PIXBUF IMPORTED_TARGET gdk-pixbuf-2.0)
endif()
if(SOLARIS_CC_PIXBUF_FOUND)
  target_link_libraries(solaris-cc PRIVATE PkgConfig::SOLARIS_CC_PIXBUF)
  target_compile_definitions(solaris-cc PRIVATE SOLARIS_HAVE_PIXBUF=1)
else()
  message(STATUS "solaris-cc: no gdk-pixbuf — the web face's video pictures are refused (videoFrame / videoThumb)")
endif()
# `ntwb install` copies the web UI, and what it borrows from cosmo's (its reactive core, its fonts)
target_compile_definitions(solaris-cc PRIVATE SOLARIS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
                                              COSMO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo")

# R-API-1 (rung 4): the committed API document must be exactly what the code generates.
add_test(NAME solaris_api_current
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DDOCS=${CMAKE_CURRENT_SOURCE_DIR}/../docs
          -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/api_current.cmake)
# R-SVC-8: the CLI as an agent drives it — scripts and `shell`, line numbers, unsaved edits (exit 4).
add_test(NAME solaris_cli_session
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_session
          -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/cli_session.cmake)
# R-SVC-9: the goal, measured — a whole song made with solaris-cc alone, rendered and measured.
find_program(SOLARIS_PYTHON3 python3)
# R-SVC-12: the EDM track, measured as a master — named outside `-R solaris` (a minute's render of a 2:17 song in a
# Debug build): `ctest -L long`
add_test(NAME edm_track_measured
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DDEMO=${CMAKE_CURRENT_SOURCE_DIR}/../demo/edm
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/demo_edm -DPY=${SOLARIS_PYTHON3} -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/demo_edm.cmake)
set_tests_properties(edm_track_measured PROPERTIES TIMEOUT 1200 LABELS "long")
# R-SVC-13: the trap / EDM track — its drop measured (the gap, half time, the 808's glides, the hats' rolls): `ctest -L long`
add_test(NAME trap_track_measured
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DDEMO=${CMAKE_CURRENT_SOURCE_DIR}/../demo/trap
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/demo_trap -DPY=${SOLARIS_PYTHON3} -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/demo_trap.cmake)
set_tests_properties(trap_track_measured PROPERTIES TIMEOUT 1200 LABELS "long")
# R-SVC-14: the progressive house track — its melody read back from the lead and the piano, note by note: `ctest -L long`
add_test(NAME progressive_track_measured
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DDEMO=${CMAKE_CURRENT_SOURCE_DIR}/../demo/progressive
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/demo_progressive -DPY=${SOLARIS_PYTHON3} -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/demo_progressive.cmake)
set_tests_properties(progressive_track_measured PROPERTIES TIMEOUT 1200 LABELS "long")
# R-SVC-15: the Trap Nation–style track — the chords' chop on the song's grid, the chops' vowels and notes: `ctest -L long`
add_test(NAME futurebass_track_measured
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DDEMO=${CMAKE_CURRENT_SOURCE_DIR}/../demo/futurebass
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/demo_futurebass -DPY=${SOLARIS_PYTHON3} -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/demo_futurebass.cmake)
set_tests_properties(futurebass_track_measured PROPERTIES TIMEOUT 1200 LABELS "long")
# R-SVC-16: the NCS-style track — the floor, the bass's bounce, the pump on the grid, the hook as written and as heard
add_test(NAME futurehouse_track_measured
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DDEMO=${CMAKE_CURRENT_SOURCE_DIR}/../demo/futurehouse
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/demo_futurehouse -DPY=${SOLARIS_PYTHON3} -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/demo_futurehouse.cmake)
set_tests_properties(futurehouse_track_measured PROPERTIES TIMEOUT 1200 LABELS "long")
# R-SVC-17: the story track — its theme read from every instrument that plays it, its harmony heard: `ctest -L long`
add_test(NAME story_track_measured
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DDEMO=${CMAKE_CURRENT_SOURCE_DIR}/../demo/storytrack
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/demo_storytrack -DPY=${SOLARIS_PYTHON3} -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/demo_storytrack.cmake)
set_tests_properties(story_track_measured PROPERTIES TIMEOUT 1200 LABELS "long")

add_test(NAME solaris_demo_canon
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DDEMO=${CMAKE_CURRENT_SOURCE_DIR}/../demo/canon
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/demo_canon -DPY=${SOLARIS_PYTHON3}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/demo_canon.cmake)

# R-SVC-7: the committed NTWB API description is what `solaris-cc ntwb api` generates — a method,
# a state key or an event added in code without regenerating it fails here.
#   build/apps/solaris/cli/solaris-cc ntwb api > apps/solaris/docs/ntwb-api.json
add_test(NAME solaris_ntwb_api_current
  COMMAND sh -c "\"$<TARGET_FILE:solaris-cc>\" ntwb api | diff -u \"${CMAKE_CURRENT_SOURCE_DIR}/../docs/ntwb-api.json\" -")

# the web UI's design tokens, printed FROM Theme.h with Solaris's teal installed (cosmo's
# cosmo_web_tokens, for Solaris): web/css/tokens.css is its committed output.
#   build/apps/solaris/cli/solaris_web_tokens > apps/solaris/web/css/tokens.css
if(TARGET artboard_core)
  # cosmo's Theme.cpp compiled straight in (the app's pattern, app/CMakeLists.txt): sharedTheme() is a token function too
  add_executable(solaris_web_tokens webTokens.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo/Theme.cpp)
  target_include_directories(solaris_web_tokens PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app ${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo
                             ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/Artboard/src ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/Artboard/include)
  target_link_libraries(solaris_web_tokens PRIVATE artboard_core solaris_strings) # the app's Theme.h names its words (R-ACCESS-2)
  add_test(NAME solaris_web_tokens_current
    COMMAND sh -c "\"$<TARGET_FILE:solaris_web_tokens>\" | diff -u \"${CMAKE_CURRENT_SOURCE_DIR}/../web/css/tokens.css\" -")
endif()
