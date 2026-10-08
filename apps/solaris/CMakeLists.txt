# Solaris — the Arstro DAW. See REQUIREMENTS.md and docs/architecture.md.
#
# Its sound is the DSP library's (R-DSP-1): the engine links arstro_dsp and hosts its devices;
# nothing below this directory implements DSP.
add_subdirectory(model)    # the .slp: no sound, no I/O
if(TARGET arstro_dsp)
  add_subdirectory(engine) # the MixGraph renderer: hosts arstro_dsp's devices
  add_subdirectory(core)   # the service: grammar, events, model, compile, render
  add_subdirectory(host)   # files (FFmpeg decode, WAV write); devices and settings later
  add_subdirectory(cli)    # solaris-cc: the whole app with no window
  add_subdirectory(app)    # the UI over AppHooks — links no service
else()
  message(STATUS "solaris: engine skipped (needs the DSP library, ARSTRO_BUILD_DSP=ON)")
endif()

# The GTK window: the app's hooks bound to the service. The ONLY OS code in the GUI.
if(TARGET solaris_app AND TARGET solaris_host)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SOLARIS_GTK3 IMPORTED_TARGET gtk+-3.0)
  if(SOLARIS_GTK3_FOUND)
    add_executable(solaris ${CMAKE_CURRENT_SOURCE_DIR}/linux_main.cpp)
    target_link_libraries(solaris PRIVATE solaris_app solaris_host PkgConfig::SOLARIS_GTK3)
  else()
    message(STATUS "solaris: the window skipped (no GTK3); solaris-cc and the tests still build")
  endif()
endif()
