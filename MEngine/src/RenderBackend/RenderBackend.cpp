#include "MEngine/RenderBackend/RenderBackend.hpp"

#include "MEngine/Camera/Camera.hpp"
#include "MEngine/Core/Log.hpp"
#include "MEngine/RenderBackend/D3D12/D3D12RHI.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanRHI.hpp"

#include <nvrhi/nvrhi.h>

#include <stdexcept>

namespace MEngine::RenderBackend {

namespace {

nvrhi::GraphicsAPI toNvrhiGraphicsApi(GraphicsApi api)
{
    switch (api) {
    case GraphicsApi::D3D12:
        return nvrhi::GraphicsAPI::D3D12;
    case GraphicsApi::Vulkan:
        return nvrhi::GraphicsAPI::VULKAN;
    }

    return nvrhi::GraphicsAPI::D3D12;
}

const char* graphicsApiName(nvrhi::GraphicsAPI api)
{
    switch (api) {
    case nvrhi::GraphicsAPI::D3D11:
        return "D3D11";
    case nvrhi::GraphicsAPI::D3D12:
        return "D3D12";
    case nvrhi::GraphicsAPI::VULKAN:
        return "Vulkan";
    }

    return "Unknown";
}

std::unique_ptr<RHIContext> createRHIContext(GraphicsApi graphicsApi)
{
    switch (graphicsApi) {
    case GraphicsApi::Vulkan:
        return std::make_unique<Vulkan::VulkanRHI>();
    case GraphicsApi::D3D12:
        return std::make_unique<D3D12::D3D12RHI>();
    }

    return nullptr;
}

} // namespace

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    shutdown();
}

void Renderer::initialize(const std::string& applicationName, int width, int height, GraphicsApi graphicsApi, void* nativeWindowHandle, bool enableRayTracing)
{
    const nvrhi::GraphicsAPI nvrhiGraphicsApi = toNvrhiGraphicsApi(graphicsApi);
    backendName_ = graphicsApiName(nvrhiGraphicsApi);

    MENGINE_INFO("[RenderBackend] {} viewport {}x{}", applicationName, width, height);
    MENGINE_INFO("[RenderBackend] NVRHI header v{} verified={} backend={}",
        nvrhi::c_HeaderVersion,
        nvrhi::verifyHeaderVersion(),
        backendName_);
    if (enableRayTracing) {
        MENGINE_INFO("[RenderBackend] Ray tracing pipeline requested");
    }

    rhiContext_ = createRHIContext(graphicsApi);
    if (rhiContext_) {
        rhiContext_->initialize(nativeWindowHandle, applicationName.c_str(), enableRayTracing);
    } else {
        MENGINE_WARN("[RenderBackend] {} device creation is not implemented yet; using placeholder backend state", backendName_);
    }

    initialized_ = true;
}

void Renderer::beginFrame()
{
    if (initialized_) {
        MENGINE_DEBUG("[RenderBackend] Begin frame via NVRHI {}", backendName_);
        if (rhiContext_) {
            rhiContext_->beginFrame();
        }
    }
}

void Renderer::endFrame(const MEngine::Camera::CameraState& camera)
{
    if (initialized_) {
        if (rhiContext_) {
            rhiContext_->endFrame(&camera);
        }
        MENGINE_DEBUG("[RenderBackend] End frame");
    }
}

void Renderer::setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
{
    if (initialized_ && rhiContext_) {
        rhiContext_->setPrimitiveInstances(primitives);
    }
}

void Renderer::setDynamicPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
{
    if (initialized_ && rhiContext_) {
        rhiContext_->setDynamicPrimitiveInstances(primitives);
    }
}

void Renderer::setMeshAsset(const Resources::MeshAsset& asset)
{
    if (initialized_ && rhiContext_) {
        rhiContext_->setMeshAsset(asset);
    }
}

void Renderer::setMeshWorldTransform(const glm::mat4& transform)
{
    if (initialized_ && rhiContext_) {
        rhiContext_->setMeshWorldTransform(transform);
    }
}

void Renderer::setMeshSkinningMatrices(const std::vector<glm::mat4>& matrices)
{
    if (initialized_ && rhiContext_) {
        rhiContext_->setMeshSkinningMatrices(matrices);
    }
}

bool Renderer::playerControlModeEnabled() const
{
    return initialized_ && rhiContext_ && rhiContext_->playerControlModeEnabled();
}

bool Renderer::shootingModeEnabled() const
{
    return initialized_ && rhiContext_ && rhiContext_->shootingModeEnabled();
}

AnimationSystem::AnimationTuning Renderer::animationTuning() const
{
    return initialized_ && rhiContext_ ? rhiContext_->animationTuning() : AnimationSystem::AnimationTuning {};
}

bool Renderer::consumePlayerResetRequested()
{
    return initialized_ && rhiContext_ && rhiContext_->consumePlayerResetRequested();
}

bool Renderer::consumePlayRequested()
{
    return initialized_ && rhiContext_ && rhiContext_->consumePlayRequested();
}

bool Renderer::consumeModelLoadRequested(std::string& outPath)
{
    return initialized_ && rhiContext_ && rhiContext_->consumeModelLoadRequested(outPath);
}

void Renderer::setEditorPlayMode(bool enabled)
{
    if (initialized_ && rhiContext_) {
        rhiContext_->setEditorPlayMode(enabled);
    }
}

void Renderer::shutdown()
{
    if (initialized_) {
        if (rhiContext_) {
            rhiContext_->shutdown();
            rhiContext_.reset();
        }

        MENGINE_INFO("[RenderBackend] Shutdown");
        initialized_ = false;
    }
}

} // namespace MEngine::RenderBackend
