# interstellar_host — the codec seams filled in (R-PLAY-1, R-RENDER-2).
#
# interstellar_core names `IFrameSource` and `IFrameWriter` and carries no codec (R-SVC-7); this
# library fills them with FFmpeg. It is shared by BOTH front ends — interstellar-cc and the GUI —
# so a render from the shell and a render from the window go through exactly one encoder.
#
# Optional by design: without libav* the CLI still builds and still writes a PPM sequence, which
# is the output a golden test compares. Only real playback and real export need this.
find_package(PkgConfig REQUIRED)
pkg_check_modules(INTERSTELLAR_AV IMPORTED_TARGET libavformat libavcodec libavutil libswscale)

if(INTERSTELLAR_AV_FOUND)
  add_library(interstellar_host STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameSourceFFmpeg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameWriterFFmpeg.cpp)
  target_include_directories(interstellar_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(interstellar_host PUBLIC interstellar_core PkgConfig::INTERSTELLAR_AV)
  target_compile_definitions(interstellar_host PUBLIC INTERSTELLAR_HAVE_FFMPEG=1)
  message(STATUS "interstellar: video enabled (libavformat ${INTERSTELLAR_AV_libavformat_VERSION})")
else()
  message(STATUS "interstellar: video DISABLED (libavformat/libavcodec/libswscale not found) — "
                 "the CLI still writes a PPM sequence")
endif()
