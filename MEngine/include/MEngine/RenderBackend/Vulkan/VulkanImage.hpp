#pragma once

#include <nvrhi/nvrhi.h>

namespace MEngine::RenderBackend::Vulkan {

class VulkanDevice;

class VulkanImage {
public:
    void create(VulkanDevice& device, const nvrhi::TextureDesc& desc);
    void wrapSwapchainImage(nvrhi::TextureHandle image);
    void reset();

    [[nodiscard]] nvrhi::TextureHandle handle() const;

private:
    nvrhi::TextureHandle handle_;
};

} // namespace MEngine::RenderBackend::Vulkan
