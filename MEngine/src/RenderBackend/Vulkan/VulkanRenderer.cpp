#include "MEngine/RenderBackend/Vulkan/VulkanRenderer.hpp"

#include "MEngine/Core/Log.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanDevice.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanShader.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanSwapchain.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef MENGINE_SHADER_BINARY_DIR
#define MENGINE_SHADER_BINARY_DIR "."
#endif

namespace MEngine::RenderBackend::Vulkan {

namespace {

constexpr uint32_t MaxPrimitiveVertices = 2097152;
constexpr uint32_t MaxPrimitiveIndices = 4194304;
constexpr uint32_t MaxDynamicPrimitiveVertices = 65536;
constexpr uint32_t MaxDynamicPrimitiveIndices = 131072;
constexpr uint32_t SkyTransmittanceLutWidth = 256;
constexpr uint32_t SkyTransmittanceLutHeight = 64;
constexpr uint32_t SkyMultiScatteringLutWidth = 32;
constexpr uint32_t SkyMultiScatteringLutHeight = 32;
constexpr uint32_t CloudDensityTextureWidth = 128;
constexpr uint32_t CloudDensityTextureHeight = 64;
constexpr uint32_t CloudDensityTextureDepth = 128;
using Vertex = Resources::MeshVertex;

uint32_t divideAndRoundUp(uint32_t value, uint32_t divisor)
{
    return (value + divisor - 1) / divisor;
}

const char* primitiveName(BasicPrimitiveType type)
{
    switch (type) {
    case PrimitiveType::Triangle:
        return "Triangle";
    case PrimitiveType::Quad:
        return "Quad";
    case PrimitiveType::Cube:
        return "Cube";
    case PrimitiveType::Sphere:
        return "Sphere";
    }

    return "Unknown";
}

class RenderAccessGraph {
public:
    explicit RenderAccessGraph(nvrhi::ICommandList* commandList)
        : commandList_(commandList)
    {
    }

    struct TextureTransition {
        nvrhi::ITexture* texture = nullptr;
        nvrhi::ResourceStates state = nvrhi::ResourceStates::Unknown;
    };

    struct BufferTransition {
        nvrhi::IBuffer* buffer = nullptr;
        nvrhi::ResourceStates state = nvrhi::ResourceStates::Unknown;
    };

    struct Pass {
        const char* name = "";
        std::vector<TextureTransition> textures;
        std::vector<BufferTransition> buffers;
        std::vector<nvrhi::IBindingSet*> bindingSets;
        std::function<void()> execute;
    };

    Pass& addPass(const char* name)
    {
        passes_.push_back(Pass {});
        passes_.back().name = name;
        return passes_.back();
    }

    void execute()
    {
        for (Pass& pass : passes_) {
            commandList_->beginMarker(pass.name);
            for (const TextureTransition& transition : pass.textures) {
                if (transition.texture) {
                    commandList_->setTextureState(transition.texture, nvrhi::AllSubresources, transition.state);
                }
            }
            for (const BufferTransition& transition : pass.buffers) {
                if (transition.buffer) {
                    commandList_->setBufferState(transition.buffer, transition.state);
                }
            }
            for (nvrhi::IBindingSet* bindingSet : pass.bindingSets) {
                if (bindingSet) {
                    commandList_->setResourceStatesForBindingSet(bindingSet);
                }
            }
            commandList_->commitBarriers();
            if (pass.execute) {
                pass.execute();
            }
            commandList_->endMarker();
        }
    }

private:
    nvrhi::ICommandList* commandList_ = nullptr;
    std::vector<Pass> passes_;
};

} // namespace

VulkanRenderer::~VulkanRenderer()
{
    shutdown();
}

void VulkanRenderer::initialize(VulkanDevice& device, VulkanSwapchain& swapchain, SDL_Window* window, bool enableRayTracing)
{
    device_ = &device;
    swapchain_ = &swapchain;
    rayTracingEnabled_ = enableRayTracing;

    createFramebuffers(swapchain);
    createShaders();
    createPipeline();
    createBuffers();

    if (primitives_.empty()) {
        addPrimitive(PrimitiveType::Cube);
    }

    imguiLayer_ = std::make_unique<VulkanImGuiLayer>();
    imguiLayer_->initialize(device, swapchain, framebuffers_.front(), window);
    imguiLayer_->setPanelCallback([this]() { drawPrimitivePanel(); });

    commandList_ = device_->nvrhiDevice()->createCommandList();
    if (!commandList_) {
        throw std::runtime_error("Failed to create NVRHI Vulkan command list");
    }

    MENGINE_INFO("[RenderBackend] Vulkan renderer initialized via NVRHI command list");
    if (rayTracingEnabled_) {
        MENGINE_INFO("[RenderBackend] Ray tracing prototype path is active");
    }
}

void VulkanRenderer::setCameraState(const Camera::CameraState& camera)
{
    camera_ = camera;
}

void VulkanRenderer::recreateSwapchainResources(VulkanSwapchain& swapchain, SDL_Window* window)
{
    releaseGpuResources();
    swapchain_ = &swapchain;
    createFramebuffers(swapchain);
    createShaders();
    createPipeline();
    createBuffers();

    imguiLayer_ = std::make_unique<VulkanImGuiLayer>();
    imguiLayer_->initialize(*device_, swapchain, framebuffers_.front(), window);
    imguiLayer_->setPanelCallback([this]() { drawPrimitivePanel(); });

    commandList_ = device_->nvrhiDevice()->createCommandList();
    if (!commandList_) {
        throw std::runtime_error("Failed to recreate NVRHI Vulkan command list");
    }

    meshDirty_ = true;
}

uint64_t VulkanRenderer::render(uint32_t imageIndex)
{
    if (!device_ || imageIndex >= framebuffers_.size()) {
        return 0;
    }

    applyPendingAntiAliasingMode();
    applyPendingTextureFilteringSettings();

    nvrhi::ITexture* target = swapchain_->nvrhiImages()[imageIndex];
    nvrhi::IFramebuffer* framebuffer = framebuffers_[imageIndex];
    const auto now = std::chrono::steady_clock::now();
    if (lastCloudUpdateTime_.time_since_epoch().count() != 0) {
        const std::chrono::duration<float> delta = now - lastCloudUpdateTime_;
        cloudTime_ += std::clamp(delta.count(), 0.0f, 0.1f);
    }
    lastCloudUpdateTime_ = now;

    commandList_->open();
    commandList_->beginMarker("MeowEngine Renderer");
    nvrhi::ViewportState fullscreenViewport;
    fullscreenViewport.addViewportAndScissorRect(nvrhi::Viewport(
        static_cast<float>(swapchain_->extent().width),
        static_cast<float>(swapchain_->extent().height)));

    RenderAccessGraph graph(commandList_);

    {
        RenderAccessGraph::Pass& pass = graph.addPass("GBuffer Clear");
        pass.textures = {
            { gBufferPosition_, nvrhi::ResourceStates::RenderTarget },
            { gBufferNormal_, nvrhi::ResourceStates::RenderTarget },
            { gBufferAlbedo_, nvrhi::ResourceStates::RenderTarget },
            { gBufferMaterial_, nvrhi::ResourceStates::RenderTarget },
            { depthTexture_, nvrhi::ResourceStates::DepthWrite },
        };
        pass.execute = [this]() {
            commandList_->clearTextureFloat(gBufferPosition_, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
            commandList_->clearTextureFloat(gBufferNormal_, nvrhi::AllSubresources, nvrhi::Color(0.0f, 1.0f, 0.0f, 0.0f));
            commandList_->clearTextureFloat(gBufferAlbedo_, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
            commandList_->clearTextureFloat(gBufferMaterial_, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.5f, 0.03f, 0.0f));
            commandList_->clearDepthStencilTexture(depthTexture_, nvrhi::AllSubresources, true, 1.0f, false, 0);
        };
    }

    {
        RenderAccessGraph::Pass& pass = graph.addPass("Mesh Upload");
        pass.execute = [this]() {
            if (meshDirty_) {
                rebuildMesh();
                meshDirty_ = false;
                gpuMeshDirty_ = true;
            }

            if (gpuMeshDirty_ && !vertices_.empty() && !indices_.empty()) {
                commandList_->writeBuffer(vertexBuffer_, vertices_.data(), vertices_.size() * sizeof(Vertex));
                commandList_->writeBuffer(indexBuffer_, indices_.data(), indices_.size() * sizeof(uint32_t));
                uploadedIndexCount_ = static_cast<uint32_t>(indices_.size());
                gpuMeshDirty_ = false;
                rayTracingAccelerationStructuresDirty_ = true;
            }

            if (dynamicMeshDirty_) {
                rebuildDynamicMesh();
                dynamicMeshDirty_ = false;
                dynamicGpuMeshDirty_ = true;
            }

            if (dynamicGpuMeshDirty_) {
                if (!dynamicVertices_.empty() && !dynamicIndices_.empty()) {
                    commandList_->writeBuffer(dynamicVertexBuffer_, dynamicVertices_.data(), dynamicVertices_.size() * sizeof(Vertex));
                    commandList_->writeBuffer(dynamicIndexBuffer_, dynamicIndices_.data(), dynamicIndices_.size() * sizeof(uint32_t));
                    uploadedDynamicIndexCount_ = static_cast<uint32_t>(dynamicIndices_.size());
                } else {
                    uploadedDynamicIndexCount_ = 0;
                }
                dynamicGpuMeshDirty_ = false;
            }
        };
    }

    {
        RenderAccessGraph::Pass& pass = graph.addPass("Geometry");
        pass.buffers = {
            { vertexBuffer_, nvrhi::ResourceStates::VertexBuffer },
            { indexBuffer_, nvrhi::ResourceStates::IndexBuffer },
            { dynamicVertexBuffer_, nvrhi::ResourceStates::VertexBuffer },
            { dynamicIndexBuffer_, nvrhi::ResourceStates::IndexBuffer },
        };
        pass.execute = [this]() {
            if (uploadedIndexCount_ == 0 && uploadedDynamicIndexCount_ == 0) {
                return;
            }

            nvrhi::ViewportState viewportState;
            viewportState.addViewportAndScissorRect(nvrhi::Viewport(
                static_cast<float>(swapchain_->extent().width),
                static_cast<float>(swapchain_->extent().height)));

            const PushConstants pushConstants = buildPushConstants();
            if (uploadedIndexCount_ > 0) {
                nvrhi::GraphicsState graphicsState;
                graphicsState.setPipeline(geometryPipeline_);
                graphicsState.setFramebuffer(gBufferFramebuffer_);
                graphicsState.setViewport(viewportState);
                graphicsState.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vertexBuffer_).setSlot(0).setOffset(0));
                graphicsState.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(indexBuffer_).setFormat(nvrhi::Format::R32_UINT).setOffset(0));
                commandList_->setGraphicsState(graphicsState);
                commandList_->setPushConstants(&pushConstants, sizeof(pushConstants));
                commandList_->drawIndexed(nvrhi::DrawArguments().setVertexCount(uploadedIndexCount_));
            }
            if (uploadedDynamicIndexCount_ > 0) {
                nvrhi::GraphicsState graphicsState;
                graphicsState.setPipeline(geometryPipeline_);
                graphicsState.setFramebuffer(gBufferFramebuffer_);
                graphicsState.setViewport(viewportState);
                graphicsState.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(dynamicVertexBuffer_).setSlot(0).setOffset(0));
                graphicsState.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(dynamicIndexBuffer_).setFormat(nvrhi::Format::R32_UINT).setOffset(0));
                commandList_->setGraphicsState(graphicsState);
                commandList_->setPushConstants(&pushConstants, sizeof(pushConstants));
                commandList_->drawIndexed(nvrhi::DrawArguments().setVertexCount(uploadedDynamicIndexCount_));
            }
        };
    }

    if (msaaSampleCount_ > 1) {
        RenderAccessGraph::Pass& pass = graph.addPass("GBuffer Resolve");
        pass.textures = {
            { gBufferPosition_, nvrhi::ResourceStates::ResolveSource },
            { gBufferNormal_, nvrhi::ResourceStates::ResolveSource },
            { gBufferAlbedo_, nvrhi::ResourceStates::ResolveSource },
            { gBufferMaterial_, nvrhi::ResourceStates::ResolveSource },
            { resolvedGBufferPosition_, nvrhi::ResourceStates::ResolveDest },
            { resolvedGBufferNormal_, nvrhi::ResourceStates::ResolveDest },
            { resolvedGBufferAlbedo_, nvrhi::ResourceStates::ResolveDest },
            { resolvedGBufferMaterial_, nvrhi::ResourceStates::ResolveDest },
        };
        pass.execute = [this]() {
            commandList_->resolveTexture(resolvedGBufferPosition_, nvrhi::AllSubresources, gBufferPosition_, nvrhi::AllSubresources);
            commandList_->resolveTexture(resolvedGBufferNormal_, nvrhi::AllSubresources, gBufferNormal_, nvrhi::AllSubresources);
            commandList_->resolveTexture(resolvedGBufferAlbedo_, nvrhi::AllSubresources, gBufferAlbedo_, nvrhi::AllSubresources);
            commandList_->resolveTexture(resolvedGBufferMaterial_, nvrhi::AllSubresources, gBufferMaterial_, nvrhi::AllSubresources);
        };
    }

    {
        RenderAccessGraph::Pass& pass = graph.addPass("Lighting Constants");
        pass.execute = [this]() {
            const LightingConstants lightingConstants = buildLightingConstants();
            commandList_->writeBuffer(lightingConstantsBuffer_, &lightingConstants, sizeof(lightingConstants));
            commandList_->setBufferState(lightingConstantsBuffer_, nvrhi::ResourceStates::ConstantBuffer);
            commandList_->commitBarriers();
        };
    }

    graph.addPass("Sky Atmosphere Compute").execute = [this]() { dispatchSkyAtmosphereCompute(); };
    graph.addPass("Cloud Density Compute").execute = [this]() { dispatchCloudDensityCompute(); };
    graph.addPass("Volumetric Clouds Compute").execute = [this]() { dispatchVolumetricCloudsCompute(); };
    graph.addPass("Water Ocean Compute").execute = [this]() { dispatchWaterOceanCompute(); };
    if (rayTracingEnabled_) {
        graph.addPass("Ray Tracing Acceleration Structures").execute = [this]() { rebuildRayTracingAccelerationStructures(); };

        RenderAccessGraph::Pass& pass = graph.addPass("Ray Tracing Prototype");
        pass.textures = { { rayTracingOutputTexture_, nvrhi::ResourceStates::UnorderedAccess } };
        pass.bindingSets = { rayTracingBindingSet_ };
        pass.execute = [this]() { dispatchRayTracingPrototype(); };
    }

    {
        RenderAccessGraph::Pass& pass = graph.addPass("Sky Composite");
        pass.textures = { { postColorTexture_, nvrhi::ResourceStates::RenderTarget } };
        pass.bindingSets = { rayTracingEnabled_ ? rayTracingCompositeBindingSet_.Get() : skyAtmosphereBindingSet_.Get() };
        pass.execute = [this, fullscreenViewport]() {
            nvrhi::IBindingSet* skyBindingSet = rayTracingEnabled_ ? rayTracingCompositeBindingSet_.Get() : skyAtmosphereBindingSet_.Get();
            nvrhi::GraphicsState skyState;
            skyState.setPipeline(skyAtmospherePipeline_);
            skyState.setFramebuffer(postFramebuffer_);
            skyState.setViewport(fullscreenViewport);
            skyState.addBindingSet(skyBindingSet);
            commandList_->setGraphicsState(skyState);
            commandList_->draw(nvrhi::DrawArguments().setVertexCount(3));
        };
    }

    {
        RenderAccessGraph::Pass& pass = graph.addPass("PBR Lighting");
        pass.textures = {
            { sampledGBufferPosition(), nvrhi::ResourceStates::ShaderResource },
            { sampledGBufferNormal(), nvrhi::ResourceStates::ShaderResource },
            { sampledGBufferAlbedo(), nvrhi::ResourceStates::ShaderResource },
            { sampledGBufferMaterial(), nvrhi::ResourceStates::ShaderResource },
            { postColorTexture_, nvrhi::ResourceStates::RenderTarget },
        };
        pass.bindingSets = { pbrLightingBindingSet_ };
        pass.execute = [this, fullscreenViewport]() {
            nvrhi::GraphicsState pbrLightingState;
            pbrLightingState.setPipeline(pbrLightingPipeline_);
            pbrLightingState.setFramebuffer(postFramebuffer_);
            pbrLightingState.setViewport(fullscreenViewport);
            pbrLightingState.addBindingSet(pbrLightingBindingSet_);
            commandList_->setGraphicsState(pbrLightingState);
            commandList_->draw(nvrhi::DrawArguments().setVertexCount(3));
        };
    }

    {
        RenderAccessGraph::Pass& pass = graph.addPass("Anti-Aliasing");
        pass.textures = {
            { postColorTexture_, nvrhi::ResourceStates::ShaderResource },
            { taaHistoryTexture_, nvrhi::ResourceStates::ShaderResource },
            { target, nvrhi::ResourceStates::RenderTarget },
        };
        pass.bindingSets = { antiAliasingBindingSet_ };
        pass.execute = [this, framebuffer, fullscreenViewport]() {
            nvrhi::GraphicsState antiAliasingState;
            antiAliasingState.setPipeline(antiAliasingPipeline_);
            antiAliasingState.setFramebuffer(framebuffer);
            antiAliasingState.setViewport(fullscreenViewport);
            antiAliasingState.addBindingSet(antiAliasingBindingSet_);
            commandList_->setGraphicsState(antiAliasingState);
            const float shaderMode = antiAliasingMode_ == AntiAliasingMode::FXAA ? 1.0f :
                (antiAliasingMode_ == AntiAliasingMode::TAA ? 2.0f : 0.0f);
            const AntiAliasingConstants constants {
                {
                    1.0f / (std::max)(static_cast<float>(swapchain_->extent().width), 1.0f),
                    1.0f / (std::max)(static_cast<float>(swapchain_->extent().height), 1.0f),
                    shaderMode,
                    taaHistoryValid_ ? 1.0f : 0.0f,
                },
                {
                    taaHistoryWeight_,
                    0.0f,
                    0.0f,
                    0.0f,
                },
            };
            commandList_->setPushConstants(&constants, sizeof(constants));
            commandList_->draw(nvrhi::DrawArguments().setVertexCount(3));
        };
    }

    if (antiAliasingMode_ == AntiAliasingMode::TAA) {
        RenderAccessGraph::Pass& pass = graph.addPass("TAA History Update");
        pass.textures = {
            { postColorTexture_, nvrhi::ResourceStates::CopySource },
            { taaHistoryTexture_, nvrhi::ResourceStates::CopyDest },
        };
        pass.execute = [this]() {
            commandList_->copyTexture(taaHistoryTexture_, nvrhi::TextureSlice(), postColorTexture_, nvrhi::TextureSlice());
            taaHistoryValid_ = true;
        };
    }

    graph.addPass("ImGui").execute = [this, framebuffer]() {
        imguiLayer_->render(commandList_, framebuffer, swapchain_->extent().width, swapchain_->extent().height);
    };

    graph.addPass("Present").textures = { { target, nvrhi::ResourceStates::Present } };
    graph.execute();

    commandList_->endMarker();
    commandList_->close();

    return device_->nvrhiDevice()->executeCommandList(commandList_);
}

void VulkanRenderer::shutdown()
{
    if (device_ && device_->nvrhiDevice()) {
        device_->nvrhiDevice()->waitForIdle();
    }

    releaseGpuResources();
    primitives_.clear();
    vertices_.clear();
    indices_.clear();
    swapchain_ = nullptr;
    device_ = nullptr;
    meshDirty_ = true;
}

void VulkanRenderer::releaseGpuResources()
{
    vertices_.clear();
    indices_.clear();
    dynamicVertices_.clear();
    dynamicIndices_.clear();
    framebuffers_.clear();
    imguiLayer_.reset();
    rayTracingCompositeBindingSet_ = nullptr;
    rayTracingBindingSet_ = nullptr;
    antiAliasingBindingSet_ = nullptr;
    waterOceanBindingSet_ = nullptr;
    volumetricCloudsBindingSet_ = nullptr;
    cloudDensityBindingSet_ = nullptr;
    skyAtmosphereComputeBindingSet_ = nullptr;
    skyMultiScatteringBindingSet_ = nullptr;
    skyTransmittanceBindingSet_ = nullptr;
    skyAtmosphereBindingSet_ = nullptr;
    pbrLightingBindingSet_ = nullptr;
    rayTracingBindingLayout_ = nullptr;
    antiAliasingBindingLayout_ = nullptr;
    waterOceanBindingLayout_ = nullptr;
    volumetricCloudsBindingLayout_ = nullptr;
    cloudDensityBindingLayout_ = nullptr;
    skyAtmosphereComputeBindingLayout_ = nullptr;
    skyMultiScatteringBindingLayout_ = nullptr;
    skyTransmittanceBindingLayout_ = nullptr;
    skyAtmosphereBindingLayout_ = nullptr;
    pbrLightingBindingLayout_ = nullptr;
    geometryBindingLayout_ = nullptr;
    cloudSampler_ = nullptr;
    lightingSampler_ = nullptr;
    rayTracingOutputTexture_ = nullptr;
    taaHistoryTexture_ = nullptr;
    postColorTexture_ = nullptr;
    waterOceanTexture_ = nullptr;
    volumetricCloudsTexture_ = nullptr;
    cloudDensityTexture_ = nullptr;
    skyAtmosphereTexture_ = nullptr;
    skyMultiScatteringLut_ = nullptr;
    skyTransmittanceLut_ = nullptr;
    postFramebuffer_ = nullptr;
    gBufferFramebuffer_ = nullptr;
    resolvedGBufferMaterial_ = nullptr;
    resolvedGBufferAlbedo_ = nullptr;
    resolvedGBufferNormal_ = nullptr;
    resolvedGBufferPosition_ = nullptr;
    gBufferMaterial_ = nullptr;
    gBufferAlbedo_ = nullptr;
    gBufferNormal_ = nullptr;
    gBufferPosition_ = nullptr;
    depthTexture_ = nullptr;
    lightingConstantsBuffer_ = nullptr;
    dynamicIndexBuffer_ = nullptr;
    dynamicVertexBuffer_ = nullptr;
    indexBuffer_ = nullptr;
    vertexBuffer_ = nullptr;
    commandList_ = nullptr;
    rayTracingShaderTable_ = nullptr;
    rayTracingPipeline_ = nullptr;
    rayTracingTlas_ = nullptr;
    rayTracingBlas_ = nullptr;
    waterOceanPipeline_ = nullptr;
    volumetricCloudsPipeline_ = nullptr;
    cloudDensityPipeline_ = nullptr;
    skyAtmosphereComputePipeline_ = nullptr;
    skyMultiScatteringPipeline_ = nullptr;
    skyTransmittancePipeline_ = nullptr;
    skyAtmospherePipeline_ = nullptr;
    antiAliasingPipeline_ = nullptr;
    pbrLightingPipeline_ = nullptr;
    geometryPipeline_ = nullptr;
    inputLayout_ = nullptr;
    rayTracingMissShader_ = nullptr;
    rayTracingClosestHitShader_ = nullptr;
    rayTracingRayGenShader_ = nullptr;
    waterOceanComputeShader_ = nullptr;
    volumetricCloudsComputeShader_ = nullptr;
    cloudDensityComputeShader_ = nullptr;
    skyAtmosphereComputeShader_ = nullptr;
    skyMultiScatteringComputeShader_ = nullptr;
    skyTransmittanceComputeShader_ = nullptr;
    skyAtmosphereFragmentShader_ = nullptr;
    skyAtmosphereVertexShader_ = nullptr;
    antiAliasingFragmentShader_ = nullptr;
    pbrLightingFragmentShader_ = nullptr;
    pbrLightingVertexShader_ = nullptr;
    geometryFragmentShader_ = nullptr;
    geometryVertexShader_ = nullptr;
    meshDirty_ = true;
    gpuMeshDirty_ = true;
    dynamicMeshDirty_ = true;
    dynamicGpuMeshDirty_ = true;
    rayTracingAccelerationStructuresDirty_ = true;
    uploadedIndexCount_ = 0;
    uploadedDynamicIndexCount_ = 0;
}

void VulkanRenderer::addPrimitive(BasicPrimitiveType type)
{
    const size_t index = primitives_.size();
    const float column = static_cast<float>(index % 5);
    const float row = static_cast<float>(index / 5);

    PrimitiveInstance primitive {};
    primitive.type = type;
    primitive.position[0] = -2.4f + column * 1.2f;
    primitive.position[1] = 0.0f;
    primitive.position[2] = -row * 1.2f;
    primitive.size = 0.75f;

    const std::array<std::array<float, 3>, 7> palette {{
        {{ 1.0f, 0.25f, 0.2f }},
        {{ 0.2f, 0.85f, 0.45f }},
        {{ 0.25f, 0.45f, 1.0f }},
        {{ 1.0f, 0.8f, 0.25f }},
        {{ 0.85f, 0.35f, 1.0f }},
        {{ 0.2f, 0.9f, 0.9f }},
        {{ 0.95f, 0.95f, 0.9f }},
    }};
    const auto& color = palette[index % palette.size()];
    primitive.color[0] = color[0];
    primitive.color[1] = color[1];
    primitive.color[2] = color[2];

    primitives_.push_back(primitive);
    meshDirty_ = true;
    gpuMeshDirty_ = true;
    rayTracingAccelerationStructuresDirty_ = true;
}

void VulkanRenderer::clearPrimitives()
{
    primitives_.clear();
    meshDirty_ = true;
    gpuMeshDirty_ = true;
    rayTracingAccelerationStructuresDirty_ = true;
    uploadedIndexCount_ = 0;
}

void VulkanRenderer::setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
{
    primitives_ = primitives;
    meshDirty_ = true;
    gpuMeshDirty_ = true;
    rayTracingAccelerationStructuresDirty_ = true;
}

void VulkanRenderer::setDynamicPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
{
    dynamicPrimitives_ = primitives;
    dynamicMeshDirty_ = true;
    dynamicGpuMeshDirty_ = true;
}

bool VulkanRenderer::shootingModeEnabled() const
{
    return shootingModeEnabled_;
}

void VulkanRenderer::createFramebuffers(VulkanSwapchain& swapchain)
{
    createGBufferTextures(swapchain);
    createPostProcessTextures(swapchain);
    createDepthTexture(swapchain);
    createAtmosphereTextures(swapchain);

    nvrhi::FramebufferDesc gBufferDesc;
    gBufferDesc.addColorAttachment(gBufferPosition_);
    gBufferDesc.addColorAttachment(gBufferNormal_);
    gBufferDesc.addColorAttachment(gBufferAlbedo_);
    gBufferDesc.addColorAttachment(gBufferMaterial_);
    gBufferDesc.setDepthAttachment(depthTexture_);
    gBufferFramebuffer_ = device_->nvrhiDevice()->createFramebuffer(gBufferDesc);
    if (!gBufferFramebuffer_) {
        throw std::runtime_error("Failed to create Vulkan renderer G-buffer framebuffer");
    }

    nvrhi::FramebufferDesc postDesc;
    postDesc.addColorAttachment(postColorTexture_);
    postFramebuffer_ = device_->nvrhiDevice()->createFramebuffer(postDesc);
    if (!postFramebuffer_) {
        throw std::runtime_error("Failed to create Vulkan renderer post-process framebuffer");
    }

    framebuffers_.reserve(swapchain.nvrhiImages().size());

    for (nvrhi::ITexture* image : swapchain.nvrhiImages()) {
        nvrhi::FramebufferDesc desc;
        desc.addColorAttachment(image);
        framebuffers_.push_back(device_->nvrhiDevice()->createFramebuffer(desc));
    }
}

void VulkanRenderer::createPostProcessTextures(VulkanSwapchain& swapchain)
{
    const nvrhi::TextureDesc postDesc = nvrhi::TextureDesc()
        .setDimension(nvrhi::TextureDimension::Texture2D)
        .setWidth(swapchain.extent().width)
        .setHeight(swapchain.extent().height)
        .setFormat(nvrhi::Format::RGBA16_FLOAT)
        .setIsRenderTarget(true)
        .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);

    postColorTexture_ = device_->nvrhiDevice()->createTexture(nvrhi::TextureDesc(postDesc)
        .setDebugName("PostColorTexture"));
    taaHistoryTexture_ = device_->nvrhiDevice()->createTexture(nvrhi::TextureDesc(postDesc)
        .setDebugName("TAAHistoryTexture"));

    if (!postColorTexture_ || !taaHistoryTexture_) {
        throw std::runtime_error("Failed to create Vulkan renderer post-process color texture");
    }
}

void VulkanRenderer::createAtmosphereTextures(VulkanSwapchain& swapchain)
{
    auto createComputeTexture = [&](const char* name, uint32_t width, uint32_t height) {
        return device_->nvrhiDevice()->createTexture(nvrhi::TextureDesc()
            .setDimension(nvrhi::TextureDimension::Texture2D)
            .setWidth(width)
            .setHeight(height)
            .setFormat(nvrhi::Format::RGBA16_FLOAT)
            .setIsUAV(true)
            .setDebugName(name)
            .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource));
    };

    skyTransmittanceLut_ = createComputeTexture("SkyTransmittanceLut", SkyTransmittanceLutWidth, SkyTransmittanceLutHeight);
    skyMultiScatteringLut_ = createComputeTexture("SkyMultiScatteringLut", SkyMultiScatteringLutWidth, SkyMultiScatteringLutHeight);
    skyAtmosphereTexture_ = createComputeTexture("SkyAtmosphereTexture", swapchain.extent().width, swapchain.extent().height);
    volumetricCloudsTexture_ = createComputeTexture("VolumetricCloudsTexture", swapchain.extent().width, swapchain.extent().height);
    waterOceanTexture_ = createComputeTexture("WaterOceanTexture", swapchain.extent().width, swapchain.extent().height);
    if (rayTracingEnabled_) {
        rayTracingOutputTexture_ = createComputeTexture("RayTracingOutputTexture", swapchain.extent().width, swapchain.extent().height);
    }
    cloudDensityTexture_ = device_->nvrhiDevice()->createTexture(nvrhi::TextureDesc()
        .setDimension(nvrhi::TextureDimension::Texture3D)
        .setWidth(CloudDensityTextureWidth)
        .setHeight(CloudDensityTextureHeight)
        .setDepth(CloudDensityTextureDepth)
        .setFormat(nvrhi::Format::RGBA16_FLOAT)
        .setIsUAV(true)
        .setDebugName("CloudDensityTexture3D")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource));

    if (!skyTransmittanceLut_ || !skyMultiScatteringLut_ || !skyAtmosphereTexture_ ||
        !cloudDensityTexture_ || !volumetricCloudsTexture_ || !waterOceanTexture_ ||
        (rayTracingEnabled_ && !rayTracingOutputTexture_)) {
        throw std::runtime_error("Failed to create Vulkan renderer atmosphere compute textures");
    }
}

void VulkanRenderer::createGBufferTextures(VulkanSwapchain& swapchain)
{
    auto createGBufferTexture = [&](const char* name, nvrhi::Format format, uint32_t sampleCount) {
        return device_->nvrhiDevice()->createTexture(nvrhi::TextureDesc()
            .setDimension(nvrhi::TextureDimension::Texture2D)
            .setWidth(swapchain.extent().width)
            .setHeight(swapchain.extent().height)
            .setFormat(format)
            .setSampleCount(sampleCount)
            .setIsRenderTarget(true)
            .setDebugName(name)
            .enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget));
    };

    gBufferPosition_ = createGBufferTexture("GBufferPosition", nvrhi::Format::RGBA16_FLOAT, msaaSampleCount_);
    gBufferNormal_ = createGBufferTexture("GBufferNormal", nvrhi::Format::RGBA16_FLOAT, msaaSampleCount_);
    gBufferAlbedo_ = createGBufferTexture("GBufferAlbedo", nvrhi::Format::RGBA16_FLOAT, msaaSampleCount_);
    gBufferMaterial_ = createGBufferTexture("GBufferMaterial", nvrhi::Format::RGBA16_FLOAT, msaaSampleCount_);

    if (!gBufferPosition_ || !gBufferNormal_ || !gBufferAlbedo_ || !gBufferMaterial_) {
        throw std::runtime_error("Failed to create Vulkan renderer G-buffer textures");
    }

    if (msaaSampleCount_ > 1) {
        resolvedGBufferPosition_ = createGBufferTexture("ResolvedGBufferPosition", nvrhi::Format::RGBA16_FLOAT, 1);
        resolvedGBufferNormal_ = createGBufferTexture("ResolvedGBufferNormal", nvrhi::Format::RGBA16_FLOAT, 1);
        resolvedGBufferAlbedo_ = createGBufferTexture("ResolvedGBufferAlbedo", nvrhi::Format::RGBA16_FLOAT, 1);
        resolvedGBufferMaterial_ = createGBufferTexture("ResolvedGBufferMaterial", nvrhi::Format::RGBA16_FLOAT, 1);
        if (!resolvedGBufferPosition_ || !resolvedGBufferNormal_ || !resolvedGBufferAlbedo_ || !resolvedGBufferMaterial_) {
            throw std::runtime_error("Failed to create Vulkan renderer resolved G-buffer textures");
        }
    }
}

void VulkanRenderer::createDepthTexture(VulkanSwapchain& swapchain)
{
    depthTexture_ = device_->nvrhiDevice()->createTexture(nvrhi::TextureDesc()
        .setDimension(nvrhi::TextureDimension::Texture2D)
        .setWidth(swapchain.extent().width)
        .setHeight(swapchain.extent().height)
        .setFormat(nvrhi::Format::D32)
        .setSampleCount(msaaSampleCount_)
        .setIsRenderTarget(true)
        .setDebugName("VulkanRendererDepth")
        .setInitialState(nvrhi::ResourceStates::DepthWrite)
        .setKeepInitialState(true));

    if (!depthTexture_) {
        throw std::runtime_error("Failed to create Vulkan renderer depth texture");
    }
}

void VulkanRenderer::createShaders()
{
    const std::string shaderDir = MENGINE_SHADER_BINARY_DIR;
    const std::vector<uint8_t> geometryVertexSpirV = VulkanShader::readSpirV(shaderDir + "/Primitive.vert.spv");
    const std::vector<uint8_t> geometryFragmentSpirV = VulkanShader::readSpirV(shaderDir + "/Primitive.frag.spv");
    const std::vector<uint8_t> pbrLightingVertexSpirV = VulkanShader::readSpirV(shaderDir + "/PBRLighting.vert.spv");
    const std::vector<uint8_t> pbrLightingFragmentSpirV = VulkanShader::readSpirV(shaderDir + "/PBRLighting.frag.spv");
    const std::vector<uint8_t> antiAliasingFragmentSpirV = VulkanShader::readSpirV(shaderDir + "/AntiAliasing.frag.spv");
    const std::vector<uint8_t> skyAtmosphereVertexSpirV = VulkanShader::readSpirV(shaderDir + "/SkyAtmosphere.vert.spv");
    const std::vector<uint8_t> skyAtmosphereFragmentSpirV = VulkanShader::readSpirV(shaderDir + "/SkyAtmosphere.frag.spv");
    const std::vector<uint8_t> skyTransmittanceComputeSpirV = VulkanShader::readSpirV(shaderDir + "/SkyTransmittanceLut.comp.spv");
    const std::vector<uint8_t> skyMultiScatteringComputeSpirV = VulkanShader::readSpirV(shaderDir + "/SkyMultiScatteringLut.comp.spv");
    const std::vector<uint8_t> skyAtmosphereComputeSpirV = VulkanShader::readSpirV(shaderDir + "/SkyAtmosphere.comp.spv");
    const std::vector<uint8_t> cloudDensityComputeSpirV = VulkanShader::readSpirV(shaderDir + "/CloudDensity.comp.spv");
    const std::vector<uint8_t> volumetricCloudsComputeSpirV = VulkanShader::readSpirV(shaderDir + "/VolumetricClouds.comp.spv");
    const std::vector<uint8_t> waterOceanComputeSpirV = VulkanShader::readSpirV(shaderDir + "/WaterOcean.comp.spv");
    std::vector<uint8_t> rayTracingRayGenSpirV;
    std::vector<uint8_t> rayTracingMissSpirV;
    std::vector<uint8_t> rayTracingClosestHitSpirV;
    if (rayTracingEnabled_) {
        rayTracingRayGenSpirV = VulkanShader::readSpirV(shaderDir + "/RayTracingPrototype.rgen.spv");
        rayTracingMissSpirV = VulkanShader::readSpirV(shaderDir + "/RayTracingPrototype.rmiss.spv");
        rayTracingClosestHitSpirV = VulkanShader::readSpirV(shaderDir + "/RayTracingPrototype.rchit.spv");
    }

    geometryVertexShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Vertex)
            .setDebugName("GBufferVS")
            .setEntryName("main"),
        geometryVertexSpirV.data(),
        geometryVertexSpirV.size());

    geometryFragmentShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Pixel)
            .setDebugName("GBufferPS")
            .setEntryName("main"),
        geometryFragmentSpirV.data(),
        geometryFragmentSpirV.size());

    pbrLightingVertexShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Vertex)
            .setDebugName("PBRLightingVS")
            .setEntryName("main"),
        pbrLightingVertexSpirV.data(),
        pbrLightingVertexSpirV.size());

    pbrLightingFragmentShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Pixel)
            .setDebugName("PBRLightingPS")
            .setEntryName("main"),
        pbrLightingFragmentSpirV.data(),
        pbrLightingFragmentSpirV.size());

    antiAliasingFragmentShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Pixel)
            .setDebugName("AntiAliasingPS")
            .setEntryName("main"),
        antiAliasingFragmentSpirV.data(),
        antiAliasingFragmentSpirV.size());

    skyAtmosphereVertexShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Vertex)
            .setDebugName("SkyAtmosphereVS")
            .setEntryName("main"),
        skyAtmosphereVertexSpirV.data(),
        skyAtmosphereVertexSpirV.size());

    skyAtmosphereFragmentShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Pixel)
            .setDebugName("SkyAtmospherePS")
            .setEntryName("main"),
        skyAtmosphereFragmentSpirV.data(),
        skyAtmosphereFragmentSpirV.size());

    skyTransmittanceComputeShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Compute)
            .setDebugName("SkyTransmittanceLutCS")
            .setEntryName("main"),
        skyTransmittanceComputeSpirV.data(),
        skyTransmittanceComputeSpirV.size());

    skyMultiScatteringComputeShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Compute)
            .setDebugName("SkyMultiScatteringLutCS")
            .setEntryName("main"),
        skyMultiScatteringComputeSpirV.data(),
        skyMultiScatteringComputeSpirV.size());

    skyAtmosphereComputeShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Compute)
            .setDebugName("SkyAtmosphereCS")
            .setEntryName("main"),
        skyAtmosphereComputeSpirV.data(),
        skyAtmosphereComputeSpirV.size());

    cloudDensityComputeShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Compute)
            .setDebugName("CloudDensityCS")
            .setEntryName("main"),
        cloudDensityComputeSpirV.data(),
        cloudDensityComputeSpirV.size());

    volumetricCloudsComputeShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Compute)
            .setDebugName("VolumetricCloudsCS")
            .setEntryName("main"),
        volumetricCloudsComputeSpirV.data(),
        volumetricCloudsComputeSpirV.size());

    waterOceanComputeShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Compute)
            .setDebugName("WaterOceanCS")
            .setEntryName("main"),
        waterOceanComputeSpirV.data(),
        waterOceanComputeSpirV.size());
    if (rayTracingEnabled_) {
        rayTracingRayGenShader_ = device_->nvrhiDevice()->createShader(
            nvrhi::ShaderDesc()
                .setShaderType(nvrhi::ShaderType::RayGeneration)
                .setDebugName("RayTracingPrototypeRayGen")
                .setEntryName("main"),
            rayTracingRayGenSpirV.data(),
            rayTracingRayGenSpirV.size());

        rayTracingMissShader_ = device_->nvrhiDevice()->createShader(
            nvrhi::ShaderDesc()
                .setShaderType(nvrhi::ShaderType::Miss)
                .setDebugName("RayTracingPrototypeMiss")
                .setEntryName("main"),
            rayTracingMissSpirV.data(),
            rayTracingMissSpirV.size());

        rayTracingClosestHitShader_ = device_->nvrhiDevice()->createShader(
            nvrhi::ShaderDesc()
                .setShaderType(nvrhi::ShaderType::ClosestHit)
                .setDebugName("RayTracingPrototypeClosestHit")
                .setEntryName("main"),
            rayTracingClosestHitSpirV.data(),
            rayTracingClosestHitSpirV.size());
    }

    if (!geometryVertexShader_ || !geometryFragmentShader_ || !pbrLightingVertexShader_ ||
        !pbrLightingFragmentShader_ || !antiAliasingFragmentShader_ ||
        !skyAtmosphereVertexShader_ || !skyAtmosphereFragmentShader_ ||
        !skyTransmittanceComputeShader_ || !skyMultiScatteringComputeShader_ ||
        !skyAtmosphereComputeShader_ || !cloudDensityComputeShader_ || !volumetricCloudsComputeShader_ ||
        !waterOceanComputeShader_ ||
        (rayTracingEnabled_ && (!rayTracingRayGenShader_ || !rayTracingMissShader_ || !rayTracingClosestHitShader_))) {
        throw std::runtime_error("Failed to create NVRHI shaders for Vulkan renderer");
    }

    nvrhi::VertexAttributeDesc inputElements[] = {
        nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, position)).setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, normal)).setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, color)).setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(Vertex, texCoord)).setElementStride(sizeof(Vertex)),
    };
    inputLayout_ = device_->nvrhiDevice()->createInputLayout(inputElements, 4, geometryVertexShader_);

    geometryBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::AllGraphics)
        .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PushConstants))));

    pbrLightingBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::AllGraphics)
        .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0).setConstantBufferOffset(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(3))
        .addItem(nvrhi::BindingLayoutItem::Sampler(4))
        .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(5)));

    antiAliasingBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::AllGraphics)
        .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0).setShaderResourceOffset(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
        .addItem(nvrhi::BindingLayoutItem::Sampler(1))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
        .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(AntiAliasingConstants))));

    skyAtmosphereBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::AllGraphics)
        .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0).setShaderResourceOffset(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
        .addItem(nvrhi::BindingLayoutItem::Sampler(1)));

    const nvrhi::VulkanBindingOffsets computeOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0)
        .setSamplerOffset(0)
        .setConstantBufferOffset(0)
        .setUnorderedAccessViewOffset(0);

    skyTransmittanceBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .setBindingOffsets(computeOffsets)
        .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(1)));

    skyMultiScatteringBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .setBindingOffsets(computeOffsets)
        .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
        .addItem(nvrhi::BindingLayoutItem::Sampler(2))
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(3)));

    skyAtmosphereComputeBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .setBindingOffsets(computeOffsets)
        .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
        .addItem(nvrhi::BindingLayoutItem::Sampler(3))
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(4)));

    cloudDensityBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .setBindingOffsets(computeOffsets)
        .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(1)));

    volumetricCloudsBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .setBindingOffsets(computeOffsets)
        .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
        .addItem(nvrhi::BindingLayoutItem::Sampler(3))
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(4)));

    waterOceanBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .setBindingOffsets(computeOffsets)
        .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(3))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(4))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(5))
        .addItem(nvrhi::BindingLayoutItem::Sampler(6))
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(7)));

    if (rayTracingEnabled_) {
        rayTracingBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::AllRayTracing)
            .setBindingOffsets(computeOffsets)
            .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
            .addItem(nvrhi::BindingLayoutItem::RayTracingAccelStruct(1))
            .addItem(nvrhi::BindingLayoutItem::Texture_UAV(2))
            .addItem(nvrhi::BindingLayoutItem::RawBuffer_SRV(3))
            .addItem(nvrhi::BindingLayoutItem::RawBuffer_SRV(4))
            .addItem(nvrhi::BindingLayoutItem::Texture_SRV(5))
            .addItem(nvrhi::BindingLayoutItem::Sampler(6)));
        if (!rayTracingBindingLayout_) {
            throw std::runtime_error("Failed to create NVRHI ray tracing binding layout");
        }
    }
}

void VulkanRenderer::createPipeline()
{
    if (!gBufferFramebuffer_ || framebuffers_.empty()) {
        throw std::runtime_error("Cannot create Vulkan renderer pipeline without framebuffers");
    }

    nvrhi::RenderState renderState;
    renderState.rasterState.setCullNone();
    renderState.depthStencilState.enableDepthTest();
    renderState.depthStencilState.enableDepthWrite();
    renderState.depthStencilState.setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);

    nvrhi::GraphicsPipelineDesc geometryPipelineDesc;
    geometryPipelineDesc
        .setPrimType(nvrhi::PrimitiveType::TriangleList)
        .setInputLayout(inputLayout_)
        .setVertexShader(geometryVertexShader_)
        .setFragmentShader(geometryFragmentShader_)
        .setRenderState(renderState)
        .addBindingLayout(geometryBindingLayout_);

    geometryPipeline_ = device_->nvrhiDevice()->createGraphicsPipeline(
        geometryPipelineDesc,
        gBufferFramebuffer_->getFramebufferInfo());

    if (!geometryPipeline_) {
        throw std::runtime_error("Failed to create NVRHI G-buffer pipeline for Vulkan renderer");
    }

    nvrhi::RenderState lightingRenderState;
    lightingRenderState.rasterState.setCullNone();
    lightingRenderState.depthStencilState.disableDepthTest();
    lightingRenderState.depthStencilState.disableDepthWrite();

    nvrhi::GraphicsPipelineDesc pbrLightingPipelineDesc;
    pbrLightingPipelineDesc
        .setPrimType(nvrhi::PrimitiveType::TriangleList)
        .setVertexShader(pbrLightingVertexShader_)
        .setFragmentShader(pbrLightingFragmentShader_)
        .setRenderState(lightingRenderState)
        .addBindingLayout(pbrLightingBindingLayout_);

    pbrLightingPipeline_ = device_->nvrhiDevice()->createGraphicsPipeline(
        pbrLightingPipelineDesc,
        postFramebuffer_->getFramebufferInfo());

    if (!pbrLightingPipeline_) {
        throw std::runtime_error("Failed to create NVRHI PBR lighting pipeline for Vulkan renderer");
    }

    nvrhi::GraphicsPipelineDesc antiAliasingPipelineDesc;
    antiAliasingPipelineDesc
        .setPrimType(nvrhi::PrimitiveType::TriangleList)
        .setVertexShader(pbrLightingVertexShader_)
        .setFragmentShader(antiAliasingFragmentShader_)
        .setRenderState(lightingRenderState)
        .addBindingLayout(antiAliasingBindingLayout_);

    antiAliasingPipeline_ = device_->nvrhiDevice()->createGraphicsPipeline(
        antiAliasingPipelineDesc,
        framebuffers_.front()->getFramebufferInfo());

    if (!antiAliasingPipeline_) {
        throw std::runtime_error("Failed to create NVRHI anti-aliasing pipeline for Vulkan renderer");
    }

    nvrhi::GraphicsPipelineDesc skyAtmospherePipelineDesc;
    skyAtmospherePipelineDesc
        .setPrimType(nvrhi::PrimitiveType::TriangleList)
        .setVertexShader(skyAtmosphereVertexShader_)
        .setFragmentShader(skyAtmosphereFragmentShader_)
        .setRenderState(lightingRenderState)
        .addBindingLayout(skyAtmosphereBindingLayout_);

    skyAtmospherePipeline_ = device_->nvrhiDevice()->createGraphicsPipeline(
        skyAtmospherePipelineDesc,
        postFramebuffer_->getFramebufferInfo());

    if (!skyAtmospherePipeline_) {
        throw std::runtime_error("Failed to create NVRHI sky atmosphere pipeline for Vulkan renderer");
    }

    skyTransmittancePipeline_ = device_->nvrhiDevice()->createComputePipeline(nvrhi::ComputePipelineDesc()
        .setComputeShader(skyTransmittanceComputeShader_)
        .addBindingLayout(skyTransmittanceBindingLayout_));

    skyMultiScatteringPipeline_ = device_->nvrhiDevice()->createComputePipeline(nvrhi::ComputePipelineDesc()
        .setComputeShader(skyMultiScatteringComputeShader_)
        .addBindingLayout(skyMultiScatteringBindingLayout_));

    skyAtmosphereComputePipeline_ = device_->nvrhiDevice()->createComputePipeline(nvrhi::ComputePipelineDesc()
        .setComputeShader(skyAtmosphereComputeShader_)
        .addBindingLayout(skyAtmosphereComputeBindingLayout_));

    cloudDensityPipeline_ = device_->nvrhiDevice()->createComputePipeline(nvrhi::ComputePipelineDesc()
        .setComputeShader(cloudDensityComputeShader_)
        .addBindingLayout(cloudDensityBindingLayout_));

    volumetricCloudsPipeline_ = device_->nvrhiDevice()->createComputePipeline(nvrhi::ComputePipelineDesc()
        .setComputeShader(volumetricCloudsComputeShader_)
        .addBindingLayout(volumetricCloudsBindingLayout_));

    waterOceanPipeline_ = device_->nvrhiDevice()->createComputePipeline(nvrhi::ComputePipelineDesc()
        .setComputeShader(waterOceanComputeShader_)
        .addBindingLayout(waterOceanBindingLayout_));

    if (!skyTransmittancePipeline_ || !skyMultiScatteringPipeline_ ||
        !skyAtmosphereComputePipeline_ || !cloudDensityPipeline_ || !volumetricCloudsPipeline_ || !waterOceanPipeline_) {
        throw std::runtime_error("Failed to create NVRHI atmosphere compute pipelines for Vulkan renderer");
    }

    if (rayTracingEnabled_) {
        rayTracingPipeline_ = device_->nvrhiDevice()->createRayTracingPipeline(nvrhi::rt::PipelineDesc()
            .addBindingLayout(rayTracingBindingLayout_)
            .setMaxPayloadSize(sizeof(float) * 4)
            .setMaxRecursionDepth(1)
            .addShader(nvrhi::rt::PipelineShaderDesc()
                .setExportName("RayGen")
                .setShader(rayTracingRayGenShader_))
            .addShader(nvrhi::rt::PipelineShaderDesc()
                .setExportName("Miss")
                .setShader(rayTracingMissShader_))
            .addHitGroup(nvrhi::rt::PipelineHitGroupDesc()
                .setExportName("GeometryHit")
                .setClosestHitShader(rayTracingClosestHitShader_)));

        if (!rayTracingPipeline_) {
            throw std::runtime_error("Failed to create NVRHI ray tracing prototype pipeline");
        }

        rayTracingShaderTable_ = rayTracingPipeline_->createShaderTable(
            nvrhi::rt::ShaderTableDesc().setDebugName("RayTracingPrototypeShaderTable"));
        rayTracingShaderTable_->setRayGenerationShader("RayGen");
        rayTracingShaderTable_->addHitGroup("GeometryHit");
        rayTracingShaderTable_->addMissShader("Miss");
    }
}

void VulkanRenderer::createBuffers()
{
    // The ray tracing closest-hit shader raw-loads MeshVertex by byte offset.
    // If MeshVertex changes, update RayTracingPrototype.rchit.glsl with it.
    static_assert(sizeof(Vertex) == 92, "Ray tracing shaders raw-load MeshVertex with a 92-byte stride");

    vertexBuffer_ = device_->nvrhiDevice()->createBuffer(nvrhi::BufferDesc()
        .setByteSize(MaxPrimitiveVertices * sizeof(Vertex))
        .setIsVertexBuffer(true)
        .setIsAccelStructBuildInput(rayTracingEnabled_)
        .setCanHaveRawViews(rayTracingEnabled_)
        .setDebugName("PrimitiveVertexBuffer")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer));

    indexBuffer_ = device_->nvrhiDevice()->createBuffer(nvrhi::BufferDesc()
        .setByteSize(MaxPrimitiveIndices * sizeof(uint32_t))
        .setIsIndexBuffer(true)
        .setIsAccelStructBuildInput(rayTracingEnabled_)
        .setCanHaveRawViews(rayTracingEnabled_)
        .setDebugName("PrimitiveIndexBuffer")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::IndexBuffer));

    dynamicVertexBuffer_ = device_->nvrhiDevice()->createBuffer(nvrhi::BufferDesc()
        .setByteSize(MaxDynamicPrimitiveVertices * sizeof(Vertex))
        .setIsVertexBuffer(true)
        .setDebugName("DynamicPrimitiveVertexBuffer")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer));

    dynamicIndexBuffer_ = device_->nvrhiDevice()->createBuffer(nvrhi::BufferDesc()
        .setByteSize(MaxDynamicPrimitiveIndices * sizeof(uint32_t))
        .setIsIndexBuffer(true)
        .setDebugName("DynamicPrimitiveIndexBuffer")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::IndexBuffer));

    lightingConstantsBuffer_ = device_->nvrhiDevice()->createBuffer(nvrhi::BufferDesc()
        .setByteSize(sizeof(LightingConstants))
        .setIsConstantBuffer(true)
        .setDebugName("LightingConstants")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer));

    lightingSampler_ = device_->nvrhiDevice()->createSampler(nvrhi::SamplerDesc()
        .setAllFilters(false)
        .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

    const float maxAnisotropy = effectiveMaxAnisotropy();
    cloudSampler_ = device_->nvrhiDevice()->createSampler(nvrhi::SamplerDesc()
        .setAllFilters(true)
        .setMaxAnisotropy(maxAnisotropy)
        .setAddressU(nvrhi::SamplerAddressMode::Wrap)
        .setAddressV(nvrhi::SamplerAddressMode::Clamp)
        .setAddressW(nvrhi::SamplerAddressMode::Wrap));

    pbrLightingBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::Texture_SRV(0, sampledGBufferPosition()))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(1, sampledGBufferNormal()))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(2, sampledGBufferAlbedo()))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(3, sampledGBufferMaterial()))
        .addItem(nvrhi::BindingSetItem::Sampler(4, lightingSampler_))
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(5, lightingConstantsBuffer_)),
        pbrLightingBindingLayout_);

    antiAliasingBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::Texture_SRV(0, postColorTexture_))
        .addItem(nvrhi::BindingSetItem::Sampler(1, lightingSampler_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(2, taaHistoryTexture_)),
        antiAliasingBindingLayout_);

    skyAtmosphereBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::Texture_SRV(0, waterOceanTexture_))
        .addItem(nvrhi::BindingSetItem::Sampler(1, cloudSampler_)),
        skyAtmosphereBindingLayout_);

    skyTransmittanceBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, lightingConstantsBuffer_))
        .addItem(nvrhi::BindingSetItem::Texture_UAV(1, skyTransmittanceLut_)),
        skyTransmittanceBindingLayout_);

    skyMultiScatteringBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, lightingConstantsBuffer_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(1, skyTransmittanceLut_))
        .addItem(nvrhi::BindingSetItem::Sampler(2, cloudSampler_))
        .addItem(nvrhi::BindingSetItem::Texture_UAV(3, skyMultiScatteringLut_)),
        skyMultiScatteringBindingLayout_);

    skyAtmosphereComputeBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, lightingConstantsBuffer_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(1, skyTransmittanceLut_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(2, skyMultiScatteringLut_))
        .addItem(nvrhi::BindingSetItem::Sampler(3, cloudSampler_))
        .addItem(nvrhi::BindingSetItem::Texture_UAV(4, skyAtmosphereTexture_)),
        skyAtmosphereComputeBindingLayout_);

    cloudDensityBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, lightingConstantsBuffer_))
        .addItem(nvrhi::BindingSetItem::Texture_UAV(1, cloudDensityTexture_)),
        cloudDensityBindingLayout_);

    volumetricCloudsBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, lightingConstantsBuffer_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(1, skyAtmosphereTexture_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(2, cloudDensityTexture_))
        .addItem(nvrhi::BindingSetItem::Sampler(3, cloudSampler_))
        .addItem(nvrhi::BindingSetItem::Texture_UAV(4, volumetricCloudsTexture_)),
        volumetricCloudsBindingLayout_);

    waterOceanBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, lightingConstantsBuffer_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(1, volumetricCloudsTexture_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(2, sampledGBufferPosition()))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(3, sampledGBufferNormal()))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(4, sampledGBufferAlbedo()))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(5, sampledGBufferMaterial()))
        .addItem(nvrhi::BindingSetItem::Sampler(6, cloudSampler_))
        .addItem(nvrhi::BindingSetItem::Texture_UAV(7, waterOceanTexture_)),
        waterOceanBindingLayout_);

    if (rayTracingEnabled_) {
        rayTracingCompositeBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, rayTracingOutputTexture_))
            .addItem(nvrhi::BindingSetItem::Sampler(1, cloudSampler_)),
            skyAtmosphereBindingLayout_);
    }

    if (!vertexBuffer_ || !indexBuffer_ || !dynamicVertexBuffer_ || !dynamicIndexBuffer_ ||
        !lightingConstantsBuffer_ || !lightingSampler_ || !cloudSampler_ ||
        !pbrLightingBindingSet_ || !antiAliasingBindingSet_ || !skyAtmosphereBindingSet_ || !skyTransmittanceBindingSet_ ||
        !skyMultiScatteringBindingSet_ || !skyAtmosphereComputeBindingSet_ || !cloudDensityBindingSet_ ||
        !volumetricCloudsBindingSet_ || !waterOceanBindingSet_ ||
        (rayTracingEnabled_ && !rayTracingCompositeBindingSet_)) {
        throw std::runtime_error("Failed to create Vulkan renderer geometry buffers");
    }
}

void VulkanRenderer::rebuildMesh()
{
    vertices_.clear();
    indices_.clear();
    uploadedIndexCount_ = 0;
    vertices_.reserve((std::min)(static_cast<size_t>(MaxPrimitiveVertices), primitives_.size() * 12));
    indices_.reserve((std::min)(static_cast<size_t>(MaxPrimitiveIndices), primitives_.size() * 18));

    for (const PrimitiveInstance& primitive : primitives_) {
        switch (primitive.type) {
        case PrimitiveType::Triangle:
            appendTriangle(primitive);
            break;
        case PrimitiveType::Quad:
            appendQuad(primitive);
            break;
        case PrimitiveType::Cube:
            appendCube(primitive);
            break;
        case PrimitiveType::Sphere:
            appendSphere(primitive);
            break;
        }
    }
}

void VulkanRenderer::rebuildDynamicMesh()
{
    std::vector<Vertex> staticVertices = std::move(vertices_);
    std::vector<uint32_t> staticIndices = std::move(indices_);

    vertices_.clear();
    indices_.clear();
    vertices_.reserve((std::min)(static_cast<size_t>(MaxDynamicPrimitiveVertices), dynamicPrimitives_.size() * 544));
    indices_.reserve((std::min)(static_cast<size_t>(MaxDynamicPrimitiveIndices), dynamicPrimitives_.size() * 3072));

    for (const PrimitiveInstance& primitive : dynamicPrimitives_) {
        switch (primitive.type) {
        case PrimitiveType::Triangle:
            appendTriangle(primitive);
            break;
        case PrimitiveType::Quad:
            appendQuad(primitive);
            break;
        case PrimitiveType::Cube:
            appendCube(primitive);
            break;
        case PrimitiveType::Sphere:
            appendSphere(primitive);
            break;
        }
    }

    dynamicVertices_ = std::move(vertices_);
    dynamicIndices_ = std::move(indices_);
    vertices_ = std::move(staticVertices);
    indices_ = std::move(staticIndices);
}

void VulkanRenderer::dispatchSkyAtmosphereCompute()
{
    nvrhi::ComputeState transmittanceState;
    transmittanceState.setPipeline(skyTransmittancePipeline_);
    transmittanceState.addBindingSet(skyTransmittanceBindingSet_);
    commandList_->setResourceStatesForBindingSet(skyTransmittanceBindingSet_);
    commandList_->commitBarriers();
    commandList_->setComputeState(transmittanceState);
    commandList_->dispatch(divideAndRoundUp(SkyTransmittanceLutWidth, 8), divideAndRoundUp(SkyTransmittanceLutHeight, 8));

    nvrhi::ComputeState multiScatteringState;
    multiScatteringState.setPipeline(skyMultiScatteringPipeline_);
    multiScatteringState.addBindingSet(skyMultiScatteringBindingSet_);
    commandList_->setResourceStatesForBindingSet(skyMultiScatteringBindingSet_);
    commandList_->commitBarriers();
    commandList_->setComputeState(multiScatteringState);
    commandList_->dispatch(divideAndRoundUp(SkyMultiScatteringLutWidth, 8), divideAndRoundUp(SkyMultiScatteringLutHeight, 8));

    const uint32_t width = swapchain_->extent().width;
    const uint32_t height = swapchain_->extent().height;
    nvrhi::ComputeState skyState;
    skyState.setPipeline(skyAtmosphereComputePipeline_);
    skyState.addBindingSet(skyAtmosphereComputeBindingSet_);
    commandList_->setResourceStatesForBindingSet(skyAtmosphereComputeBindingSet_);
    commandList_->commitBarriers();
    commandList_->setComputeState(skyState);
    commandList_->dispatch(divideAndRoundUp(width, 8), divideAndRoundUp(height, 8));
}

void VulkanRenderer::dispatchVolumetricCloudsCompute()
{
    const uint32_t width = swapchain_->extent().width;
    const uint32_t height = swapchain_->extent().height;

    nvrhi::ComputeState cloudsState;
    cloudsState.setPipeline(volumetricCloudsPipeline_);
    cloudsState.addBindingSet(volumetricCloudsBindingSet_);
    commandList_->setResourceStatesForBindingSet(volumetricCloudsBindingSet_);
    commandList_->commitBarriers();
    commandList_->setComputeState(cloudsState);
    commandList_->dispatch(divideAndRoundUp(width, 8), divideAndRoundUp(height, 8));
}

void VulkanRenderer::dispatchCloudDensityCompute()
{
    nvrhi::ComputeState densityState;
    densityState.setPipeline(cloudDensityPipeline_);
    densityState.addBindingSet(cloudDensityBindingSet_);
    commandList_->setResourceStatesForBindingSet(cloudDensityBindingSet_);
    commandList_->commitBarriers();
    commandList_->setComputeState(densityState);
    commandList_->dispatch(
        divideAndRoundUp(CloudDensityTextureWidth, 4),
        divideAndRoundUp(CloudDensityTextureHeight, 4),
        divideAndRoundUp(CloudDensityTextureDepth, 4));
}

void VulkanRenderer::dispatchWaterOceanCompute()
{
    const uint32_t width = swapchain_->extent().width;
    const uint32_t height = swapchain_->extent().height;

    nvrhi::ComputeState waterState;
    waterState.setPipeline(waterOceanPipeline_);
    waterState.addBindingSet(waterOceanBindingSet_);
    commandList_->setResourceStatesForBindingSet(waterOceanBindingSet_);
    commandList_->commitBarriers();
    commandList_->setComputeState(waterState);
    commandList_->dispatch(divideAndRoundUp(width, 8), divideAndRoundUp(height, 8));
}

void VulkanRenderer::rebuildRayTracingAccelerationStructures()
{
    if (!rayTracingEnabled_ || !rayTracingAccelerationStructuresDirty_) {
        return;
    }

    if (uploadedIndexCount_ == 0 || vertices_.empty()) {
        rayTracingBindingSet_ = nullptr;
        rayTracingTlas_ = nullptr;
        rayTracingBlas_ = nullptr;
        rayTracingAccelerationStructuresDirty_ = false;
        return;
    }

    nvrhi::rt::GeometryTriangles triangles;
    triangles
        .setVertexBuffer(vertexBuffer_)
        .setVertexFormat(nvrhi::Format::RGB32_FLOAT)
        .setVertexOffset(offsetof(Vertex, position))
        .setVertexCount(static_cast<uint32_t>(vertices_.size()))
        .setVertexStride(sizeof(Vertex))
        .setIndexBuffer(indexBuffer_)
        .setIndexFormat(nvrhi::Format::R32_UINT)
        .setIndexCount(uploadedIndexCount_);

    rayTracingGeometryDesc_ = nvrhi::rt::GeometryDesc()
        .setFlags(nvrhi::rt::GeometryFlags::Opaque)
        .setTriangles(triangles);

    rayTracingBlas_ = device_->nvrhiDevice()->createAccelStruct(nvrhi::rt::AccelStructDesc()
        .setDebugName("PrimitiveWorldBLAS")
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .addBottomLevelGeometry(rayTracingGeometryDesc_));

    rayTracingTlas_ = device_->nvrhiDevice()->createAccelStruct(nvrhi::rt::AccelStructDesc()
        .setDebugName("PrimitiveWorldTLAS")
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .setTopLevelMaxInstances(1));

    if (!rayTracingBlas_ || !rayTracingTlas_) {
        throw std::runtime_error("Failed to create NVRHI ray tracing acceleration structures");
    }

    commandList_->setBufferState(vertexBuffer_, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList_->setBufferState(indexBuffer_, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList_->commitBarriers();
    commandList_->buildBottomLevelAccelStruct(rayTracingBlas_, &rayTracingGeometryDesc_, 1, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);

    nvrhi::rt::InstanceDesc instance;
    instance
        .setBLAS(rayTracingBlas_)
        .setInstanceMask(0xff)
        .setInstanceContributionToHitGroupIndex(0)
        .setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable | nvrhi::rt::InstanceFlags::ForceOpaque)
        .setTransform(nvrhi::rt::c_IdentityTransform);

    commandList_->buildTopLevelAccelStruct(rayTracingTlas_, &instance, 1, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);
    commandList_->setAccelStructState(rayTracingTlas_, nvrhi::ResourceStates::AccelStructRead);
    commandList_->commitBarriers();

    rayTracingBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, lightingConstantsBuffer_))
        .addItem(nvrhi::BindingSetItem::RayTracingAccelStruct(1, rayTracingTlas_))
        .addItem(nvrhi::BindingSetItem::Texture_UAV(2, rayTracingOutputTexture_))
        .addItem(nvrhi::BindingSetItem::RawBuffer_SRV(3, vertexBuffer_))
        .addItem(nvrhi::BindingSetItem::RawBuffer_SRV(4, indexBuffer_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(5, waterOceanTexture_))
        .addItem(nvrhi::BindingSetItem::Sampler(6, cloudSampler_)),
        rayTracingBindingLayout_);

    if (!rayTracingBindingSet_) {
        throw std::runtime_error("Failed to create NVRHI ray tracing binding set");
    }

    rayTracingAccelerationStructuresDirty_ = false;
}

void VulkanRenderer::dispatchRayTracingPrototype()
{
    if (!rayTracingPipeline_ || !rayTracingShaderTable_ || !rayTracingBindingSet_) {
        return;
    }

    commandList_->setResourceStatesForBindingSet(rayTracingBindingSet_);
    commandList_->commitBarriers();
    commandList_->setRayTracingState(nvrhi::rt::State()
        .setShaderTable(rayTracingShaderTable_)
        .addBindingSet(rayTracingBindingSet_));
    commandList_->dispatchRays(nvrhi::rt::DispatchRaysArguments()
        .setDimensions(swapchain_->extent().width, swapchain_->extent().height));
}

VulkanRenderer::PushConstants VulkanRenderer::buildPushConstants() const
{
    const float width = static_cast<float>(swapchain_->extent().width);
    const float height = static_cast<float>(swapchain_->extent().height);
    const float aspect = width / (std::max)(height, 1.0f);
    glm::mat4 projection = glm::perspective(
        glm::radians(camera_.fovDegrees),
        aspect,
        camera_.nearPlane,
        camera_.farPlane);
    // GLM's Vulkan depth range is enabled above; flip Y to match Vulkan clip space.
    projection[1][1] *= -1.0f;
    const glm::mat4 view = glm::lookAt(
        glm::vec3(camera_.position[0], camera_.position[1], camera_.position[2]),
        glm::vec3(camera_.target[0], camera_.target[1], camera_.target[2]),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 viewProjection = projection * view;

    PushConstants constants {};
    std::copy(glm::value_ptr(viewProjection), glm::value_ptr(viewProjection) + 16, std::begin(constants.viewProjection));
    constants.materialParameters[0] = materialMetallic_;
    constants.materialParameters[1] = materialRoughness_;
    constants.materialParameters[2] = materialAmbient_;
    constants.materialParameters[3] = 0.0f;
    return constants;
}

VulkanRenderer::LightingConstants VulkanRenderer::buildLightingConstants() const
{
    const float width = static_cast<float>(swapchain_->extent().width);
    const float height = static_cast<float>(swapchain_->extent().height);
    const float aspect = width / (std::max)(height, 1.0f);
    const float tanHalfFov = std::tan(glm::radians(camera_.fovDegrees) * 0.5f);

    const glm::vec3 cameraPosition { camera_.position[0], camera_.position[1], camera_.position[2] };
    const glm::vec3 cameraTarget { camera_.target[0], camera_.target[1], camera_.target[2] };
    const glm::vec3 forward = glm::normalize(cameraTarget - cameraPosition);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, forward);
    const glm::vec3 sunDirection = glm::normalize(glm::vec3(sunDirection_[0], sunDirection_[1], sunDirection_[2]));

    LightingConstants constants {};
    constants.cameraPosition[0] = cameraPosition.x;
    constants.cameraPosition[1] = cameraPosition.y;
    constants.cameraPosition[2] = cameraPosition.z;
    constants.cameraPosition[3] = 1.0f;
    constants.cameraForward[0] = forward.x;
    constants.cameraForward[1] = forward.y;
    constants.cameraForward[2] = forward.z;
    constants.cameraForward[3] = 0.0f;
    constants.cameraRight[0] = right.x;
    constants.cameraRight[1] = right.y;
    constants.cameraRight[2] = right.z;
    constants.cameraRight[3] = 0.0f;
    constants.cameraUp[0] = up.x;
    constants.cameraUp[1] = up.y;
    constants.cameraUp[2] = up.z;
    constants.cameraUp[3] = 0.0f;
    constants.sunDirection[0] = sunDirection.x;
    constants.sunDirection[1] = sunDirection.y;
    constants.sunDirection[2] = sunDirection.z;
    constants.sunDirection[3] = 0.0f;
    constants.sunColorAndIntensity[0] = sunColor_[0];
    constants.sunColorAndIntensity[1] = sunColor_[1];
    constants.sunColorAndIntensity[2] = sunColor_[2];
    constants.sunColorAndIntensity[3] = sunIntensity_;
    constants.pointLightCount[0] = static_cast<float>(std::clamp(activePointLightCount_, 0, 4));
    constants.pointLightCount[1] = 0.0f;
    constants.pointLightCount[2] = 0.0f;
    constants.pointLightCount[3] = 0.0f;

    for (int i = 0; i < 4; ++i) {
        constants.pointLightPosition[i][0] = pointLights_[i].position[0];
        constants.pointLightPosition[i][1] = pointLights_[i].position[1];
        constants.pointLightPosition[i][2] = pointLights_[i].position[2];
        constants.pointLightPosition[i][3] = 1.0f;
        constants.pointLightColorAndIntensity[i][0] = pointLights_[i].color[0];
        constants.pointLightColorAndIntensity[i][1] = pointLights_[i].color[1];
        constants.pointLightColorAndIntensity[i][2] = pointLights_[i].color[2];
        constants.pointLightColorAndIntensity[i][3] = pointLights_[i].intensity;
    }

    constants.skyParameters[0] = skyRayleighScale_;
    constants.skyParameters[1] = skyMieScale_;
    constants.skyParameters[2] = skyExposure_;
    constants.skyParameters[3] = 0.0f;
    constants.cloudParameters[0] = cloudCoverage_;
    constants.cloudParameters[1] = cloudDensity_;
    constants.cloudParameters[2] = cloudHeight_;
    constants.cloudParameters[3] = cloudThickness_;
    const float windLength = std::sqrt(cloudWindDirection_[0] * cloudWindDirection_[0] + cloudWindDirection_[1] * cloudWindDirection_[1]);
    const float invWindLength = windLength > 0.0001f ? 1.0f / windLength : 1.0f;
    constants.cloudWindParameters[0] = cloudWindDirection_[0] * invWindLength;
    constants.cloudWindParameters[1] = cloudWindDirection_[1] * invWindLength;
    constants.cloudWindParameters[2] = cloudWindSpeed_;
    constants.cloudWindParameters[3] = cloudTime_;
    const float waterWindLength = std::sqrt(waterWindDirection_[0] * waterWindDirection_[0] + waterWindDirection_[1] * waterWindDirection_[1]);
    const float invWaterWindLength = waterWindLength > 0.0001f ? 1.0f / waterWindLength : 1.0f;
    constants.waterParameters[0] = waterLevel_;
    constants.waterParameters[1] = waterAmplitude_;
    constants.waterParameters[2] = waterChoppiness_;
    constants.waterParameters[3] = 0.0f;
    constants.waterWindParameters[0] = waterWindDirection_[0] * invWaterWindLength;
    constants.waterWindParameters[1] = waterWindDirection_[1] * invWaterWindLength;
    constants.waterWindParameters[2] = waterWindSpeed_;
    constants.waterWindParameters[3] = cloudTime_;
    constants.cameraParameters[0] = tanHalfFov;
    constants.cameraParameters[1] = aspect;
    constants.cameraParameters[2] = width;
    constants.cameraParameters[3] = height;
    return constants;
}

uint32_t VulkanRenderer::sampleCountForAntiAliasingMode(AntiAliasingMode mode) const
{
    switch (mode) {
    case AntiAliasingMode::MSAA2x:
        return 2;
    case AntiAliasingMode::MSAA4x:
        return 4;
    case AntiAliasingMode::MSAA8x:
        return 8;
    default:
        return 1;
    }
}

float VulkanRenderer::effectiveMaxAnisotropy() const
{
    if (!device_ || !device_->samplerAnisotropyEnabled() || !anisotropicFilteringEnabled_) {
        return 1.0f;
    }

    return std::clamp(anisotropyLevel_, 1.0f, device_->maxSamplerAnisotropy());
}

nvrhi::ITexture* VulkanRenderer::sampledGBufferPosition() const
{
    return msaaSampleCount_ > 1 ? resolvedGBufferPosition_.Get() : gBufferPosition_.Get();
}

nvrhi::ITexture* VulkanRenderer::sampledGBufferNormal() const
{
    return msaaSampleCount_ > 1 ? resolvedGBufferNormal_.Get() : gBufferNormal_.Get();
}

nvrhi::ITexture* VulkanRenderer::sampledGBufferAlbedo() const
{
    return msaaSampleCount_ > 1 ? resolvedGBufferAlbedo_.Get() : gBufferAlbedo_.Get();
}

nvrhi::ITexture* VulkanRenderer::sampledGBufferMaterial() const
{
    return msaaSampleCount_ > 1 ? resolvedGBufferMaterial_.Get() : gBufferMaterial_.Get();
}

void VulkanRenderer::applyPendingAntiAliasingMode()
{
    if (pendingAntiAliasingMode_ == antiAliasingMode_) {
        return;
    }

    const uint32_t requestedSampleCount = sampleCountForAntiAliasingMode(pendingAntiAliasingMode_);
    const bool sampleCountChanged = requestedSampleCount != msaaSampleCount_;
    antiAliasingMode_ = pendingAntiAliasingMode_;
    taaHistoryValid_ = false;

    if (!sampleCountChanged || !device_ || !swapchain_) {
        return;
    }

    device_->nvrhiDevice()->waitForIdle();
    msaaSampleCount_ = requestedSampleCount;

    // MSAA changes the G-buffer framebuffer sample count, so rebuild the
    // resources and pipelines that directly depend on those attachments.
    pbrLightingBindingSet_ = nullptr;
    antiAliasingBindingSet_ = nullptr;
    skyAtmosphereBindingSet_ = nullptr;
    skyTransmittanceBindingSet_ = nullptr;
    skyMultiScatteringBindingSet_ = nullptr;
    skyAtmosphereComputeBindingSet_ = nullptr;
    cloudDensityBindingSet_ = nullptr;
    volumetricCloudsBindingSet_ = nullptr;
    waterOceanBindingSet_ = nullptr;
    rayTracingBindingSet_ = nullptr;
    rayTracingCompositeBindingSet_ = nullptr;
    vertexBuffer_ = nullptr;
    indexBuffer_ = nullptr;
    dynamicVertexBuffer_ = nullptr;
    dynamicIndexBuffer_ = nullptr;
    lightingConstantsBuffer_ = nullptr;
    lightingSampler_ = nullptr;
    cloudSampler_ = nullptr;
    geometryPipeline_ = nullptr;
    pbrLightingPipeline_ = nullptr;
    antiAliasingPipeline_ = nullptr;
    skyAtmospherePipeline_ = nullptr;
    skyTransmittancePipeline_ = nullptr;
    skyMultiScatteringPipeline_ = nullptr;
    skyAtmosphereComputePipeline_ = nullptr;
    cloudDensityPipeline_ = nullptr;
    volumetricCloudsPipeline_ = nullptr;
    waterOceanPipeline_ = nullptr;
    rayTracingShaderTable_ = nullptr;
    rayTracingPipeline_ = nullptr;
    rayTracingTlas_ = nullptr;
    rayTracingBlas_ = nullptr;
    gBufferFramebuffer_ = nullptr;
    gBufferPosition_ = nullptr;
    gBufferNormal_ = nullptr;
    gBufferAlbedo_ = nullptr;
    gBufferMaterial_ = nullptr;
    resolvedGBufferPosition_ = nullptr;
    resolvedGBufferNormal_ = nullptr;
    resolvedGBufferAlbedo_ = nullptr;
    resolvedGBufferMaterial_ = nullptr;
    depthTexture_ = nullptr;

    createGBufferTextures(*swapchain_);
    createDepthTexture(*swapchain_);

    nvrhi::FramebufferDesc gBufferDesc;
    gBufferDesc.addColorAttachment(gBufferPosition_);
    gBufferDesc.addColorAttachment(gBufferNormal_);
    gBufferDesc.addColorAttachment(gBufferAlbedo_);
    gBufferDesc.addColorAttachment(gBufferMaterial_);
    gBufferDesc.setDepthAttachment(depthTexture_);
    gBufferFramebuffer_ = device_->nvrhiDevice()->createFramebuffer(gBufferDesc);
    if (!gBufferFramebuffer_) {
        throw std::runtime_error("Failed to recreate Vulkan renderer G-buffer framebuffer for AA mode");
    }

    createPipeline();
    createBuffers();
    gpuMeshDirty_ = true;
    dynamicGpuMeshDirty_ = true;
    rayTracingAccelerationStructuresDirty_ = true;
    uploadedIndexCount_ = 0;
    uploadedDynamicIndexCount_ = 0;

    MENGINE_INFO("[RenderBackend] Anti-aliasing mode changed, MSAA sample count is {}", msaaSampleCount_);
}

void VulkanRenderer::applyPendingTextureFilteringSettings()
{
    const float clampedPendingLevel = device_
        ? std::clamp(pendingAnisotropyLevel_, 1.0f, (std::max)(device_->maxSamplerAnisotropy(), 1.0f))
        : pendingAnisotropyLevel_;
    pendingAnisotropyLevel_ = clampedPendingLevel;

    if (pendingAnisotropicFilteringEnabled_ == anisotropicFilteringEnabled_ &&
        std::abs(pendingAnisotropyLevel_ - anisotropyLevel_) < 0.01f) {
        return;
    }

    anisotropicFilteringEnabled_ = pendingAnisotropicFilteringEnabled_;
    anisotropyLevel_ = pendingAnisotropyLevel_;
    if (!device_ || !swapchain_) {
        return;
    }

    device_->nvrhiDevice()->waitForIdle();

    // Sampler changes are baked into binding sets, so rebuild the sets that
    // reference the renderer's shared samplers.
    pbrLightingBindingSet_ = nullptr;
    antiAliasingBindingSet_ = nullptr;
    skyAtmosphereBindingSet_ = nullptr;
    skyTransmittanceBindingSet_ = nullptr;
    skyMultiScatteringBindingSet_ = nullptr;
    skyAtmosphereComputeBindingSet_ = nullptr;
    cloudDensityBindingSet_ = nullptr;
    volumetricCloudsBindingSet_ = nullptr;
    waterOceanBindingSet_ = nullptr;
    rayTracingBindingSet_ = nullptr;
    rayTracingCompositeBindingSet_ = nullptr;
    vertexBuffer_ = nullptr;
    indexBuffer_ = nullptr;
    lightingConstantsBuffer_ = nullptr;
    lightingSampler_ = nullptr;
    cloudSampler_ = nullptr;

    createBuffers();
    gpuMeshDirty_ = true;
    rayTracingAccelerationStructuresDirty_ = true;
    uploadedIndexCount_ = 0;

    MENGINE_INFO("[RenderBackend] Anisotropic filtering {} at {:.1f}x",
        anisotropicFilteringEnabled_ ? "enabled" : "disabled",
        effectiveMaxAnisotropy());
}

void VulkanRenderer::drawPrimitivePanel()
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Vulkan Renderer");
    ImGui::Text("Render Path");
    if (rayTracingEnabled_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Ray Tracing");
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Raster");
    }
    if (rayTracingEnabled_) {
        ImGui::Text("RT AS: %s", rayTracingTlas_ ? "Ready" : "Building");
    }

    ImGui::Separator();
    ImGui::Text("Anti-Aliasing");
    const char* aaModes[] = { "Off", "FXAA", "TAA", "MSAA 2x", "MSAA 4x", "MSAA 8x" };
    int aaModeIndex = static_cast<int>(pendingAntiAliasingMode_);
    if (ImGui::Combo("Mode", &aaModeIndex, aaModes, static_cast<int>(sizeof(aaModes) / sizeof(aaModes[0])))) {
        pendingAntiAliasingMode_ = static_cast<AntiAliasingMode>(aaModeIndex);
    }
    if (pendingAntiAliasingMode_ == AntiAliasingMode::TAA) {
        ImGui::SliderFloat("History Weight", &taaHistoryWeight_, 0.0f, 0.45f);
    }
    if (pendingAntiAliasingMode_ != antiAliasingMode_) {
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "Applies next frame");
    }

    ImGui::Separator();
    ImGui::Text("Texture Filtering");
    const bool anisotropySupported = device_ && device_->samplerAnisotropyEnabled();
    if (!anisotropySupported) {
        ImGui::BeginDisabled();
    }
    ImGui::Checkbox("Anisotropic Filtering", &pendingAnisotropicFilteringEnabled_);
    if (pendingAnisotropicFilteringEnabled_) {
        const float maxAnisotropy = device_ ? (std::max)(device_->maxSamplerAnisotropy(), 1.0f) : 16.0f;
        ImGui::SliderFloat("Anisotropy", &pendingAnisotropyLevel_, 1.0f, maxAnisotropy, "%.0fx");
    }
    if (!anisotropySupported) {
        ImGui::EndDisabled();
        ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.35f, 1.0f), "Not supported by this GPU");
    } else if (pendingAnisotropicFilteringEnabled_ != anisotropicFilteringEnabled_ ||
        std::abs(pendingAnisotropyLevel_ - anisotropyLevel_) >= 0.01f) {
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "Applies next frame");
    }

    ImGui::Separator();
    ImGui::Text("Interaction");
    ImGui::Checkbox("Shooting Mode", &shootingModeEnabled_);

    if (ImGui::Button("Add Triangle")) {
        addPrimitive(PrimitiveType::Triangle);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Quad")) {
        addPrimitive(PrimitiveType::Quad);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Cube")) {
        addPrimitive(PrimitiveType::Cube);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Sphere")) {
        addPrimitive(PrimitiveType::Sphere);
    }

    if (ImGui::Button("Clear")) {
        clearPrimitives();
    }

    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::SliderFloat3("Position", camera_.position, -10.0f, 10.0f);
    ImGui::SliderFloat("FOV", &camera_.fovDegrees, 20.0f, 100.0f);

    ImGui::Separator();
    ImGui::Text("Sun & Sky");
    ImGui::SliderFloat3("Sun Direction", sunDirection_, -1.0f, 1.0f);
    ImGui::ColorEdit3("Sun Color", sunColor_);
    ImGui::SliderFloat("Sun Intensity", &sunIntensity_, 0.0f, 10.0f);
    ImGui::SliderFloat("Rayleigh Scale", &skyRayleighScale_, 0.0f, 4.0f);
    ImGui::SliderFloat("Mie Scale", &skyMieScale_, 0.0f, 2.0f);
    ImGui::SliderFloat("Sky Exposure", &skyExposure_, 0.1f, 4.0f);
    ImGui::SliderFloat("Cloud Coverage", &cloudCoverage_, 0.0f, 1.0f);
    ImGui::SliderFloat("Cloud Density", &cloudDensity_, 0.0f, 2.0f);
    ImGui::SliderFloat("Cloud Height", &cloudHeight_, 8.0f, 80.0f);
    ImGui::SliderFloat("Cloud Thickness", &cloudThickness_, 1.0f, 30.0f);
    ImGui::SliderFloat2("Cloud Wind Direction", cloudWindDirection_, -1.0f, 1.0f);
    ImGui::SliderFloat("Cloud Wind Speed", &cloudWindSpeed_, 0.0f, 12.0f);

    ImGui::Separator();
    ImGui::Text("Ocean");
    ImGui::SliderFloat("Water Level", &waterLevel_, -4.0f, 4.0f);
    ImGui::SliderFloat("Wave Amplitude", &waterAmplitude_, 0.0f, 1.5f);
    ImGui::SliderFloat("Wave Choppiness", &waterChoppiness_, 0.0f, 2.5f);
    ImGui::SliderFloat2("Water Wind Direction", waterWindDirection_, -1.0f, 1.0f);
    ImGui::SliderFloat("Water Wind Speed", &waterWindSpeed_, 0.0f, 30.0f);

    ImGui::Separator();
    ImGui::Text("Point Lights");
    ImGui::SliderInt("Active Lights", &activePointLightCount_, 0, 4);
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        if (ImGui::TreeNode("Point Light")) {
            ImGui::SliderFloat3("Position", pointLights_[i].position, -8.0f, 8.0f);
            ImGui::ColorEdit3("Color", pointLights_[i].color);
            ImGui::SliderFloat("Intensity", &pointLights_[i].intensity, 0.0f, 100.0f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::Text("PBR Material");
    ImGui::SliderFloat("Metallic", &materialMetallic_, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &materialRoughness_, 0.04f, 1.0f);
    ImGui::SliderFloat("Ambient", &materialAmbient_, 0.0f, 0.2f);

    ImGui::Separator();
    ImGui::Text("Primitive count: %zu", primitives_.size());
    constexpr size_t MaxVisiblePrimitiveRows = 64;
    const size_t visibleRows = (std::min)(primitives_.size(), MaxVisiblePrimitiveRows);
    for (size_t index = 0; index < visibleRows; ++index) {
        ImGui::Text("%zu. %s", index + 1, primitiveName(primitives_[index].type));
    }
    if (primitives_.size() > visibleRows) {
        ImGui::Text("... %zu more hidden", primitives_.size() - visibleRows);
    }

    ImGui::End();
}

void VulkanRenderer::appendTriangle(const PrimitiveInstance& primitive)
{
    const uint32_t base = static_cast<uint32_t>(vertices_.size());
    const float half = primitive.size * 0.5f;
    const float cx = primitive.position[0];
    const float cy = primitive.position[1];
    const float cz = primitive.position[2];
    const float r = primitive.color[0];
    const float g = primitive.color[1];
    const float b = primitive.color[2];

    vertices_.push_back({ { cx, cy + half, cz }, { 0.0f, 0.9f, 0.2f }, { r, g, b }, { 0.5f, 1.0f } });
    vertices_.push_back({ { cx - half, cy - half, cz + half }, { -0.6f, -0.45f, 0.6f }, { r, g, b }, { 0.0f, 0.0f } });
    vertices_.push_back({ { cx + half, cy - half, cz + half }, { 0.6f, -0.45f, 0.6f }, { r, g, b }, { 1.0f, 0.0f } });
    vertices_.push_back({ { cx, cy - half, cz - half }, { 0.0f, -0.45f, -0.9f }, { r, g, b }, { 0.5f, 0.0f } });
    indices_.insert(indices_.end(), {
        base, base + 1, base + 2,
        base, base + 2, base + 3,
        base, base + 3, base + 1,
        base + 1, base + 3, base + 2,
    });
}

void VulkanRenderer::appendQuad(const PrimitiveInstance& primitive)
{
    const float hx = primitive.size * 0.62f;
    const float hy = primitive.size * 0.28f;
    const float hz = primitive.size * 0.42f;
    const float cx = primitive.position[0];
    const float cy = primitive.position[1];
    const float cz = primitive.position[2];
    const float r = primitive.color[0];
    const float g = primitive.color[1];
    const float b = primitive.color[2];

    const std::array<std::array<float, 3>, 8> corners {{
        {{ cx - hx, cy - hy, cz - hz }},
        {{ cx + hx, cy - hy, cz - hz }},
        {{ cx + hx, cy + hy, cz - hz }},
        {{ cx - hx, cy + hy, cz - hz }},
        {{ cx - hx, cy - hy, cz + hz }},
        {{ cx + hx, cy - hy, cz + hz }},
        {{ cx + hx, cy + hy, cz + hz }},
        {{ cx - hx, cy + hy, cz + hz }},
    }};

    auto appendFace = [&](std::array<uint32_t, 4> face, std::array<float, 3> normal) {
        const uint32_t faceBase = static_cast<uint32_t>(vertices_.size());
        const std::array<std::array<float, 2>, 4> uvs {{
            {{ 0.0f, 0.0f }},
            {{ 1.0f, 0.0f }},
            {{ 1.0f, 1.0f }},
            {{ 0.0f, 1.0f }},
        }};
        size_t uvIndex = 0;
        for (uint32_t cornerIndex : face) {
            const auto& position = corners[cornerIndex];
            const auto& uv = uvs[uvIndex++];
            vertices_.push_back({
                { position[0], position[1], position[2] },
                { normal[0], normal[1], normal[2] },
                { r, g, b },
                { uv[0], uv[1] }
            });
        }
        indices_.insert(indices_.end(), { faceBase, faceBase + 2, faceBase + 1, faceBase, faceBase + 3, faceBase + 2 });
    };

    if ((primitive.visibleFaces & PrimitiveFaceNegativeZ) != 0) {
        appendFace({ 0, 1, 2, 3 }, { 0.0f, 0.0f, -1.0f });
    }
    if ((primitive.visibleFaces & PrimitiveFacePositiveZ) != 0) {
        appendFace({ 4, 7, 6, 5 }, { 0.0f, 0.0f, 1.0f });
    }
    if ((primitive.visibleFaces & PrimitiveFaceNegativeY) != 0) {
        appendFace({ 0, 4, 5, 1 }, { 0.0f, -1.0f, 0.0f });
    }
    if ((primitive.visibleFaces & PrimitiveFacePositiveY) != 0) {
        appendFace({ 3, 2, 6, 7 }, { 0.0f, 1.0f, 0.0f });
    }
    if ((primitive.visibleFaces & PrimitiveFacePositiveX) != 0) {
        appendFace({ 1, 5, 6, 2 }, { 1.0f, 0.0f, 0.0f });
    }
    if ((primitive.visibleFaces & PrimitiveFaceNegativeX) != 0) {
        appendFace({ 0, 3, 7, 4 }, { -1.0f, 0.0f, 0.0f });
    }
}

void VulkanRenderer::appendCube(const PrimitiveInstance& primitive)
{
    const float half = primitive.size * 0.5f;
    const float cx = primitive.position[0];
    const float cy = primitive.position[1];
    const float cz = primitive.position[2];
    const float r = primitive.color[0];
    const float g = primitive.color[1];
    const float b = primitive.color[2];

    const std::array<std::array<float, 3>, 8> corners {{
        {{ cx - half, cy - half, cz - half }},
        {{ cx + half, cy - half, cz - half }},
        {{ cx + half, cy + half, cz - half }},
        {{ cx - half, cy + half, cz - half }},
        {{ cx - half, cy - half, cz + half }},
        {{ cx + half, cy - half, cz + half }},
        {{ cx + half, cy + half, cz + half }},
        {{ cx - half, cy + half, cz + half }},
    }};

    auto appendFace = [&](std::array<uint32_t, 4> face, std::array<float, 3> normal) {
        const uint32_t faceBase = static_cast<uint32_t>(vertices_.size());
        const std::array<std::array<float, 2>, 4> uvs {{
            {{ 0.0f, 0.0f }},
            {{ 1.0f, 0.0f }},
            {{ 1.0f, 1.0f }},
            {{ 0.0f, 1.0f }},
        }};
        size_t uvIndex = 0;
        for (uint32_t cornerIndex : face) {
            const auto& position = corners[cornerIndex];
            const auto& uv = uvs[uvIndex++];
            vertices_.push_back({
                { position[0], position[1], position[2] },
                { normal[0], normal[1], normal[2] },
                { r, g, b },
                { uv[0], uv[1] }
            });
        }
        indices_.insert(indices_.end(), { faceBase, faceBase + 2, faceBase + 1, faceBase, faceBase + 3, faceBase + 2 });
    };

    appendFace({ 0, 1, 2, 3 }, { 0.0f, 0.0f, -1.0f });
    appendFace({ 4, 7, 6, 5 }, { 0.0f, 0.0f, 1.0f });
    appendFace({ 0, 4, 5, 1 }, { 0.0f, -1.0f, 0.0f });
    appendFace({ 3, 2, 6, 7 }, { 0.0f, 1.0f, 0.0f });
    appendFace({ 1, 5, 6, 2 }, { 1.0f, 0.0f, 0.0f });
    appendFace({ 0, 3, 7, 4 }, { -1.0f, 0.0f, 0.0f });
}

void VulkanRenderer::appendSphere(const PrimitiveInstance& primitive)
{
    constexpr uint32_t rings = 16;
    constexpr uint32_t segments = 32;
    const uint32_t base = static_cast<uint32_t>(vertices_.size());
    const float radius = primitive.size * 0.5f;
    const float cx = primitive.position[0];
    const float cy = primitive.position[1];
    const float cz = primitive.position[2];
    const float r = primitive.color[0];
    const float g = primitive.color[1];
    const float b = primitive.color[2];

    for (uint32_t ring = 0; ring <= rings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float theta = v * glm::pi<float>();
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (uint32_t segment = 0; segment <= segments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float phi = u * glm::two_pi<float>();
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);
            const float nx = sinTheta * cosPhi;
            const float ny = cosTheta;
            const float nz = sinTheta * sinPhi;

            vertices_.push_back({
                { cx + nx * radius, cy + ny * radius, cz + nz * radius },
                { nx, ny, nz },
                { r, g, b },
                { u, v }
            });
        }
    }

    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t segment = 0; segment < segments; ++segment) {
            const uint32_t a = base + ring * (segments + 1) + segment;
            const uint32_t b0 = a + segments + 1;
            indices_.insert(indices_.end(), {
                a, b0, a + 1,
                a + 1, b0, b0 + 1,
            });
        }
    }
}

} // namespace MEngine::RenderBackend::Vulkan
