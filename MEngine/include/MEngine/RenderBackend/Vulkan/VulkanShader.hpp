#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MEngine::RenderBackend::Vulkan {

class VulkanShader {
public:
    [[nodiscard]] static std::vector<uint8_t> readSpirV(const std::string& path);
};

} // namespace MEngine::RenderBackend::Vulkan
