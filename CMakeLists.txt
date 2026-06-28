cmake_minimum_required(VERSION 3.24)
project(blackhole4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BH_WITH_NANOVDB "Enable NanoVDB volumetric grid support" ON)

include(FetchContent)

find_package(Vulkan REQUIRED)

# ---------------------------------------------------------------- dependencies

# GLFW (windowing)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz)

# GLM (math)
FetchContent_Declare(glm
    URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz)

# VMA (used header-only; SOURCE_SUBDIR points nowhere so its CMakeLists is skipped)
FetchContent_Declare(vma
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.1.0.tar.gz
    SOURCE_SUBDIR _do_not_add_subdir_)

# Dear ImGui (no CMakeLists in repo root; we compile the sources ourselves)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.5.tar.gz)

# TinyEXR (compiled manually together with its bundled miniz)
FetchContent_Declare(tinyexr
    URL https://github.com/syoyo/tinyexr/archive/refs/tags/v1.0.9.tar.gz
    SOURCE_SUBDIR _do_not_add_subdir_)

# stb (HDR image loading)
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/5c205738c191bcb0abc65c4febfa9bd25ff35234.tar.gz
    SOURCE_SUBDIR _do_not_add_subdir_)

set(_deps glfw glm vma imgui tinyexr stb)

if(BH_WITH_NANOVDB)
    # NanoVDB is header-only and lives inside the OpenVDB repository.
    # We only need the headers; nothing from OpenVDB proper is built.
    FetchContent_Declare(openvdb
        URL https://github.com/AcademySoftwareFoundation/openvdb/archive/refs/tags/v12.0.0.tar.gz
        SOURCE_SUBDIR _do_not_add_subdir_)
    list(APPEND _deps openvdb)
endif()

FetchContent_MakeAvailable(${_deps})

# ImGui static library
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC glfw Vulkan::Vulkan)

# TinyEXR static library
add_library(tinyexr_lib STATIC
    ${tinyexr_SOURCE_DIR}/tinyexr.cc
    ${tinyexr_SOURCE_DIR}/deps/miniz/miniz.c)
target_include_directories(tinyexr_lib PUBLIC
    ${tinyexr_SOURCE_DIR} ${tinyexr_SOURCE_DIR}/deps/miniz)
target_compile_definitions(tinyexr_lib PUBLIC TINYEXR_USE_MINIZ=1)

# ------------------------------------------------------------------- shaders

find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin REQUIRED)

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

set(SHADER_SOURCES trace.comp post.vert post.frag)
set(SHADER_INCLUDES
    ${SHADER_SRC_DIR}/common.glsl
    ${SHADER_SRC_DIR}/kerr.glsl
    ${SHADER_SRC_DIR}/integrators.glsl
    ${SHADER_SRC_DIR}/disk.glsl
    ${SHADER_SRC_DIR}/env.glsl
    ${SHADER_SRC_DIR}/volume.glsl)

set(SPV_FILES "")
foreach(SHADER ${SHADER_SOURCES})
    set(SPV ${SHADER_BIN_DIR}/${SHADER}.spv)
    add_custom_command(
        OUTPUT ${SPV}
        COMMAND ${GLSLC} -O --target-env=vulkan1.2 -I ${SHADER_SRC_DIR}
                ${SHADER_SRC_DIR}/${SHADER} -o ${SPV}
        DEPENDS ${SHADER_SRC_DIR}/${SHADER} ${SHADER_INCLUDES}
        COMMENT "Compiling ${SHADER}")
    list(APPEND SPV_FILES ${SPV})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPV_FILES})

# ----------------------------------------------------------------------- app

add_executable(blackhole4
    src/main.cpp
    src/vk/Context.cpp
    src/vk/Swapchain.cpp
    src/vk/Resources.cpp
    src/vk/Pipeline.cpp
    src/app/Camera.cpp
    src/app/Blackbody.cpp
    src/app/AssetLoad.cpp
    src/app/Renderer.cpp)

add_dependencies(blackhole4 shaders)

target_include_directories(blackhole4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${vma_SOURCE_DIR}/include
    ${stb_SOURCE_DIR})

target_link_libraries(blackhole4 PRIVATE
    Vulkan::Vulkan glfw glm::glm imgui_lib tinyexr_lib)

target_compile_definitions(blackhole4 PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN
    BH_SHADER_DIR="${SHADER_BIN_DIR}")

if(BH_WITH_NANOVDB)
    target_include_directories(blackhole4 PRIVATE ${openvdb_SOURCE_DIR}/nanovdb)
    target_compile_definitions(blackhole4 PRIVATE BH_WITH_NANOVDB=1)
endif()

# OpenVDB (.vdb reading, e.g. Houdini exports). Built once into extern/install
# by scripts/build_openvdb.ps1; picked up automatically when present.
set(BH_OPENVDB_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/extern/install)
if(EXISTS ${BH_OPENVDB_PREFIX}/lib/libopenvdb.lib)
    message(STATUS "OpenVDB found in ${BH_OPENVDB_PREFIX} — .vdb support enabled")
    # note: OPENVDB_USE_DELAYED_LOADING must stay UNdefined (the headers test
    # it with #ifdef); the library was built without delayed loading
    target_compile_definitions(blackhole4 PRIVATE
        BH_WITH_OPENVDB=1 OPENVDB_STATICLIB)
    target_include_directories(blackhole4 PRIVATE ${BH_OPENVDB_PREFIX}/include)
    target_link_libraries(blackhole4 PRIVATE
        ${BH_OPENVDB_PREFIX}/lib/libopenvdb.lib
        ${BH_OPENVDB_PREFIX}/lib/libblosc.lib
        ${BH_OPENVDB_PREFIX}/lib/zlibstatic.lib
        ${BH_OPENVDB_PREFIX}/lib/tbb12.lib)
    add_custom_command(TARGET blackhole4 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${BH_OPENVDB_PREFIX}/bin/tbb12.dll $<TARGET_FILE_DIR:blackhole4>)
else()
    message(STATUS "OpenVDB not found (run scripts/build_openvdb.ps1 for .vdb support)")
endif()

if(MSVC)
    # /Zc:__cplusplus: NanoVDB checks __cplusplus to select C++17+ code paths
    target_compile_options(blackhole4 PRIVATE /bigobj /Zc:__cplusplus)
endif()

# Test-volume generator (procedural torus .nvdb for exercising --vdb)
if(BH_WITH_NANOVDB)
    add_executable(make_test_volume tools/make_test_volume.cpp)
    target_include_directories(make_test_volume PRIVATE ${openvdb_SOURCE_DIR}/nanovdb)
    target_compile_definitions(make_test_volume PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(make_test_volume PRIVATE /bigobj /Zc:__cplusplus)
    endif()
endif()
