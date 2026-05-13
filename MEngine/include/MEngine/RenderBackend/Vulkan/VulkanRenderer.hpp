#pragma once

#include "MEngine/Camera/Camera.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanImGuiLayer.hpp"
#include "MEngine/Resources/MeshAsset.hpp"

#include <nvrhi/nvrhi.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

struct SDL_Window;

namespace MEngine::RenderBackend::Vulkan {

class VulkanDevice;
class VulkanSwapchain;

using BasicPrimitiveType = PrimitiveType;

class VulkanRenderer {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void initialize(VulkanDevice& device, VulkanSwapchain& swapchain, SDL_Window* window, bool enableRayTracing);
    void setCameraState(const Camera::CameraState& camera);
    uint64_t render(uint32_t imageIndex);
    void recreateSwapchainResources(VulkanSwapchain& swapchain, SDL_Window* window);
    void shutdown();

    void addPrimitive(BasicPrimitiveType type);
    void setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives);
    void setDynamicPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives);
    [[nodiscard]] bool shootingModeEnabled() const;
    void clearPrimitives();

private:
    struct PushConstants {
        float viewProjection[16];
        float materialParameters[4];
    };

    struct AntiAliasingConstants {
        float inverseResolutionAndMode[4];
        float temporalParameters[4];
    };

    enum class AntiAliasingMode {
        Off = 0,
        FXAA,
        TAA,
        MSAA2x,
        MSAA4x,
        MSAA8x,
    };

    struct PointLight {
        float position[3];
        float color[3];
        float intensity;
    };

    struct LightingConstants {
        float cameraPosition[4];
        float cameraForward[4];
        float cameraRight[4];
        float cameraUp[4];
        float sunDirection[4];
        float sunColorAndIntensity[4];
        float pointLightCount[4];
        float pointLightPosition[4][4];
        float pointLightColorAndIntensity[4][4];
        float skyParameters[4];
        float cloudParameters[4];
        float cloudWindParameters[4];
        float waterParameters[4];
        float waterWindParameters[4];
        float cameraParameters[4];
    };

    void createFramebuffers(VulkanSwapchain& swapchain);
    void createGBufferTextures(VulkanSwapchain& swapchain);
    void createPostProcessTextures(VulkanSwapchain& swapchain);
    void createDepthTexture(VulkanSwapchain& swapchain);
    void createAtmosphereTextures(VulkanSwapchain& swapchain);
    void createShaders();
    void createPipeline();
    void createBuffers();
    void releaseGpuResources();
    void dispatchSkyAtmosphereCompute();
    void dispatchCloudDensityCompute();
    void dispatchVolumetricCloudsCompute();
    void dispatchWaterOceanCompute();
    void rebuildRayTracingAccelerationStructures();
    void dispatchRayTracingPrototype();
    void rebuildMesh();
    void rebuildDynamicMesh();
    PushConstants buildPushConstants() const;
    LightingConstants buildLightingConstants() const;
    void drawPrimitivePanel();
    void applyPendingAntiAliasingMode();
    void applyPendingTextureFilteringSettings();
    uint32_t sampleCountForAntiAliasingMode(AntiAliasingMode mode) const;
    float effectiveMaxAnisotropy() const;
    nvrhi::ITexture* sampledGBufferPosition() const;
    nvrhi::ITexture* sampledGBufferNormal() const;
    nvrhi::ITexture* sampledGBufferAlbedo() const;
    nvrhi::ITexture* sampledGBufferMaterial() const;

    void appendTriangle(const PrimitiveInstance& primitive);
    void appendQuad(const PrimitiveInstance& primitive);
    void appendCube(const PrimitiveInstance& primitive);
    void appendSphere(const PrimitiveInstance& primitive);

    VulkanDevice* device_ = nullptr;
    VulkanSwapchain* swapchain_ = nullptr;
    nvrhi::ShaderHandle geometryVertexShader_;
    nvrhi::ShaderHandle geometryFragmentShader_;
    nvrhi::ShaderHandle pbrLightingVertexShader_;
    nvrhi::ShaderHandle pbrLightingFragmentShader_;
    nvrhi::ShaderHandle antiAliasingFragmentShader_;
    nvrhi::ShaderHandle skyAtmosphereVertexShader_;
    nvrhi::ShaderHandle skyAtmosphereFragmentShader_;
    nvrhi::ShaderHandle skyTransmittanceComputeShader_;
    nvrhi::ShaderHandle skyMultiScatteringComputeShader_;
    nvrhi::ShaderHandle skyAtmosphereComputeShader_;
    nvrhi::ShaderHandle cloudDensityComputeShader_;
    nvrhi::ShaderHandle volumetricCloudsComputeShader_;
    nvrhi::ShaderHandle waterOceanComputeShader_;
    nvrhi::ShaderHandle rayTracingRayGenShader_;
    nvrhi::ShaderHandle rayTracingMissShader_;
    nvrhi::ShaderHandle rayTracingClosestHitShader_;
    nvrhi::InputLayoutHandle inputLayout_;
    nvrhi::GraphicsPipelineHandle geometryPipeline_;
    nvrhi::GraphicsPipelineHandle pbrLightingPipeline_;
    nvrhi::GraphicsPipelineHandle antiAliasingPipeline_;
    nvrhi::GraphicsPipelineHandle skyAtmospherePipeline_;
    nvrhi::ComputePipelineHandle skyTransmittancePipeline_;
    nvrhi::ComputePipelineHandle skyMultiScatteringPipeline_;
    nvrhi::ComputePipelineHandle skyAtmosphereComputePipeline_;
    nvrhi::ComputePipelineHandle cloudDensityPipeline_;
    nvrhi::ComputePipelineHandle volumetricCloudsPipeline_;
    nvrhi::ComputePipelineHandle waterOceanPipeline_;
    nvrhi::rt::PipelineHandle rayTracingPipeline_;
    nvrhi::rt::ShaderTableHandle rayTracingShaderTable_;
    nvrhi::rt::AccelStructHandle rayTracingBlas_;
    nvrhi::rt::AccelStructHandle rayTracingTlas_;
    nvrhi::rt::GeometryDesc rayTracingGeometryDesc_;
    nvrhi::CommandListHandle commandList_;
    nvrhi::BufferHandle vertexBuffer_;
    nvrhi::BufferHandle indexBuffer_;
    nvrhi::BufferHandle dynamicVertexBuffer_;
    nvrhi::BufferHandle dynamicIndexBuffer_;
    nvrhi::BufferHandle lightingConstantsBuffer_;
    nvrhi::TextureHandle depthTexture_;
    nvrhi::TextureHandle gBufferPosition_;
    nvrhi::TextureHandle gBufferNormal_;
    nvrhi::TextureHandle gBufferAlbedo_;
    nvrhi::TextureHandle gBufferMaterial_;
    nvrhi::TextureHandle resolvedGBufferPosition_;
    nvrhi::TextureHandle resolvedGBufferNormal_;
    nvrhi::TextureHandle resolvedGBufferAlbedo_;
    nvrhi::TextureHandle resolvedGBufferMaterial_;
    nvrhi::TextureHandle postColorTexture_;
    nvrhi::TextureHandle taaHistoryTexture_;
    nvrhi::TextureHandle skyTransmittanceLut_;
    nvrhi::TextureHandle skyMultiScatteringLut_;
    nvrhi::TextureHandle skyAtmosphereTexture_;
    nvrhi::TextureHandle cloudDensityTexture_;
    nvrhi::TextureHandle volumetricCloudsTexture_;
    nvrhi::TextureHandle waterOceanTexture_;
    nvrhi::TextureHandle rayTracingOutputTexture_;
    nvrhi::SamplerHandle lightingSampler_;
    nvrhi::SamplerHandle cloudSampler_;
    nvrhi::BindingLayoutHandle geometryBindingLayout_;
    nvrhi::BindingLayoutHandle pbrLightingBindingLayout_;
    nvrhi::BindingLayoutHandle antiAliasingBindingLayout_;
    nvrhi::BindingLayoutHandle skyAtmosphereBindingLayout_;
    nvrhi::BindingLayoutHandle skyTransmittanceBindingLayout_;
    nvrhi::BindingLayoutHandle skyMultiScatteringBindingLayout_;
    nvrhi::BindingLayoutHandle skyAtmosphereComputeBindingLayout_;
    nvrhi::BindingLayoutHandle cloudDensityBindingLayout_;
    nvrhi::BindingLayoutHandle volumetricCloudsBindingLayout_;
    nvrhi::BindingLayoutHandle waterOceanBindingLayout_;
    nvrhi::BindingLayoutHandle rayTracingBindingLayout_;
    nvrhi::BindingSetHandle pbrLightingBindingSet_;
    nvrhi::BindingSetHandle antiAliasingBindingSet_;
    nvrhi::BindingSetHandle skyAtmosphereBindingSet_;
    nvrhi::BindingSetHandle skyTransmittanceBindingSet_;
    nvrhi::BindingSetHandle skyMultiScatteringBindingSet_;
    nvrhi::BindingSetHandle skyAtmosphereComputeBindingSet_;
    nvrhi::BindingSetHandle cloudDensityBindingSet_;
    nvrhi::BindingSetHandle volumetricCloudsBindingSet_;
    nvrhi::BindingSetHandle waterOceanBindingSet_;
    nvrhi::BindingSetHandle rayTracingBindingSet_;
    nvrhi::BindingSetHandle rayTracingCompositeBindingSet_;
    nvrhi::FramebufferHandle gBufferFramebuffer_;
    nvrhi::FramebufferHandle postFramebuffer_;
    std::vector<nvrhi::FramebufferHandle> framebuffers_;
    std::unique_ptr<VulkanImGuiLayer> imguiLayer_;
    std::vector<PrimitiveInstance> primitives_;
    std::vector<PrimitiveInstance> dynamicPrimitives_;
    std::vector<Resources::MeshVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<Resources::MeshVertex> dynamicVertices_;
    std::vector<uint32_t> dynamicIndices_;
    Camera::CameraState camera_;
    PointLight pointLights_[4] {
        { { 0.0f, 2.5f, 1.5f }, { 1.0f, 0.92f, 0.82f }, 30.0f },
        { { -2.5f, 1.5f, -1.5f }, { 0.45f, 0.65f, 1.0f }, 18.0f },
        { { 2.0f, 1.0f, -2.0f }, { 1.0f, 0.35f, 0.25f }, 12.0f },
        { { 0.0f, 3.5f, -3.5f }, { 0.35f, 1.0f, 0.6f }, 8.0f },
    };
    int activePointLightCount_ = 3;
    float sunDirection_[3] { -0.35f, -0.72f, -0.6f };
    float sunColor_[3] { 1.0f, 0.93f, 0.78f };
    float sunIntensity_ = 3.0f;
    float skyRayleighScale_ = 1.0f;
    float skyMieScale_ = 0.45f;
    float skyExposure_ = 1.0f;
    float cloudCoverage_ = 0.48f;
    float cloudDensity_ = 0.85f;
    float cloudHeight_ = 24.0f;
    float cloudThickness_ = 8.0f;
    float cloudWindDirection_[2] { 1.0f, 0.25f };
    float cloudWindSpeed_ = 1.5f;
    float cloudTime_ = 0.0f;
    float waterLevel_ = -0.08f;
    float waterAmplitude_ = 0.22f;
    float waterWindDirection_[2] { 1.0f, 0.2f };
    float waterWindSpeed_ = 8.0f;
    float waterChoppiness_ = 1.15f;
    float materialMetallic_ = 0.0f;
    float materialRoughness_ = 0.45f;
    float materialAmbient_ = 0.03f;
    AntiAliasingMode antiAliasingMode_ = AntiAliasingMode::FXAA;
    AntiAliasingMode pendingAntiAliasingMode_ = AntiAliasingMode::FXAA;
    uint32_t msaaSampleCount_ = 1;
    float taaHistoryWeight_ = 0.18f;
    bool taaHistoryValid_ = false;
    bool anisotropicFilteringEnabled_ = true;
    bool pendingAnisotropicFilteringEnabled_ = true;
    float anisotropyLevel_ = 8.0f;
    float pendingAnisotropyLevel_ = 8.0f;
    bool shootingModeEnabled_ = false;
    bool meshDirty_ = true;
    bool gpuMeshDirty_ = true;
    bool dynamicMeshDirty_ = true;
    bool dynamicGpuMeshDirty_ = true;
    bool rayTracingEnabled_ = false;
    bool rayTracingAccelerationStructuresDirty_ = true;
    uint32_t uploadedIndexCount_ = 0;
    uint32_t uploadedDynamicIndexCount_ = 0;
    std::chrono::steady_clock::time_point lastCloudUpdateTime_ {};
};

} // namespace MEngine::RenderBackend::Vulkan
