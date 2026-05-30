cmake_minimum_required(VERSION 3.24)
project(BlackHole2 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(BH2_USE_OPENVDB "Enable OpenVDB/NanoVDB disk loading" OFF)

# ── Vulkan SDK ───────────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)

# ── FetchContent dependencies ────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE)

FetchContent_Declare(tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG        v1.0.9
    GIT_SHALLOW    TRUE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    GIT_SHALLOW    TRUE)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(glfw glm vma tinyexr imgui stb)

# ── ImGui build (Vulkan + GLFW backend) ─────────────────────────────────────
set(IMGUI_DIR ${imgui_SOURCE_DIR})
add_library(imgui_impl STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui_impl PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui_impl PUBLIC Vulkan::Vulkan glfw)

# ── NanoVDB (standalone headers only — no Boost/TBB needed) ─────────────────
# We fetch only the nanovdb header directory from a pinned commit.
# Full OpenVDB CPU loading (for Houdini .vdb files) requires BH2_USE_OPENVDB=ON
# plus vcpkg: vcpkg install openvdb:x64-windows
FetchContent_Declare(nanovdb_headers
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openvdb.git
    GIT_TAG        v12.0.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  nanovdb)   # only configure the nanovdb subdir (no Boost needed)
FetchContent_MakeAvailable(nanovdb_headers)

add_library(nanovdb INTERFACE)
target_include_directories(nanovdb INTERFACE ${nanovdb_headers_SOURCE_DIR}/nanovdb)

if(BH2_USE_OPENVDB)
    find_package(OpenVDB REQUIRED COMPONENTS openvdb)
    target_link_libraries(blackhole2 PRIVATE OpenVDB::openvdb)
    target_compile_definitions(blackhole2 PRIVATE BH2_HAS_NANOVDB=1)
endif()

# ── stb as interface library ────────────────────────────────────────────────
add_library(stb_lib INTERFACE)
target_include_directories(stb_lib INTERFACE ${stb_SOURCE_DIR})

# ── tinyexr as interface library ────────────────────────────────────────────
add_library(tinyexr_lib INTERFACE)
target_include_directories(tinyexr_lib INTERFACE
    ${tinyexr_SOURCE_DIR}
    ${tinyexr_SOURCE_DIR}/deps/miniz)

# ── Shader compilation ──────────────────────────────────────────────────────
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin)
if(NOT GLSLC)
    message(WARNING "glslc not found — shaders must be compiled manually")
endif()

set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SPIRV_DIR  ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SPIRV_DIR})

set(SHADER_FILES
    ${SHADER_DIR}/trace.comp.glsl
    ${SHADER_DIR}/tonemap.frag.glsl
    ${SHADER_DIR}/fullscreen.vert.glsl)
set(SHADER_STAGES compute fragment vertex)

list(LENGTH SHADER_FILES SHADER_COUNT)
math(EXPR SHADER_LAST "${SHADER_COUNT} - 1")

foreach(I RANGE ${SHADER_LAST})
    list(GET SHADER_FILES  ${I} SHADER)
    list(GET SHADER_STAGES ${I} STAGE)
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUT ${SPIRV_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPIRV_OUT}
        COMMAND ${GLSLC}
            -O
            --target-env=vulkan1.2
            -fshader-stage=${STAGE}
            -I ${SHADER_DIR}
            -o ${SPIRV_OUT}
            ${SHADER}
        DEPENDS ${SHADER}
                ${SHADER_DIR}/kerr.glsl
                ${SHADER_DIR}/disk.glsl
                ${SHADER_DIR}/sky.glsl
                ${SHADER_DIR}/blackbody.glsl
        COMMENT "Compiling ${SHADER_NAME} (${STAGE}) -> SPIR-V")
    list(APPEND SPIRV_OUTPUTS ${SPIRV_OUT})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPIRV_OUTPUTS})

# ── Main executable ─────────────────────────────────────────────────────────
file(GLOB_RECURSE BH2_SOURCES
    src/*.cpp
    src/*.h)

add_executable(blackhole2 ${BH2_SOURCES})
add_dependencies(blackhole2 shaders)

target_include_directories(blackhole2 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS})

target_link_libraries(blackhole2 PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    GPUOpen::VulkanMemoryAllocator
    imgui_impl
    tinyexr
    miniz
    tinyexr_lib
    stb_lib
    nanovdb)

target_compile_definitions(blackhole2 PRIVATE
    BH2_SHADER_DIR="${SPIRV_DIR}"
    BH2_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE)

# ── Install ─────────────────────────────────────────────────────────────────
install(TARGETS blackhole2 RUNTIME DESTINATION bin)
install(DIRECTORY ${SPIRV_DIR}/ DESTINATION share/blackhole2/shaders)
install(DIRECTORY assets/ DESTINATION share/blackhole2/assets)
