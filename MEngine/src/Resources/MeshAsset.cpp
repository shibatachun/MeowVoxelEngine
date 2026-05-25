#include "MEngine/Resources/MeshAsset.hpp"

#include <fstream>
#include <utility>

namespace MEngine::Resources {

namespace {

template <typename T>
bool writeValue(std::ofstream& stream, const T& value)
{
    // .mo is an engine-owned binary format, so trivially-copyable values are
    // written directly and guarded by MeshAssetVersion.
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return stream.good();
}

template <typename T>
bool readValue(std::ifstream& stream, T& value)
{
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    return stream.good();
}

bool writeString(std::ofstream& stream, const std::string& value)
{
    // Strings are length-prefixed so later fields remain seekable and binary-safe.
    const uint32_t size = static_cast<uint32_t>(value.size());
    if (!writeValue(stream, size)) {
        return false;
    }

    if (size > 0) {
        stream.write(value.data(), size);
    }
    return stream.good();
}

bool readString(std::ifstream& stream, std::string& value)
{
    uint32_t size = 0;
    if (!readValue(stream, size)) {
        return false;
    }

    value.resize(size);
    if (size > 0) {
        stream.read(value.data(), size);
    }
    return stream.good();
}

template <typename T>
bool writeVector(std::ofstream& stream, const std::vector<T>& values)
{
    const uint32_t count = static_cast<uint32_t>(values.size());
    if (!writeValue(stream, count)) {
        return false;
    }

    if (count > 0) {
        stream.write(reinterpret_cast<const char*>(values.data()), sizeof(T) * values.size());
    }
    return stream.good();
}

template <typename T>
bool readVector(std::ifstream& stream, std::vector<T>& values)
{
    uint32_t count = 0;
    if (!readValue(stream, count)) {
        return false;
    }

    values.resize(count);
    if (count > 0) {
        stream.read(reinterpret_cast<char*>(values.data()), sizeof(T) * values.size());
    }
    return stream.good();
}

} // namespace

bool saveMeshAsset(const MeshAsset& asset, const std::string& path)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }

    const uint32_t magic = MeshAssetMagic;
    const uint32_t version = MeshAssetVersion;
    // Magic rejects accidental files; version lets us evolve the layout later.
    if (!writeValue(stream, magic) || !writeValue(stream, version)) {
        return false;
    }

    if (!writeVector(stream, asset.vertices) ||
        !writeVector(stream, asset.indices) ||
        !writeVector(stream, asset.surfaces)) {
        return false;
    }

    const uint32_t materialCount = static_cast<uint32_t>(asset.materials.size());
    if (!writeValue(stream, materialCount)) {
        return false;
    }
    for (const MeshMaterial& material : asset.materials) {
        if (!writeString(stream, material.name) ||
            !writeValue(stream, material.baseColor) ||
            !writeValue(stream, material.metallic) ||
            !writeValue(stream, material.roughness) ||
            !writeString(stream, material.baseColorTexture) ||
            !writeString(stream, material.normalTexture) ||
            !writeString(stream, material.metallicRoughnessTexture)) {
            return false;
        }
    }

    const uint32_t embeddedTextureCount = static_cast<uint32_t>(asset.embeddedTextures.size());
    if (!writeValue(stream, embeddedTextureCount)) {
        return false;
    }
    for (const MeshEmbeddedTexture& texture : asset.embeddedTextures) {
        if (!writeString(stream, texture.name) ||
            !writeValue(stream, texture.width) ||
            !writeValue(stream, texture.height) ||
            !writeVector(stream, texture.rgba)) {
            return false;
        }
    }

    const uint32_t jointCount = static_cast<uint32_t>(asset.skeleton.size());
    if (!writeValue(stream, jointCount)) {
        return false;
    }
    for (const SkeletonJoint& joint : asset.skeleton) {
        if (!writeString(stream, joint.name) ||
            !writeValue(stream, joint.parentIndex) ||
            !writeValue(stream, joint.inverseBindMatrix)) {
            return false;
        }
    }

    const uint32_t animationCount = static_cast<uint32_t>(asset.animations.size());
    if (!writeValue(stream, animationCount)) {
        return false;
    }
    for (const AnimationClip& clip : asset.animations) {
        if (!writeString(stream, clip.name) ||
            !writeValue(stream, clip.durationSeconds) ||
            !writeValue(stream, clip.ticksPerSecond)) {
            return false;
        }

        const uint32_t channelCount = static_cast<uint32_t>(clip.channels.size());
        if (!writeValue(stream, channelCount)) {
            return false;
        }
        for (const AnimationChannel& channel : clip.channels) {
            if (!writeValue(stream, channel.jointIndex) ||
                !writeVector(stream, channel.positions) ||
                !writeVector(stream, channel.rotations) ||
                !writeVector(stream, channel.scales)) {
                return false;
            }
        }
    }

    if (!writeString(stream, asset.animationTuning.displayName) ||
        !writeValue(stream, asset.animationTuning.lockRootHorizontalMotion) ||
        !writeValue(stream, asset.animationTuning.lockRootVerticalMotion) ||
        !writeValue(stream, asset.animationTuning.rootHorizontalMotionScale) ||
        !writeValue(stream, asset.animationTuning.rootVerticalMotionScale) ||
        !writeValue(stream, asset.animationTuning.jumpStartOffsetSeconds) ||
        !writeValue(stream, asset.animationTuning.jumpPlaybackRate) ||
        !writeValue(stream, asset.animationTuning.jumpHoldNormalizedTime) ||
        !writeValue(stream, asset.animationTuning.jumpBlendInSeconds) ||
        !writeValue(stream, asset.animationTuning.landingBlendSeconds) ||
        !writeValue(stream, asset.animationTuning.locomotionBlendSeconds) ||
        !writeValue(stream, asset.animationTuning.physicalJumpDelaySeconds)) {
        return false;
    }

    return stream.good();
}

bool loadMeshAsset(const std::string& path, MeshAsset& asset)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!readValue(stream, magic) || !readValue(stream, version) ||
        magic != MeshAssetMagic || version < 2 || version > MeshAssetVersion) {
        return false;
    }

    MeshAsset loaded;
    // Load into a temporary first so callers keep their previous asset on failure.
    if (!readVector(stream, loaded.vertices) ||
        !readVector(stream, loaded.indices) ||
        !readVector(stream, loaded.surfaces)) {
        return false;
    }

    uint32_t materialCount = 0;
    if (!readValue(stream, materialCount)) {
        return false;
    }
    loaded.materials.resize(materialCount);
    for (MeshMaterial& material : loaded.materials) {
        if (!readString(stream, material.name) ||
            !readValue(stream, material.baseColor) ||
            !readValue(stream, material.metallic) ||
            !readValue(stream, material.roughness) ||
            !readString(stream, material.baseColorTexture) ||
            !readString(stream, material.normalTexture) ||
            !readString(stream, material.metallicRoughnessTexture)) {
            return false;
        }
    }

    uint32_t embeddedTextureCount = 0;
    if (!readValue(stream, embeddedTextureCount)) {
        return false;
    }
    loaded.embeddedTextures.resize(embeddedTextureCount);
    for (MeshEmbeddedTexture& texture : loaded.embeddedTextures) {
        if (!readString(stream, texture.name) ||
            !readValue(stream, texture.width) ||
            !readValue(stream, texture.height) ||
            !readVector(stream, texture.rgba)) {
            return false;
        }
    }

    uint32_t jointCount = 0;
    if (!readValue(stream, jointCount)) {
        return false;
    }
    loaded.skeleton.resize(jointCount);
    for (SkeletonJoint& joint : loaded.skeleton) {
        if (!readString(stream, joint.name) ||
            !readValue(stream, joint.parentIndex) ||
            !readValue(stream, joint.inverseBindMatrix)) {
            return false;
        }
    }

    uint32_t animationCount = 0;
    if (!readValue(stream, animationCount)) {
        return false;
    }
    loaded.animations.resize(animationCount);
    for (AnimationClip& clip : loaded.animations) {
        if (!readString(stream, clip.name) ||
            !readValue(stream, clip.durationSeconds) ||
            !readValue(stream, clip.ticksPerSecond)) {
            return false;
        }

        uint32_t channelCount = 0;
        if (!readValue(stream, channelCount)) {
            return false;
        }
        clip.channels.resize(channelCount);
        for (AnimationChannel& channel : clip.channels) {
            if (!readValue(stream, channel.jointIndex) ||
                !readVector(stream, channel.positions) ||
                !readVector(stream, channel.rotations) ||
                !readVector(stream, channel.scales)) {
                return false;
            }
        }
    }

    if (version >= 3) {
        if (!readString(stream, loaded.animationTuning.displayName) ||
            !readValue(stream, loaded.animationTuning.lockRootHorizontalMotion) ||
            !readValue(stream, loaded.animationTuning.lockRootVerticalMotion) ||
            !readValue(stream, loaded.animationTuning.rootHorizontalMotionScale) ||
            !readValue(stream, loaded.animationTuning.rootVerticalMotionScale) ||
            !readValue(stream, loaded.animationTuning.jumpStartOffsetSeconds) ||
            !readValue(stream, loaded.animationTuning.jumpPlaybackRate) ||
            !readValue(stream, loaded.animationTuning.jumpHoldNormalizedTime) ||
            !readValue(stream, loaded.animationTuning.jumpBlendInSeconds) ||
            !readValue(stream, loaded.animationTuning.landingBlendSeconds) ||
            !readValue(stream, loaded.animationTuning.locomotionBlendSeconds) ||
            !readValue(stream, loaded.animationTuning.physicalJumpDelaySeconds)) {
            return false;
        }
    }

    asset = std::move(loaded);
    return true;
}

} // namespace MEngine::Resources
