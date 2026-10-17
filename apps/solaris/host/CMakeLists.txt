# solaris_host — the things the core may not touch (R-SVC-4): audio files today (FFmpeg decode,
# WAV writing); audio devices, settings and recents land with H1/P1.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SOLARIS_AV IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
add_library(solaris_host STATIC AudioFiles.cpp VideoFiles.cpp Machine.cpp Plugins.cpp PluginInstance.cpp) # R-VIDEO-1: the picture
target_include_directories(solaris_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(solaris_host PUBLIC solaris_core)
# R-VST-6: the plugin host this build made, when it made one (the SDK found)
target_compile_definitions(solaris_host PRIVATE $<$<TARGET_EXISTS:solaris-plughost>:SOLARIS_PLUGHOST="$<TARGET_FILE:solaris-plughost>">)
# R-PRESET-1: the factory presets shipped beside the app (its source tree in this build)
target_compile_definitions(solaris_host PRIVATE SOLARIS_FACTORY_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../presets"
                                                  SOLARIS_FACTORY_TEMPLATES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../templates") # R-PRESET-2
if(SOLARIS_AV_FOUND)
  target_link_libraries(solaris_host PRIVATE PkgConfig::SOLARIS_AV)
  target_compile_definitions(solaris_host PRIVATE SOLARIS_HAVE_FFMPEG=1)
else()
  message(STATUS "solaris_host: no FFmpeg — audio files cannot be decoded (rendering still works)")
endif()
# R-VIDEO-1: a frame of the picture scaled to fit the window
pkg_check_modules(SOLARIS_SWS IMPORTED_TARGET libswscale)
if(SOLARIS_AV_FOUND AND SOLARIS_SWS_FOUND)
  target_link_libraries(solaris_host PRIVATE PkgConfig::SOLARIS_SWS)
  target_compile_definitions(solaris_host PRIVATE SOLARIS_HAVE_SWSCALE=1)
endif()

# R-DEV-8: the PulseAudio API (PipeWire serves it too) — the device list and the clock device's stream
pkg_check_modules(SOLARIS_PULSE IMPORTED_TARGET libpulse libpulse-simple)
if(SOLARIS_PULSE_FOUND)
  target_sources(solaris_host PRIVATE AudioOutPulse.cpp AudioInPulse.cpp) # R-REC-1: and its capture
  target_link_libraries(solaris_host PRIVATE PkgConfig::SOLARIS_PULSE)
  target_compile_definitions(solaris_host PUBLIC SOLARIS_HAVE_PULSE=1)
endif()

# R-DEV-9: PipeWire's own API beside it — small buffers, counted xruns; PulseAudio stays the fallback
pkg_check_modules(SOLARIS_PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
if(SOLARIS_PIPEWIRE_FOUND)
  target_sources(solaris_host PRIVATE AudioOutPipewire.cpp AudioInPipewire.cpp) # R-REC-1: and its capture
  target_link_libraries(solaris_host PRIVATE PkgConfig::SOLARIS_PIPEWIRE)
  target_compile_definitions(solaris_host PUBLIC SOLARIS_HAVE_PIPEWIRE=1)
endif()

# R-MIDI-2: a MIDI keyboard through the ALSA sequencer
pkg_check_modules(SOLARIS_ALSA IMPORTED_TARGET alsa)
if(SOLARIS_ALSA_FOUND)
  target_sources(solaris_host PRIVATE MidiInAlsa.cpp)
  target_link_libraries(solaris_host PRIVATE PkgConfig::SOLARIS_ALSA)
  target_compile_definitions(solaris_host PUBLIC SOLARIS_HAVE_ALSA=1)
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
