# interstellar-cc — the whole application without a window (R-SVC, R-API-2).
#
# It holds argv, stdout, the clock and the codecs, and NO behaviour. If a subcommand here needs
# something no Command expresses, that is a defect in the command set.
add_executable(interstellar-cc ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
if(TARGET interstellar_host)
  target_link_libraries(interstellar-cc PRIVATE interstellar_host)
else()
  target_link_libraries(interstellar-cc PRIVATE interstellar_core)
endif()

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
