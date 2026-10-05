# interstellar_host — the codec seams filled in with FFmpeg (R-VOL, R-RENDER-3), plus the decoder
# that lets Cosmo grade a video frame without Cosmo changing (R-RACK-3).
#
# Optional: without libav* the app still builds and still handles stills, which is what keeps the
# core suite runnable on a machine with no FFmpeg.
find_package(PkgConfig REQUIRED)
pkg_check_modules(INTERSTELLAR_AV IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
pkg_check_modules(INTERSTELLAR_PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)

if(INTERSTELLAR_AV_FOUND)
  add_library(interstellar_host STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/AudioSourceFFmpeg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameSourceFFmpeg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameWriterFFmpeg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoFrameDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HostFrameSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PngWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Thumbnailer.cpp)
  target_include_directories(interstellar_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(interstellar_host PUBLIC interstellar_core PkgConfig::INTERSTELLAR_AV PkgConfig::INTERSTELLAR_PIXBUF)
  target_compile_definitions(interstellar_host PUBLIC INTERSTELLAR_HAVE_FFMPEG=1)
  message(STATUS "interstellar: video enabled (libavformat ${INTERSTELLAR_AV_libavformat_VERSION})")
else()
  message(STATUS "interstellar: video DISABLED (libav* not found) — stills only")
endif()

# The FFmpeg source against real containers, made by the ffmpeg binary (skipped without it).
if(INTERSTELLAR_AV_FOUND)
  add_executable(interstellar_host_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/hostTests.cpp)
  target_link_libraries(interstellar_host_tests PRIVATE interstellar_host)
  find_program(INTERSTELLAR_FFMPEG_FOR_HOST ffmpeg)
  if(INTERSTELLAR_FFMPEG_FOR_HOST)
    add_test(NAME interstellar_host
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/../tests/host_media.sh" "$<TARGET_FILE:interstellar_host_tests>"
              "${INTERSTELLAR_FFMPEG_FOR_HOST}" "${CMAKE_CURRENT_BINARY_DIR}/host_media")
  endif()
endif()
# Not a test: the cost of a source in the shapes the app pays it (D-5).
if(INTERSTELLAR_AV_FOUND)
  add_executable(interstellar_source_bench EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/bench/sourceBench.cpp)
  target_link_libraries(interstellar_source_bench PRIVATE interstellar_host)
endif()
if(INTERSTELLAR_AV_FOUND)
  add_executable(interstellar_open_bench EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/bench/openBench.cpp)
  target_link_libraries(interstellar_open_bench PRIVATE interstellar_host)
endif()
# Not a test: what a frame of playback costs, graded and not (R-PLAY).
if(INTERSTELLAR_AV_FOUND)
  add_executable(interstellar_play_bench EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/bench/playBench.cpp)
  target_link_libraries(interstellar_play_bench PRIVATE interstellar_host)
endif()
