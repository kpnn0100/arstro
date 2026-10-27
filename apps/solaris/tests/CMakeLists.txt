# Solaris's faces, proved equal (R-SVC-5…7, arstro.rule §1). Registered after solaris-cc and the
# window, because the tests drive the ordinary product binaries — no test-only path.

if(TARGET solaris_control)
  # the control channel with no window: line order, `wait` holding the queue, and `attach` printing
  # exactly what `solaris-cc --watch --script` prints (tests/faces/controlTests.cpp)
  add_executable(solaris_control_tests faces/controlTests.cpp)
  target_link_libraries(solaris_control_tests PRIVATE solaris_control solaris_host solaris_core) # solaris_host: presetPaths, as the window
  target_compile_definitions(solaris_control_tests PRIVATE SOLARIS_CC="$<TARGET_FILE:solaris-cc>")
  add_dependencies(solaris_control_tests solaris-cc)
  add_test(NAME solaris_control COMMAND solaris_control_tests)
endif()

# R-DEV-9: the PipeWire driver's ring — the player's thread and the server's realtime callback — with no sound server
add_executable(solaris_ring_tests faces/ringTests.cpp)
target_include_directories(solaris_ring_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../host)
find_package(Threads REQUIRED)
target_link_libraries(solaris_ring_tests PRIVATE Threads::Threads)
add_test(NAME solaris_ring COMMAND solaris_ring_tests)

# R-MIDI-2: the ALSA sequencer input, live through the kernel's Midi Through (no sound device; 77 = skipped)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SOLARIS_TEST_ALSA IMPORTED_TARGET alsa)
endif()
if(SOLARIS_TEST_ALSA_FOUND AND TARGET solaris_host)
  add_executable(solaris_midi_tests faces/midiTests.cpp)
  target_link_libraries(solaris_midi_tests PRIVATE solaris_host PkgConfig::SOLARIS_TEST_ALSA)
  add_test(NAME solaris_midi COMMAND solaris_midi_tests)
  set_tests_properties(solaris_midi PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
endif()

# the web face's adapter over ntwb::MemoryTransport, and `ntwb install` into a scratch data dir
add_executable(solaris_ntwb_tests faces/ntwbAdapterTests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../cli/NtwbAdapter.cpp)
target_include_directories(solaris_ntwb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../cli)
# solaris_host: `browse` is tested with the host's own folder lister (Machine.cpp listDir), as `ntwb serve` binds it
target_link_libraries(solaris_ntwb_tests PRIVATE solaris_core solaris_host arstro_ntwb)
target_compile_definitions(solaris_ntwb_tests PRIVATE SOLARIS_CC="$<TARGET_FILE:solaris-cc>"
                                                      COSMO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../cosmo"
                                                      SOLARIS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_dependencies(solaris_ntwb_tests solaris-cc)
add_test(NAME solaris_ntwb COMMAND solaris_ntwb_tests)

# R-SVC-6 — the equivalence test: one committed script through solaris-cc (headless) and through a
# LIVE window over its control socket; same outputs, same events, same stable state, same song file.
# It needs a display: with none it reports SKIPPED (exit 77), never a silent pass.
if(TARGET solaris)
  add_test(NAME solaris_equivalence
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/acceptance/run.sh $<TARGET_FILE:solaris> $<TARGET_FILE:solaris-cc>)
  set_tests_properties(solaris_equivalence PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

# R-VST-6: other makers' plugins found for real — this build's own two and a module that aborts when loaded, each read by
# solaris-plughost in a process of its own (needs the VST3 SDK, as the helper does)
if(TARGET solaris-plughost AND TARGET solaris_host)
  add_library(solaris_crash_module SHARED faces/crashModule.cpp)
  add_executable(solaris_plugins_tests faces/pluginsTests.cpp)
  target_link_libraries(solaris_plugins_tests PRIVATE solaris_host)
  target_compile_definitions(solaris_plugins_tests PRIVATE SOLARIS_CC="$<TARGET_FILE:solaris-cc>" SOLARIS_PLUGHOST_BIN="$<TARGET_FILE:solaris-plughost>"
                                                           SOLARIS_CRASH_MODULE="$<TARGET_FILE:solaris_crash_module>" ARSTRO_VST3_DIR="${ARSTRO_VST3_DIR}")
  add_dependencies(solaris_plugins_tests solaris-cc solaris-plughost solaris_crash_module)
  add_test(NAME solaris_plugins COMMAND solaris_plugins_tests)
endif()

# R-VIDEO-1: the song's picture through the real FFmpeg — probe, frames, the mux, and `solaris-cc render --video` read
# back by ffprobe (the clips made by the ffmpeg program, so registered only where it and ffprobe are found)
find_program(SOLARIS_FFMPEG ffmpeg)
find_program(SOLARIS_FFPROBE ffprobe)
if(PkgConfig_FOUND)
  pkg_check_modules(SOLARIS_TEST_AV QUIET libavformat libavcodec libavutil libswscale)
endif()
if(SOLARIS_TEST_AV_FOUND AND SOLARIS_FFMPEG AND SOLARIS_FFPROBE AND TARGET solaris_host)
  add_executable(solaris_video_tests faces/videoTests.cpp)
  target_link_libraries(solaris_video_tests PRIVATE solaris_host)
  target_compile_definitions(solaris_video_tests PRIVATE SOLARIS_CC="$<TARGET_FILE:solaris-cc>" FFMPEG="${SOLARIS_FFMPEG}" FFPROBE="${SOLARIS_FFPROBE}")
  add_dependencies(solaris_video_tests solaris-cc)
  add_test(NAME solaris_video COMMAND solaris_video_tests)
  set_tests_properties(solaris_video PROPERTIES TIMEOUT 120)
endif()

# R-ACCESS-1: the window's controls as AT-SPI reads them — the host's ATK bridge read back through ATK's own calls, no
# display (registered where the host built it)
if(TARGET solaris_host AND TARGET PkgConfig::SOLARIS_ATK)
  add_executable(solaris_a11y_tests faces/a11yTests.cpp)
  target_link_libraries(solaris_a11y_tests PRIVATE solaris_host)
  add_test(NAME solaris_a11y COMMAND solaris_a11y_tests)
endif()
