# cosmo — the photo editor GUI.
#
# Native backend: GTK3 (window, file dialogs, GdkPixbuf JPEG/PNG/TIFF decode) +
# Cairo (the Artboard native render adapter) + optional LibRaw (RAW). All are
# available on Linux and on Windows via MSYS2/MinGW-w64, so this one target builds
# on both. The web build (Emscripten + Canvas2D) stays in build.sh.

# App sources (root + panels + widgets + decode), minus the Emscripten-only entry.
file(GLOB COSMO_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/panels/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/widgets/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/decode/*.cpp)
list(FILTER COSMO_SOURCES EXCLUDE REGEX "web_main\\.cpp$")

# The Cairo adapter lives in Artboard but is compiled per-app (adapters are not in
# the platform-free artboard_core library).
set(CAIRO_ADAPTER ${CMAKE_CURRENT_SOURCE_DIR}/../Artboard/src/adapter/native/CairoTarget.cpp)

add_executable(cosmo ${COSMO_SOURCES} ${CAIRO_ADAPTER})

target_include_directories(cosmo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../Artboard/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../Artboard/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../ImageProcessing/src)

# artboard_core + arstro_image are the portable library targets; arstro_image
# carries ARSTRO_ENABLE_THREADS and Threads:: transitively, keeping the engine
# header layout consistent between the library and the app.
target_link_libraries(cosmo PRIVATE artboard_core arstro_image)

# ── GTK3 (pulls in Cairo, GdkPixbuf, GLib) via pkg-config ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
target_link_libraries(cosmo PRIVATE PkgConfig::GTK3)

# ── LibRaw (optional): native RAW decode. Prefer a system/pkg-config LibRaw
#    (MSYS2: mingw-w64-x86_64-libraw); else fall back to a vendored static build
#    under ImageProcessing/lib/LibRaw if present (Linux). Absent -> JPEG/PNG only. ──
pkg_check_modules(LIBRAW IMPORTED_TARGET libraw)
set(VENDORED_LIBRAW ${CMAKE_CURRENT_SOURCE_DIR}/../ImageProcessing/lib/LibRaw)
if(LIBRAW_FOUND)
  target_link_libraries(cosmo PRIVATE PkgConfig::LIBRAW)
  target_compile_definitions(cosmo PRIVATE COSMO_HAVE_LIBRAW)
  message(STATUS "cosmo: RAW enabled (system LibRaw ${LIBRAW_VERSION})")
elseif(EXISTS ${VENDORED_LIBRAW}/libraw/libraw.h AND EXISTS ${VENDORED_LIBRAW}/lib/libraw.a)
  target_include_directories(cosmo PRIVATE ${VENDORED_LIBRAW})
  target_link_libraries(cosmo PRIVATE ${VENDORED_LIBRAW}/lib/libraw.a z)
  target_compile_definitions(cosmo PRIVATE COSMO_HAVE_LIBRAW)
  message(STATUS "cosmo: RAW enabled (vendored LibRaw)")
else()
  message(STATUS "cosmo: RAW disabled (LibRaw not found) — JPEG/PNG/TIFF via GdkPixbuf only")
endif()
