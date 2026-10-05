# interstellar_render — the per-frame render path: which clips are live at t (ActiveSet), the
# grade step (GradeEngine over arstro::EditEngine), the frame cache, and the composite.
#
# It is its own library, separate from interstellar_core, for one reason: it must not depend on the
# project model. Its inputs are plain structs (ClipSpan, Layer, Raster), so the render path can be
# tested and measured against fixed inputs while the model is still moving, and so nothing here can
# quietly start reading a project field it was never given — a render stays a pure function of what
# it was handed (R-RENDER-2).
#
# Usable two ways: standalone (`cmake -S apps/interstellar/render`), which pulls arstro_image in
# itself, and from the umbrella via add_subdirectory, where arstro_image already exists.
cmake_minimum_required(VERSION 3.14)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(interstellar_render LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
  endif()
  enable_testing()
endif()

if(NOT TARGET arstro_image)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../core/ImageProcessing ${CMAKE_BINARY_DIR}/arstro_image)
endif()

add_library(interstellar_render STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/ActiveSet.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Composite.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Effects.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Prescale.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/FrameCache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GradeEngine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ParamHash.cpp)
target_include_directories(interstellar_render PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../core
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/ImageProcessing/src)
target_link_libraries(interstellar_render PUBLIC arstro_image)

# L1: plain asserts over fixed inputs, headless, well under a second.
add_executable(interstellar_render_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/renderTests.cpp)
target_link_libraries(interstellar_render_tests PRIVATE interstellar_render)
add_test(NAME interstellar_render COMMAND interstellar_render_tests)

# Not a test: the timing harness behind the numbers in NOTES.md. Built so the numbers can be
# re-measured on another machine with one command rather than re-derived from prose.
add_executable(interstellar_render_bench EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/bench/renderBench.cpp)
target_link_libraries(interstellar_render_bench PRIVATE interstellar_render)
