# cmake/llama.cmake - llama.cpp backend setup

# -----------------------------
# Path to llama.cpp
# -----------------------------
set(LLAMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine/llama.cpp)

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
set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
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
  message(STATUS "hali backend: CPU-only")
  set(GGML_NATIVE ON CACHE BOOL "" FORCE)
elseif(LLAMA_BACKEND STREQUAL "GPU")
  message(STATUS "hali backend: GPU (non-CUDA)")
  set(GGML_OPENMP ON CACHE BOOL "" FORCE)
elseif(LLAMA_BACKEND STREQUAL "CUDA")
  message(STATUS "hali backend: CUDA")
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    set(GGML_CUDA_GRAPHS ON CACHE BOOL "" FORCE)
  else()
    message(FATAL_ERROR "CUDA backend requested but nvcc not found")
  endif()
elseif(LLAMA_BACKEND STREQUAL "AUTO")
  message(STATUS "hali backend: AUTO")
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    set(GGML_CUDA_GRAPHS ON CACHE BOOL "" FORCE)
    message(STATUS "CUDA detected - enabling GGML_CUDA + CUDA graphs")
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
# llama-bench (for CUDA tuning)
# -----------------------------
if(GGML_CUDA)
  find_package(Threads REQUIRED)
  add_subdirectory(${LLAMA_DIR}/tools/llama-bench)
  set_target_properties(llama-bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  )
endif()

# -----------------------------
# Link into hali
# -----------------------------
target_link_libraries(hali PRIVATE
  llama
  llama-common
  ggml
)

target_include_directories(hali PRIVATE
  ${LLAMA_DIR}/include
  ${LLAMA_DIR}/ggml/include
  ${LLAMA_DIR}/src
  ${LLAMA_DIR}/common
)

# -----------------------------
# llama source files
# -----------------------------
target_sources(hali PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/llama/llama_sb.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/llama/llama_sb_mtp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/llama/llama_sb_rag.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/llama/llama_sb_suffix.cpp
)
