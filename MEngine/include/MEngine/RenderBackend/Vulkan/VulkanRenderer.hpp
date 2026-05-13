#pragma once

#include "MEngine/Camera/Camera.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanImGuiLayer.hpp"

#include <nvrhi/nvrhi.h>

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

    void initialize(VulkanDevice& device, VulkanSwapchain& swapchain, SDL_Window* window);
    void setCameraState(const Camera::CameraState& camera);
    uint64_t render(uint32_t imageIndex);
    void recreateSwapchainResources(VulkanSwapchain& swapchain, SDL_Window* window);
    void shutdown();

    void addPrimitive(BasicPrimitiveType type);
    void setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives);
    void clearPrimitives();

private:
    struct Vertex {
        float position[3];
        float normal[3];
        float color[3];
        float faceUv[2];
    };

    struct PushConstants {
        float viewProjection[16];
        float materialParameters[4];
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
        float cameraParameters[4];
    };

    void createFramebuffers(VulkanSwapchain& swapchain);
    void createGBufferTextures(VulkanSwapchain& swapchain);
    void createDepthTexture(VulkanSwapchain& swapchain);
    void createShaders();
    void createPipeline();
    void createBuffers();
    void releaseGpuResources();
    void rebuildMesh();
    PushConstants buildPushConstants() const;
    LightingConstants buildLightingConstants() const;
    void drawPrimitivePanel();

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
    nvrhi::ShaderHandle skyAtmosphereVertexShader_;
    nvrhi::ShaderHandle skyAtmosphereFragmentShader_;
    nvrhi::InputLayoutHandle inputLayout_;
    nvrhi::GraphicsPipelineHandle geometryPipeline_;
    nvrhi::GraphicsPipelineHandle pbrLightingPipeline_;
    nvrhi::GraphicsPipelineHandle skyAtmospherePipeline_;
    nvrhi::CommandListHandle commandList_;
    nvrhi::BufferHandle vertexBuffer_;
    nvrhi::BufferHandle indexBuffer_;
    nvrhi::BufferHandle lightingConstantsBuffer_;
    nvrhi::TextureHandle depthTexture_;
    nvrhi::TextureHandle gBufferPosition_;
    nvrhi::TextureHandle gBufferNormal_;
    nvrhi::TextureHandle gBufferAlbedo_;
    nvrhi::TextureHandle gBufferMaterial_;
    nvrhi::SamplerHandle lightingSampler_;
    nvrhi::BindingLayoutHandle geometryBindingLayout_;
    nvrhi::BindingLayoutHandle pbrLightingBindingLayout_;
    nvrhi::BindingLayoutHandle skyAtmosphereBindingLayout_;
    nvrhi::BindingSetHandle pbrLightingBindingSet_;
    nvrhi::BindingSetHandle skyAtmosphereBindingSet_;
    nvrhi::FramebufferHandle gBufferFramebuffer_;
    std::vector<nvrhi::FramebufferHandle> framebuffers_;
    std::unique_ptr<VulkanImGuiLayer> imguiLayer_;
    std::vector<PrimitiveInstance> primitives_;
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
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
    float materialMetallic_ = 0.0f;
    float materialRoughness_ = 0.45f;
    float materialAmbient_ = 0.03f;
    bool meshDirty_ = true;
    bool gpuMeshDirty_ = true;
    uint32_t uploadedIndexCount_ = 0;
};

} // namespace MEngine::RenderBackend::Vulkan
