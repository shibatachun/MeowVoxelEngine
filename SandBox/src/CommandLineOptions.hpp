#pragma once

#include "MEngine/MEngine.hpp"

#include <cstdint>
#include <optional>

namespace SandBox {

struct CommandLineOptions {
    MEngine::GraphicsApi graphicsApi = MEngine::GraphicsApi::Vulkan;
    bool enableRayTracing = false;
    std::optional<uint32_t> seed;
    std::optional<int> viewDistanceChunks;
    std::optional<int> worldSizeChunks;
    std::optional<int> chunkCacheCapacity;
};

[[nodiscard]] CommandLineOptions parseCommandLineOptions(int argc, char** argv);
[[nodiscard]] uint32_t createRandomSeed();
[[nodiscard]] const char* graphicsApiName(MEngine::GraphicsApi api);

} // namespace SandBox
