#include "MEngine/RenderBackend/Vulkan/VulkanShader.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace MEngine::RenderBackend::Vulkan {

std::vector<uint8_t> VulkanShader::readSpirV(const std::string& path)
{
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open shader: " + path);
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("Shader is empty: " + path);
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("Failed to read shader: " + path);
    }

    return bytes;
}

} // namespace MEngine::RenderBackend::Vulkan
