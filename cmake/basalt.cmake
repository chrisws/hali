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
#
# Force-include the printf redirect header into every basalt translation
# unit so that std::fwrite / std::printf / std::fprintf are renamed to
# hali_basalt_* wrappers at compile time.  Save/restore CMAKE flags so
# the -include does not leak into hali's own compilation.
set(_hali_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
set(_hali_saved_c_flags   "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${CMAKE_SOURCE_DIR}/src/engine/basalt/basalt_printf_redirect.h")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -include ${CMAKE_SOURCE_DIR}/src/engine/basalt/basalt_printf_redirect.h")

add_subdirectory(${BASALT_DIR} ${CMAKE_BINARY_DIR}/basalt)

set(CMAKE_CXX_FLAGS "${_hali_saved_cxx_flags}")
set(CMAKE_C_FLAGS   "${_hali_saved_c_flags}")

# --- Printf redirect wrappers (static lib for all basalt targets) ---
add_library(basalt_printf_redirect STATIC
  ${CMAKE_SOURCE_DIR}/src/engine/basalt/basalt_printf_redirect.cpp
)
target_include_directories(basalt_printf_redirect PRIVATE
  ${CMAKE_SOURCE_DIR}/src/util
)

# Link the wrappers into every real target built by the basalt subdirectory
get_property(_basalt_targets DIRECTORY ${CMAKE_BINARY_DIR}/basalt
  PROPERTY BUILDSYSTEM_TARGETS)
foreach(_tgt IN LISTS _basalt_targets)
  if(TARGET ${_tgt})
    get_target_property(_tgt_type ${_tgt} TYPE)
    if(_tgt_type STREQUAL "STATIC_LIBRARY"
       OR _tgt_type STREQUAL "SHARED_LIBRARY"
       OR _tgt_type STREQUAL "MODULE_LIBRARY"
       OR _tgt_type STREQUAL "EXECUTABLE")
      target_link_libraries(${_tgt} PRIVATE basalt_printf_redirect)
    endif()
  endif()
endforeach()

# --- Link basalt into hali ---
target_link_libraries(hali PRIVATE models basalt_printf_redirect)
target_include_directories(hali PRIVATE
  ${BASALT_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/engine/basalt
)
target_compile_definitions(hali PRIVATE HALI_ENGINE_BASALT)

# Compile basalt.cpp + no-op RAG stubs + printf redirect wrappers
target_sources(hali PRIVATE
  ${CMAKE_SOURCE_DIR}/src/engine/basalt/basalt.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/basalt/basalt_rag.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/basalt/basalt_printf_redirect.cpp
)
