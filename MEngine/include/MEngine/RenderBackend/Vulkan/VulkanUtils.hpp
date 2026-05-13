#pragma once

#include <string>
#include <vulkan/vulkan.h>

namespace MEngine::RenderBackend::Vulkan {

std::string vulkanResultMessage(VkResult result, const char* operation);
void checkVulkan(VkResult result, const char* operation);

} // namespace MEngine::RenderBackend::Vulkan
