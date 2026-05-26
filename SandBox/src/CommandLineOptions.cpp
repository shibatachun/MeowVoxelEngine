#include "CommandLineOptions.hpp"

#include <chrono>
#include <cstring>
#include <random>
#include <string>
#include <string_view>

namespace SandBox {

namespace {

std::optional<uint32_t> parseSeed(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            return static_cast<uint32_t>(std::stoul(argv[++i]));
        }
    }

    return std::nullopt;
}

std::optional<int> parseIntOption(int argc, char** argv, const char* optionName)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], optionName) == 0 && i + 1 < argc) {
            return std::stoi(argv[++i]);
        }
    }

    return std::nullopt;
}

bool hasFlag(int argc, char** argv, const char* flagName)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flagName) == 0) {
            return true;
        }
    }

    return false;
}

std::string_view normalizeApiName(const char* apiName)
{
    std::string_view value { apiName };
    while (value.rfind("-", 0) == 0) {
        value.remove_prefix(1);
    }
    return value;
}

std::optional<MEngine::GraphicsApi> graphicsApiFromName(const char* apiName)
{
    const std::string_view value = normalizeApiName(apiName);
    if (value == "vulkan" || value == "vk") {
        return MEngine::GraphicsApi::Vulkan;
    }

    if (value == "d3d12" || value == "dx12") {
        return MEngine::GraphicsApi::D3D12;
    }

    return std::nullopt;
}

MEngine::GraphicsApi parseGraphicsApi(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--d3d12") == 0 || std::strcmp(argv[i], "--dx12") == 0) {
            return MEngine::GraphicsApi::D3D12;
        }

        if (std::strcmp(argv[i], "--vulkan") == 0 || std::strcmp(argv[i], "--vk") == 0) {
            return MEngine::GraphicsApi::Vulkan;
        }

        if (std::strcmp(argv[i], "--api") == 0 && i + 1 < argc) {
            if (const std::optional<MEngine::GraphicsApi> api = graphicsApiFromName(argv[++i])) {
                return *api;
            }
        }

        constexpr std::string_view ApiAssignmentPrefix = "--api=";
        const std::string_view argument { argv[i] };
        if (argument.rfind(ApiAssignmentPrefix, 0) == 0) {
            const std::string value { argument.substr(ApiAssignmentPrefix.size()) };
            if (const std::optional<MEngine::GraphicsApi> api = graphicsApiFromName(value.c_str())) {
                return *api;
            }
        }
    }

    return MEngine::GraphicsApi::Vulkan;
}

} // namespace

CommandLineOptions parseCommandLineOptions(int argc, char** argv)
{
    CommandLineOptions options {};
    options.graphicsApi = parseGraphicsApi(argc, argv);
    options.enableRayTracing = hasFlag(argc, argv, "--rt");
    if (options.enableRayTracing && options.graphicsApi != MEngine::GraphicsApi::Vulkan) {
        options.graphicsApi = MEngine::GraphicsApi::Vulkan;
    }

    options.seed = parseSeed(argc, argv);
    options.viewDistanceChunks = parseIntOption(argc, argv, "--view-distance");
    options.worldSizeChunks = parseIntOption(argc, argv, "--world-chunks");
    options.chunkCacheCapacity = parseIntOption(argc, argv, "--chunk-cache");
    return options;
}

uint32_t createRandomSeed()
{
    std::random_device randomDevice;
    const uint32_t entropy = randomDevice();
    const uint32_t timeBits = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return entropy ^ (timeBits * 1664525u + 1013904223u);
}

const char* graphicsApiName(MEngine::GraphicsApi api)
{
    switch (api) {
    case MEngine::GraphicsApi::D3D12:
        return "D3D12";
    case MEngine::GraphicsApi::Vulkan:
        return "Vulkan";
    }

    return "Unknown";
}

} // namespace SandBox
