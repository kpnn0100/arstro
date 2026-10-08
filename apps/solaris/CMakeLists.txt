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
else()
  message(STATUS "solaris: engine skipped (needs the DSP library, ARSTRO_BUILD_DSP=ON)")
endif()
