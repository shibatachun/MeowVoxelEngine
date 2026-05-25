#pragma once

#include "MEngine/Resources/MeshAsset.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace MEngine::AnimationSystem {

enum class AnimationState {
    Idle,
    Forward,
    Jump,
};

struct AnimationTuning {
    bool lockRootHorizontalMotion = true;
    bool lockRootVerticalMotion = true;
    float rootHorizontalMotionScale = 0.0f;
    float rootVerticalMotionScale = 0.0f;
    float jumpStartOffsetSeconds = 0.12f;
    float jumpPlaybackRate = 0.82f;
    float jumpHoldNormalizedTime = 0.88f;
    float jumpBlendInSeconds = 0.12f;
    float landingBlendSeconds = 0.28f;
    float locomotionBlendSeconds = 0.16f;
    float physicalJumpDelaySeconds = 0.18f;
};

class Animator {
public:
    void initialize();
    void setMeshAsset(const Resources::MeshAsset& asset);
    void setTuning(const AnimationTuning& tuning);
    void setAnimationState(AnimationState state);
    void update(float deltaSeconds);
    void shutdown();

    [[nodiscard]] const std::vector<glm::mat4>& skinningMatrices() const { return skinningMatrices_; }
    [[nodiscard]] bool hasSkinnedAnimation() const { return hasSkinnedAnimation_; }

private:
    void buildBindPose();
    [[nodiscard]] size_t clipIndexForState(AnimationState state) const;
    [[nodiscard]] std::vector<glm::mat4> sampleClipLocalTransforms(size_t clipIndex, float timeSeconds) const;
    void applyLocalTransforms(const std::vector<glm::mat4>& localTransforms);

    bool initialized_ = false;
    bool hasSkinnedAnimation_ = false;
    AnimationTuning tuning_;
    float animationTime_ = 0.0f;
    float transitionTime_ = 0.0f;
    float transitionDuration_ = 0.0f;
    AnimationState animationState_ = AnimationState::Idle;
    AnimationState desiredAnimationState_ = AnimationState::Idle;
    Resources::MeshAsset meshAsset_;
    bool hasMeshAsset_ = false;
    std::vector<glm::mat4> bindLocalTransforms_;
    std::vector<glm::mat4> currentLocalTransforms_;
    std::vector<glm::mat4> transitionStartLocalTransforms_;
    std::vector<glm::mat4> globalTransforms_;
    std::vector<glm::mat4> skinningMatrices_;
};

} // namespace MEngine::AnimationSystem
