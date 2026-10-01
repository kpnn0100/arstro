# interstellar_host — the codec seams filled in with FFmpeg (R-VOL, R-RENDER-3), plus the decoder
# that lets Cosmo grade a video frame without Cosmo changing (R-RACK-3).
#
# Optional: without libav* the app still builds and still handles stills, which is what keeps the
# core suite runnable on a machine with no FFmpeg.
find_package(PkgConfig REQUIRED)
pkg_check_modules(INTERSTELLAR_AV IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
pkg_check_modules(INTERSTELLAR_PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)

if(INTERSTELLAR_AV_FOUND)
  add_library(interstellar_host STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameSourceFFmpeg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameWriterFFmpeg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoFrameDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HostFrameSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PngWriter.cpp)
  target_include_directories(interstellar_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(interstellar_host PUBLIC interstellar_core PkgConfig::INTERSTELLAR_AV PkgConfig::INTERSTELLAR_PIXBUF)
  target_compile_definitions(interstellar_host PUBLIC INTERSTELLAR_HAVE_FFMPEG=1)
  message(STATUS "interstellar: video enabled (libavformat ${INTERSTELLAR_AV_libavformat_VERSION})")
else()
  message(STATUS "interstellar: video DISABLED (libav* not found) — stills only")
endif()
