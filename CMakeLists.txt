cmake_minimum_required(VERSION 3.15)
project(nitro C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)

# -----------------------------
# Path to llama.cpp
# -----------------------------
set(LLAMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp)

# -----------------------------
# FORCE static builds
# -----------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LLAMA_STATIC ON CACHE BOOL "" FORCE)
set(LLAMA_SHARED OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_LLAMA_SHARED OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_GGML_SHARED OFF CACHE BOOL "" FORCE)
set(LLAMA_SERVER_BUILD OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GGML_STATIC ON CACHE BOOL "" FORCE)
set(GGML_SHARED OFF CACHE BOOL "" FORCE)
set(GGML_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(GGML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -------------------------------
# Backend selection
# -------------------------------
set(LLAMA_BACKEND "AUTO" CACHE STRING "Select llama.cpp backend: AUTO, CPU, GPU, CUDA")
set_property(CACHE LLAMA_BACKEND PROPERTY STRINGS AUTO CPU GPU CUDA)
#
# sudo apt install nvidia-open cuda-toolkit   (for CUDA backend)
# export CUDACXX=/usr/local/cuda/bin/nvcc
#

# # --- Fallback logic for CUDA compiler path ---
if(NOT DEFINED CMAKE_CUDA_COMPILER AND NOT DEFINED ENV{CUDACXX})
  find_program(CUDA_NVCC_EXECUTABLE nvcc
    HINTS /usr/local/cuda/bin /usr/local/cuda/targets/x86_64-linux/bin
    DOC "Path to the CUDA compiler"
  )
  if(CUDA_NVCC_EXECUTABLE)
    set(CMAKE_CUDA_COMPILER ${CUDA_NVCC_EXECUTABLE} CACHE FILEPATH "CUDA compiler")
  endif()
endif()

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
  message(STATUS "nitro backend: CPU-only")
  set(GGML_NATIVE ON CACHE BOOL "" FORCE)
elseif(LLAMA_BACKEND STREQUAL "GPU")
  message(STATUS "nitro backend: GPU (non-CUDA)")
  set(GGML_OPENMP ON CACHE BOOL "" FORCE)
elseif(LLAMA_BACKEND STREQUAL "CUDA")
  message(STATUS "nitro backend: CUDA")
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
  else()
    message(FATAL_ERROR "CUDA backend requested but nvcc not found")
  endif()
elseif(LLAMA_BACKEND STREQUAL "AUTO")
  message(STATUS "nitro backend: AUTO")
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    message(STATUS "CUDA detected - enabling GGML_CUDA")
  else()
    set(GGML_OPENMP ON CACHE BOOL "" FORCE)
    set(GGML_NATIVE ON CACHE BOOL "" FORCE)
    message(STATUS "CUDA not found - using CPU/OpenMP")
  endif()
else()
  message(FATAL_ERROR "Invalid LLAMA_BACKEND value: ${LLAMA_BACKEND}")
endif()

# -----------------------------
# llama.cpp / ggml
# -----------------------------
add_subdirectory(${LLAMA_DIR}/ggml)
add_subdirectory(${LLAMA_DIR})

# -----------------------------
# Dependency discovery
# -----------------------------
find_package(PkgConfig QUIET)

# ── notcurses (required) ───────────────────────────────────────────────────
set(NC_FOUND FALSE)
set(NC_TARGET "")
if(DEFINED NOTCURSES_DIR)
  find_library(NC_LIB      NAMES notcurses      HINTS "${NOTCURSES_DIR}/lib" REQUIRED)
  find_library(NC_CORE_LIB NAMES notcurses-core HINTS "${NOTCURSES_DIR}/lib")
  add_library(notcurses_imported INTERFACE IMPORTED)
  target_include_directories(notcurses_imported INTERFACE "${NOTCURSES_DIR}/include")
  target_link_libraries(notcurses_imported INTERFACE ${NC_LIB})
  if(NC_CORE_LIB)
    target_link_libraries(notcurses_imported INTERFACE ${NC_CORE_LIB})
  endif()
  set(NC_TARGET notcurses_imported)
  set(NC_FOUND TRUE)
elseif(PkgConfig_FOUND)
  pkg_check_modules(NC QUIET IMPORTED_TARGET notcurses)
  if(NC_FOUND)
    set(NC_TARGET PkgConfig::NC)
  endif()
else()
  find_library(NC_LIB      NAMES notcurses)
  find_library(NC_CORE_LIB NAMES notcurses-core)
  if(NC_LIB AND NC_CORE_LIB)
    add_library(notcurses_imported INTERFACE IMPORTED)
    target_link_libraries(notcurses_imported INTERFACE ${NC_LIB} ${NC_CORE_LIB})
    set(NC_TARGET notcurses_imported)
    set(NC_FOUND TRUE)
  endif()
endif()

if(NOT NC_FOUND)
  message(FATAL_ERROR "notcurses not found. Install libnotcurses-dev, or pass -DNOTCURSES_DIR=<prefix>.")
endif()

# ── libcurl (optional, enables TOOL:CURL) ──────────────────────────────────
set(CURL_FOUND_INTERNAL FALSE)
set(CURL_TARGET "")
if(DEFINED CURL_DIR)
  find_library(CURL_LIB NAMES curl HINTS "${CURL_DIR}/lib" REQUIRED)
  find_path(CURL_INCLUDE NAMES curl/curl.h HINTS "${CURL_DIR}/include" REQUIRED)
  add_library(curl_imported INTERFACE IMPORTED)
  target_include_directories(curl_imported INTERFACE "${CURL_INCLUDE}")
  target_link_libraries(curl_imported INTERFACE ${CURL_LIB})
  set(CURL_TARGET curl_imported)
  set(CURL_FOUND_INTERNAL TRUE)
else()
  find_package(CURL QUIET)
  if(CURL_FOUND)
    set(CURL_TARGET CURL::libcurl)
    set(CURL_FOUND_INTERNAL TRUE)
  elseif(PkgConfig_FOUND)
    pkg_check_modules(CURL_PC QUIET IMPORTED_TARGET libcurl)
    if(CURL_PC_FOUND)
      set(CURL_TARGET PkgConfig::CURL_PC)
      set(CURL_FOUND_INTERNAL TRUE)
    endif()
  endif()
  if(NOT CURL_FOUND_INTERNAL)
    find_library(CURL_LIB NAMES curl)
    find_path(CURL_INCLUDE NAMES curl/curl.h)
    if(CURL_LIB AND CURL_INCLUDE)
      add_library(curl_imported INTERFACE IMPORTED)
      target_include_directories(curl_imported INTERFACE "${CURL_INCLUDE}")
      target_link_libraries(curl_imported INTERFACE ${CURL_LIB})
      set(CURL_TARGET curl_imported)
      set(CURL_FOUND_INTERNAL TRUE)
    endif()
  endif()
endif()

if(NOT CURL_FOUND_INTERNAL)
  message(WARNING "libcurl not found - TOOL:CURL will be unavailable. "
                   "Install libcurl-dev or set -DCURL_DIR=<prefix> to enable.")
endif()

# ── yyjson (MCP dependency) ───────────────────────────────────────────────
set(JSON_FOUND FALSE)
set(JSON_TARGET "")
if(DEFINED JSON_DIR)
  find_path(JSON_INCLUDE yyjson.h HINTS "${JSON_DIR}/src" REQUIRED)
  if(JSON_INCLUDE)
    set(JSON_TARGET json_imported)
    set(JSON_FOUND TRUE)
    set(JSON_INCLUDE_DIR "${JSON_INCLUDE}")
  endif()
else()
  find_path(JSON_INCLUDE yyjson.h PATHS ${CMAKE_CURRENT_SOURCE_DIR}/yyjson/src)
  if(JSON_INCLUDE)
    add_library(json_imported INTERFACE IMPORTED)
    target_include_directories(json_imported INTERFACE "${JSON_INCLUDE}")
    set(JSON_TARGET json_imported)
    set(JSON_FOUND TRUE)
    set(JSON_INCLUDE_DIR "${JSON_INCLUDE}")
  endif()
endif()

if(NOT JSON_FOUND)
  message(FATAL_ERROR "yyjson.h not found. Install it or place it in yyjson/src/")
endif()

# -----------------------------
# nitro executable
# -----------------------------
add_executable(nitro
  src/string_utils.cpp
  yyjson/src/yyjson.c
  src/config.cpp
  src/sha1.cpp
  src/mcp_format.cpp
  src/mcp_client.cpp
  src/json.cpp
  src/input.cpp
  src/llama_sb_rag.cpp
  src/llama_sb.cpp
  src/file.cpp
  src/agent.cpp
  src/tui.cpp
  src/ui_text.cpp
  src/graph.cpp
  src/curl.cpp
  src/webview.cpp
  src/logging.cpp
  src/main.cpp
)

target_include_directories(nitro PRIVATE
  ${LLAMA_DIR}/include
  ${LLAMA_DIR}/ggml/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${JSON_INCLUDE}
)

target_link_libraries(nitro PRIVATE
  llama
  ggml
  ${NC_TARGET}
)

if(CURL_FOUND_INTERNAL)
  target_link_libraries(nitro PRIVATE ${CURL_TARGET})
  target_compile_definitions(nitro PRIVATE NITRO_HAVE_CURL=1)
else()
  target_compile_definitions(nitro PRIVATE NITRO_HAVE_CURL=0)
endif()

set_target_properties(nitro PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
