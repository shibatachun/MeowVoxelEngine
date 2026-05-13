#pragma once

#include "MEngine/Physics/TerrainCollision.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"

#include <glm/vec3.hpp>

#include <future>
#include <vector>

namespace MEngine::Physics {

class PhysicsWorld {
public:
    void initialize();
    void setTerrainColliders(const std::vector<RenderBackend::PrimitiveInstance>& colliders);
    void setInteractiveColliders(const std::vector<RenderBackend::PrimitiveInstance>& colliders);
    void shootSphere(const glm::vec3& origin, const glm::vec3& direction);
    void step(float deltaSeconds);
    [[nodiscard]] const std::vector<RenderBackend::PrimitiveInstance>& dynamicPrimitives() const;
    [[nodiscard]] size_t terrainColliderCount() const;
    [[nodiscard]] size_t terrainNodeCount() const;
    void shutdown();

private:
    struct SphereBody {
        glm::vec3 position { 0.0f, 18.0f, 0.0f };
        glm::vec3 velocity { 1.4f, 0.0f, 0.6f };
        float radius = 0.45f;
        float lifetime = 1000000.0f;
    };

    void rebuildDynamicPrimitives();
    void requestCollisionBake();
    void scheduleCollisionBake();
    void publishCompletedCollisionBake();
    void simulateSphere(SphereBody& sphere, float deltaSeconds);

    SparseVoxelOctree terrainCollision_;
    std::future<SparseVoxelOctree> collisionBakeFuture_;
    std::vector<RenderBackend::PrimitiveInstance> terrainColliders_;
    std::vector<RenderBackend::PrimitiveInstance> interactiveColliders_;
    std::vector<SphereBody> spheres_;
    std::vector<RenderBackend::PrimitiveInstance> dynamicPrimitives_;
    bool collisionBakeQueued_ = false;
    bool initialized_ = false;
};

} // namespace MEngine::Physics
