cmake_minimum_required(VERSION 3.14)
project(arstro_image LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Library: the headless image-edit engine ──
# The whole core is platform-free / WASM-friendly (no codecs, no OS calls), so
# unlike the DSP library there is nothing to exclude from the glob.
file(GLOB_RECURSE ARSTRO_IMG_SOURCES CONFIGURE_DEPENDS src/*.cpp)

find_package(Threads REQUIRED)

add_library(arstro_image STATIC ${ARSTRO_IMG_SOURCES})
target_include_directories(arstro_image PUBLIC src)
# Multicore acceleration + RenderService worker thread (native). The web build
# omits this define and runs the same code single-threaded / synchronously.
target_compile_definitions(arstro_image PUBLIC ARSTRO_ENABLE_THREADS)
target_link_libraries(arstro_image PUBLIC Threads::Threads)

# ── Unit tests (dependency-free MiniTest harness) ──
enable_testing()
add_executable(image_tests unittest/imageTests.cpp unittest/engineTests.cpp)
target_link_libraries(image_tests PRIVATE arstro_image)
add_test(NAME unit COMMAND image_tests)
