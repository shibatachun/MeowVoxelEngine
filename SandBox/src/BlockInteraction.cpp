#include "BlockInteraction.hpp"

#include "CameraUtils.hpp"

#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace SandBox {

namespace {

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

} // namespace

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

} // namespace SandBox
