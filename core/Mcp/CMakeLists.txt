# Arstro Mcp - an Arstro app as a Model Context Protocol server (docs: README.md).
#
# The protocol only: JSON-RPC 2.0 over stdio, initialize / tools/list / tools/call. The tools
# are supplied by each app's CLI and hold no behaviour of their own - they dispatch the app's
# text grammar and read its model (arstro.rule section 1). Platform-free but for Stdio.cpp.
# Reuses Ntwb's dependency-free Json rather than growing a second one.
cmake_minimum_required(VERSION 3.16)
project(arstro_mcp LANGUAGES CXX)

add_library(arstro_mcp STATIC
  src/Server.cpp
  src/Stdio.cpp
  src/ServiceTools.cpp)
target_include_directories(arstro_mcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(arstro_mcp PUBLIC arstro_ntwb)
target_compile_features(arstro_mcp PUBLIC cxx_std_17)

# mcp_tests: the handshake, the tool list, argument validation (an unknown key is refused),
# a throwing tool, image content, notifications, and the JSON-RPC error codes.
add_executable(mcp_tests tests/mcpTests.cpp)
target_link_libraries(mcp_tests PRIVATE arstro_mcp)
add_test(NAME mcp_tests COMMAND mcp_tests)
