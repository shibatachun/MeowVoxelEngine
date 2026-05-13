#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace MEngine::RenderBackend::Vulkan {

class VulkanDevice;

class VulkanCommandContext {
public:
    static constexpr uint32_t FramesInFlight = 2;

    VulkanCommandContext() = default;
    ~VulkanCommandContext();

    VulkanCommandContext(const VulkanCommandContext&) = delete;
    VulkanCommandContext& operator=(const VulkanCommandContext&) = delete;

    void initialize(VulkanDevice& device);
    void shutdown();

    [[nodiscard]] VkCommandBuffer commandBuffer(uint32_t frameIndex) const;
    [[nodiscard]] VkSemaphore imageAvailableSemaphore(uint32_t frameIndex) const;
    [[nodiscard]] VkSemaphore renderFinishedSemaphore(uint32_t frameIndex) const;
    [[nodiscard]] VkFence inFlightFence(uint32_t frameIndex) const;
    [[nodiscard]] uint32_t currentFrameIndex() const;
    void advanceFrame();

private:
    VulkanDevice* device_ = nullptr;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrameIndex_ = 0;
};

} // namespace MEngine::RenderBackend::Vulkan
