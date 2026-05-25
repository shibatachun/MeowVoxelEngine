#pragma once

#include "MEngine/AnimationSystem/AnimationSystem.hpp"
#include "MEngine/Camera/Camera.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanImGuiLayer.hpp"
#include "MEngine/Resources/MeshAsset.hpp"

#include <glm/glm.hpp>
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
    void setMeshAsset(const Resources::MeshAsset& asset);
    void setMeshWorldTransform(const glm::mat4& transform);
    void setMeshSkinningMatrices(const std::vector<glm::mat4>& matrices);
    [[nodiscard]] bool playerControlModeEnabled() const;
    [[nodiscard]] bool shootingModeEnabled() const;
    [[nodiscard]] AnimationSystem::AnimationTuning animationTuning() const;
    bool consumePlayerResetRequested();
    bool consumePlayRequested();
    bool consumeModelLoadRequested(std::string& outPath);
    void setEditorPlayMode(bool enabled);
    void clearPrimitives();

private:
    struct PushConstants {
        float viewProjection[16];
        float materialParameters[4];
    };

    struct AntiAliasingConstants {
        float inverseResolutionAndMode[4];
        float temporalParameters[4];
        float lensFlareSun[4];
        float lensFlareColor[4];
    };

    struct SkinningConstants {
        float fitTransform[16];
        uint32_t vertexCount = 0;
        uint32_t jointCount = 0;
        uint32_t _padding[2] {};
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
        float actorShadowParameters[4];
        float shadowLightViewProjection[16];
    };

    void createFramebuffers(VulkanSwapchain& swapchain);
    void createGBufferTextures(VulkanSwapchain& swapchain);
    void createPostProcessTextures(VulkanSwapchain& swapchain);
    void createDepthTexture(VulkanSwapchain& swapchain);
    void createShadowResources();
    void createAtmosphereTextures(VulkanSwapchain& swapchain);
    void createShaders();
    void createPipeline();
    void createBuffers();
    void createGeometryBindingSets();
    void uploadGeometryTextures();
    void createSkinningBindingSet();
    void dispatchSkinningCompute();
    void releaseGpuResources();
    void dispatchSkyAtmosphereCompute();
    void dispatchCloudDensityCompute();
    void dispatchVolumetricCloudsCompute();
    void dispatchWaterOceanCompute();
    void rebuildRayTracingAccelerationStructures();
    void dispatchRayTracingPrototype();
    void rebuildMesh();
    void rebuildDynamicMesh();
    void computeMeshFitTransform();
    glm::mat4 buildShadowViewProjection() const;
    PushConstants buildPushConstants() const;
    PushConstants buildShadowPushConstants() const;
    LightingConstants buildLightingConstants() const;
    void drawPrimitivePanel();
    void drawEditorTopBar();
    void drawResourcePanel();
    void refreshModelResources();
    void loadSelectedModelMetadata();
    void saveSelectedModelMetadata();
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
    void appendMeshAsset();

    VulkanDevice* device_ = nullptr;
    VulkanSwapchain* swapchain_ = nullptr;
    nvrhi::ShaderHandle geometryVertexShader_;
    nvrhi::ShaderHandle geometryFragmentShader_;
    nvrhi::ShaderHandle shadowDepthVertexShader_;
    nvrhi::ShaderHandle shadowDepthFragmentShader_;
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
    nvrhi::ShaderHandle skinMeshComputeShader_;
    nvrhi::ShaderHandle rayTracingRayGenShader_;
    nvrhi::ShaderHandle rayTracingMissShader_;
    nvrhi::ShaderHandle rayTracingClosestHitShader_;
    nvrhi::InputLayoutHandle inputLayout_;
    nvrhi::InputLayoutHandle shadowInputLayout_;
    nvrhi::GraphicsPipelineHandle geometryPipeline_;
    nvrhi::GraphicsPipelineHandle shadowDepthPipeline_;
    nvrhi::GraphicsPipelineHandle pbrLightingPipeline_;
    nvrhi::GraphicsPipelineHandle antiAliasingPipeline_;
    nvrhi::GraphicsPipelineHandle skyAtmospherePipeline_;
    nvrhi::ComputePipelineHandle skyTransmittancePipeline_;
    nvrhi::ComputePipelineHandle skyMultiScatteringPipeline_;
    nvrhi::ComputePipelineHandle skyAtmosphereComputePipeline_;
    nvrhi::ComputePipelineHandle cloudDensityPipeline_;
    nvrhi::ComputePipelineHandle volumetricCloudsPipeline_;
    nvrhi::ComputePipelineHandle waterOceanPipeline_;
    nvrhi::ComputePipelineHandle skinMeshPipeline_;
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
    nvrhi::BufferHandle meshSourceVertexBuffer_;
    nvrhi::BufferHandle skinningConstantsBuffer_;
    nvrhi::BufferHandle skinningMatricesBuffer_;
    nvrhi::TextureHandle depthTexture_;
    nvrhi::TextureHandle shadowDepthTexture_;
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
    nvrhi::TextureHandle whiteTexture_;
    nvrhi::TextureHandle modelBaseColorTexture_;
    nvrhi::SamplerHandle lightingSampler_;
    nvrhi::SamplerHandle cloudSampler_;
    nvrhi::SamplerHandle geometrySampler_;
    nvrhi::BindingLayoutHandle geometryBindingLayout_;
    nvrhi::BindingLayoutHandle shadowDepthBindingLayout_;
    nvrhi::BindingLayoutHandle pbrLightingBindingLayout_;
    nvrhi::BindingLayoutHandle antiAliasingBindingLayout_;
    nvrhi::BindingLayoutHandle skyAtmosphereBindingLayout_;
    nvrhi::BindingLayoutHandle skyTransmittanceBindingLayout_;
    nvrhi::BindingLayoutHandle skyMultiScatteringBindingLayout_;
    nvrhi::BindingLayoutHandle skyAtmosphereComputeBindingLayout_;
    nvrhi::BindingLayoutHandle cloudDensityBindingLayout_;
    nvrhi::BindingLayoutHandle volumetricCloudsBindingLayout_;
    nvrhi::BindingLayoutHandle waterOceanBindingLayout_;
    nvrhi::BindingLayoutHandle skinMeshBindingLayout_;
    nvrhi::BindingLayoutHandle rayTracingBindingLayout_;
    nvrhi::BindingSetHandle pbrLightingBindingSet_;
    nvrhi::BindingSetHandle geometryWhiteBindingSet_;
    nvrhi::BindingSetHandle geometryModelBindingSet_;
    nvrhi::BindingSetHandle antiAliasingBindingSet_;
    nvrhi::BindingSetHandle skyAtmosphereBindingSet_;
    nvrhi::BindingSetHandle skyTransmittanceBindingSet_;
    nvrhi::BindingSetHandle skyMultiScatteringBindingSet_;
    nvrhi::BindingSetHandle skyAtmosphereComputeBindingSet_;
    nvrhi::BindingSetHandle cloudDensityBindingSet_;
    nvrhi::BindingSetHandle volumetricCloudsBindingSet_;
    nvrhi::BindingSetHandle waterOceanBindingSet_;
    nvrhi::BindingSetHandle skinMeshBindingSet_;
    nvrhi::BindingSetHandle rayTracingBindingSet_;
    nvrhi::BindingSetHandle rayTracingCompositeBindingSet_;
    nvrhi::FramebufferHandle gBufferFramebuffer_;
    nvrhi::FramebufferHandle shadowFramebuffer_;
    nvrhi::FramebufferHandle postFramebuffer_;
    std::vector<nvrhi::FramebufferHandle> framebuffers_;
    std::unique_ptr<VulkanImGuiLayer> imguiLayer_;
    std::vector<PrimitiveInstance> primitives_;
    std::vector<PrimitiveInstance> dynamicPrimitives_;
    Resources::MeshAsset meshAsset_;
    bool hasMeshAsset_ = false;
    std::vector<Resources::MeshVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<Resources::MeshVertex> dynamicVertices_;
    std::vector<uint32_t> dynamicIndices_;
    std::vector<Resources::MeshVertex> meshRenderVertices_;
    std::vector<glm::mat4> meshSkinningMatrices_;
    std::vector<uint8_t> modelBaseColorPixels_;
    glm::mat4 meshFitTransform_ { 1.0f };
    glm::mat4 meshWorldTransform_ { 1.0f };
    Camera::CameraState camera_;
    PointLight pointLights_[4] {
        { { 0.0f, 2.5f, 1.5f }, { 1.0f, 0.92f, 0.82f }, 30.0f },
        { { -2.5f, 1.5f, -1.5f }, { 0.45f, 0.65f, 1.0f }, 18.0f },
        { { 2.0f, 1.0f, -2.0f }, { 1.0f, 0.35f, 0.25f }, 12.0f },
        { { 0.0f, 3.5f, -3.5f }, { 0.35f, 1.0f, 0.6f }, 8.0f },
    };
    int activePointLightCount_ = 3;
    float sunDirection_[3] { -0.35f, -0.72f, -0.6f };
    float sunColor_[3] { 1.0f, 0.96f, 0.84f };
    float sunIntensity_ = 4.35f;
    float skyRayleighScale_ = 1.0f;
    float skyMieScale_ = 0.68f;
    float skyExposure_ = 1.18f;
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
    bool playerControlModeEnabled_ = false;
    bool playerResetRequested_ = false;
    bool playRequested_ = false;
    bool editorPlayMode_ = false;
    bool modelResourcesDirty_ = true;
    bool animationEditorOpen_ = false;
    AnimationSystem::AnimationTuning animationTuning_ {};
    std::vector<std::string> modelResourcePaths_;
    int selectedModelResourceIndex_ = -1;
    char modelDisplayName_[128] {};
    std::string pendingModelLoadPath_;
    std::string modelMetadataStatus_;
    bool meshDirty_ = true;
    bool gpuMeshDirty_ = true;
    bool dynamicMeshDirty_ = true;
    bool dynamicGpuMeshDirty_ = true;
    bool whiteTextureDirty_ = true;
    bool modelBaseColorTextureDirty_ = false;
    bool skinningMatricesDirty_ = false;
    bool skinningBindingSetDirty_ = true;
    bool rayTracingEnabled_ = false;
    bool rayTracingAccelerationStructuresDirty_ = true;
    uint32_t uploadedIndexCount_ = 0;
    uint32_t meshAssetIndexCount_ = 0;
    uint32_t meshAssetVertexCount_ = 0;
    uint32_t uploadedMeshAssetIndexCount_ = 0;
    uint32_t uploadedDynamicIndexCount_ = 0;
    uint32_t modelBaseColorWidth_ = 1;
    uint32_t modelBaseColorHeight_ = 1;
    std::chrono::steady_clock::time_point lastCloudUpdateTime_ {};
};

} // namespace MEngine::RenderBackend::Vulkan
