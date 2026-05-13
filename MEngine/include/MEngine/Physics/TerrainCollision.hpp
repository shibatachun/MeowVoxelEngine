#pragma once

#include "MEngine/RenderBackend/Primitive.hpp"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace MEngine::Physics {

struct CollisionHit {
    glm::vec3 normal { 0.0f, 1.0f, 0.0f };
    float penetration = 0.0f;
};

class SparseVoxelOctree {
public:
    void rebuild(const std::vector<RenderBackend::PrimitiveInstance>& cubes);
    [[nodiscard]] bool resolveSphere(const glm::vec3& center, float radius, CollisionHit& hit) const;
    [[nodiscard]] size_t colliderCount() const;
    [[nodiscard]] size_t nodeCount() const;

private:
    struct Aabb {
        glm::vec3 min { 0.0f };
        glm::vec3 max { 0.0f };
    };

    struct Node {
        Aabb bounds {};
        std::array<std::unique_ptr<Node>, 8> children {};
        std::vector<size_t> colliderIndices;
    };

    [[nodiscard]] static bool intersects(const Aabb& a, const Aabb& b);
    [[nodiscard]] static bool sphereIntersectsAabb(const glm::vec3& center, float radius, const Aabb& aabb, CollisionHit& hit);
    [[nodiscard]] static Aabb cubeBounds(const RenderBackend::PrimitiveInstance& cube);
    void insert(Node& node, size_t colliderIndex, const Aabb& colliderBounds, int depth);
    [[nodiscard]] bool resolveSphere(const Node& node, const glm::vec3& center, float radius, CollisionHit& hit) const;
    [[nodiscard]] size_t countNodes(const Node& node) const;

    std::unique_ptr<Node> root_;
    std::vector<Aabb> colliders_;
};

} // namespace MEngine::Physics
