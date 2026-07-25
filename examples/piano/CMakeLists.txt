# Native GTK3 + ALSA playable piano-keyboard UI (Artboard + PianoEngine).
# Optional: skipped (with a status message, not a hard failure) if gtk+-3.0 or
# alsa dev packages aren't present — same pattern as the libsndfile-gated
# wav_demo target in DigitalSignalProcessing/CMakeLists.txt.
find_package(PkgConfig)
find_package(Threads)
if(PkgConfig_FOUND)
  pkg_check_modules(PIANO_GTK3 IMPORTED_TARGET gtk+-3.0)
  pkg_check_modules(PIANO_ALSA IMPORTED_TARGET alsa)
  # x11: needed for XkbSetDetectableAutoRepeat (linux_main.cpp) — GTK3 doesn't
  # re-export -lX11 via its own pkg-config --libs.
  pkg_check_modules(PIANO_X11 IMPORTED_TARGET x11)
endif()

if(PIANO_GTK3_FOUND AND PIANO_ALSA_FOUND AND PIANO_X11_FOUND AND Threads_FOUND)
  add_executable(arstro_piano_ui
    linux_main.cpp
    PianoApp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Artboard/src/adapter/native/CairoTarget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../DigitalSignalProcessing/apps/piano_demo/PianoEngine.cpp
  )
  target_include_directories(arstro_piano_ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Artboard/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Artboard/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../DigitalSignalProcessing/src
  )
  target_link_libraries(arstro_piano_ui PRIVATE
    artboard_core arstro_dsp Threads::Threads
    PkgConfig::PIANO_GTK3 PkgConfig::PIANO_ALSA PkgConfig::PIANO_X11)
else()
  message(STATUS "examples/piano: skipped (needs gtk+-3.0, alsa, and x11 dev packages)")
endif()
