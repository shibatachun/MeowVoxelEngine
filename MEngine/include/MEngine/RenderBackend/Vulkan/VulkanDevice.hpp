#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

struct SDL_Window;

namespace MEngine::RenderBackend::Vulkan {

class VulkanDevice {
public:
    VulkanDevice() = default;
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    void initialize(SDL_Window* window, const char* applicationName);
    void shutdown();

    [[nodiscard]] VkInstance instance() const;
    [[nodiscard]] VkSurfaceKHR surface() const;
    [[nodiscard]] VkPhysicalDevice physicalDevice() const;
    [[nodiscard]] VkDevice device() const;
    [[nodiscard]] VkQueue graphicsQueue() const;
    [[nodiscard]] uint32_t graphicsQueueFamily() const;
    [[nodiscard]] const std::string& physicalDeviceName() const;
    [[nodiscard]] nvrhi::DeviceHandle nvrhiDevice() const;

private:
    void createInstance(const char* applicationName);
    void createSurface(SDL_Window* window);
    void selectPhysicalDevice();
    void createLogicalDevice();
    void createNvrhiDevice();

    [[nodiscard]] bool supportsDeviceExtension(VkPhysicalDevice device, const char* extensionName) const;
    [[nodiscard]] bool findGraphicsQueue(VkPhysicalDevice device, uint32_t& queueFamilyIndex) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    std::string physicalDeviceName_;
    std::vector<const char*> instanceExtensions_;
    std::vector<const char*> deviceExtensions_;
    nvrhi::IMessageCallback* nvrhiMessageCallback_ = nullptr;
    nvrhi::DeviceHandle nvrhiDevice_;
};

} // namespace MEngine::RenderBackend::Vulkan
