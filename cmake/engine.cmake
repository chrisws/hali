# cmake/engine.cmake - inference engine selection
#
# Set -DHALI_ENGINE=llama (default) or -DHALI_ENGINE=basalt

set(HALI_ENGINE "llama" CACHE STRING "Inference engine: llama|basalt")

if(HALI_ENGINE STREQUAL "basalt")

  set(BASALT_DIR "${CMAKE_SOURCE_DIR}/engine/basalt")

  # CUDA is required for basalt
  find_package(CUDAToolkit REQUIRED)
  set(CMAKE_CUDA_ARCHITECTURES "120" CACHE STRING "CUDA architectures" FORCE)

  # Build basalt as a subdirectory - its CMakeLists.txt handles
  # all internal targets and dependencies.
  add_subdirectory(${BASALT_DIR} ${CMAKE_BINARY_DIR}/basalt)

  # --- Link basalt into hali ---
  target_link_libraries(hali PRIVATE models)
  target_include_directories(hali PRIVATE ${BASALT_DIR}/include)
  target_compile_definitions(hali PRIVATE HALI_ENGINE_BASALT)

  # Compile basalt.cpp
  target_sources(hali PRIVATE ${CMAKE_SOURCE_DIR}/src/model/basalt.cpp)

else()
  # llama.cpp backend (existing)
  target_sources(hali PRIVATE
    ${CMAKE_SOURCE_DIR}/src/model/llama_sb.cpp
    ${CMAKE_SOURCE_DIR}/src/model/llama_sb_mtp.cpp
    ${CMAKE_SOURCE_DIR}/src/model/llama_sb_rag.cpp
    ${CMAKE_SOURCE_DIR}/src/model/llama_sb_suffix.cpp)
endif()