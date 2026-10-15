# R-SVC-10: the second song, made in the window, measured against its score. J. S. Bach's Prelude in C,
# BWV 846 (public domain): score.txt must be what demo/prelude/score.py writes; the driver makes the song
# through the window's gestures (the real app, the test rig's host); solaris-cc prints its notes and renders
# it, and renders the score's own song straight from the table; measure.py diffs the notes against the score
# and the two renders bar by bar. Run by ctest as prelude_made_in_the_window (label `long`) with -DDRIVER=<solaris_demo_prelude>
# -DCC=<solaris-cc> -DDEMO=<apps/solaris/demo/prelude> -DWORK=<a scratch folder> [-DPY=<python3>].
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(ENV{SOLARIS_SETTINGS} "${WORK}/settings.txt") # never the user's own files
set(ENV{SOLARIS_RECENTS} "${WORK}/recents")
set(ENV{SOLARIS_UI_TEST_DIR} "${WORK}/rig")

# 1. the committed score is the generator's output (the notes are spelled once, in score.py)
if(PY)
  file(COPY "${DEMO}/score.py" DESTINATION "${WORK}/gen")
  execute_process(COMMAND "${PY}" -I "${WORK}/gen/score.py" RESULT_VARIABLE rc OUTPUT_QUIET)
  file(READ "${WORK}/gen/score.txt" want)
  file(READ "${DEMO}/score.txt" have)
  if(NOT rc EQUAL 0 OR NOT want STREQUAL have)
    message(FATAL_ERROR "demo/prelude/score.txt is stale — run: python3 score.py (in demo/prelude)")
  endif()
endif()

# 2. the song, made in the window
execute_process(COMMAND "${DRIVER}" "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0 OR NOT EXISTS "${WORK}/prelude.slp")
  message(FATAL_ERROR "the Prelude was not made in the window (exit ${rc}):\n${err}\n${out}")
endif()
string(REGEX MATCH "the Prelude made with [^\n]*" said "${out}")
message(STATUS "${said}")

# 3. its notes and its render; the score's own song and render
file(WRITE "${WORK}/print.txt" "project open prelude.slp\npattern print pt_1\n")
execute_process(COMMAND "${CC}" --script print.txt WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_FILE "${WORK}/notes.txt")
file(WRITE "${WORK}/render.txt" "project open prelude.slp\nrender --out prelude.wav\n")
execute_process(COMMAND "${CC}" --script render.txt WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc2 OUTPUT_QUIET)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0 OR NOT EXISTS "${WORK}/prelude.wav")
  message(FATAL_ERROR "the made song could not be printed or rendered (${rc}, ${rc2})")
endif()
if(NOT PY)
  message(STATUS "solaris_demo_prelude: no python3 — the song was made but not measured")
  return()
endif()
execute_process(COMMAND "${PY}" -I "${DEMO}/score.py" --reference "${WORK}/reference.txt" RESULT_VARIABLE rc)
execute_process(COMMAND "${CC}" --script reference.txt WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc2 OUTPUT_QUIET)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0)
  message(FATAL_ERROR "the score's own song could not be made (${rc}, ${rc2})")
endif()

# 4. measured
execute_process(COMMAND "${PY}" -c "import numpy" RESULT_VARIABLE has_numpy OUTPUT_QUIET ERROR_QUIET)
if(NOT has_numpy EQUAL 0)
  message(STATUS "solaris_demo_prelude: no numpy — made, not measured")
  return()
endif()
execute_process(COMMAND "${PY}" -I "${DEMO}/measure.py" "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the Prelude made in the window does not measure as the score:\n${out}${err}")
endif()
