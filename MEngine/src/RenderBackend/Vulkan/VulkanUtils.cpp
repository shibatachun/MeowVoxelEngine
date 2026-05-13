#include "MEngine/RenderBackend/Vulkan/VulkanUtils.hpp"

#include <nvrhi/vulkan.h>

#include <stdexcept>

namespace MEngine::RenderBackend::Vulkan {

std::string vulkanResultMessage(VkResult result, const char* operation)
{
    return std::string(operation) + " failed: " + nvrhi::vulkan::resultToString(result);
}

void checkVulkan(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(vulkanResultMessage(result, operation));
    }
}

} // namespace MEngine::RenderBackend::Vulkan
