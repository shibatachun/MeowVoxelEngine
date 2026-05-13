#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace MEngine::Resources {

constexpr uint32_t MeshAssetMagic = 0x574F454Du; // MEOW
constexpr uint32_t MeshAssetVersion = 1;
constexpr uint32_t MeshMaxBoneInfluences = 4;

// Renderer-facing vertex layout. Keep this API-neutral so baked assets can be
// shared by Vulkan, D3D12, and any future backend without re-importing models.
struct MeshVertex {
    glm::vec3 position { 0.0f };
    glm::vec3 normal { 0.0f, 1.0f, 0.0f };
    glm::vec3 color { 1.0f };
    glm::vec2 texCoord { 0.0f };
    glm::vec4 tangent { 0.0f, 0.0f, 1.0f, 1.0f };
    glm::uvec4 boneIndices { 0u };
    glm::vec4 boneWeights { 0.0f };
};

struct MeshSurface {
    // A surface is one draw range into the shared index buffer.
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
};

struct MeshMaterial {
    std::string name;
    glm::vec4 baseColor { 1.0f };
    float metallic = 0.0f;
    float roughness = 1.0f;
    std::string baseColorTexture;
    std::string normalTexture;
    std::string metallicRoughnessTexture;
};

struct SkeletonJoint {
    std::string name;
    // -1 marks the skeleton root.
    int32_t parentIndex = -1;
    glm::mat4 inverseBindMatrix { 1.0f };
};

struct AnimationKeyVec3 {
    float timeSeconds = 0.0f;
    glm::vec3 value { 0.0f };
};

struct AnimationKeyQuat {
    float timeSeconds = 0.0f;
    glm::quat value { 1.0f, 0.0f, 0.0f, 0.0f };
};

struct AnimationChannel {
    // Channels target skeleton joints, not importer node pointers.
    uint32_t jointIndex = 0;
    std::vector<AnimationKeyVec3> positions;
    std::vector<AnimationKeyQuat> rotations;
    std::vector<AnimationKeyVec3> scales;
};

struct AnimationClip {
    std::string name;
    float durationSeconds = 0.0f;
    float ticksPerSecond = 0.0f;
    std::vector<AnimationChannel> channels;
};

struct MeshAsset {
    // Versioned CPU-side representation of a .mo file after import/bake.
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshSurface> surfaces;
    std::vector<MeshMaterial> materials;
    std::vector<SkeletonJoint> skeleton;
    std::vector<AnimationClip> animations;
};

bool saveMeshAsset(const MeshAsset& asset, const std::string& path);
bool loadMeshAsset(const std::string& path, MeshAsset& asset);

} // namespace MEngine::Resources
