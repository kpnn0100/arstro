# solaris-cc — argv/stdout only; every verb is the service's grammar, but the two faces of Faces.h
# (attach: a terminal on a running window; ntwb: the service on the Arstro Remote bridge).
add_executable(solaris-cc main.cpp
  Attach.cpp        # attach: a terminal on a running window (R-SVC-5) — builds no service
  NtwbAdapter.cpp   # ntwb: Solaris as an Arstro Remote app (R-SVC-7)
  NtwbMain.cpp)
target_link_libraries(solaris-cc PRIVATE solaris_host arstro_ntwb)
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
  add_executable(solaris_web_tokens webTokens.cpp)
  target_include_directories(solaris_web_tokens PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app ${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo
                             ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/Artboard/src ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/Artboard/include)
  target_link_libraries(solaris_web_tokens PRIVATE artboard_core)
  add_test(NAME solaris_web_tokens_current
    COMMAND sh -c "\"$<TARGET_FILE:solaris_web_tokens>\" | diff -u \"${CMAKE_CURRENT_SOURCE_DIR}/../web/css/tokens.css\" -")
endif()
