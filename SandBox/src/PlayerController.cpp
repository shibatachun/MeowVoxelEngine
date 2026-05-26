#include "PlayerController.hpp"

#include "PrimitiveWorld.hpp"
#include "WorldSubmission.hpp"

#include "MEngine/MEngine.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

namespace SandBox {

namespace {

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

} // namespace

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

void resetPlayerToSpawn(
    MEngine::Engine& engine,
    PrimitiveWorldStreamer& worldStreamer,
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

} // namespace SandBox
