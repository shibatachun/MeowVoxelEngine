#include "SandboxApplication.hpp"

#include "BlockInteraction.hpp"
#include "CameraUtils.hpp"
#include "WorldSubmission.hpp"

#include "MEngine/Core/Log.hpp"
#include "MEngine/MEngine.hpp"

#include <SDL3/SDL.h>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace SandBox {

SandboxApplication::SandboxApplication(const CommandLineOptions& options)
    : options_(options)
    , worldConfig_(makeWorldConfig())
    , worldStreamer_(worldConfig_)
{
}

PrimitiveWorldConfig SandboxApplication::makeWorldConfig() const
{
    PrimitiveWorldConfig worldConfig {};
    worldConfig.seed = options_.seed.value_or(createRandomSeed());
    worldConfig.viewDistanceChunks = std::clamp(options_.viewDistanceChunks.value_or(4), 1, 8);
    worldConfig.worldSizeChunks = std::max(options_.worldSizeChunks.value_or(4096), 16);
    const int chunkDiameter = worldConfig.viewDistanceChunks * 2 + 1;
    worldConfig.cachedChunkCapacity = std::max(
        options_.chunkCacheCapacity.value_or(chunkDiameter * chunkDiameter * 3),
        chunkDiameter * chunkDiameter);
    return worldConfig;
}

void SandboxApplication::initialize(MEngine::Engine& engine)
{
    modelPreprocessor_.loadDefaultModel(engine);

    const MEngine::Camera::CameraState& initialCamera = engine.cameraState();
    (void)worldStreamer_.update(initialCamera.position[0], initialCamera.position[2]);
    (void)worldStreamer_.waitForPendingLoad();
    submitVisibleWorld(engine, worldStreamer_, false);
    engine.setInteractivePrimitives(placedBlocks_);
    resetPlayerToSpawn(engine, worldStreamer_, player_, placedBlocks_, true);

    MENGINE_INFO(
        "[SandBox] Created async streaming primitive world seed={} worldChunks={}x{} viewDistance={} chunkCache={} loadedChunks={} primitives={}",
        worldConfig_.seed,
        worldConfig_.worldSizeChunks,
        worldConfig_.worldSizeChunks,
        worldConfig_.viewDistanceChunks,
        worldConfig_.cachedChunkCapacity,
        worldStreamer_.loadedChunkCount(),
        worldStreamer_.visiblePrimitives().size());
}

void SandboxApplication::tick(MEngine::Engine& engine, float deltaSeconds)
{
    shouldSubmitWorld_ = false;

    updateModelRequests(engine);
    updatePlayMode(engine);
    updatePlayerMode(engine);

    engine.setCameraExternalControlEnabled(engine.playerControlModeEnabled());
    engine.pollInput();

    const bool resetRequested = engine.consumePlayerResetRequested();
    if (resetRequested || player_.position.y < -48.0f) {
        resetPlayerToSpawn(engine, worldStreamer_, player_, placedBlocks_, true);
    }

    updatePlayer(engine, deltaSeconds);
    updateWorldStreaming(engine);
    updateEditorInteraction(engine);

    if (shouldSubmitWorld_ && !worldStreamer_.hasPendingLoad()) {
        submitVisibleWorld(engine, worldStreamer_, true);
        if (engine.playerControlModeEnabled()) {
            const float groundTop = sampleTerrainTop(
                worldStreamer_.visiblePrimitives(),
                placedBlocks_,
                player_.position,
                PlayerRadius,
                player_.position.y);
            if (player_.position.y < groundTop) {
                player_.position.y = groundTop;
                player_.velocity.y = 0.0f;
            }
        }
    }
}

void SandboxApplication::updateModelRequests(MEngine::Engine& engine)
{
    std::string requestedModelPath;
    if (engine.consumeModelLoadRequested(requestedModelPath)) {
        modelPreprocessor_.loadModel(engine, requestedModelPath.c_str());
        submitPlayerTransform(engine, player_);
    }
}

void SandboxApplication::updatePlayMode(MEngine::Engine& engine)
{
    if (engine.consumePlayRequested()) {
        playMode_ = true;
        resetPlayerToSpawn(engine, worldStreamer_, player_, placedBlocks_, true);
    }

    const bool escapeDown = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_ESCAPE] != 0;
    if (playMode_ && escapeDown && !wasEscapeDown_) {
        playMode_ = false;
        engine.setEditorPlayMode(false);
        engine.setPlayerInputEnabled(false);
    }
    wasEscapeDown_ = escapeDown;
}

void SandboxApplication::updatePlayerMode(MEngine::Engine& engine)
{
    const bool playerMode = engine.playerControlModeEnabled();
    if (playerMode != wasPlayerMode_) {
        engine.setPlayerInputEnabled(playerMode);
        if (playerMode) {
            const MEngine::Camera::CameraState& currentCamera = engine.cameraState();
            const glm::vec3 currentForward = cameraForward(currentCamera);
            player_.cameraYawRadians = std::atan2(-currentForward.x, -currentForward.z);
            player_.actorYawRadians = player_.cameraYawRadians;
        }
        wasPlayerMode_ = playerMode;
    }
}

void SandboxApplication::updatePlayer(MEngine::Engine& engine, float deltaSeconds)
{
    if (engine.playerControlModeEnabled()) {
        const auto& playerInput = engine.playerInput();
        updatePlayerController(
            player_,
            deltaSeconds,
            engine.animationTuning().physicalJumpDelaySeconds,
            playerInput,
            worldStreamer_.visiblePrimitives(),
            placedBlocks_);
        submitPlayerTransform(engine, player_);
        engine.setCameraState(makePlayerCamera(player_));
        if (!player_.grounded || player_.jumpTakeoffPending) {
            engine.setAnimationState(MEngine::AnimationSystem::AnimationState::Jump);
        } else if (std::abs(playerInput.moveForward) > 0.01f || std::abs(playerInput.moveRight) > 0.01f) {
            engine.setAnimationState(MEngine::AnimationSystem::AnimationState::Forward);
        } else {
            engine.setAnimationState(MEngine::AnimationSystem::AnimationState::Idle);
        }
    } else {
        engine.setAnimationState(MEngine::AnimationSystem::AnimationState::Idle);
    }
}

void SandboxApplication::updateWorldStreaming(MEngine::Engine& engine)
{
    const MEngine::Camera::CameraState& camera = engine.cameraState();
    const bool playerMode = engine.playerControlModeEnabled();
    const float streamX = playerMode ? player_.position.x : camera.position[0];
    const float streamZ = playerMode ? player_.position.z : camera.position[2];
    if (worldStreamer_.update(streamX, streamZ)) {
        shouldSubmitWorld_ = true;
        const SandBox::ChunkCoord center = worldStreamer_.centerChunk();
        MENGINE_DEBUG(
            "[SandBox] Published async primitive chunks center=({}, {}) loadedChunks={} primitives={}",
            center.x,
            center.z,
            worldStreamer_.loadedChunkCount(),
            worldStreamer_.visiblePrimitives().size());
    }
}

void SandboxApplication::updateEditorInteraction(MEngine::Engine& engine)
{
    const MEngine::Camera::CameraState& camera = engine.cameraState();
    const bool playerMode = engine.playerControlModeEnabled();
    const bool leftMouseDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
    if (!playerMode && leftMouseDown && !wasLeftMouseDown_ && (SDL_GetModState() & SDL_KMOD_ALT) == 0) {
        const glm::vec3 origin { camera.position[0], camera.position[1], camera.position[2] };
        const glm::vec3 direction = cameraForward(camera);
        if (engine.shootingModeEnabled()) {
            const float originValues[3] { origin.x, origin.y, origin.z };
            const float directionValues[3] { direction.x, direction.y, direction.z };
            engine.shootPhysicsSphere(originValues, directionValues);
        } else if (tryPlaceBlock(camera, worldStreamer_.visiblePrimitives(), placedBlocks_, worldStreamer_.config().cellSize)) {
            engine.setInteractivePrimitives(placedBlocks_);
        }
    }
    wasLeftMouseDown_ = leftMouseDown;
}

} // namespace SandBox
