# interstellar_core — UI-free Interstellar logic. No Artboard, no window.
#
# It DOES link cosmo_core, and that is the architecture rather than an accident: the rack is a real
# hosted CosmoService (R-RACK-1), so cosmo_core's own dependencies (GTK3 for GdkPixbuf, optional
# LibRaw) come with it. The first build avoided this to keep its test suite displayless — and that
# avoidance is exactly what let colour be a fake for two releases. A real authority is worth a link
# dependency; `cosmo_core_tests` already runs headless in under a second with the same one.
file(GLOB INTERSTELLAR_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(interstellar_core STATIC ${INTERSTELLAR_CORE_SOURCES})
target_include_directories(interstellar_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/ImageProcessing/src)
target_link_libraries(interstellar_core PUBLIC cosmo_core arstro_image)

# L2: the real service, headless. The suite's own gate is a colour edit reaching a real .cmp.
add_executable(interstellar_core_tests tests/coreTests.cpp)
target_link_libraries(interstellar_core_tests PRIVATE interstellar_core)
add_test(NAME interstellar_core COMMAND interstellar_core_tests)
