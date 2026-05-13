#include "MEngine/RenderBackend/Vulkan/VulkanDevice.hpp"

#include "MEngine/Core/Log.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanUtils.hpp"

#include <nvrhi/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <stdexcept>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace MEngine::RenderBackend::Vulkan {

class NvrhiMessageCallback final : public nvrhi::IMessageCallback {
public:
    void message(nvrhi::MessageSeverity severity, const char* messageText) override
    {
        switch (severity) {
        case nvrhi::MessageSeverity::Info:
            MENGINE_INFO("[NVRHI] {}", messageText);
            break;
        case nvrhi::MessageSeverity::Warning:
            MENGINE_WARN("[NVRHI] {}", messageText);
            break;
        case nvrhi::MessageSeverity::Error:
            MENGINE_ERROR("[NVRHI] {}", messageText);
            break;
        case nvrhi::MessageSeverity::Fatal:
            MENGINE_ERROR("[NVRHI Fatal] {}", messageText);
            break;
        }
    }
};

NvrhiMessageCallback g_NvrhiMessageCallback;

VulkanDevice::~VulkanDevice()
{
    shutdown();
}

void VulkanDevice::initialize(SDL_Window* window, const char* applicationName)
{
    if (!window) {
        throw std::runtime_error("Vulkan requires a valid SDL window handle");
    }

    createInstance(applicationName);
    createSurface(window);
    selectPhysicalDevice();
    createLogicalDevice();
    createNvrhiDevice();
}

void VulkanDevice::shutdown()
{
    nvrhiDevice_ = nullptr;

    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_ != VK_NULL_HANDLE) {
        SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

VkInstance VulkanDevice::instance() const { return instance_; }
VkSurfaceKHR VulkanDevice::surface() const { return surface_; }
VkPhysicalDevice VulkanDevice::physicalDevice() const { return physicalDevice_; }
VkDevice VulkanDevice::device() const { return device_; }
VkQueue VulkanDevice::graphicsQueue() const { return graphicsQueue_; }
uint32_t VulkanDevice::graphicsQueueFamily() const { return graphicsQueueFamily_; }
const std::string& VulkanDevice::physicalDeviceName() const { return physicalDeviceName_; }
nvrhi::DeviceHandle VulkanDevice::nvrhiDevice() const { return nvrhiDevice_; }

void VulkanDevice::createInstance(const char* applicationName)
{
    uint32_t extensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!sdlExtensions || extensionCount == 0) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }

    instanceExtensions_.assign(sdlExtensions, sdlExtensions + extensionCount);

    VkApplicationInfo appInfo {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = applicationName;
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "MeowEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions_.size());
    createInfo.ppEnabledExtensionNames = instanceExtensions_.data();

    checkVulkan(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
}

void VulkanDevice::createSurface(SDL_Window* window)
{
    if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
}

bool VulkanDevice::supportsDeviceExtension(VkPhysicalDevice device, const char* extensionName) const
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

    return std::any_of(extensions.begin(), extensions.end(), [extensionName](const VkExtensionProperties& extension) {
        return std::string(extension.extensionName) == extensionName;
    });
}

bool VulkanDevice::findGraphicsQueue(VkPhysicalDevice device, uint32_t& queueFamilyIndex) const
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupported);

        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupported) {
            queueFamilyIndex = i;
            return true;
        }
    }

    return false;
}

void VulkanDevice::selectPhysicalDevice()
{
    uint32_t deviceCount = 0;
    checkVulkan(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    checkVulkan(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices");

    for (VkPhysicalDevice candidate : devices) {
        uint32_t candidateQueueFamily = 0;
        if (!supportsDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }
        if (!findGraphicsQueue(candidate, candidateQueueFamily)) {
            continue;
        }

        physicalDevice_ = candidate;
        graphicsQueueFamily_ = candidateQueueFamily;

        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        physicalDeviceName_ = properties.deviceName;
        return;
    }

    throw std::runtime_error("No Vulkan physical device supports graphics + present + swapchain");
}

void VulkanDevice::createLogicalDevice()
{
    deviceExtensions_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamily_;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures features {};

    VkDeviceCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &features;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions_.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions_.data();

    checkVulkan(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice");

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance_, vkGetInstanceProcAddr, device_);
}

void VulkanDevice::createNvrhiDevice()
{
    nvrhi::vulkan::DeviceDesc desc {};
    desc.instance = instance_;
    desc.physicalDevice = physicalDevice_;
    desc.device = device_;
    desc.graphicsQueue = graphicsQueue_;
    desc.graphicsQueueIndex = static_cast<int>(graphicsQueueFamily_);
    desc.instanceExtensions = instanceExtensions_.data();
    desc.numInstanceExtensions = instanceExtensions_.size();
    desc.deviceExtensions = deviceExtensions_.data();
    desc.numDeviceExtensions = deviceExtensions_.size();
    nvrhiMessageCallback_ = &g_NvrhiMessageCallback;
    desc.errorCB = nvrhiMessageCallback_;

    nvrhiDevice_ = nvrhi::vulkan::createDevice(desc);
    if (!nvrhiDevice_) {
        throw std::runtime_error("nvrhi::vulkan::createDevice returned null");
    }
}

} // namespace MEngine::RenderBackend::Vulkan
