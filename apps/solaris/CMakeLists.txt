# Solaris — the Arstro DAW. See REQUIREMENTS.md and docs/architecture.md.
#
# Its sound is the DSP library's (R-DSP-1): the engine links arstro_dsp and hosts its devices;
# nothing below this directory implements DSP.
add_subdirectory(model)    # the .slp: no sound, no I/O
