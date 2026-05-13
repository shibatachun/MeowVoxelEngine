#include "MEngine/RenderBackend/Vulkan/VulkanSwapchain.hpp"

#include "MEngine/Core/Log.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanDevice.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanUtils.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

namespace MEngine::RenderBackend::Vulkan {

namespace {

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes)
{
    for (VkPresentModeKHR mode : presentModes) {
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            return mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, SDL_Window* window)
{
    if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)()) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);

    VkExtent2D extent {
        static_cast<uint32_t>((std::max)(width, 1)),
        static_cast<uint32_t>((std::max)(height, 1)),
    };

    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

nvrhi::Format toNvrhiFormat(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
        return nvrhi::Format::BGRA8_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return nvrhi::Format::RGBA8_UNORM;
    default:
        return nvrhi::Format::UNKNOWN;
    }
}

} // namespace

VulkanSwapchain::~VulkanSwapchain()
{
    shutdown();
}

void VulkanSwapchain::initialize(VulkanDevice& device, SDL_Window* window)
{
    device_ = &device;
    createSwapchain(device, window);
    createImageViews(device);
    createNvrhiTextures(device);
    MENGINE_INFO("[RenderBackend] Vulkan swapchain initialized: {}x{} images={}", extent_.width, extent_.height, images_.size());
}

void VulkanSwapchain::shutdown()
{
    nvrhiImages_.clear();

    if (device_) {
        for (VkImageView imageView : imageViews_) {
            vkDestroyImageView(device_->device(), imageView, nullptr);
        }
        imageViews_.clear();

        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_->device(), swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    images_.clear();
    device_ = nullptr;
}

VkSwapchainKHR VulkanSwapchain::swapchain() const { return swapchain_; }
VkFormat VulkanSwapchain::imageFormat() const { return imageFormat_; }
VkExtent2D VulkanSwapchain::extent() const { return extent_; }
const std::vector<nvrhi::TextureHandle>& VulkanSwapchain::nvrhiImages() const { return nvrhiImages_; }

void VulkanSwapchain::createSwapchain(VulkanDevice& device, SDL_Window* window)
{
    VkSurfaceCapabilitiesKHR capabilities {};
    checkVulkan(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physicalDevice(), device.surface(), &capabilities),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    uint32_t formatCount = 0;
    checkVulkan(vkGetPhysicalDeviceSurfaceFormatsKHR(device.physicalDevice(), device.surface(), &formatCount, nullptr),
        "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formatCount == 0) {
        throw std::runtime_error("Vulkan surface exposes no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    checkVulkan(vkGetPhysicalDeviceSurfaceFormatsKHR(device.physicalDevice(), device.surface(), &formatCount, formats.data()),
        "vkGetPhysicalDeviceSurfaceFormatsKHR");

    uint32_t presentModeCount = 0;
    checkVulkan(vkGetPhysicalDeviceSurfacePresentModesKHR(device.physicalDevice(), device.surface(), &presentModeCount, nullptr),
        "vkGetPhysicalDeviceSurfacePresentModesKHR");
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    checkVulkan(vkGetPhysicalDeviceSurfacePresentModesKHR(device.physicalDevice(), device.surface(), &presentModeCount, presentModes.data()),
        "vkGetPhysicalDeviceSurfacePresentModesKHR");

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    const VkPresentModeKHR presentMode = choosePresentMode(presentModes);
    extent_ = chooseExtent(capabilities, window);
    imageFormat_ = surfaceFormat.format;

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = device.surface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    checkVulkan(vkCreateSwapchainKHR(device.device(), &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR");

    uint32_t actualImageCount = 0;
    checkVulkan(vkGetSwapchainImagesKHR(device.device(), swapchain_, &actualImageCount, nullptr), "vkGetSwapchainImagesKHR");
    images_.resize(actualImageCount);
    checkVulkan(vkGetSwapchainImagesKHR(device.device(), swapchain_, &actualImageCount, images_.data()), "vkGetSwapchainImagesKHR");
}

void VulkanSwapchain::createImageViews(VulkanDevice& device)
{
    imageViews_.reserve(images_.size());

    for (VkImage image : images_) {
        VkImageViewCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = image;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = imageFormat_;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        checkVulkan(vkCreateImageView(device.device(), &createInfo, nullptr, &imageView), "vkCreateImageView");
        imageViews_.push_back(imageView);
    }
}

void VulkanSwapchain::createNvrhiTextures(VulkanDevice& device)
{
    const nvrhi::Format nvrhiFormat = toNvrhiFormat(imageFormat_);
    if (nvrhiFormat == nvrhi::Format::UNKNOWN) {
        throw std::runtime_error("Unsupported swapchain format for NVRHI texture wrapping");
    }

    nvrhiImages_.reserve(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        auto desc = nvrhi::TextureDesc()
            .setDimension(nvrhi::TextureDimension::Texture2D)
            .setFormat(nvrhiFormat)
            .setWidth(extent_.width)
            .setHeight(extent_.height)
            .setIsRenderTarget(true)
            .setInitialState(nvrhi::ResourceStates::Present)
            .setKeepInitialState(true)
            .setDebugName("SwapchainImage" + std::to_string(i));

        nvrhiImages_.push_back(device.nvrhiDevice()->createHandleForNativeTexture(
            nvrhi::ObjectTypes::VK_Image,
            nvrhi::Object(reinterpret_cast<uint64_t>(images_[i])),
            desc));
    }
}

} // namespace MEngine::RenderBackend::Vulkan
