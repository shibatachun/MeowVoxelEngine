add_library(MEngine STATIC
    src/MEngine.cpp
    src/Camera/Camera.cpp
    src/RenderBackend/RenderBackend.cpp
    src/RenderBackend/Vulkan/VulkanBuffer.cpp
    src/RenderBackend/Vulkan/VulkanCommandContext.cpp
    src/RenderBackend/Vulkan/VulkanDevice.cpp
    src/RenderBackend/Vulkan/VulkanImGuiLayer.cpp
    src/RenderBackend/Vulkan/VulkanImage.cpp
    src/RenderBackend/Vulkan/VulkanRenderer.cpp
    src/RenderBackend/Vulkan/VulkanRenderThread.cpp
    src/RenderBackend/Vulkan/VulkanRHI.cpp
    src/RenderBackend/Vulkan/VulkanShader.cpp
    src/RenderBackend/Vulkan/VulkanSwapchain.cpp
    src/RenderBackend/Vulkan/VulkanUtils.cpp
    src/Windows/Window.cpp
    src/Physics/PhysicsWorld.cpp
    src/Audio/AudioSystem.cpp
    src/InputSystem/InputSystem.cpp
    src/AnimationSystem/AnimationSystem.cpp
)

target_include_directories(MEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(MEngine PUBLIC cxx_std_17)

find_program(MENGINE_GLSLC_EXECUTABLE
    NAMES glslc
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

set(MENGINE_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Shader)
set(MENGINE_SHADER_BINARY_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Resources/Shader)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/Primitive.vert.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=vertex ${MENGINE_SHADER_SOURCE_DIR}/Primitive.vert.glsl -o ${MENGINE_SHADER_BINARY_DIR}/Primitive.vert.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/Primitive.vert.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/Primitive.frag.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=fragment ${MENGINE_SHADER_SOURCE_DIR}/Primitive.frag.glsl -o ${MENGINE_SHADER_BINARY_DIR}/Primitive.frag.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/Primitive.frag.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/PBRLighting.vert.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=vertex ${MENGINE_SHADER_SOURCE_DIR}/PBRLighting.vert.glsl -o ${MENGINE_SHADER_BINARY_DIR}/PBRLighting.vert.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/PBRLighting.vert.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/PBRLighting.frag.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=fragment ${MENGINE_SHADER_SOURCE_DIR}/PBRLighting.frag.glsl -o ${MENGINE_SHADER_BINARY_DIR}/PBRLighting.frag.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/PBRLighting.frag.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.vert.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=vertex ${MENGINE_SHADER_SOURCE_DIR}/SkyAtmosphere.vert.glsl -o ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.vert.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/SkyAtmosphere.vert.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.frag.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=fragment ${MENGINE_SHADER_SOURCE_DIR}/SkyAtmosphere.frag.glsl -o ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.frag.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/SkyAtmosphere.frag.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/ImGui.vert.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=vertex ${MENGINE_SHADER_SOURCE_DIR}/ImGui.vert.glsl -o ${MENGINE_SHADER_BINARY_DIR}/ImGui.vert.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/ImGui.vert.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/ImGui.frag.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=fragment ${MENGINE_SHADER_SOURCE_DIR}/ImGui.frag.glsl -o ${MENGINE_SHADER_BINARY_DIR}/ImGui.frag.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/ImGui.frag.glsl
    VERBATIM
)

add_custom_target(MEngineShaders
    DEPENDS
        ${MENGINE_SHADER_BINARY_DIR}/Primitive.vert.spv
        ${MENGINE_SHADER_BINARY_DIR}/Primitive.frag.spv
        ${MENGINE_SHADER_BINARY_DIR}/PBRLighting.vert.spv
        ${MENGINE_SHADER_BINARY_DIR}/PBRLighting.frag.spv
        ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.vert.spv
        ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.frag.spv
        ${MENGINE_SHADER_BINARY_DIR}/ImGui.vert.spv
        ${MENGINE_SHADER_BINARY_DIR}/ImGui.frag.spv
)

add_dependencies(MEngine MEngineShaders)

file(TO_CMAKE_PATH "${MENGINE_SHADER_BINARY_DIR}" MENGINE_SHADER_BINARY_DIR_NORMALIZED)

target_link_libraries(MEngine
    PRIVATE
        nvrhi
        imgui
        SDL3::SDL3
        ${MEOWENGINE_VULKAN_LOADER_LIBRARY}
    PUBLIC
        spdlog::spdlog
)

if(WIN32 AND TARGET nvrhi_d3d12)
    target_link_libraries(MEngine PRIVATE nvrhi_d3d12)
endif()

if(TARGET nvrhi_vk)
    target_link_libraries(MEngine PRIVATE nvrhi_vk Vulkan-Headers)
endif()

target_compile_definitions(MEngine
    PRIVATE
        MENGINE_BUILD_STATIC
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        MENGINE_SHADER_BINARY_DIR="${MENGINE_SHADER_BINARY_DIR_NORMALIZED}"
)
