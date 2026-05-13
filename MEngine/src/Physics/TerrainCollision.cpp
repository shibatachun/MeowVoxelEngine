#include "MEngine/Physics/TerrainCollision.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace MEngine::Physics {

namespace {

constexpr int MaxOctreeDepth = 7;
constexpr size_t MaxCollidersPerNode = 12;

} // namespace

void SparseVoxelOctree::rebuild(const std::vector<RenderBackend::PrimitiveInstance>& cubes)
{
    colliders_.clear();
    root_.reset();

    Aabb worldBounds {};
    worldBounds.min = glm::vec3(std::numeric_limits<float>::max());
    worldBounds.max = glm::vec3(-std::numeric_limits<float>::max());

    for (const RenderBackend::PrimitiveInstance& cube : cubes) {
        if (cube.type != RenderBackend::PrimitiveType::Cube) {
            continue;
        }

        const Aabb bounds = cubeBounds(cube);
        colliders_.push_back(bounds);
        worldBounds.min = glm::min(worldBounds.min, bounds.min);
        worldBounds.max = glm::max(worldBounds.max, bounds.max);
    }

    if (colliders_.empty()) {
        return;
    }

    const glm::vec3 center = (worldBounds.min + worldBounds.max) * 0.5f;
    const glm::vec3 extents = worldBounds.max - worldBounds.min;
    const float halfExtent = (std::max)({ extents.x, extents.y, extents.z, 1.0f }) * 0.5f + 1.0f;

    root_ = std::make_unique<Node>();
    root_->bounds.min = center - glm::vec3(halfExtent);
    root_->bounds.max = center + glm::vec3(halfExtent);

    for (size_t index = 0; index < colliders_.size(); ++index) {
        insert(*root_, index, colliders_[index], 0);
    }
}

bool SparseVoxelOctree::resolveSphere(const glm::vec3& center, float radius, CollisionHit& hit) const
{
    hit = {};
    if (!root_) {
        return false;
    }

    return resolveSphere(*root_, center, radius, hit);
}

size_t SparseVoxelOctree::colliderCount() const
{
    return colliders_.size();
}

size_t SparseVoxelOctree::nodeCount() const
{
    return root_ ? countNodes(*root_) : 0;
}

bool SparseVoxelOctree::intersects(const Aabb& a, const Aabb& b)
{
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
        a.min.y <= b.max.y && a.max.y >= b.min.y &&
        a.min.z <= b.max.z && a.max.z >= b.min.z;
}

bool SparseVoxelOctree::sphereIntersectsAabb(const glm::vec3& center, float radius, const Aabb& aabb, CollisionHit& hit)
{
    const glm::vec3 closest = glm::clamp(center, aabb.min, aabb.max);
    const glm::vec3 delta = center - closest;
    const float distanceSquared = glm::dot(delta, delta);
    const float radiusSquared = radius * radius;
    if (distanceSquared >= radiusSquared) {
        return false;
    }

    if (distanceSquared > 0.000001f) {
        const float distance = std::sqrt(distanceSquared);
        hit.normal = delta / distance;
        hit.penetration = radius - distance;
        return true;
    }

    const float distances[] = {
        std::abs(center.x - aabb.min.x),
        std::abs(aabb.max.x - center.x),
        std::abs(center.y - aabb.min.y),
        std::abs(aabb.max.y - center.y),
        std::abs(center.z - aabb.min.z),
        std::abs(aabb.max.z - center.z),
    };
    int axis = 0;
    for (int i = 1; i < 6; ++i) {
        if (distances[i] < distances[axis]) {
            axis = i;
        }
    }

    static constexpr glm::vec3 Normals[] = {
        { -1.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, -1.0f },
        { 0.0f, 0.0f, 1.0f },
    };
    hit.normal = Normals[axis];
    hit.penetration = radius + distances[axis];
    return true;
}

SparseVoxelOctree::Aabb SparseVoxelOctree::cubeBounds(const RenderBackend::PrimitiveInstance& cube)
{
    const float half = cube.size * 0.5f;
    const glm::vec3 center { cube.position[0], cube.position[1], cube.position[2] };
    return { center - glm::vec3(half), center + glm::vec3(half) };
}

void SparseVoxelOctree::insert(Node& node, size_t colliderIndex, const Aabb& colliderBounds, int depth)
{
    if (depth >= MaxOctreeDepth || (node.colliderIndices.size() < MaxCollidersPerNode && node.children[0] == nullptr)) {
        node.colliderIndices.push_back(colliderIndex);
        return;
    }

    const glm::vec3 center = (node.bounds.min + node.bounds.max) * 0.5f;
    bool insertedChild = false;
    for (int childIndex = 0; childIndex < 8; ++childIndex) {
        Aabb childBounds {};
        childBounds.min = {
            (childIndex & 1) ? center.x : node.bounds.min.x,
            (childIndex & 2) ? center.y : node.bounds.min.y,
            (childIndex & 4) ? center.z : node.bounds.min.z,
        };
        childBounds.max = {
            (childIndex & 1) ? node.bounds.max.x : center.x,
            (childIndex & 2) ? node.bounds.max.y : center.y,
            (childIndex & 4) ? node.bounds.max.z : center.z,
        };

        if (!intersects(childBounds, colliderBounds)) {
            continue;
        }

        if (!node.children[childIndex]) {
            node.children[childIndex] = std::make_unique<Node>();
            node.children[childIndex]->bounds = childBounds;
        }
        insert(*node.children[childIndex], colliderIndex, colliderBounds, depth + 1);
        insertedChild = true;
    }

    if (!insertedChild) {
        node.colliderIndices.push_back(colliderIndex);
    }
}

bool SparseVoxelOctree::resolveSphere(const Node& node, const glm::vec3& center, float radius, CollisionHit& hit) const
{
    CollisionHit nodeHit {};
    if (!sphereIntersectsAabb(center, radius, node.bounds, nodeHit)) {
        return false;
    }

    bool found = false;
    for (size_t colliderIndex : node.colliderIndices) {
        CollisionHit candidate {};
        if (sphereIntersectsAabb(center, radius, colliders_[colliderIndex], candidate) &&
            candidate.penetration > hit.penetration) {
            hit = candidate;
            found = true;
        }
    }

    for (const std::unique_ptr<Node>& child : node.children) {
        if (child && resolveSphere(*child, center, radius, hit)) {
            found = true;
        }
    }

    if (found) {
        hit.normal = glm::normalize(hit.normal);
    }
    return found;
}

size_t SparseVoxelOctree::countNodes(const Node& node) const
{
    size_t count = 1;
    for (const std::unique_ptr<Node>& child : node.children) {
        if (child) {
            count += countNodes(*child);
        }
    }
    return count;
}

} // namespace MEngine::Physics
