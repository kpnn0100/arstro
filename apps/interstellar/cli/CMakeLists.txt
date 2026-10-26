# interstellar-cc — the whole application without a window (R-SVC, R-API-2).
#
# It holds argv, stdout, the clock and the codecs, and NO behaviour. If a subcommand here needs
# something no Command expresses, that is a defect in the command set.
add_executable(interstellar-cc ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/HostSetup.cpp      # the codecs and paths every face builds the service with
  ${CMAKE_CURRENT_SOURCE_DIR}/NtwbAdapter.cpp    # ntwb: Interstellar as an Arstro Remote app (R-SVC-4)
  ${CMAKE_CURRENT_SOURCE_DIR}/NtwbMain.cpp)
if(TARGET interstellar_host)
  target_link_libraries(interstellar-cc PRIVATE interstellar_host arstro_ntwb)
else()
  target_link_libraries(interstellar-cc PRIVATE interstellar_core arstro_ntwb)
endif()
# `ntwb install` copies the page from the source tree, and cosmo's borrowed core and fonts beside it
target_compile_definitions(interstellar-cc PRIVATE
  INTERSTELLAR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
  COSMO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo")

# R-API-1, rung 4: the COMMITTED API document is what `interstellar-cc api` generates. A command, an
# event, a model field or an address added without regenerating it fails here:
#   build/apps/interstellar/cli/interstellar-cc api --json > apps/interstellar/docs/api.json
#   build/apps/interstellar/cli/interstellar-cc api --md   > apps/interstellar/docs/API.md
add_test(NAME interstellar_api_current
  COMMAND sh -c "\"$<TARGET_FILE:interstellar-cc>\" api --json | diff -u \"${CMAKE_CURRENT_SOURCE_DIR}/../docs/api.json\" - && \"$<TARGET_FILE:interstellar-cc>\" api --md | diff -u \"${CMAKE_CURRENT_SOURCE_DIR}/../docs/API.md\" -")

# R-RENDER-5: an Interstellar still == the same frame exported by Cosmo, pixel for pixel. Needs the
# ffmpeg binary (to make the photo and decode both PNGs) and cosmo-cc; skipped without them.
find_program(INTERSTELLAR_FFMPEG_BIN ffmpeg)
if(INTERSTELLAR_FFMPEG_BIN AND TARGET cosmo-cc AND TARGET interstellar_host)
  add_test(NAME interstellar_still_equals_cosmo
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/../tests/still_equals_cosmo.sh"
            "$<TARGET_FILE:interstellar-cc>" "$<TARGET_FILE:cosmo-cc>" "${INTERSTELLAR_FFMPEG_BIN}"
            "${CMAKE_CURRENT_BINARY_DIR}/still_equals_cosmo")
endif()

# R-SVC-4: the web face's API (what `ntwb install` writes as the manifest's `api`) is generated from the
# event table; the committed copy fails here when it drifts:
#   build/apps/interstellar/cli/interstellar-cc ntwb api > apps/interstellar/docs/ntwb-api.json
add_test(NAME interstellar_ntwb_api_current
  COMMAND sh -c "\"$<TARGET_FILE:interstellar-cc>\" ntwb api | diff -u \"${CMAKE_CURRENT_SOURCE_DIR}/../docs/ntwb-api.json\" -")

# R-SVC-4: the adapter over an in-memory bridge (the real service and client), and `ntwb install`
# into a scratch data dir — never the user's.
add_executable(interstellar_ntwb_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/ntwbAdapterTests.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/NtwbAdapter.cpp)
target_include_directories(interstellar_ntwb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(interstellar_ntwb_tests PRIVATE interstellar_core arstro_ntwb)
target_compile_definitions(interstellar_ntwb_tests PRIVATE
  INTERSTELLAR_CC="$<TARGET_FILE:interstellar-cc>"
  COSMO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo")
add_dependencies(interstellar_ntwb_tests interstellar-cc)
add_test(NAME interstellar_ntwb COMMAND interstellar_ntwb_tests)

# R-SVC-4: the page's tokens are Theme.h's, GENERATED (cosmo_web_tokens / solaris_web_tokens are the
# examples); the committed stylesheet fails here when Theme.h moved:
#   build/apps/interstellar/cli/interstellar_web_tokens > apps/interstellar/web/css/tokens.css
if(TARGET artboard_core)
  add_executable(interstellar_web_tokens ${CMAKE_CURRENT_SOURCE_DIR}/webTokens.cpp)
  target_include_directories(interstellar_web_tokens PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../app ${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/Artboard/src ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/Artboard/include)
  target_link_libraries(interstellar_web_tokens PRIVATE artboard_core)
  add_test(NAME interstellar_web_tokens_current
    COMMAND sh -c "\"$<TARGET_FILE:interstellar_web_tokens>\" | diff -u \"${CMAKE_CURRENT_SOURCE_DIR}/../web/css/tokens.css\" -")
endif()
