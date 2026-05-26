#pragma once

#include "MEngine/Camera/Camera.hpp"
#include "MEngine/InputSystem/InputSystem.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"

#include <glm/vec3.hpp>

#include <vector>

namespace MEngine {
class Engine;
}

namespace SandBox {

class PrimitiveWorldStreamer;

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

void updatePlayerController(
    PlayerControllerState& player,
    float deltaSeconds,
    float jumpTakeoffDelay,
    const MEngine::InputSystem::PlayerInputState& inputState,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& terrain,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& placedBlocks);

[[nodiscard]] MEngine::Camera::CameraState makePlayerCamera(const PlayerControllerState& player);
void submitPlayerTransform(MEngine::Engine& engine, const PlayerControllerState& player);
float sampleTerrainTop(
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& terrain,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& placedBlocks,
    glm::vec3 position,
    float radius,
    float fallback);
void resetPlayerToSpawn(
    MEngine::Engine& engine,
    PrimitiveWorldStreamer& worldStreamer,
    PlayerControllerState& player,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& placedBlocks,
    bool reloadSpawnWorld);

} // namespace SandBox
