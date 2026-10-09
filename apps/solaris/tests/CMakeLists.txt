# Solaris's faces, proved equal (R-SVC-5…7, arstro.rule §1). Registered after solaris-cc and the
# window, because the tests drive the ordinary product binaries — no test-only path.

if(TARGET solaris_control)
  # the control channel with no window: line order, `wait` holding the queue, and `attach` printing
  # exactly what `solaris-cc --watch --script` prints (tests/faces/controlTests.cpp)
  add_executable(solaris_control_tests faces/controlTests.cpp)
  target_link_libraries(solaris_control_tests PRIVATE solaris_control solaris_core)
  target_compile_definitions(solaris_control_tests PRIVATE SOLARIS_CC="$<TARGET_FILE:solaris-cc>")
  add_dependencies(solaris_control_tests solaris-cc)
  add_test(NAME solaris_control COMMAND solaris_control_tests)
endif()

# the web face's adapter over ntwb::MemoryTransport, and `ntwb install` into a scratch data dir
add_executable(solaris_ntwb_tests faces/ntwbAdapterTests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../cli/NtwbAdapter.cpp)
target_include_directories(solaris_ntwb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../cli)
target_link_libraries(solaris_ntwb_tests PRIVATE solaris_core arstro_ntwb)
target_compile_definitions(solaris_ntwb_tests PRIVATE SOLARIS_CC="$<TARGET_FILE:solaris-cc>"
                                                      COSMO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo")
add_dependencies(solaris_ntwb_tests solaris-cc)
add_test(NAME solaris_ntwb COMMAND solaris_ntwb_tests)

# R-SVC-6 — the equivalence test: one committed script through solaris-cc (headless) and through a
# LIVE window over its control socket; same outputs, same events, same stable state, same song file.
# It needs a display: with none it reports SKIPPED (exit 77), never a silent pass.
if(TARGET solaris)
  add_test(NAME solaris_equivalence
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/acceptance/run.sh $<TARGET_FILE:solaris> $<TARGET_FILE:solaris-cc>)
  set_tests_properties(solaris_equivalence PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
