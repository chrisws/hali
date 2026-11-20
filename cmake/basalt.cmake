# cmake/basalt.cmake - basalt inference engine setup

set(BASALT_DIR "${CMAKE_SOURCE_DIR}/engine/basalt")

include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  message(STATUS "CUDA compiler found: ${CMAKE_CUDA_COMPILER}")
else()
  message(FATAL_ERROR
    "CUDA compiler (nvcc) not found. "
    "Install it with: sudo apt install nvidia-open cuda-toolkit\n"
    "Or set CUDACXX=/usr/local/cuda/bin/nvcc"
  )
endif()

find_package(CUDAToolkit REQUIRED)
set(CMAKE_CUDA_ARCHITECTURES "120" CACHE STRING "CUDA architectures" FORCE)

# Build basalt as a subdirectory - its CMakeLists.txt handles
# all internal targets and dependencies.
add_subdirectory(${BASALT_DIR} ${CMAKE_BINARY_DIR}/basalt)

# --- Link basalt into hali ---
target_link_libraries(hali PRIVATE models)
target_include_directories(hali PRIVATE
  ${BASALT_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/model/basalt
)
target_compile_definitions(hali PRIVATE HALI_ENGINE_BASALT)

# Compile basalt.cpp
target_sources(hali PRIVATE ${CMAKE_SOURCE_DIR}/src/model/basalt/basalt.cpp)
