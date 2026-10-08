# solaris_host — the things the core may not touch (R-SVC-4): audio files today (FFmpeg decode,
# WAV writing); audio devices, settings and recents land with H1/P1.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SOLARIS_AV IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
add_library(solaris_host STATIC AudioFiles.cpp Machine.cpp)
target_include_directories(solaris_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(solaris_host PUBLIC solaris_core)
if(SOLARIS_AV_FOUND)
  target_link_libraries(solaris_host PRIVATE PkgConfig::SOLARIS_AV)
  target_compile_definitions(solaris_host PRIVATE SOLARIS_HAVE_FFMPEG=1)
else()
  message(STATUS "solaris_host: no FFmpeg — audio files cannot be decoded (rendering still works)")
endif()

# R-DEV-8: the PulseAudio API (PipeWire serves it too) — device lists now, streams with P1
pkg_check_modules(SOLARIS_PULSE IMPORTED_TARGET libpulse)
if(SOLARIS_PULSE_FOUND)
  target_link_libraries(solaris_host PRIVATE PkgConfig::SOLARIS_PULSE)
  target_compile_definitions(solaris_host PRIVATE SOLARIS_HAVE_PULSE=1)
endif()
