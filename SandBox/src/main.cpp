#include "MEngine/MEngine.hpp"
#include "MEngine/Core/Log.hpp"
#include "MEngine/Windows/Window.hpp"
#include "PrimitiveWorld.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

MEngine::GraphicsApi parseGraphicsApi(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--api") == 0 && i + 1 < argc) {
            const char* apiName = argv[++i];
            if (std::strcmp(apiName, "vulkan") == 0 || std::strcmp(apiName, "vk") == 0) {
                return MEngine::GraphicsApi::Vulkan;
            }

            if (std::strcmp(apiName, "d3d12") == 0 || std::strcmp(apiName, "dx12") == 0) {
                return MEngine::GraphicsApi::D3D12;
            }
        }
    }

    return MEngine::GraphicsApi::D3D12;
}

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

} // namespace

int main(int argc, char** argv)
{
    try {
        const MEngine::GraphicsApi graphicsApi = parseGraphicsApi(argc, argv);
        MEngine::Core::initializeLogging();
        MENGINE_INFO("[SandBox] Requested graphics API: {}", graphicsApiName(graphicsApi));

        MEngine::Windows::Window window({
            "MeowEngine SandBox",
            1280,
            720,
            true,
            graphicsApi == MEngine::GraphicsApi::Vulkan,
        });
        window.create();

        MEngine::Engine engine({
            "SandBox",
            window.width(),
            window.height(),
            graphicsApi,
            window.nativeHandle(),
        });

        engine.initialize();

        SandBox::PrimitiveWorldConfig worldConfig {};
        worldConfig.seed = parseSeed(argc, argv).value_or(createRandomSeed());
        worldConfig.viewDistanceChunks = std::clamp(parseIntOption(argc, argv, "--view-distance").value_or(3), 1, 4);
        worldConfig.worldSizeChunks = std::max(parseIntOption(argc, argv, "--world-chunks").value_or(4096), 16);

        SandBox::PrimitiveWorldStreamer worldStreamer(worldConfig);
        const MEngine::Camera::CameraState& initialCamera = engine.cameraState();
        const bool initialWorldLoaded = worldStreamer.update(initialCamera.position[0], initialCamera.position[2]);
        (void)initialWorldLoaded;
        engine.setPrimitiveWorld(worldStreamer.visiblePrimitives());
        MENGINE_INFO(
            "[SandBox] Created streaming primitive world seed={} worldChunks={}x{} viewDistance={} loadedChunks={} primitives={}",
            worldConfig.seed,
            worldConfig.worldSizeChunks,
            worldConfig.worldSizeChunks,
            worldConfig.viewDistanceChunks,
            worldStreamer.loadedChunkCount(),
            worldStreamer.visiblePrimitives().size());

        constexpr auto frameSleep = std::chrono::milliseconds(16);
        auto lastFrameTime = std::chrono::steady_clock::now();
        while (window.isOpen() && engine.isRunning()) {
            const auto now = std::chrono::steady_clock::now();
            const std::chrono::duration<float> delta = now - lastFrameTime;
            lastFrameTime = now;

            window.pollEvents();
            const MEngine::Camera::CameraState& camera = engine.cameraState();
            if (worldStreamer.update(camera.position[0], camera.position[2])) {
                engine.setPrimitiveWorld(worldStreamer.visiblePrimitives());
                const SandBox::ChunkCoord center = worldStreamer.centerChunk();
                MENGINE_INFO(
                    "[SandBox] Streamed primitive chunks center=({}, {}) loadedChunks={} primitives={}",
                    center.x,
                    center.z,
                    worldStreamer.loadedChunkCount(),
                    worldStreamer.visiblePrimitives().size());
            }
            engine.tick(delta.count());
        }

        engine.shutdown();
        window.destroy();
    } catch (const std::exception& exception) {
        MENGINE_ERROR("[SandBox] {}", exception.what());
        return 1;
    }

    return 0;
}
