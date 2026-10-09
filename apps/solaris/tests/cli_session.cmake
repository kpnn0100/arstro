# R-SVC-8: solaris-cc as an agent drives it — a script that says where it failed, comment lines that print
# nothing, everything made said, `--keep-going`, unsaved edits never dropped in silence (exit 4 unless
# `--discard`), and `shell`: one service across many stdin lines, undo kept. Run by ctest as
# solaris_cli_session with -DCC=<solaris-cc> -DWORK=<a scratch folder in the build tree>.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(ENV{SOLARIS_SETTINGS} "${WORK}/settings.txt") # never the user's own files
set(ENV{SOLARIS_RECENTS} "${WORK}/recents")

function(expect what haystack needle)
  string(FIND "${haystack}" "${needle}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "${what}: expected `${needle}` in:\n${haystack}")
  endif()
endfunction()
function(refuse what haystack needle)
  string(FIND "${haystack}" "${needle}" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "${what}: did not expect `${needle}` in:\n${haystack}")
  endif()
endfunction()
function(code what got want)
  if(NOT got EQUAL want)
    message(FATAL_ERROR "${what}: exit ${got}, expected ${want}")
  endif()
endfunction()

# 1. a script: comments (whole-line and trailing) print nothing, ids are printed ONCE, `made:` follows
file(WRITE "${WORK}/one.txt" "project new one.slp --bpm 120   # a song\n# the drums\nclip add --instrument drums --at 0 --length 8\n# a comment after it must not reprint ac_1\n\npattern steps pt_1 --pitch kick \"x...x...x...x...\"  # four on the floor\nproject save\n")
execute_process(COMMAND "${CC}" --script one.txt WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
code("a clean script" ${rc} 0)
expect("a clean script" "${out}" "ac_1\nmade: strip=ch_2 device=dv_1 pattern=pt_1 lane=ln_1\n4 notes\n")
string(REGEX MATCHALL "ac_1" ids "${out}")
list(LENGTH ids n)
if(NOT n EQUAL 1) # once, as the id — `made:` names only the others
  message(FATAL_ERROR "ac_1 printed ${n} times (a comment line reprinted it?):\n${out}")
endif()
refuse("a clean script" "${err}" "unsaved")

# 2. a refusal names its line, stops the run (exit 3) and says the edits were not saved
file(WRITE "${WORK}/two.txt" "project open one.slp\nset ch_2.gain=-3\n# a comment\nset ch_2.gian=-4\nstrip add --kind bus\n")
execute_process(COMMAND "${CC}" --script two.txt WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
code("a refused line" ${rc} 3)
expect("a refused line" "${err}" "refused: line 4: `ch_2` (a strip) has no field `gian` (did you mean: gain?)")
expect("a refused line" "${err}" "unsaved: one.slp has edits that were not saved")
refuse("a refused line" "${out}" "ch_3") # line 5 never ran

# 3. --keep-going runs past it, and still exits 3
execute_process(COMMAND "${CC}" --keep-going --discard --script two.txt WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
code("--keep-going" ${rc} 3)
expect("--keep-going" "${out}" "ch_3")
refuse("--keep-going --discard" "${err}" "unsaved")

# 4. one-shot chained lines: unsaved edits → exit 4; --discard → 0; saved → 0
execute_process(COMMAND "${CC}" project open one.slp : set ch_2.gain=-5 WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc ERROR_VARIABLE err)
code("unsaved edits" ${rc} 4)
expect("unsaved edits" "${err}" "end with `project save`, or pass --discard")
execute_process(COMMAND "${CC}" --discard project open one.slp : set ch_2.gain=-5 WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc)
code("--discard" ${rc} 0)
execute_process(COMMAND "${CC}" project open one.slp : ls WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out)
code("read only" ${rc} 0)
expect("ls" "${out}" "ch_2 \"Drum Machine\" instrument [drums dv_1] → ch_1")

# 5. shell: ONE service across the lines — ids, undo and the open song last; a refusal names its line
file(WRITE "${WORK}/session.txt" "notes add pt_1 \"snare@1 snare@3\"\nundo\nnote add pt_1 --pitch nope --at 0\nnotes add pt_1 \"clap@1 clap@3\" # the backbeat\npattern print pt_1\nproject save\n")
execute_process(COMMAND "${CC}" shell --song one.slp WORKING_DIRECTORY "${WORK}" INPUT_FILE "${WORK}/session.txt"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
code("shell" ${rc} 3) # a line was refused; the session went on
expect("shell" "${out}" "2 notes\nnotes add pt_1 snare@1 snare@3\n2 notes\n")
expect("shell" "${err}" "refused: line 3: `nope` is not a pitch")
expect("shell" "${out}" "clap@1:0.25:100  # 39")
refuse("shell" "${out}" "snare@1:") # undone, in the same session
refuse("shell" "${err}" "unsaved")

# 6. shell --song makes the song when it is not there; an unsaved session ends with exit 4
file(WRITE "${WORK}/unsaved.txt" "clip add --instrument synth --at 0\n")
execute_process(COMMAND "${CC}" shell --song fresh.slp WORKING_DIRECTORY "${WORK}" INPUT_FILE "${WORK}/unsaved.txt"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
code("shell, unsaved" ${rc} 4)
expect("shell, unsaved" "${out}" "fresh.slp\nac_1\nmade: strip=ch_2 device=dv_1 pattern=pt_1 lane=ln_1\n")
expect("shell, unsaved" "${err}" "unsaved: fresh.slp")
message(STATUS "solaris-cc: scripts, shell, line numbers, made ids, unsaved edits — as specified (R-SVC-8)")
