#include "MEngine/RenderBackend/Vulkan/VulkanBuffer.hpp"

#include "MEngine/RenderBackend/Vulkan/VulkanDevice.hpp"

namespace MEngine::RenderBackend::Vulkan {

void VulkanBuffer::create(VulkanDevice& device, const nvrhi::BufferDesc& desc)
{
    handle_ = device.nvrhiDevice()->createBuffer(desc);
}

void VulkanBuffer::reset()
{
    handle_ = nullptr;
}

nvrhi::BufferHandle VulkanBuffer::handle() const
{
    return handle_;
}

} // namespace MEngine::RenderBackend::Vulkan
