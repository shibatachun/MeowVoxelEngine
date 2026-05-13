#include "MEngine/MEngine.hpp"
#include "MEngine/Core/Log.hpp"
#include "MEngine/Windows/Window.hpp"
#include "PrimitiveWorld.hpp"
#include <SDL3/SDL.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <cmath>
#include <limits>
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

bool hasFlag(int argc, char** argv, const char* flagName)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flagName) == 0) {
            return true;
        }
    }

    return false;
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

glm::vec3 cameraForward(const MEngine::Camera::CameraState& camera)
{
    return glm::normalize(
        glm::vec3 { camera.target[0], camera.target[1], camera.target[2] } -
        glm::vec3 { camera.position[0], camera.position[1], camera.position[2] });
}

bool rayIntersectsCube(
    glm::vec3 origin,
    glm::vec3 direction,
    const MEngine::RenderBackend::PrimitiveInstance& cube,
    float& outDistance,
    glm::vec3& outNormal)
{
    const float half = cube.size * 0.5f;
    const glm::vec3 minCorner {
        cube.position[0] - half,
        cube.position[1] - half,
        cube.position[2] - half,
    };
    const glm::vec3 maxCorner {
        cube.position[0] + half,
        cube.position[1] + half,
        cube.position[2] + half,
    };

    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    glm::vec3 entryNormal {};
    const auto testAxis = [&](float rayOrigin, float rayDirection, float minValue, float maxValue, glm::vec3 negativeNormal, glm::vec3 positiveNormal) {
        if (std::abs(rayDirection) <= 0.00001f) {
            return rayOrigin >= minValue && rayOrigin <= maxValue;
        }

        float nearT = (minValue - rayOrigin) / rayDirection;
        float farT = (maxValue - rayOrigin) / rayDirection;
        glm::vec3 nearNormal = negativeNormal;
        if (nearT > farT) {
            std::swap(nearT, farT);
            nearNormal = positiveNormal;
        }

        if (nearT > tMin) {
            tMin = nearT;
            entryNormal = nearNormal;
        }
        tMax = (std::min)(tMax, farT);
        return tMin <= tMax;
    };

    if (!testAxis(origin.x, direction.x, minCorner.x, maxCorner.x, { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }) ||
        !testAxis(origin.y, direction.y, minCorner.y, maxCorner.y, { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }) ||
        !testAxis(origin.z, direction.z, minCorner.z, maxCorner.z, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f })) {
        return false;
    }

    if (tMax < 0.0f) {
        return false;
    }

    outDistance = tMin >= 0.0f ? tMin : tMax;
    outNormal = entryNormal;
    return true;
}

bool blockExists(
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& blocks,
    glm::vec3 position,
    float epsilon)
{
    for (const MEngine::RenderBackend::PrimitiveInstance& block : blocks) {
        if (std::abs(block.position[0] - position.x) <= epsilon &&
            std::abs(block.position[1] - position.y) <= epsilon &&
            std::abs(block.position[2] - position.z) <= epsilon) {
            return true;
        }
    }

    return false;
}

bool tryPlaceBlock(
    const MEngine::Camera::CameraState& camera,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& visibleBlocks,
    std::vector<MEngine::RenderBackend::PrimitiveInstance>& placedBlocks,
    float blockSize)
{
    const glm::vec3 origin { camera.position[0], camera.position[1], camera.position[2] };
    const glm::vec3 direction = cameraForward(camera);
    constexpr float MaxPlaceDistance = 8.0f;

    float bestDistance = MaxPlaceDistance;
    glm::vec3 bestNormal {};
    const MEngine::RenderBackend::PrimitiveInstance* bestHit = nullptr;
    auto testBlock = [&](const MEngine::RenderBackend::PrimitiveInstance& block) {
        if (block.type != MEngine::RenderBackend::PrimitiveType::Cube) {
            return;
        }

        float distance = 0.0f;
        glm::vec3 normal {};
        if (rayIntersectsCube(origin, direction, block, distance, normal) && distance < bestDistance) {
            bestDistance = distance;
            bestNormal = normal;
            bestHit = &block;
        }
    };

    for (const MEngine::RenderBackend::PrimitiveInstance& block : visibleBlocks) {
        testBlock(block);
    }
    for (const MEngine::RenderBackend::PrimitiveInstance& block : placedBlocks) {
        testBlock(block);
    }

    if (!bestHit) {
        return false;
    }

    const glm::vec3 newPosition {
        bestHit->position[0] + bestNormal.x * blockSize,
        bestHit->position[1] + bestNormal.y * blockSize,
        bestHit->position[2] + bestNormal.z * blockSize,
    };
    if (blockExists(visibleBlocks, newPosition, blockSize * 0.1f) ||
        blockExists(placedBlocks, newPosition, blockSize * 0.1f)) {
        return false;
    }

    MEngine::RenderBackend::PrimitiveInstance block {};
    block.type = MEngine::RenderBackend::PrimitiveType::Cube;
    block.position[0] = newPosition.x;
    block.position[1] = newPosition.y;
    block.position[2] = newPosition.z;
    block.size = blockSize;
    block.color[0] = 0.62f;
    block.color[1] = 0.58f;
    block.color[2] = 0.48f;
    block.visibleFaces = MEngine::RenderBackend::PrimitiveFaceAll;
    placedBlocks.push_back(block);
    return true;
}

void submitVisibleWorld(
    MEngine::Engine& engine,
    const SandBox::PrimitiveWorldStreamer& worldStreamer)
{
    engine.setPrimitiveWorld(worldStreamer.visiblePrimitives());
}

} // namespace

int main(int argc, char** argv)
{
    try {
        MEngine::GraphicsApi graphicsApi = parseGraphicsApi(argc, argv);
        const bool enableRayTracing = hasFlag(argc, argv, "--rt");
        if (enableRayTracing && graphicsApi != MEngine::GraphicsApi::Vulkan) {
            graphicsApi = MEngine::GraphicsApi::Vulkan;
        }

        MEngine::Core::initializeLogging();
        MENGINE_INFO("[SandBox] Requested graphics API: {}", graphicsApiName(graphicsApi));
        if (enableRayTracing) {
            MENGINE_INFO("[SandBox] Ray tracing mode enabled by --rt");
        }

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
            enableRayTracing,
        });

        engine.initialize();

        SandBox::PrimitiveWorldConfig worldConfig {};
        worldConfig.seed = parseSeed(argc, argv).value_or(createRandomSeed());
        worldConfig.viewDistanceChunks = std::clamp(parseIntOption(argc, argv, "--view-distance").value_or(4), 1, 6);
        worldConfig.worldSizeChunks = std::max(parseIntOption(argc, argv, "--world-chunks").value_or(4096), 16);

        SandBox::PrimitiveWorldStreamer worldStreamer(worldConfig);
        std::vector<MEngine::RenderBackend::PrimitiveInstance> placedBlocks;
        const MEngine::Camera::CameraState& initialCamera = engine.cameraState();
        (void)worldStreamer.update(initialCamera.position[0], initialCamera.position[2]);
        (void)worldStreamer.waitForPendingLoad();
        submitVisibleWorld(engine, worldStreamer);
        engine.setInteractivePrimitives(placedBlocks);
        MENGINE_INFO(
            "[SandBox] Created async streaming primitive world seed={} worldChunks={}x{} viewDistance={} loadedChunks={} primitives={}",
            worldConfig.seed,
            worldConfig.worldSizeChunks,
            worldConfig.worldSizeChunks,
            worldConfig.viewDistanceChunks,
            worldStreamer.loadedChunkCount(),
            worldStreamer.visiblePrimitives().size());

        constexpr auto frameSleep = std::chrono::milliseconds(16);
        auto lastFrameTime = std::chrono::steady_clock::now();
        bool wasLeftMouseDown = false;
        while (window.isOpen() && engine.isRunning()) {
            const auto now = std::chrono::steady_clock::now();
            const std::chrono::duration<float> delta = now - lastFrameTime;
            lastFrameTime = now;

            window.pollEvents();
            const MEngine::Camera::CameraState& camera = engine.cameraState();
            bool shouldSubmitWorld = false;
            if (worldStreamer.update(camera.position[0], camera.position[2])) {
                shouldSubmitWorld = true;
                const SandBox::ChunkCoord center = worldStreamer.centerChunk();
                MENGINE_INFO(
                    "[SandBox] Published async primitive chunks center=({}, {}) loadedChunks={} primitives={}",
                    center.x,
                    center.z,
                    worldStreamer.loadedChunkCount(),
                    worldStreamer.visiblePrimitives().size());
            }

            const bool altDown = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
            const bool leftMouseDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
            if (leftMouseDown && !wasLeftMouseDown && !altDown) {
                const glm::vec3 origin { camera.position[0], camera.position[1], camera.position[2] };
                const glm::vec3 direction = cameraForward(camera);
                if (engine.shootingModeEnabled()) {
                    const float originValues[3] { origin.x, origin.y, origin.z };
                    const float directionValues[3] { direction.x, direction.y, direction.z };
                    engine.shootPhysicsSphere(originValues, directionValues);
                } else if (tryPlaceBlock(camera, worldStreamer.visiblePrimitives(), placedBlocks, worldStreamer.config().cellSize)) {
                    engine.setInteractivePrimitives(placedBlocks);
                }
            }
            wasLeftMouseDown = leftMouseDown;

            if (shouldSubmitWorld) {
                submitVisibleWorld(engine, worldStreamer);
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
