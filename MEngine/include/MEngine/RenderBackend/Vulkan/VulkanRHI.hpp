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

    void initialize(void* nativeWindowHandle, const char* applicationName) override;
    void beginFrame() override;
    void endFrame(const Camera::CameraState* camera = nullptr) override;
    void setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives) override;
    void shutdown() override;

    [[nodiscard]] nvrhi::DeviceHandle device() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MEngine::RenderBackend::Vulkan
