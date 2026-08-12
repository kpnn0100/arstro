# Genesis — an animation designer for Artboard whose deliverable is source code.
#
#   genesis_core  the headless model: document, Gene language, code emitter, preview
#                 runtime, verifier. No UI, no OS calls beyond file + compiler invocation.
#   genesis-cc    the headless compiler (build systems + CI).
#   genesis       the GUI, built ON Artboard (it previews what it generates).
#   genesis_tests the unit tests (MiniTest, the same harness Artboard uses).
cmake_minimum_required(VERSION 3.16)

file(GLOB_RECURSE GENESIS_CORE CONFIGURE_DEPENDS core/*.cpp)
add_library(genesis_core STATIC ${GENESIS_CORE})
target_include_directories(genesis_core PUBLIC core)
target_link_libraries(genesis_core PUBLIC artboard_core)

# The verifier compiles a generated component at runtime, so it needs to know where the
# Artboard headers and the built core library are. Baked in here rather than guessed.
target_compile_definitions(genesis_core PRIVATE
  GENESIS_ARTBOARD_INCLUDE=${CMAKE_SOURCE_DIR}/Artboard/include
  GENESIS_ARTBOARD_SRC=${CMAKE_SOURCE_DIR}/Artboard/src
  GENESIS_ARTBOARD_LIBDIR=$<TARGET_FILE_DIR:artboard_core>)

add_executable(genesis-cc cli/main.cpp)
target_link_libraries(genesis-cc PRIVATE genesis_core)

# ── unit tests ──
add_executable(genesis_tests tests/genesisTests.cpp)
target_include_directories(genesis_tests PRIVATE ${CMAKE_SOURCE_DIR}/Artboard/tests)
target_link_libraries(genesis_tests PRIVATE genesis_core)
add_test(NAME genesis_core_tests COMMAND genesis_tests)

# ── the GUI ──
# Needs GTK3 (window + event loop) and Cairo (the Artboard native adapter), exactly like
# cosmo. Absent either, the library + CLI + tests still build; only the GUI is skipped.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GENESIS_GTK QUIET gtk+-3.0 cairo)
endif()

file(GLOB GENESIS_APP CONFIGURE_DEPENDS app/*.cpp app/widgets/*.cpp)
if(GENESIS_GTK_FOUND AND GENESIS_APP)
  add_executable(genesis ${GENESIS_APP} ${CMAKE_SOURCE_DIR}/Artboard/src/adapter/native/CairoTarget.cpp)
  target_include_directories(genesis PRIVATE app ${GENESIS_GTK_INCLUDE_DIRS})
  target_link_libraries(genesis PRIVATE genesis_core ${GENESIS_GTK_LIBRARIES})
  target_link_directories(genesis PRIVATE ${GENESIS_GTK_LIBRARY_DIRS})
  target_compile_options(genesis PRIVATE ${GENESIS_GTK_CFLAGS_OTHER})
else()
  message(STATUS "genesis GUI: skipped (GTK3 + Cairo not found); genesis_core, genesis-cc and tests still build")
endif()
