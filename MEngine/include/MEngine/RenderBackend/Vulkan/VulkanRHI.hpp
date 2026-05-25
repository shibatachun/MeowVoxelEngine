#pragma once

#include "MEngine/RenderBackend/RHIContext.hpp"

#include <memory>

namespace MEngine::RenderBackend::Vulkan {

class VulkanRHI final : public RHIContext {
public:
    VulkanRHI();
    ~VulkanRHI() override;

    VulkanRHI(const VulkanRHI&) = delete;
    VulkanRHI& operator=(const VulkanRHI&) = delete;

    void initialize(void* nativeWindowHandle, const char* applicationName, bool enableRayTracing) override;
    void beginFrame() override;
    void endFrame(const Camera::CameraState* camera = nullptr) override;
    void setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives) override;
    void setDynamicPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives) override;
    void setMeshAsset(const Resources::MeshAsset& asset) override;
    void setMeshWorldTransform(const glm::mat4& transform) override;
    void setMeshSkinningMatrices(const std::vector<glm::mat4>& matrices) override;
    [[nodiscard]] bool playerControlModeEnabled() const override;
    [[nodiscard]] bool shootingModeEnabled() const override;
    [[nodiscard]] AnimationSystem::AnimationTuning animationTuning() const override;
    bool consumePlayerResetRequested() override;
    bool consumePlayRequested() override;
    bool consumeModelLoadRequested(std::string& outPath) override;
    void setEditorPlayMode(bool enabled) override;
    void shutdown() override;

    [[nodiscard]] nvrhi::DeviceHandle device() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MEngine::RenderBackend::Vulkan
