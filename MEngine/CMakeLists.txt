add_library(MEngine STATIC
    src/MEngine.cpp
    src/Camera/Camera.cpp
    src/RenderBackend/RenderBackend.cpp
    src/RenderBackend/D3D12/D3D12RHI.cpp
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
    src/Physics/TerrainCollision.cpp
    src/Audio/AudioSystem.cpp
    src/InputSystem/InputSystem.cpp
    src/AnimationSystem/AnimationSystem.cpp
    src/Resources/MeshAsset.cpp
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

find_program(MENGINE_GLSLANG_VALIDATOR_EXECUTABLE
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

set(MENGINE_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Shader)
set(MENGINE_SHADER_BINARY_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Resources/Shader)
set(MENGINE_HLSL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Shader/D3D12)
set(MENGINE_HLSL_BINARY_DIR ${MENGINE_SHADER_BINARY_DIR}/D3D12)

find_program(MENGINE_DXC_EXECUTABLE
    NAMES dxc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
)

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
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/ShadowDepth.vert.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=vertex ${MENGINE_SHADER_SOURCE_DIR}/ShadowDepth.vert.glsl -o ${MENGINE_SHADER_BINARY_DIR}/ShadowDepth.vert.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/ShadowDepth.vert.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/ShadowDepth.frag.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=fragment ${MENGINE_SHADER_SOURCE_DIR}/ShadowDepth.frag.glsl -o ${MENGINE_SHADER_BINARY_DIR}/ShadowDepth.frag.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/ShadowDepth.frag.glsl
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
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/SkyTransmittanceLut.comp.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=compute ${MENGINE_SHADER_SOURCE_DIR}/SkyTransmittanceLut.comp.glsl -o ${MENGINE_SHADER_BINARY_DIR}/SkyTransmittanceLut.comp.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/SkyTransmittanceLut.comp.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/SkyMultiScatteringLut.comp.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=compute ${MENGINE_SHADER_SOURCE_DIR}/SkyMultiScatteringLut.comp.glsl -o ${MENGINE_SHADER_BINARY_DIR}/SkyMultiScatteringLut.comp.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/SkyMultiScatteringLut.comp.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.comp.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=compute ${MENGINE_SHADER_SOURCE_DIR}/SkyAtmosphere.comp.glsl -o ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.comp.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/SkyAtmosphere.comp.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/CloudDensity.comp.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=compute ${MENGINE_SHADER_SOURCE_DIR}/CloudDensity.comp.glsl -o ${MENGINE_SHADER_BINARY_DIR}/CloudDensity.comp.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/CloudDensity.comp.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/VolumetricClouds.comp.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=compute ${MENGINE_SHADER_SOURCE_DIR}/VolumetricClouds.comp.glsl -o ${MENGINE_SHADER_BINARY_DIR}/VolumetricClouds.comp.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/VolumetricClouds.comp.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/WaterOcean.comp.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=compute ${MENGINE_SHADER_SOURCE_DIR}/WaterOcean.comp.glsl -o ${MENGINE_SHADER_BINARY_DIR}/WaterOcean.comp.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/WaterOcean.comp.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/SkinMesh.comp.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=compute ${MENGINE_SHADER_SOURCE_DIR}/SkinMesh.comp.glsl -o ${MENGINE_SHADER_BINARY_DIR}/SkinMesh.comp.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/SkinMesh.comp.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/RayTracingPrototype.rgen.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLANG_VALIDATOR_EXECUTABLE} -V --target-env vulkan1.2 -S rgen ${MENGINE_SHADER_SOURCE_DIR}/RayTracingPrototype.rgen.glsl -o ${MENGINE_SHADER_BINARY_DIR}/RayTracingPrototype.rgen.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/RayTracingPrototype.rgen.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/RayTracingPrototype.rmiss.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLANG_VALIDATOR_EXECUTABLE} -V --target-env vulkan1.2 -S rmiss ${MENGINE_SHADER_SOURCE_DIR}/RayTracingPrototype.rmiss.glsl -o ${MENGINE_SHADER_BINARY_DIR}/RayTracingPrototype.rmiss.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/RayTracingPrototype.rmiss.glsl
    VERBATIM
)

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/RayTracingPrototype.rchit.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLANG_VALIDATOR_EXECUTABLE} -V --target-env vulkan1.2 -S rchit ${MENGINE_SHADER_SOURCE_DIR}/RayTracingPrototype.rchit.glsl -o ${MENGINE_SHADER_BINARY_DIR}/RayTracingPrototype.rchit.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/RayTracingPrototype.rchit.glsl
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

add_custom_command(
    OUTPUT ${MENGINE_SHADER_BINARY_DIR}/AntiAliasing.frag.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_SHADER_BINARY_DIR}
    COMMAND ${MENGINE_GLSLC_EXECUTABLE} -fshader-stage=fragment ${MENGINE_SHADER_SOURCE_DIR}/AntiAliasing.frag.glsl -o ${MENGINE_SHADER_BINARY_DIR}/AntiAliasing.frag.spv
    DEPENDS ${MENGINE_SHADER_SOURCE_DIR}/AntiAliasing.frag.glsl
    VERBATIM
)

add_custom_target(MEngineShaders
    DEPENDS
        ${MENGINE_SHADER_BINARY_DIR}/Primitive.vert.spv
        ${MENGINE_SHADER_BINARY_DIR}/Primitive.frag.spv
        ${MENGINE_SHADER_BINARY_DIR}/PBRLighting.vert.spv
        ${MENGINE_SHADER_BINARY_DIR}/PBRLighting.frag.spv
        ${MENGINE_SHADER_BINARY_DIR}/ShadowDepth.vert.spv
        ${MENGINE_SHADER_BINARY_DIR}/ShadowDepth.frag.spv
        ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.vert.spv
        ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.frag.spv
        ${MENGINE_SHADER_BINARY_DIR}/SkyTransmittanceLut.comp.spv
        ${MENGINE_SHADER_BINARY_DIR}/SkyMultiScatteringLut.comp.spv
        ${MENGINE_SHADER_BINARY_DIR}/SkyAtmosphere.comp.spv
        ${MENGINE_SHADER_BINARY_DIR}/CloudDensity.comp.spv
        ${MENGINE_SHADER_BINARY_DIR}/VolumetricClouds.comp.spv
        ${MENGINE_SHADER_BINARY_DIR}/WaterOcean.comp.spv
        ${MENGINE_SHADER_BINARY_DIR}/SkinMesh.comp.spv
        ${MENGINE_SHADER_BINARY_DIR}/RayTracingPrototype.rgen.spv
        ${MENGINE_SHADER_BINARY_DIR}/RayTracingPrototype.rmiss.spv
        ${MENGINE_SHADER_BINARY_DIR}/RayTracingPrototype.rchit.spv
        ${MENGINE_SHADER_BINARY_DIR}/ImGui.vert.spv
        ${MENGINE_SHADER_BINARY_DIR}/ImGui.frag.spv
        ${MENGINE_SHADER_BINARY_DIR}/AntiAliasing.frag.spv
)

add_dependencies(MEngine MEngineShaders)

if(MENGINE_DXC_EXECUTABLE)
    set(MENGINE_D3D12_SHADER_OUTPUTS
        ${MENGINE_HLSL_BINARY_DIR}/Primitive.vs.cso
        ${MENGINE_HLSL_BINARY_DIR}/Primitive.ps.cso
        ${MENGINE_HLSL_BINARY_DIR}/Fullscreen.vs.cso
        ${MENGINE_HLSL_BINARY_DIR}/PBRLighting.ps.cso
        ${MENGINE_HLSL_BINARY_DIR}/ShadowDepth.vs.cso
        ${MENGINE_HLSL_BINARY_DIR}/ShadowDepth.ps.cso
        ${MENGINE_HLSL_BINARY_DIR}/AntiAliasing.ps.cso
        ${MENGINE_HLSL_BINARY_DIR}/SkinMesh.cs.cso
    )

    add_custom_command(
        OUTPUT ${MENGINE_HLSL_BINARY_DIR}/Primitive.vs.cso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_HLSL_BINARY_DIR}
        COMMAND ${MENGINE_DXC_EXECUTABLE} -T vs_6_0 -E VSMain ${MENGINE_HLSL_SOURCE_DIR}/Primitive.hlsl -Fo ${MENGINE_HLSL_BINARY_DIR}/Primitive.vs.cso
        DEPENDS ${MENGINE_HLSL_SOURCE_DIR}/Primitive.hlsl
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${MENGINE_HLSL_BINARY_DIR}/Primitive.ps.cso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_HLSL_BINARY_DIR}
        COMMAND ${MENGINE_DXC_EXECUTABLE} -T ps_6_0 -E PSMain ${MENGINE_HLSL_SOURCE_DIR}/Primitive.hlsl -Fo ${MENGINE_HLSL_BINARY_DIR}/Primitive.ps.cso
        DEPENDS ${MENGINE_HLSL_SOURCE_DIR}/Primitive.hlsl
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${MENGINE_HLSL_BINARY_DIR}/Fullscreen.vs.cso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_HLSL_BINARY_DIR}
        COMMAND ${MENGINE_DXC_EXECUTABLE} -T vs_6_0 -E VSMain ${MENGINE_HLSL_SOURCE_DIR}/Fullscreen.hlsl -Fo ${MENGINE_HLSL_BINARY_DIR}/Fullscreen.vs.cso
        DEPENDS ${MENGINE_HLSL_SOURCE_DIR}/Fullscreen.hlsl
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${MENGINE_HLSL_BINARY_DIR}/PBRLighting.ps.cso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_HLSL_BINARY_DIR}
        COMMAND ${MENGINE_DXC_EXECUTABLE} -T ps_6_0 -E PSMain ${MENGINE_HLSL_SOURCE_DIR}/PBRLighting.hlsl -Fo ${MENGINE_HLSL_BINARY_DIR}/PBRLighting.ps.cso
        DEPENDS ${MENGINE_HLSL_SOURCE_DIR}/PBRLighting.hlsl
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${MENGINE_HLSL_BINARY_DIR}/ShadowDepth.vs.cso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_HLSL_BINARY_DIR}
        COMMAND ${MENGINE_DXC_EXECUTABLE} -T vs_6_0 -E VSMain ${MENGINE_HLSL_SOURCE_DIR}/ShadowDepth.hlsl -Fo ${MENGINE_HLSL_BINARY_DIR}/ShadowDepth.vs.cso
        DEPENDS ${MENGINE_HLSL_SOURCE_DIR}/ShadowDepth.hlsl
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${MENGINE_HLSL_BINARY_DIR}/ShadowDepth.ps.cso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_HLSL_BINARY_DIR}
        COMMAND ${MENGINE_DXC_EXECUTABLE} -T ps_6_0 -E PSMain ${MENGINE_HLSL_SOURCE_DIR}/ShadowDepth.hlsl -Fo ${MENGINE_HLSL_BINARY_DIR}/ShadowDepth.ps.cso
        DEPENDS ${MENGINE_HLSL_SOURCE_DIR}/ShadowDepth.hlsl
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${MENGINE_HLSL_BINARY_DIR}/AntiAliasing.ps.cso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_HLSL_BINARY_DIR}
        COMMAND ${MENGINE_DXC_EXECUTABLE} -T ps_6_0 -E PSMain ${MENGINE_HLSL_SOURCE_DIR}/AntiAliasing.hlsl -Fo ${MENGINE_HLSL_BINARY_DIR}/AntiAliasing.ps.cso
        DEPENDS ${MENGINE_HLSL_SOURCE_DIR}/AntiAliasing.hlsl
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${MENGINE_HLSL_BINARY_DIR}/SkinMesh.cs.cso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MENGINE_HLSL_BINARY_DIR}
        COMMAND ${MENGINE_DXC_EXECUTABLE} -T cs_6_0 -E CSMain ${MENGINE_HLSL_SOURCE_DIR}/SkinMesh.hlsl -Fo ${MENGINE_HLSL_BINARY_DIR}/SkinMesh.cs.cso
        DEPENDS ${MENGINE_HLSL_SOURCE_DIR}/SkinMesh.hlsl
        VERBATIM
    )

    add_custom_target(MEngineD3D12Shaders DEPENDS ${MENGINE_D3D12_SHADER_OUTPUTS})
    add_dependencies(MEngine MEngineD3D12Shaders)
else()
    message(WARNING "dxc was not found; D3D12 HLSL shaders will not be compiled")
endif()

file(TO_CMAKE_PATH "${MENGINE_SHADER_BINARY_DIR}" MENGINE_SHADER_BINARY_DIR_NORMALIZED)

target_link_libraries(MEngine
    PRIVATE
        nvrhi
        imgui
        SDL3::SDL3
        ${MEOWENGINE_VULKAN_LOADER_LIBRARY}
    PUBLIC
        glm::glm
        spdlog::spdlog
)

if(WIN32 AND TARGET nvrhi_d3d12)
    target_link_libraries(MEngine PRIVATE nvrhi_d3d12 d3d12 dxgi dxguid)
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
