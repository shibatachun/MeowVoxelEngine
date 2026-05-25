#include "ModelAssetPreprocessor.hpp"

#include "MEngine/Core/Log.hpp"
#include "MEngine/MEngine.hpp"
#include "MEngine/Resources/MeshAsset.hpp"

#include <filesystem>
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace SandBox {
namespace {

std::optional<std::filesystem::path> findDefaultModelAsset()
{
    const std::vector<std::filesystem::path> candidates {
        "MEngine/Resources/Model/Standing Run Forward.mo",
        "../../MEngine/Resources/Model/Standing Run Forward.mo",
        "../../../MEngine/Resources/Model/Standing Run Forward.mo",
    };

    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> findAnimationAsset(const char* filename)
{
    const std::vector<std::filesystem::path> candidates {
        std::filesystem::path("MEngine/Resources/Model") / filename,
        std::filesystem::path("../../MEngine/Resources/Model") / filename,
        std::filesystem::path("../../../MEngine/Resources/Model") / filename,
    };

    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

void appendAnimationClip(
    MEngine::Resources::MeshAsset& target,
    const char* filename,
    const char* clipName)
{
    const std::optional<std::filesystem::path> path = findAnimationAsset(filename);
    if (!path) {
        MENGINE_WARN("[SandBox] Animation asset {} was not found", filename);
        return;
    }

    MEngine::Resources::MeshAsset animationAsset;
    if (!MEngine::Resources::loadMeshAsset(path->string(), animationAsset) || animationAsset.animations.empty()) {
        MENGINE_WARN("[SandBox] Failed to load animation asset {}", path->string());
        return;
    }

    MEngine::Resources::AnimationClip clip = animationAsset.animations.front();
    clip.name = clipName;
    for (MEngine::Resources::AnimationChannel& channel : clip.channels) {
        if (channel.jointIndex >= animationAsset.skeleton.size()) {
            continue;
        }

        const std::string& jointName = animationAsset.skeleton[channel.jointIndex].name;
        auto targetIt = std::find_if(target.skeleton.begin(), target.skeleton.end(), [&](const auto& joint) {
            return joint.name == jointName;
        });
        if (targetIt == target.skeleton.end()) {
            channel.jointIndex = static_cast<uint32_t>(target.skeleton.size());
            continue;
        }

        channel.jointIndex = static_cast<uint32_t>(std::distance(target.skeleton.begin(), targetIt));
    }
    clip.channels.erase(
        std::remove_if(clip.channels.begin(), clip.channels.end(), [&](const auto& channel) {
            return channel.jointIndex >= target.skeleton.size();
        }),
        clip.channels.end());
    target.animations.push_back(std::move(clip));
}

void logMeshAsset(const std::filesystem::path& path, const MEngine::Resources::MeshAsset& asset)
{
    MENGINE_INFO(
        "[SandBox] Loaded model {} vertices={} indices={} surfaces={} materials={} joints={} animations={}",
        path.string(),
        asset.vertices.size(),
        asset.indices.size(),
        asset.surfaces.size(),
        asset.materials.size(),
        asset.skeleton.size(),
        asset.animations.size());

    for (const MEngine::Resources::AnimationClip& clip : asset.animations) {
        MENGINE_INFO(
            "[SandBox] Animation '{}' duration={}s channels={}",
            clip.name,
            clip.durationSeconds,
            clip.channels.size());
    }

    for (const MEngine::Resources::MeshMaterial& material : asset.materials) {
        MENGINE_INFO(
            "[SandBox] Material '{}' baseColorTexture='{}' normalTexture='{}' metallicRoughnessTexture='{}'",
            material.name,
            material.baseColorTexture,
            material.normalTexture,
            material.metallicRoughnessTexture);
    }
}

} // namespace

void ModelAssetPreprocessor::loadDefaultModel(MEngine::Engine& engine) const
{
    const std::optional<std::filesystem::path> modelPath = findDefaultModelAsset();
    if (!modelPath) {
        MENGINE_WARN("[SandBox] Standing Run Forward.mo was not found; run MeowAssetBaker first");
        return;
    }

    MEngine::Resources::MeshAsset asset;
    if (!MEngine::Resources::loadMeshAsset(modelPath->string(), asset)) {
        MENGINE_WARN("[SandBox] Failed to load mesh asset {}", modelPath->string());
        return;
    }

    MEngine::Resources::AnimationClip forwardClip;
    if (!asset.animations.empty()) {
        forwardClip = asset.animations.front();
        forwardClip.name = "forward";
    }
    asset.animations.clear();
    appendAnimationClip(asset, "Standing Idle.mo", "idle");
    if (!forwardClip.channels.empty()) {
        asset.animations.push_back(std::move(forwardClip));
    }
    appendAnimationClip(asset, "Standing Jump.mo", "jump");

    engine.setMeshAsset(asset);
    logMeshAsset(*modelPath, asset);
}

bool ModelAssetPreprocessor::loadModel(MEngine::Engine& engine, const char* modelPath) const
{
    if (!modelPath || modelPath[0] == '\0') {
        return false;
    }

    MEngine::Resources::MeshAsset asset;
    if (!MEngine::Resources::loadMeshAsset(modelPath, asset)) {
        MENGINE_WARN("[SandBox] Failed to load mesh asset {}", modelPath);
        return false;
    }

    MEngine::Resources::AnimationClip forwardClip;
    if (!asset.animations.empty()) {
        forwardClip = asset.animations.front();
        forwardClip.name = "forward";
    }
    asset.animations.clear();
    appendAnimationClip(asset, "Standing Idle.mo", "idle");
    if (!forwardClip.channels.empty()) {
        asset.animations.push_back(std::move(forwardClip));
    }
    appendAnimationClip(asset, "Standing Jump.mo", "jump");

    engine.setMeshAsset(asset);
    logMeshAsset(modelPath, asset);
    return true;
}

} // namespace SandBox
