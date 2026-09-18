# -----------------------------
# Standalone Unit Tests (Isolated Build)
# -----------------------------
# This CMakeLists.txt can be built independently without the main project
# Usage: cd tests && cmake && make

cmake_minimum_required(VERSION 3.15)
project(nitro_tests C CXX)

find_package(OpenSSL REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(nitro_tests
  ../yyjson/src/yyjson.c
  ../src/json.cpp
  ../src/graph.cpp
  ../src/logging.cpp
  ../src/string_utils.cpp
  ../src/mcp_format.cpp
  ../src/sha1.cpp
  src/utf8_test.cpp
  src/file_test.cpp
  src/graph_test.cpp
  src/string_utils_test.cpp
  src/mcp_format_test.cpp
  src/sha1_test.cpp
  src/main.cpp
)

pkg_check_modules(NC QUIET IMPORTED_TARGET notcurses)
if(NC_FOUND)
  set(NC_TARGET PkgConfig::NC)
endif()

# Basic test includes (no llama, no curl dependencies)
target_include_directories(nitro_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../yyjson/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../lib/utfcpp/source
)

target_link_libraries(nitro_tests PRIVATE
  OpenSSL::Crypto
  ${NC_TARGET}
)

set_target_properties(nitro_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Add test discovery
enable_testing()
add_test(NAME nitro_tests COMMAND nitro_tests)

