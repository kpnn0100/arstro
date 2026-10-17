# solaris_strings — the window's words in the user's language (R-ACCESS-2, docs/discussion.md Round 21): the tables in
# ../lang compiled in, the look-ups, the plural rules, the pseudo-language. No dependencies: the service links it to
# validate and list the languages, the window to draw in one.
file(GLOB SOLARIS_LANG_TABLES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../lang/*.strings)
list(SORT SOLARIS_LANG_TABLES)
set(SOLARIS_LANG_EMBEDDED ${CMAKE_CURRENT_BINARY_DIR}/LangTables.generated.cpp)
add_custom_command(OUTPUT ${SOLARIS_LANG_EMBEDDED}
  COMMAND ${CMAKE_COMMAND} "-DTABLES=${SOLARIS_LANG_TABLES}" -DOUT=${SOLARIS_LANG_EMBEDDED}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_strings.cmake
  DEPENDS ${SOLARIS_LANG_TABLES} ${CMAKE_CURRENT_SOURCE_DIR}/embed_strings.cmake
  VERBATIM
  COMMENT "solaris: the languages' tables into the binary (R-ACCESS-2)")
add_library(solaris_strings STATIC Strings.cpp ${SOLARIS_LANG_EMBEDDED})
target_include_directories(solaris_strings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(solaris_strings PUBLIC cxx_std_17)

add_executable(solaris_strings_tests tests/stringsTests.cpp)
target_link_libraries(solaris_strings_tests PRIVATE solaris_strings)
add_test(NAME solaris_strings COMMAND solaris_strings_tests)
