#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

struct SDL_Window;

namespace MEngine::RenderBackend::Vulkan {

class VulkanDevice;

class VulkanSwapchain {
public:
    VulkanSwapchain() = default;
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    void initialize(VulkanDevice& device, SDL_Window* window);
    void shutdown();

    [[nodiscard]] VkSwapchainKHR swapchain() const;
    [[nodiscard]] VkFormat imageFormat() const;
    [[nodiscard]] VkExtent2D extent() const;
    [[nodiscard]] const std::vector<nvrhi::TextureHandle>& nvrhiImages() const;

private:
    void createSwapchain(VulkanDevice& device, SDL_Window* window);
    void createImageViews(VulkanDevice& device);
    void createNvrhiTextures(VulkanDevice& device);

    VulkanDevice* device_ = nullptr;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat imageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_ {};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    std::vector<nvrhi::TextureHandle> nvrhiImages_;
};

} // namespace MEngine::RenderBackend::Vulkan
