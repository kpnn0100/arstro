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

# R-DEV-8: the PulseAudio API (PipeWire serves it too) — the device list and the clock device's stream
pkg_check_modules(SOLARIS_PULSE IMPORTED_TARGET libpulse libpulse-simple)
if(SOLARIS_PULSE_FOUND)
  target_sources(solaris_host PRIVATE AudioOutPulse.cpp)
  target_link_libraries(solaris_host PRIVATE PkgConfig::SOLARIS_PULSE)
  target_compile_definitions(solaris_host PUBLIC SOLARIS_HAVE_PULSE=1)
endif()

# R-SVC-5: the window's control channel. cosmo's ControlChannel is COMPILED IN PLACE (a non-blocking
# Unix line socket that moves lines and never parses them — linked, not copied, as R-UI-4 reuses
# cosmo's widgets); ControlLog.cpp gives it the one log symbol it needs from cosmo's host, and
# ControlServer is the Solaris half: each line to dispatchText, each answer framed. GTK-free, so the
# tests drive it with no window (tests/faces).
if(UNIX)
  add_library(solaris_control STATIC
    ControlServer.cpp
    ControlLog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo/ControlChannel.cpp)
  target_include_directories(solaris_control PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(solaris_control PUBLIC solaris_core)
endif()
