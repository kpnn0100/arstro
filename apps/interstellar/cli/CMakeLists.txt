# interstellar-cc — the headless CLI front end over InterstellarService (R-CLI-1).
#
# It holds argv, stdout, the clock, the codecs and the filesystem — and NO behaviour. Every
# state change goes in as a Command and everything printed comes out of AppModel/Event. If a
# subcommand here needs a behaviour no Command expresses, that is a defect in the command set.
#
# No GTK and no display: the PNG/PPM writer is a few lines of its own, so the CLI needs no
# image library at all to produce the one output a golden test can compare byte-for-byte
# (R-RENDER-3).
add_executable(interstellar-cc ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(interstellar-cc PRIVATE interstellar_core)
