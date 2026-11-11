# pgs_puente.dll: our C API (pgs_puente.h) over paraLLEl-GS, built in its STANDALONE mode (only Granite's Vulkan
# backend, volk and the Vulkan headers; shaders are precompiled in the source tree).
# PGS_SOURCE_DIR: the paraLLEl-GS clone (scripts/obtener-parallel-gs.ps1 pins it to commit cc6184a).

if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
cmake_minimum_required(VERSION 3.18)
project(pgs_puente LANGUAGES C CXX)

set(PGS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/parallel-gs" CACHE PATH "paraLLEl-GS source tree")
if (NOT EXISTS "${PGS_SOURCE_DIR}/gs/gs_interface.hpp")
    message(FATAL_ERROR "paraLLEl-GS not found in ${PGS_SOURCE_DIR}: run scripts/obtener-parallel-gs.ps1")
endif()

# Static CRT, as paraLLEl-GS does: only the C API crosses the DLL boundary.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(PARALLEL_GS_STANDALONE ON CACHE BOOL "" FORCE)
add_subdirectory("${PGS_SOURCE_DIR}" parallel-gs EXCLUDE_FROM_ALL)

# Our own compute shader (compose_circuits.comp) is compiled to SPIR-V at build time with the glslang of Granite's
# submodule (only its standalone compiler, without SPIRV-Tools) and embedded as a C array.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${PGS_SOURCE_DIR}/Granite/third_party/glslang" glslang EXCLUDE_FROM_ALL)

set(PGS_COMPOSE_SPIRV "${CMAKE_CURRENT_BINARY_DIR}/generated/compose_circuits.spv.h")
add_custom_command(
    OUTPUT "${PGS_COMPOSE_SPIRV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND glslang-standalone -V --target-env vulkan1.1 --vn kComposeCircuitsSpirv -o "${PGS_COMPOSE_SPIRV}"
            "${CMAKE_CURRENT_SOURCE_DIR}/compose_circuits.comp"
    DEPENDS glslang-standalone "${CMAKE_CURRENT_SOURCE_DIR}/compose_circuits.comp"
    COMMENT "SPIR-V of compose_circuits.comp"
    VERBATIM)

add_library(pgs_puente SHARED pgs_puente.cpp pgs_puente.h compose_circuits.comp "${PGS_COMPOSE_SPIRV}")
set_target_properties(pgs_puente PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON PREFIX "")
target_include_directories(pgs_puente PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pgs_puente PRIVATE parallel-gs)
if (MSVC)
    target_compile_options(pgs_puente PRIVATE /D_CRT_SECURE_NO_WARNINGS /wd4267 /wd4244)
endif()
