#include "MEngine/Physics/PhysicsWorld.hpp"

#include "MEngine/Core/Log.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace MEngine::Physics {

void PhysicsWorld::initialize()
{
    spheres_.push_back(SphereBody {});
    initialized_ = true;
    rebuildDynamicPrimitives();
    MENGINE_INFO("[Physics] Initialized");
}

void PhysicsWorld::setTerrainColliders(const std::vector<RenderBackend::PrimitiveInstance>& colliders)
{
    terrainColliders_ = colliders;
    requestCollisionBake();
}

void PhysicsWorld::setInteractiveColliders(const std::vector<RenderBackend::PrimitiveInstance>& colliders)
{
    interactiveColliders_ = colliders;
    requestCollisionBake();
    rebuildDynamicPrimitives();
}

void PhysicsWorld::shootSphere(const glm::vec3& origin, const glm::vec3& direction)
{
    if (spheres_.size() > 64) {
        spheres_.erase(spheres_.begin() + 1);
    }

    SphereBody sphere {};
    sphere.radius = 0.22f;
    sphere.position = origin + glm::normalize(direction) * 0.75f;
    sphere.velocity = glm::normalize(direction) * 22.0f;
    sphere.lifetime = 8.0f;
    spheres_.push_back(sphere);
    rebuildDynamicPrimitives();
}

void PhysicsWorld::step(float deltaSeconds)
{
    if (!initialized_) {
        return;
    }

    publishCompletedCollisionBake();

    for (SphereBody& sphere : spheres_) {
        simulateSphere(sphere, deltaSeconds);
        sphere.lifetime -= deltaSeconds;
    }
    if (spheres_.size() > 1) {
        spheres_.erase(
            std::remove_if(spheres_.begin() + 1, spheres_.end(), [](const SphereBody& sphere) {
                return sphere.lifetime <= 0.0f || sphere.position.y < -100.0f;
            }),
            spheres_.end());
    }
    if (!spheres_.empty() && spheres_.front().position.y < -80.0f) {
        spheres_.front().position = { 0.0f, 24.0f, 0.0f };
        spheres_.front().velocity = { 1.4f, 0.0f, 0.6f };
    }

    rebuildDynamicPrimitives();
}

const std::vector<RenderBackend::PrimitiveInstance>& PhysicsWorld::dynamicPrimitives() const
{
    return dynamicPrimitives_;
}

size_t PhysicsWorld::terrainColliderCount() const
{
    return terrainCollision_.colliderCount();
}

size_t PhysicsWorld::terrainNodeCount() const
{
    return terrainCollision_.nodeCount();
}

void PhysicsWorld::shutdown()
{
    if (initialized_) {
        if (collisionBakeFuture_.valid()) {
            collisionBakeFuture_.wait();
        }
        MENGINE_INFO("[Physics] Shutdown");
        initialized_ = false;
    }
}

void PhysicsWorld::rebuildDynamicPrimitives()
{
    dynamicPrimitives_ = interactiveColliders_;
    dynamicPrimitives_.reserve(dynamicPrimitives_.size() + spheres_.size());

    for (const SphereBody& sphereBody : spheres_) {
        RenderBackend::PrimitiveInstance sphere {};
        sphere.type = RenderBackend::PrimitiveType::Sphere;
        sphere.position[0] = sphereBody.position.x;
        sphere.position[1] = sphereBody.position.y;
        sphere.position[2] = sphereBody.position.z;
        sphere.size = sphereBody.radius * 2.0f;
        sphere.color[0] = sphereBody.radius < 0.3f ? 0.95f : 1.0f;
        sphere.color[1] = sphereBody.radius < 0.3f ? 0.95f : 0.42f;
        sphere.color[2] = sphereBody.radius < 0.3f ? 0.18f : 0.18f;
        dynamicPrimitives_.push_back(sphere);
    }
}

void PhysicsWorld::requestCollisionBake()
{
    if (!initialized_) {
        return;
    }

    if (collisionBakeFuture_.valid()) {
        collisionBakeQueued_ = true;
        return;
    }

    scheduleCollisionBake();
}

void PhysicsWorld::scheduleCollisionBake()
{
    std::vector<RenderBackend::PrimitiveInstance> colliders = terrainColliders_;
    colliders.insert(colliders.end(), interactiveColliders_.begin(), interactiveColliders_.end());
    collisionBakeQueued_ = false;
    collisionBakeFuture_ = std::async(std::launch::async, [colliders = std::move(colliders)]() mutable {
        SparseVoxelOctree tree;
        tree.rebuild(colliders);
        return tree;
    });
}

void PhysicsWorld::publishCompletedCollisionBake()
{
    if (!collisionBakeFuture_.valid()) {
        return;
    }

    if (collisionBakeFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    terrainCollision_ = collisionBakeFuture_.get();
    MENGINE_INFO("[Physics] Baked terrain collision nodes={} colliders={}", terrainCollision_.nodeCount(), terrainCollision_.colliderCount());
    if (collisionBakeQueued_) {
        scheduleCollisionBake();
    }
}

void PhysicsWorld::simulateSphere(SphereBody& sphere, float deltaSeconds)
{
    constexpr float Gravity = -18.0f;
    constexpr float Restitution = 0.48f;
    constexpr float LinearDamping = 0.992f;
    const float dt = std::clamp(deltaSeconds, 0.0f, 0.033f);

    sphere.velocity.y += Gravity * dt;
    sphere.position += sphere.velocity * dt;

    for (int iteration = 0; iteration < 4; ++iteration) {
        CollisionHit hit {};
        if (!terrainCollision_.resolveSphere(sphere.position, sphere.radius, hit)) {
            break;
        }

        sphere.position += hit.normal * (hit.penetration + 0.001f);
        const float velocityIntoSurface = glm::dot(sphere.velocity, hit.normal);
        if (velocityIntoSurface < 0.0f) {
            sphere.velocity -= hit.normal * ((1.0f + Restitution) * velocityIntoSurface);
            sphere.velocity *= LinearDamping;
            if (std::abs(sphere.velocity.y) < 0.18f && hit.normal.y > 0.6f) {
                sphere.velocity.y = 0.0f;
            }
        }
    }
}

} // namespace MEngine::Physics
