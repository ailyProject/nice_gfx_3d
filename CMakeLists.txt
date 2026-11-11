set(nice_gfx_3d_sources
    src/Memory.cpp src/Renderer.cpp src/Model.cpp src/NiceGfxPresenter.cpp)
# Older exported snapshots do not contain the optional render-task API.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/src/RenderTask.cpp")
    list(APPEND nice_gfx_3d_sources src/RenderTask.cpp)
endif()
# Source snapshots before the optional cache-workspace API remain buildable.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/src/ModelCacheWorkspace.cpp")
    list(APPEND nice_gfx_3d_sources src/ModelCacheWorkspace.cpp)
endif()

if(COMMAND idf_component_register)
    if(IDF_TARGET AND NOT IDF_TARGET MATCHES "^(esp32p4|esp32s31)$")
        message(FATAL_ERROR "NICE_GFX_3D supports ESP32-P4 and ESP32-S31 only")
    endif()
    idf_component_register(SRCS ${nice_gfx_3d_sources} INCLUDE_DIRS "src"
        REQUIRES nice_gfx esp_timer heap esp_mm esp_driver_ppa esp_rom mbedtls)
    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
        # Local hot-loop optimization only; never alter consumer FP semantics.
        set_source_files_properties(src/Renderer.cpp PROPERTIES COMPILE_OPTIONS "-O3")
    endif()
    return()
endif()

cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nice_gfx_3d VERSION 0.1.0 LANGUAGES CXX)
endif()
add_library(nice_gfx_3d STATIC ${nice_gfx_3d_sources})
add_library(NICE_GFX_3D::runtime ALIAS nice_gfx_3d)
target_compile_features(nice_gfx_3d PUBLIC cxx_std_17)
target_include_directories(nice_gfx_3d PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
set(NICE_GFX_3D_NICE_GFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../nice_gfx" CACHE PATH "NICE_GFX source directory")
target_include_directories(nice_gfx_3d PUBLIC "${NICE_GFX_3D_NICE_GFX_DIR}/src")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/Renderer.cpp PROPERTIES COMPILE_OPTIONS "-O3")
endif()
