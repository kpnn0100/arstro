# solaris_model — the .slp document: parse, serialize, validate. No sound, no I/O, no dependency.
add_library(solaris_model STATIC Format.cpp Project.cpp)
target_include_directories(solaris_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(solaris_model PUBLIC cxx_std_17)

add_executable(solaris_model_tests tests/modelTests.cpp)
target_link_libraries(solaris_model_tests PRIVATE solaris_model)
add_test(NAME solaris_model COMMAND solaris_model_tests)
