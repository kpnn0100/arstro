# solaris_engine — renders a MixGraph through the DSP library's devices. Knows no project, no file;
# implements no DSP (R-DSP-1): every instrument and effect is arstro_dsp's.
add_library(solaris_engine STATIC Engine.cpp)
target_include_directories(solaris_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(solaris_engine PUBLIC arstro_dsp)
target_compile_features(solaris_engine PUBLIC cxx_std_17)

add_executable(solaris_engine_tests tests/engineTests.cpp)
target_link_libraries(solaris_engine_tests PRIVATE solaris_engine)
add_test(NAME solaris_engine COMMAND solaris_engine_tests)
