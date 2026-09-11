# gene_core — Gene, the shared binding language: every numeric field of an authored
# object can be an EXPRESSION over the others rather than a number.
#
# Promoted out of genesis_core (2026-09-11) when Interstellar needed the same language to
# bind one parameter to a calculation over others (interstellar R-BIND-2). Two consumers is
# what justifies a library; one is what makes an API shaped by exactly one caller.
#
# Dependency-free by construction — no Artboard, no threads, no OS, no I/O — so it links into
# a UI-free core, a CLI and a test with nothing coming along. `emitCpp` writes the text
# `artboard::Color{...}` but includes no Artboard header: it is a code generator, and the
# generated code is somebody else's to compile.
add_library(gene_core STATIC src/Gene.cpp)
target_include_directories(gene_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(gene_tests tests/geneTests.cpp)
target_link_libraries(gene_tests PRIVATE gene_core)
add_test(NAME gene COMMAND gene_tests)
