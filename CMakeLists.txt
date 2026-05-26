cmake_minimum_required(VERSION 3.20)

project(MeowEngine VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_library(MEOWENGINE_VULKAN_LOADER_LIBRARY
    NAMES vulkan-1
    HINTS "$ENV{VULKAN_SDK}/Lib"
    REQUIRED
)

set(NVRHI_INSTALL OFF CACHE BOOL "Generate install rules for NVRHI" FORCE)
set(NVRHI_INSTALL_EXPORTS OFF CACHE BOOL "Install NVRHI CMake exports" FORCE)
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "Build NVRHI as a shared library" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "Build NVRHI validation layer" FORCE)
set(NVRHI_WITH_AFTERMATH OFF CACHE BOOL "Include Nsight Aftermath support" FORCE)
set(NVRHI_WITH_NVAPI OFF CACHE BOOL "Include NVAPI support" FORCE)
set(NVRHI_WITH_RTXMU OFF CACHE BOOL "Use RTXMU acceleration structure management" FORCE)

set(SDL_SHARED OFF CACHE BOOL "Build SDL3 as a shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL3 as a static library" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL3 tests" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3 test library" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL3 examples" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "Enable installation of SDL3" FORCE)

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build spdlog as a shared library" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Build spdlog examples" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Build spdlog tests" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "Generate spdlog install target" FORCE)

set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Build GLM as a compiled library" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "Build GLM tests" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "Generate GLM install target" FORCE)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Build Assimp tests" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "Generate Assimp install target" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "Treat Assimp warnings as errors" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "Build Assimp tools" FORCE)
set(ENKITS_BUILD_C_INTERFACE OFF CACHE BOOL "Build enkiTS C interface" FORCE)
set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "Build enkiTS examples" FORCE)
set(ENKITS_INSTALL OFF CACHE BOOL "Generate enkiTS install target" FORCE)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)

FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.4.3
)

FetchContent_Declare(
    enkiTS
    GIT_REPOSITORY https://github.com/dougbinks/enkiTS.git
    GIT_TAG v1.11
)

FetchContent_MakeAvailable(glm assimp enkiTS)

add_subdirectory(ThirdParty/NVRHI EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/SDL EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/spdlog EXCLUDE_FROM_ALL)

add_library(imgui STATIC
    ThirdParty/imgui/imgui.cpp
    ThirdParty/imgui/imgui_draw.cpp
    ThirdParty/imgui/imgui_tables.cpp
    ThirdParty/imgui/imgui_widgets.cpp
    ThirdParty/imgui/backends/imgui_impl_dx12.cpp
)

target_include_directories(imgui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/imgui/backends
)

if(TARGET SDL3-static)
    set_target_properties(SDL3-static PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
endif()

add_subdirectory(MEngine)
add_subdirectory(SandBox)
