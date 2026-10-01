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

# ── GPU compute backend (OpenGL 4.3 compute over a headless context) — R-GPU ──
# Optional accelerator: PUBLIC so consumers (cosmo, image_tests) link the GL libs and
# see the ARSTRO_GL_COMPUTE define. The CPU pipeline stays the reference + the
# guaranteed fallback. `createGlComputeAccelerator()` has one implementation per OS
# (src/compute/GlComputeBackend.cpp on Linux over surfaceless EGL, src/compute/
# WglComputeBackend.cpp on Windows over WGL/a hidden window — see GlComputeBackend.h);
# both are picked up by the src/*.cpp glob above and self-guard on ARSTRO_GL_COMPUTE
# plus the OS, so only the right one compiles. Skipped (CPU-only) when the platform's
# GL libs aren't found (e.g. the web build, or a Linux box without EGL/GL dev libs).
option(ARSTRO_IMG_GL_COMPUTE "Build the OpenGL compute GPU backend" ON)
if(ARSTRO_IMG_GL_COMPUTE)
  if(WIN32)
    # Windows ships OpenGL natively (opengl32 + the vendor ICD via WGL) — no EGL
    # needed and nothing extra to install; this is always available to find.
    target_compile_definitions(arstro_image PUBLIC ARSTRO_GL_COMPUTE)
    target_link_libraries(arstro_image PUBLIC opengl32 gdi32 user32)
    message(STATUS "arstro_image: GPU compute backend ENABLED (OpenGL via WGL)")
  else()
    find_library(ARSTRO_EGL_LIB EGL)
    find_library(ARSTRO_GL_LIB GL)
    if(ARSTRO_EGL_LIB AND ARSTRO_GL_LIB)
      target_compile_definitions(arstro_image PUBLIC ARSTRO_GL_COMPUTE)
      target_link_libraries(arstro_image PUBLIC ${ARSTRO_EGL_LIB} ${ARSTRO_GL_LIB})
      message(STATUS "arstro_image: GPU compute backend ENABLED (OpenGL ${ARSTRO_GL_LIB})")
    else()
      message(STATUS "arstro_image: GPU compute backend disabled (EGL/GL not found) — CPU only")
    endif()
  endif()
endif()

# ── GPU compute backend, OpenGL ES 3.1 (Android; ARM Linux; desktop Mesa) — R-GPU-6/7 ──
# ON by default wherever EGL exists: an RK3588's Mali-G610 driver (libmali) exposes ONLY
# OpenGL ES, so a Linux build with just the desktop-GL backend ran every edit on the CPU
# with a GPU in the machine. Both backends are built side by side and the factory picks
# the first one the driver actually runs (desktop GL, then GLES). The backend loads its
# gl* entry points through eglGetProcAddress, so it links EGL ONLY — never libGLESv2,
# whose gl* symbols would collide with libGL's in the same process. It needs the GLES3
# headers at compile time (libgles-dev on Debian). libgbm, for the headless GBM display,
# is dlopen'ed at run time and is not a build dependency. Windows has no stock EGL, and
# the web build (Emscripten ships an EGL of its own) has no compute shaders.
option(ARSTRO_IMG_GLES_COMPUTE "Build the OpenGL ES 3.1 compute GPU backend" ON)
if(ARSTRO_IMG_GLES_COMPUTE AND NOT WIN32 AND NOT EMSCRIPTEN)
  find_library(ARSTRO_EGL_LIB EGL)
  find_path(ARSTRO_GLES_INCLUDE GLES3/gl31.h)
  if(ARSTRO_EGL_LIB AND ARSTRO_GLES_INCLUDE)
    target_compile_definitions(arstro_image PUBLIC ARSTRO_GLES_COMPUTE)
    target_include_directories(arstro_image PRIVATE ${ARSTRO_GLES_INCLUDE})
    target_link_libraries(arstro_image PUBLIC ${ARSTRO_EGL_LIB} ${CMAKE_DL_LIBS})
    message(STATUS "arstro_image: GLES compute backend ENABLED (EGL ${ARSTRO_EGL_LIB})")
  else()
    message(STATUS "arstro_image: GLES compute backend disabled (EGL or GLES3/gl31.h not found)")
  endif()
elseif(ARSTRO_IMG_GLES_COMPUTE AND WIN32)
  message(STATUS "arstro_image: GLES compute backend disabled (Windows has no EGL)")
endif()

# ── Unit tests (dependency-free MiniTest harness) ──
enable_testing()
add_executable(image_tests unittest/imageTests.cpp unittest/engineTests.cpp)
target_link_libraries(image_tests PRIVATE arstro_image)
add_test(NAME unit COMMAND image_tests)

# R-VOL / R-FX-2: the lazy video volume and the temporal ops (src/volume/). Their own executable
# rather than MiniTest cases, because the suite carries its own measurements and a guard that it
# refuses to run with asserts compiled out.
add_executable(volume_tests unittest/volumeTests.cpp)
target_link_libraries(volume_tests PRIVATE arstro_image)
add_test(NAME volume COMMAND volume_tests)
# Not a test: prints ms per output frame for the ledger. Run it on a Release build.
add_executable(volume_bench EXCLUDE_FROM_ALL unittest/volumeBench.cpp)
target_link_libraries(volume_bench PRIVATE arstro_image)
