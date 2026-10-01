# interstellar_model — the `.isp` document and timelines-as-versions. Pure C++17 logic.
#
# Deliberately NOT part of interstellar_core: core links cosmo_core (and through it GTK) because
# the rack is a real hosted Cosmo, but the project model needs none of that. Keeping it a separate
# library means the format and the version rules are testable in milliseconds with no display, no
# decoder and no Cosmo — and a format tool (lint, diff, a merge driver) can link it alone.
#
# Usable standalone (`cmake -S apps/interstellar/model`) and via add_subdirectory: project() and
# enable_testing() only run when this is the top-level source dir.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(interstellar_model CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  enable_testing()
endif()

add_library(interstellar_model STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Project.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Versions.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Arrange.cpp)
# AppModel.h (../core/service) owns NodeId and Provenance; it includes engine/EditParams.h, which
# is header-only — an include path, no link.
target_include_directories(interstellar_model PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../core
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/ImageProcessing/src)
target_compile_features(interstellar_model PUBLIC cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(interstellar_model PRIVATE -Wall -Wextra)
endif()

add_executable(interstellar_model_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/modelTests.cpp)
target_link_libraries(interstellar_model_tests PRIVATE interstellar_model)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(interstellar_model_tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME interstellar_model COMMAND interstellar_model_tests)
