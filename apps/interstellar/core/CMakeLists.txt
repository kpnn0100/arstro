# interstellar_core — UI-free Interstellar logic: the .isp project, the timeline, the parameter
# address space, automation, bindings, the evaluator, the compositor and the service every front
# end binds to. No Artboard, no GTK, no codec, no OS path handling (R-SCOPE-2, R-SVC-7).
#
# It links gene_core (the shared binding language) and NOTHING else. The rack — the hosted Cosmo
# project that owns colour — arrives through the RackAccess seam instead of a link dependency,
# which is the one place the architecture was amended during implementation: carrying
# cosmo::AppModel by value would have put GTK3 on every file in this library and every test
# (see service/AppModel.h's note on R-COSMO-4). The real implementation lives in the app layer.
file(GLOB INTERSTELLAR_CORE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/service/*.cpp)

add_library(interstellar_core STATIC ${INTERSTELLAR_CORE_SOURCES})
target_include_directories(interstellar_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(interstellar_core PUBLIC gene_core)

# ── L2: the real service, headlessly. No display, no codec, no clicking — and it runs in
# milliseconds, which is what the RackAccess seam bought.
add_executable(interstellar_core_tests tests/coreTests.cpp)
target_link_libraries(interstellar_core_tests PRIVATE interstellar_core)
add_test(NAME interstellar_core COMMAND interstellar_core_tests)
