# cosmo_core — UI-free Cosmo business logic (image sessions, group tree, develop
# params, branching history, presets, session/workspace persistence), shared by
# any Cosmo front end. Depends only on arstro_image (the headless edit engine)
# and, for native RAW/JPEG/PNG decode, GdkPixbuf + optional LibRaw -- it has NO
# Artboard dependency, so it stays reusable by a UI that isn't built on Artboard
# at all.
#
# The branching History and the native image decoder are UI-free too, so they
# live here and are compiled straight in (decode/ is in its own subfolder).
# Paths are relative to this dir (cosmo/core), so the shared libs are two levels up.

# pkg-config itself (not just gtk+-3.0) is optional here: on native Windows (no
# MSYS2) there simply is no pkg-config, and cosmo — a GTK3-only native GUI, see
# cosmo/CMakeLists.txt — can't be built there, so this (and the `cosmo` app that
# depends on it) is skipped gracefully instead of hard-failing the whole tree's
# configure. Once pkg-config IS present (Linux, or Windows via MSYS2) gtk+-3.0
# stays REQUIRED as before, so a genuinely missing dev package still fails loudly.
find_package(PkgConfig)
if(NOT PkgConfig_FOUND)
  message(STATUS "cosmo_core: skipped (no pkg-config — needed for GTK3; on Windows install MSYS2, or pass -DARSTRO_BUILD_COSMO=OFF)")
else()

file(GLOB COSMO_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library(cosmo_core STATIC
  ${COSMO_CORE_SOURCES}
  ${CMAKE_CURRENT_SOURCE_DIR}/decode/NativeImageDecoder.cpp)

target_include_directories(cosmo_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/ImageProcessing/src)

target_link_libraries(cosmo_core PUBLIC arstro_image)

# ── GdkPixbuf (via GTK3) for JPEG/PNG/TIFF decode in NativeImageDecoder ──
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
target_link_libraries(cosmo_core PRIVATE PkgConfig::GTK3)

# ── LibRaw (optional): native RAW decode. Same detection as the app build. ──
pkg_check_modules(LIBRAW IMPORTED_TARGET libraw)
set(VENDORED_LIBRAW ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/ImageProcessing/lib/LibRaw)
if(LIBRAW_FOUND)
  target_link_libraries(cosmo_core PRIVATE PkgConfig::LIBRAW)
  target_compile_definitions(cosmo_core PRIVATE COSMO_HAVE_LIBRAW)
  message(STATUS "cosmo_core: RAW enabled (system LibRaw ${LIBRAW_VERSION})")
elseif(EXISTS ${VENDORED_LIBRAW}/libraw/libraw.h AND EXISTS ${VENDORED_LIBRAW}/lib/libraw.a)
  target_include_directories(cosmo_core PRIVATE ${VENDORED_LIBRAW})
  target_link_libraries(cosmo_core PRIVATE ${VENDORED_LIBRAW}/lib/libraw.a z)
  target_compile_definitions(cosmo_core PRIVATE COSMO_HAVE_LIBRAW)
  message(STATUS "cosmo_core: RAW enabled (vendored LibRaw)")
else()
  message(STATUS "cosmo_core: RAW disabled (LibRaw not found) — JPEG/PNG/TIFF via GdkPixbuf only")
endif()

# ── Smoke tests (plain assert(), no external framework) ──
add_executable(cosmo_core_tests tests/sessionTests.cpp)
target_link_libraries(cosmo_core_tests PRIVATE cosmo_core)
add_test(NAME cosmo_core COMMAND cosmo_core_tests)

endif()  # PkgConfig_FOUND
