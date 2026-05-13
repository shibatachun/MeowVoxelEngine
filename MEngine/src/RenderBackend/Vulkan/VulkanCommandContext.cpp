#include "MEngine/RenderBackend/Vulkan/VulkanCommandContext.hpp"

#include "MEngine/Core/Log.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanDevice.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanUtils.hpp"

namespace MEngine::RenderBackend::Vulkan {

VulkanCommandContext::~VulkanCommandContext()
{
    shutdown();
}

void VulkanCommandContext::initialize(VulkanDevice& device)
{
    device_ = &device;

    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = device.graphicsQueueFamily();
    checkVulkan(vkCreateCommandPool(device.device(), &poolInfo, nullptr, &commandPool_), "vkCreateCommandPool");

    commandBuffers_.resize(FramesInFlight);
    VkCommandBufferAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    checkVulkan(vkAllocateCommandBuffers(device.device(), &allocInfo, commandBuffers_.data()), "vkAllocateCommandBuffers");

    imageAvailableSemaphores_.resize(FramesInFlight);
    renderFinishedSemaphores_.resize(FramesInFlight);
    inFlightFences_.resize(FramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < FramesInFlight; ++i) {
        checkVulkan(vkCreateSemaphore(device.device(), &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]), "vkCreateSemaphore");
        checkVulkan(vkCreateSemaphore(device.device(), &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]), "vkCreateSemaphore");
        checkVulkan(vkCreateFence(device.device(), &fenceInfo, nullptr, &inFlightFences_[i]), "vkCreateFence");
    }

    MENGINE_INFO("[RenderBackend] Vulkan command context initialized: framesInFlight={}", FramesInFlight);
}

void VulkanCommandContext::shutdown()
{
    if (!device_) {
        return;
    }

    for (VkFence fence : inFlightFences_) {
        vkDestroyFence(device_->device(), fence, nullptr);
    }
    for (VkSemaphore semaphore : renderFinishedSemaphores_) {
        vkDestroySemaphore(device_->device(), semaphore, nullptr);
    }
    for (VkSemaphore semaphore : imageAvailableSemaphores_) {
        vkDestroySemaphore(device_->device(), semaphore, nullptr);
    }

    inFlightFences_.clear();
    renderFinishedSemaphores_.clear();
    imageAvailableSemaphores_.clear();

    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_->device(), commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }

    commandBuffers_.clear();
    device_ = nullptr;
}

VkCommandBuffer VulkanCommandContext::commandBuffer(uint32_t frameIndex) const { return commandBuffers_[frameIndex]; }
VkSemaphore VulkanCommandContext::imageAvailableSemaphore(uint32_t frameIndex) const { return imageAvailableSemaphores_[frameIndex]; }
VkSemaphore VulkanCommandContext::renderFinishedSemaphore(uint32_t frameIndex) const { return renderFinishedSemaphores_[frameIndex]; }
VkFence VulkanCommandContext::inFlightFence(uint32_t frameIndex) const { return inFlightFences_[frameIndex]; }
uint32_t VulkanCommandContext::currentFrameIndex() const { return currentFrameIndex_; }

void VulkanCommandContext::advanceFrame()
{
    currentFrameIndex_ = (currentFrameIndex_ + 1) % FramesInFlight;
}

} // namespace MEngine::RenderBackend::Vulkan
