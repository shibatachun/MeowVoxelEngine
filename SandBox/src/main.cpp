#include "MEngine/MEngine.hpp"
#include "MEngine/Core/Log.hpp"
#include "MEngine/Windows/Window.hpp"
#include "ModelAssetPreprocessor.hpp"
#include "PrimitiveWorld.hpp"

#include <SDL3/SDL.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
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

struct PlayerControllerState {
    glm::vec3 position { 0.0f, 2.0f, 0.0f };
    glm::vec3 velocity { 0.0f };
    float actorYawRadians = 0.0f;
    float cameraYawRadians = 0.0f;
    float cameraPitchRadians = -0.34f;
    float jumpTakeoffTimer = 0.0f;
    bool jumpTakeoffPending = false;
    bool grounded = false;
};

constexpr glm::vec3 PlayerSpawnPosition { 0.0f, 2.0f, 0.0f };
constexpr float PlayerRadius = 0.42f;

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

    return MEngine::GraphicsApi::Vulkan;
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

float wrapRadians(float radians)
{
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float TwoPi = Pi * 2.0f;
    while (radians > Pi) {
        radians -= TwoPi;
    }
    while (radians < -Pi) {
        radians += TwoPi;
    }
    return radians;
}

float moveTowardsAngle(float currentRadians, float targetRadians, float maxDeltaRadians)
{
    const float delta = wrapRadians(targetRadians - currentRadians);
    if (std::abs(delta) <= maxDeltaRadians) {
        return targetRadians;
    }

    const float direction = delta >= 0.0f ? 1.0f : -1.0f;
    return wrapRadians(currentRadians + direction * maxDeltaRadians);
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

float sampleTerrainTop(
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& terrain,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& placedBlocks,
    glm::vec3 position,
    float radius,
    float fallback)
{
    float bestTop = fallback;
    auto testBlock = [&](const MEngine::RenderBackend::PrimitiveInstance& block) {
        if (block.type != MEngine::RenderBackend::PrimitiveType::Cube) {
            return;
        }

        const float half = block.size * 0.5f;
        const bool overlapsX = position.x + radius >= block.position[0] - half && position.x - radius <= block.position[0] + half;
        const bool overlapsZ = position.z + radius >= block.position[2] - half && position.z - radius <= block.position[2] + half;
        if (!overlapsX || !overlapsZ) {
            return;
        }

        const float top = block.position[1] + half;
        if (top <= position.y + 0.5f) {
            bestTop = std::max(bestTop, top);
        }
    };

    for (const auto& block : terrain) {
        testBlock(block);
    }
    for (const auto& block : placedBlocks) {
        testBlock(block);
    }

    return bestTop;
}

void resolvePlayerHorizontalCollisionAxis(
    PlayerControllerState& player,
    float axisSign,
    bool resolveX,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& terrain,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& placedBlocks)
{
    constexpr float PlayerHeight = 1.75f;
    constexpr float SkinWidth = 0.02f;

    auto testBlock = [&](const MEngine::RenderBackend::PrimitiveInstance& block) {
        if (block.type != MEngine::RenderBackend::PrimitiveType::Cube) {
            return;
        }

        const float half = block.size * 0.5f;
        const float minY = block.position[1] - half;
        const float maxY = block.position[1] + half;
        if (player.position.y + PlayerHeight <= minY + SkinWidth || player.position.y >= maxY - SkinWidth) {
            return;
        }

        const float minX = block.position[0] - half;
        const float maxX = block.position[0] + half;
        const float minZ = block.position[2] - half;
        const float maxZ = block.position[2] + half;

        if (resolveX) {
            const bool overlapsZ = player.position.z + PlayerRadius > minZ && player.position.z - PlayerRadius < maxZ;
            if (!overlapsZ) {
                return;
            }

            if (axisSign > 0.0f && player.position.x + PlayerRadius > minX && player.position.x < minX) {
                player.position.x = minX - PlayerRadius - SkinWidth;
            } else if (axisSign < 0.0f && player.position.x - PlayerRadius < maxX && player.position.x > maxX) {
                player.position.x = maxX + PlayerRadius + SkinWidth;
            }
        } else {
            const bool overlapsX = player.position.x + PlayerRadius > minX && player.position.x - PlayerRadius < maxX;
            if (!overlapsX) {
                return;
            }

            if (axisSign > 0.0f && player.position.z + PlayerRadius > minZ && player.position.z < minZ) {
                player.position.z = minZ - PlayerRadius - SkinWidth;
            } else if (axisSign < 0.0f && player.position.z - PlayerRadius < maxZ && player.position.z > maxZ) {
                player.position.z = maxZ + PlayerRadius + SkinWidth;
            }
        }
    };

    for (const auto& block : terrain) {
        testBlock(block);
    }
    for (const auto& block : placedBlocks) {
        testBlock(block);
    }
}

void updatePlayerController(
    PlayerControllerState& player,
    float deltaSeconds,
    float jumpTakeoffDelay,
    const MEngine::InputSystem::PlayerInputState& inputState,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& terrain,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& placedBlocks)
{
    constexpr float MoveSpeed = 4.8f;
    constexpr float JumpSpeed = 6.2f;
    constexpr float Gravity = 16.0f;
    constexpr float MouseSensitivity = 0.0025f;
    constexpr float GroundTurnSpeedRadians = 7.5f;
    constexpr float AirTurnSpeedRadians = 4.2f;

    if (!inputState.modifierDown) {
        player.cameraYawRadians -= inputState.mouseDeltaX * MouseSensitivity;
        player.cameraPitchRadians -= inputState.mouseDeltaY * MouseSensitivity;
        player.cameraPitchRadians = std::clamp(player.cameraPitchRadians, -0.95f, 0.18f);
    }

    glm::vec2 input { inputState.moveRight, inputState.moveForward };

    if (glm::dot(input, input) > 0.001f) {
        input = glm::normalize(input);
        const glm::vec3 cameraForward { -std::sin(player.cameraYawRadians), 0.0f, -std::cos(player.cameraYawRadians) };
        const glm::vec3 cameraRight { std::cos(player.cameraYawRadians), 0.0f, -std::sin(player.cameraYawRadians) };
        const glm::vec3 moveDirection = glm::normalize(cameraForward * input.y + cameraRight * input.x);
        const glm::vec3 movement = moveDirection * MoveSpeed * deltaSeconds;
        player.position.x += movement.x;
        if (std::abs(movement.x) > 0.0001f) {
            resolvePlayerHorizontalCollisionAxis(player, movement.x, true, terrain, placedBlocks);
        }
        player.position.z += movement.z;
        if (std::abs(movement.z) > 0.0001f) {
            resolvePlayerHorizontalCollisionAxis(player, movement.z, false, terrain, placedBlocks);
        }
        const float targetActorYaw = std::atan2(-moveDirection.x, -moveDirection.z);
        const float turnSpeed = player.grounded ? GroundTurnSpeedRadians : AirTurnSpeedRadians;
        player.actorYawRadians = moveTowardsAngle(
            player.actorYawRadians,
            targetActorYaw,
            turnSpeed * deltaSeconds);
    }

    if (inputState.jumpPressed && player.grounded && !player.jumpTakeoffPending) {
        player.jumpTakeoffPending = true;
        player.jumpTakeoffTimer = std::clamp(jumpTakeoffDelay, 0.0f, 5.0f);
    }

    if (player.jumpTakeoffPending) {
        if (!player.grounded) {
            player.jumpTakeoffPending = false;
            player.jumpTakeoffTimer = 0.0f;
        } else {
            player.jumpTakeoffTimer -= deltaSeconds;
        }
    }

    if (player.jumpTakeoffPending && player.jumpTakeoffTimer <= 0.0f && player.grounded) {
        player.velocity.y = JumpSpeed;
        player.jumpTakeoffPending = false;
        player.jumpTakeoffTimer = 0.0f;
        player.grounded = false;
    }

    player.velocity.y -= Gravity * deltaSeconds;
    player.position.y += player.velocity.y * deltaSeconds;

    const float groundTop = sampleTerrainTop(terrain, placedBlocks, player.position, PlayerRadius, -1000.0f);
    if (player.position.y <= groundTop) {
        player.position.y = groundTop;
        player.velocity.y = 0.0f;
        player.grounded = true;
    } else {
        player.grounded = false;
    }
}

MEngine::Camera::CameraState makePlayerCamera(const PlayerControllerState& player)
{
    MEngine::Camera::CameraState camera {};
    const glm::vec3 forward {
        -std::sin(player.cameraYawRadians) * std::cos(player.cameraPitchRadians),
        std::sin(player.cameraPitchRadians),
        -std::cos(player.cameraYawRadians) * std::cos(player.cameraPitchRadians)
    };
    const glm::vec3 target = player.position + glm::vec3(0.0f, 1.35f, 0.0f);
    const glm::vec3 position = target - glm::normalize(forward) * 5.2f;

    camera.position[0] = position.x;
    camera.position[1] = position.y;
    camera.position[2] = position.z;
    camera.target[0] = target.x;
    camera.target[1] = target.y;
    camera.target[2] = target.z;
    camera.fovDegrees = 60.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    return camera;
}

void submitPlayerTransform(MEngine::Engine& engine, const PlayerControllerState& player)
{
    const glm::mat4 transform =
        glm::translate(glm::mat4(1.0f), player.position) *
        glm::rotate(glm::mat4(1.0f), player.actorYawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
    engine.setMeshWorldTransform(glm::value_ptr(transform));
}

void submitVisibleWorld(
    MEngine::Engine& engine,
    const SandBox::PrimitiveWorldStreamer& worldStreamer,
    bool updateCollision)
{
    engine.setPrimitiveVisualWorld(worldStreamer.visiblePrimitives());
    if (updateCollision) {
        engine.setPrimitiveCollisionWorld(worldStreamer.visiblePrimitives());
    }
}

void resetPlayerToSpawn(
    MEngine::Engine& engine,
    SandBox::PrimitiveWorldStreamer& worldStreamer,
    PlayerControllerState& player,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& placedBlocks,
    bool reloadSpawnWorld)
{
    if (reloadSpawnWorld && worldStreamer.update(PlayerSpawnPosition.x, PlayerSpawnPosition.z)) {
        (void)worldStreamer.waitForPendingLoad();
        submitVisibleWorld(engine, worldStreamer, true);
    }

    player.position = PlayerSpawnPosition;
    player.velocity = glm::vec3(0.0f);
    player.actorYawRadians = 0.0f;
    player.cameraYawRadians = 0.0f;
    player.cameraPitchRadians = -0.34f;
    player.jumpTakeoffTimer = 0.0f;
    player.jumpTakeoffPending = false;
    player.grounded = false;

    const glm::vec3 spawnProbe { PlayerSpawnPosition.x, 1000.0f, PlayerSpawnPosition.z };
    const float groundTop = sampleTerrainTop(
        worldStreamer.visiblePrimitives(),
        placedBlocks,
        spawnProbe,
        PlayerRadius,
        PlayerSpawnPosition.y);
    player.position.y = groundTop;
    player.grounded = true;

    submitPlayerTransform(engine, player);
    engine.setCameraState(makePlayerCamera(player));
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
        SandBox::ModelAssetPreprocessor modelPreprocessor;
        modelPreprocessor.loadDefaultModel(engine);

        SandBox::PrimitiveWorldConfig worldConfig {};
        worldConfig.seed = parseSeed(argc, argv).value_or(createRandomSeed());
        worldConfig.viewDistanceChunks = std::clamp(parseIntOption(argc, argv, "--view-distance").value_or(4), 1, 8);
        worldConfig.worldSizeChunks = std::max(parseIntOption(argc, argv, "--world-chunks").value_or(4096), 16);
        const int chunkDiameter = worldConfig.viewDistanceChunks * 2 + 1;
        worldConfig.cachedChunkCapacity = std::max(parseIntOption(argc, argv, "--chunk-cache").value_or(chunkDiameter * chunkDiameter * 3), chunkDiameter * chunkDiameter);

        SandBox::PrimitiveWorldStreamer worldStreamer(worldConfig);
        std::vector<MEngine::RenderBackend::PrimitiveInstance> placedBlocks;
        PlayerControllerState player;
        const MEngine::Camera::CameraState& initialCamera = engine.cameraState();
        (void)worldStreamer.update(initialCamera.position[0], initialCamera.position[2]);
        (void)worldStreamer.waitForPendingLoad();
        submitVisibleWorld(engine, worldStreamer, false);
        engine.setInteractivePrimitives(placedBlocks);
        resetPlayerToSpawn(engine, worldStreamer, player, placedBlocks, true);
        MENGINE_INFO(
            "[SandBox] Created async streaming primitive world seed={} worldChunks={}x{} viewDistance={} chunkCache={} loadedChunks={} primitives={}",
            worldConfig.seed,
            worldConfig.worldSizeChunks,
            worldConfig.worldSizeChunks,
            worldConfig.viewDistanceChunks,
            worldConfig.cachedChunkCapacity,
            worldStreamer.loadedChunkCount(),
            worldStreamer.visiblePrimitives().size());

        constexpr auto frameSleep = std::chrono::milliseconds(16);
        auto lastFrameTime = std::chrono::steady_clock::now();
        bool wasLeftMouseDown = false;
        bool wasPlayerMode = false;
        bool playMode = false;
        bool wasEscapeDown = false;
        while (window.isOpen() && engine.isRunning()) {
            const auto now = std::chrono::steady_clock::now();
            const std::chrono::duration<float> delta = now - lastFrameTime;
            lastFrameTime = now;

            window.pollEvents();
            std::string requestedModelPath;
            if (engine.consumeModelLoadRequested(requestedModelPath)) {
                modelPreprocessor.loadModel(engine, requestedModelPath.c_str());
                submitPlayerTransform(engine, player);
            }
            if (engine.consumePlayRequested()) {
                playMode = true;
                resetPlayerToSpawn(engine, worldStreamer, player, placedBlocks, true);
            }
            const bool escapeDown = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_ESCAPE] != 0;
            if (playMode && escapeDown && !wasEscapeDown) {
                playMode = false;
                engine.setEditorPlayMode(false);
                engine.setPlayerInputEnabled(false);
            }
            wasEscapeDown = escapeDown;

            const bool playerMode = engine.playerControlModeEnabled();
            if (playerMode != wasPlayerMode) {
                engine.setPlayerInputEnabled(playerMode);
                if (playerMode) {
                    const MEngine::Camera::CameraState& currentCamera = engine.cameraState();
                    const glm::vec3 currentForward = cameraForward(currentCamera);
                    player.cameraYawRadians = std::atan2(-currentForward.x, -currentForward.z);
                    player.actorYawRadians = player.cameraYawRadians;
                }
                wasPlayerMode = playerMode;
            }

            engine.setCameraExternalControlEnabled(playerMode);
            engine.pollInput();
            const bool resetRequested = engine.consumePlayerResetRequested();
            if (resetRequested || player.position.y < -48.0f) {
                resetPlayerToSpawn(engine, worldStreamer, player, placedBlocks, true);
            }

            if (playerMode) {
                const auto& playerInput = engine.playerInput();
                updatePlayerController(
                    player,
                    delta.count(),
                    engine.animationTuning().physicalJumpDelaySeconds,
                    playerInput,
                    worldStreamer.visiblePrimitives(),
                    placedBlocks);
                submitPlayerTransform(engine, player);
                engine.setCameraState(makePlayerCamera(player));
                if (!player.grounded || player.jumpTakeoffPending) {
                    engine.setAnimationState(MEngine::AnimationSystem::AnimationState::Jump);
                } else if (std::abs(playerInput.moveForward) > 0.01f || std::abs(playerInput.moveRight) > 0.01f) {
                    engine.setAnimationState(MEngine::AnimationSystem::AnimationState::Forward);
                } else {
                    engine.setAnimationState(MEngine::AnimationSystem::AnimationState::Idle);
                }
            } else {
                engine.setAnimationState(MEngine::AnimationSystem::AnimationState::Idle);
            }

            const MEngine::Camera::CameraState& camera = engine.cameraState();
            const float streamX = playerMode ? player.position.x : camera.position[0];
            const float streamZ = playerMode ? player.position.z : camera.position[2];
            bool shouldSubmitWorld = false;
            if (worldStreamer.update(streamX, streamZ)) {
                shouldSubmitWorld = true;
                const SandBox::ChunkCoord center = worldStreamer.centerChunk();
                MENGINE_DEBUG(
                    "[SandBox] Published async primitive chunks center=({}, {}) loadedChunks={} primitives={}",
                    center.x,
                    center.z,
                    worldStreamer.loadedChunkCount(),
                    worldStreamer.visiblePrimitives().size());
            }

            const bool leftMouseDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
            if (!playerMode && leftMouseDown && !wasLeftMouseDown && (SDL_GetModState() & SDL_KMOD_ALT) == 0) {
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
                if (!worldStreamer.hasPendingLoad()) {
                    submitVisibleWorld(engine, worldStreamer, true);
                    if (playerMode) {
                        const float groundTop = sampleTerrainTop(worldStreamer.visiblePrimitives(), placedBlocks, player.position, 0.42f, player.position.y);
                        if (player.position.y < groundTop) {
                            player.position.y = groundTop;
                            player.velocity.y = 0.0f;
                        }
                    }
                }
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
