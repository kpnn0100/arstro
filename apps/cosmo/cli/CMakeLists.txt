# cosmo-cc — the headless CLI front end over CosmoService (R-SVC-1, S3).
#
# Same service the GTK app hosts, with argv and stdout where the window is: no
# Artboard scene, no GTK widgets, no App. GTK3 is still linked, because the two
# host-side seams cosmo_core deliberately does not contain are GdkPixbuf-based —
# NativeImageDecoder (JPEG/PNG/TIFF; LibRaw for RAW) and ExportWriter's encoder —
# and both live in the app layer per R-SVC-7. Linking gtk+-3.0 costs nothing at
# runtime here: no gtk_init() is ever called, so no display is needed.
#
# Only builds when cosmo_core exists (see core/CMakeLists.txt — pkg-config +
# GTK3), which is also the condition the parent uses for the `cosmo` app.

add_executable(cosmo-cc
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
  # The encoder is REUSED, not reimplemented: a headless export and the Export
  # modal must not diverge on quality, the long-edge cap, EXIF or the sRGB
  # profile (R-EXPORT-4/5). ExportWriter.cpp only needs ExportDialog::Request —
  # a plain struct — so no widget, and no Artboard object code, comes with it.
  ${CMAKE_CURRENT_SOURCE_DIR}/../ExportWriter.cpp
  # R-CPU-2c / D-12: the OpenMP pin the CLI must not silently drop. Per-thread,
  # resolved with dlsym rather than linked, hence ${CMAKE_DL_LIBS} below.
  ${CMAKE_CURRENT_SOURCE_DIR}/../OmpPin.cpp)

target_include_directories(cosmo-cc PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/Artboard/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/Artboard/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/ImageProcessing/src)

# arstro_image carries GL/EGL PUBLICly (R-GPU), so the GPU backend links itself.
target_link_libraries(cosmo-cc PRIVATE cosmo_core arstro_image ${CMAKE_DL_LIBS})

# ── GdkPixbuf (via GTK3) for the decoder + the export encoder ──
# find_package(PkgConfig) has to run in THIS directory scope: its result
# variables are directory-scoped, so they do not inherit from the parent's call
# (the same reason cosmo/CMakeLists.txt repeats it after core/'s).
find_package(PkgConfig REQUIRED)
pkg_check_modules(COSMO_CC_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
target_link_libraries(cosmo-cc PRIVATE PkgConfig::COSMO_CC_GTK3)
