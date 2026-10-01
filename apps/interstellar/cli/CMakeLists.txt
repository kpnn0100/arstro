# interstellar-cc — the whole application without a window (R-SVC, R-API-2).
#
# It holds argv, stdout, the clock and the codecs, and NO behaviour. If a subcommand here needs
# something no Command expresses, that is a defect in the command set.
add_executable(interstellar-cc ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
if(TARGET interstellar_host)
  target_link_libraries(interstellar-cc PRIVATE interstellar_host)
else()
  target_link_libraries(interstellar-cc PRIVATE interstellar_core)
endif()
