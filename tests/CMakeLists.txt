# -----------------------------
# Standalone Unit Tests (Isolated Build)
# -----------------------------
# This CMakeLists.txt can be built independently without the main project
# Usage: cd tests && cmake && make

cmake_minimum_required(VERSION 3.15)
project(hali_tests C CXX)

find_package(OpenSSL REQUIRED)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/gen_skills.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB TEST_CORE_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/*.cpp)
file(GLOB TEST_MODEL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/model/*.cpp)
file(GLOB TEST_UI_SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/*.cpp)
file(GLOB TEST_TOOLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/tools/*.cpp)
file(GLOB TEST_UTIL_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/../src/util/*.cpp)

# Exclude files that are #include'd directly by test files or replaced by stubs
list(REMOVE_ITEM TEST_UI_SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/input.cpp)
list(REMOVE_ITEM TEST_UI_SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/tui.cpp)
list(REMOVE_ITEM TEST_UI_SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/gpu_info.cpp)
list(REMOVE_ITEM TEST_UI_SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/webview.cpp)
list(REMOVE_ITEM TEST_UI_SOURCES    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/ui_text.cpp)
list(REMOVE_ITEM TEST_CORE_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/agent.cpp)
list(REMOVE_ITEM TEST_TOOLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/tools/curl.cpp)
list(REMOVE_ITEM TEST_TOOLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/tools/file.cpp)

# -------------------------------
# Backend selection (mirror main CMakeLists)
# -------------------------------
set(LLAMA_BACKEND "AUTO" CACHE STRING "Select llama.cpp backend: AUTO, CPU, GPU, CUDA")
set_property(CACHE LLAMA_BACKEND PROPERTY STRINGS AUTO CPU GPU CUDA)

set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
set(GGML_OPENCL OFF CACHE BOOL "" FORCE)
set(GGML_KOMPUTE OFF CACHE BOOL "" FORCE)
set(GGML_SYCL OFF CACHE BOOL "" FORCE)
set(GGML_ACCELERATE OFF CACHE BOOL "" FORCE)

include(CheckLanguage)
if(LLAMA_BACKEND STREQUAL "CPU")
  set(GGML_NATIVE ON CACHE BOOL "" FORCE)
elseif(LLAMA_BACKEND STREQUAL "GPU")
  set(GGML_OPENMP ON CACHE BOOL "" FORCE)
elseif(LLAMA_BACKEND STREQUAL "CUDA")
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    set(GGML_CUDA_GRAPHS ON CACHE BOOL "" FORCE)
  else()
    message(FATAL_ERROR "CUDA backend requested but nvcc not found")
  endif()
elseif(LLAMA_BACKEND STREQUAL "AUTO")
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    set(GGML_CUDA_GRAPHS ON CACHE BOOL "" FORCE)
  else()
    set(GGML_OPENMP ON CACHE BOOL "" FORCE)
    set(GGML_NATIVE ON CACHE BOOL "" FORCE)
  endif()
else()
  message(FATAL_ERROR "Invalid LLAMA_BACKEND value: ${LLAMA_BACKEND}")
endif()

set(LLAMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../llama.cpp)
add_subdirectory(${LLAMA_DIR}/ggml ${CMAKE_BINARY_DIR}/llama.cpp/ggml)
add_subdirectory(${LLAMA_DIR} ${CMAKE_BINARY_DIR}/llama.cpp)

add_executable(hali_tests
  ../yyjson/src/yyjson.c
  ${TEST_CORE_SOURCES}
  ${TEST_MODEL_SOURCES}
  ${TEST_UI_SOURCES}
  ${TEST_TOOLS_SOURCES}
  ${TEST_UTIL_SOURCES}
  src/gpu_info_stub.cpp
  src/tui_stub.cpp
  src/agent_stub.cpp
  src/webview_stub.cpp
  src/utf8_test.cpp
  src/file_test.cpp
  src/graph_test.cpp
  src/string_utils_test.cpp
  src/mcp_format_test.cpp
  src/sha1_test.cpp
  src/input_test.cpp
  src/agent_utils_test.cpp
  src/run_allowed_test.cpp
  src/curl_test.cpp
  src/mtp_adaptive_test.cpp
  src/logging_filter_test.cpp
  src/replace_test.cpp
  src/build_test.cpp
  src/table_test.cpp
  src/test_think_parser.cpp
  src/usage_log_test.cpp
  src/suffix_drafter_test.cpp
  src/audio_test.cpp
  src/config_test.cpp
  src/test_planner.cpp
  src/audit_test.cpp
  src/sysinfo_test.cpp
  src/git_tool_test.cpp
  src/music_project_test.cpp
  src/rag_test.cpp
  src/knowledge_test.cpp
  src/gui_test.cpp
  src/main.cpp
)

pkg_check_modules(NC QUIET IMPORTED_TARGET notcurses)
if(NC_FOUND)
  set(NC_TARGET PkgConfig::NC)
endif()

# Basic test includes (no llama, no curl dependencies)
target_include_directories(hali_tests PRIVATE
  ${CMAKE_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../yyjson/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/model
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/tools
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/util
  ${CMAKE_CURRENT_SOURCE_DIR}/../llama.cpp/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../llama.cpp/ggml/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../llama.cpp/common
  ${CMAKE_CURRENT_SOURCE_DIR}/../libs
  ${CMAKE_CURRENT_SOURCE_DIR}/../llama.cpp/vendor
)

find_package(CURL REQUIRED)

target_link_libraries(hali_tests PRIVATE
  llama
  llama-common
  ggml
  OpenSSL::Crypto
  CURL::libcurl
  ${NC_TARGET}
)

set_target_properties(hali_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Add test discovery
enable_testing()
add_test(NAME hali_tests COMMAND hali_tests)

