# interstellar_core — UI-free Interstellar logic. No Artboard, no window.
#
# It DOES link cosmo_core, and that is the architecture rather than an accident: the rack is a real
# hosted CosmoService (R-RACK-1), so cosmo_core's own dependencies (GTK3 for GdkPixbuf, optional
# LibRaw) come with it. The first build avoided this to keep its test suite displayless — and that
# avoidance is exactly what let colour be a fake for two releases. A real authority is worth a link
# dependency; `cosmo_core_tests` already runs headless in under a second with the same one.
file(GLOB INTERSTELLAR_CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/service/*.cpp)

add_library(interstellar_core STATIC ${INTERSTELLAR_CORE_SOURCES})
target_include_directories(interstellar_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/service
  ${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/ImageProcessing/src)
target_link_libraries(interstellar_core PUBLIC cosmo_core arstro_image interstellar_model interstellar_render)

# L2: the real service, headless. The suite's own gate is a colour edit reaching a real .cmp.
add_executable(interstellar_core_tests tests/coreTests.cpp)
target_link_libraries(interstellar_core_tests PRIVATE interstellar_core)
add_test(NAME interstellar_core COMMAND interstellar_core_tests)

# L1: the interchange formats, text in and text out (R-XCH).
add_executable(interstellar_interchange_tests tests/interchangeTests.cpp)
target_link_libraries(interstellar_interchange_tests PRIVATE interstellar_core)
add_test(NAME interstellar_interchange COMMAND interstellar_interchange_tests)

# L1: the grammar, the codec and the address registry — the tables the API document is made of.
add_executable(interstellar_service_tests tests/serviceTests.cpp)
target_link_libraries(interstellar_service_tests PRIVATE interstellar_core)
add_test(NAME interstellar_service COMMAND interstellar_service_tests)

# L2: the REAL service — hosted Cosmo, real model, real render path — with fake decoders only.
add_executable(interstellar_service_l2_tests tests/serviceL2Tests.cpp)
target_link_libraries(interstellar_service_l2_tests PRIVATE interstellar_core)
add_test(NAME interstellar_service_l2 COMMAND interstellar_service_l2_tests)
