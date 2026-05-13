#include "MEngine/RenderBackend/Vulkan/VulkanImage.hpp"

#include "MEngine/RenderBackend/Vulkan/VulkanDevice.hpp"

namespace MEngine::RenderBackend::Vulkan {

void VulkanImage::create(VulkanDevice& device, const nvrhi::TextureDesc& desc)
{
    handle_ = device.nvrhiDevice()->createTexture(desc);
}

void VulkanImage::wrapSwapchainImage(nvrhi::TextureHandle image)
{
    handle_ = image;
}

void VulkanImage::reset()
{
    handle_ = nullptr;
}

nvrhi::TextureHandle VulkanImage::handle() const
{
    return handle_;
}

} // namespace MEngine::RenderBackend::Vulkan
