# solaris-cc — argv/stdout only; every verb is the service's grammar.
add_executable(solaris-cc main.cpp)
target_link_libraries(solaris-cc PRIVATE solaris_host)

# R-API-1 (rung 4): the committed API document must be exactly what the code generates.
add_test(NAME solaris_api_current
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DDOCS=${CMAKE_CURRENT_SOURCE_DIR}/../docs
          -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/api_current.cmake)
# R-SVC-8: the CLI as an agent drives it — scripts and `shell`, line numbers, unsaved edits (exit 4).
add_test(NAME solaris_cli_session
  COMMAND ${CMAKE_COMMAND} -DCC=$<TARGET_FILE:solaris-cc> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_session
          -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/cli_session.cmake)
