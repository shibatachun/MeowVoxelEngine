#pragma once

#include <nvrhi/nvrhi.h>

namespace MEngine::RenderBackend::Vulkan {

class VulkanDevice;

class VulkanBuffer {
public:
    void create(VulkanDevice& device, const nvrhi::BufferDesc& desc);
    void reset();

    [[nodiscard]] nvrhi::BufferHandle handle() const;

private:
    nvrhi::BufferHandle handle_;
};

} // namespace MEngine::RenderBackend::Vulkan
