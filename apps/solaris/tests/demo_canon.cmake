# R-SVC-9: the goal, measured — a whole song made with solaris-cc alone. The committed script must be what
# demo/canon/make_script.py writes, must run to the end with exit 0, and its render must measure as the
# song (demo/canon/measure.py: not silent, under 0 dBFS, the kick on the beats, the sidechain pumping, the
# canon's line in tune, the pad opening). Run by ctest as solaris_demo_canon with -DCC=<solaris-cc>
# -DDEMO=<apps/solaris/demo/canon> -DWORK=<a scratch folder in the build tree> [-DPY=<python3>].
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(ENV{SOLARIS_SETTINGS} "${WORK}/settings.txt") # never the user's own files
set(ENV{SOLARIS_RECENTS} "${WORK}/recents")
file(COPY "${DEMO}/song.txt" DESTINATION "${WORK}")

# 1. the committed script is the generator's output (the notes are spelled once, in make_script.py)
if(PY)
  execute_process(COMMAND "${PY}" "${DEMO}/make_script.py" "${WORK}/regenerated.txt" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "make_script.py failed: ${rc}")
  endif()
  file(READ "${WORK}/regenerated.txt" want)
  file(READ "${DEMO}/song.txt" have)
  if(NOT want STREQUAL have)
    message(FATAL_ERROR "demo/canon/song.txt is stale — run: python3 make_script.py (in demo/canon)")
  endif()
endif()

# 2. the song: every line lands, the render is written
execute_process(COMMAND "${CC}" --script song.txt WORKING_DIRECTORY "${WORK}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the song script failed (exit ${rc}):\n${err}\n${out}")
endif()
foreach(f canon.slp canon.wav canon.ch_2.wav canon.ch_4.wav canon.ch_5.wav canon.ch_6.wav)
  if(NOT EXISTS "${WORK}/${f}")
    message(FATAL_ERROR "the song script did not write ${f}")
  endif()
endforeach()

# 3. the render, measured — skipped (said, not silent) where there is no python3 with numpy
if(PY)
  execute_process(COMMAND "${PY}" -c "import numpy" RESULT_VARIABLE has_numpy OUTPUT_QUIET ERROR_QUIET)
endif()
if(NOT PY OR NOT has_numpy EQUAL 0)
  message(STATUS "solaris_demo_canon: no python3 with numpy — the render was made but not measured")
  return()
endif()
execute_process(COMMAND "${PY}" "${DEMO}/measure.py" "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the render does not measure as the song:\n${out}${err}")
endif()
