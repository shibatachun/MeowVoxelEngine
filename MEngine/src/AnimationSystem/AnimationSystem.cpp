#include "MEngine/AnimationSystem/AnimationSystem.hpp"

#include "MEngine/Core/Log.hpp"

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace MEngine::AnimationSystem {
namespace {

glm::vec3 sampleVec3Key(const std::vector<Resources::AnimationKeyVec3>& keys, float timeSeconds, const glm::vec3& fallback)
{
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1 || timeSeconds <= keys.front().timeSeconds) {
        return keys.front().value;
    }
    if (timeSeconds >= keys.back().timeSeconds) {
        return keys.back().value;
    }

    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto& current = keys[i];
        const auto& next = keys[i + 1];
        if (timeSeconds >= current.timeSeconds && timeSeconds <= next.timeSeconds) {
            const float span = std::max(next.timeSeconds - current.timeSeconds, 0.0001f);
            const float t = glm::clamp((timeSeconds - current.timeSeconds) / span, 0.0f, 1.0f);
            return glm::mix(current.value, next.value, t);
        }
    }

    return keys.back().value;
}

glm::quat sampleQuatKey(const std::vector<Resources::AnimationKeyQuat>& keys, float timeSeconds, const glm::quat& fallback)
{
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1 || timeSeconds <= keys.front().timeSeconds) {
        return glm::normalize(keys.front().value);
    }
    if (timeSeconds >= keys.back().timeSeconds) {
        return glm::normalize(keys.back().value);
    }

    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto& current = keys[i];
        const auto& next = keys[i + 1];
        if (timeSeconds >= current.timeSeconds && timeSeconds <= next.timeSeconds) {
            const float span = std::max(next.timeSeconds - current.timeSeconds, 0.0001f);
            const float t = glm::clamp((timeSeconds - current.timeSeconds) / span, 0.0f, 1.0f);
            return glm::normalize(glm::slerp(current.value, next.value, t));
        }
    }

    return glm::normalize(keys.back().value);
}

} // namespace

void Animator::initialize()
{
    initialized_ = true;
    MENGINE_INFO("[AnimationSystem] Initialized");
}

void Animator::setMeshAsset(const Resources::MeshAsset& asset)
{
    meshAsset_ = asset;
    hasMeshAsset_ = !meshAsset_.vertices.empty();
    animationTime_ = 0.0f;
    transitionTime_ = 0.0f;
    transitionDuration_ = 0.0f;
    animationState_ = AnimationState::Idle;
    desiredAnimationState_ = AnimationState::Idle;
    buildBindPose();
}

void Animator::setTuning(const AnimationTuning& tuning)
{
    tuning_ = tuning;
    tuning_.rootHorizontalMotionScale = glm::clamp(tuning_.rootHorizontalMotionScale, 0.0f, 1.0f);
    tuning_.rootVerticalMotionScale = glm::clamp(tuning_.rootVerticalMotionScale, 0.0f, 1.0f);
    tuning_.jumpStartOffsetSeconds = std::max(tuning_.jumpStartOffsetSeconds, 0.0f);
    tuning_.jumpPlaybackRate = std::max(tuning_.jumpPlaybackRate, 0.05f);
    tuning_.jumpHoldNormalizedTime = glm::clamp(tuning_.jumpHoldNormalizedTime, 0.05f, 0.98f);
    tuning_.jumpBlendInSeconds = glm::clamp(tuning_.jumpBlendInSeconds, 0.0f, 0.5f);
    tuning_.landingBlendSeconds = glm::clamp(tuning_.landingBlendSeconds, 0.0f, 0.7f);
    tuning_.locomotionBlendSeconds = glm::clamp(tuning_.locomotionBlendSeconds, 0.0f, 0.5f);
    tuning_.physicalJumpDelaySeconds = glm::clamp(tuning_.physicalJumpDelaySeconds, 0.0f, 5.0f);
}

void Animator::setAnimationState(AnimationState state)
{
    desiredAnimationState_ = state;
}

void Animator::update(float deltaSeconds)
{
    if (!initialized_ || !hasMeshAsset_) {
        return;
    }

    hasSkinnedAnimation_ = !meshAsset_.animations.empty() &&
        !meshAsset_.skeleton.empty() &&
        bindLocalTransforms_.size() == meshAsset_.skeleton.size();
    if (!hasSkinnedAnimation_) {
        return;
    }

    const float clampedDelta = glm::clamp(deltaSeconds, 0.0f, 0.1f);
    if (desiredAnimationState_ != animationState_) {
        const bool landingFromJump = animationState_ == AnimationState::Jump && desiredAnimationState_ != AnimationState::Jump;
        transitionStartLocalTransforms_ = currentLocalTransforms_.empty() ? bindLocalTransforms_ : currentLocalTransforms_;
        animationTime_ = desiredAnimationState_ == AnimationState::Jump ? tuning_.jumpStartOffsetSeconds : 0.0f;
        transitionTime_ = 0.0f;
        transitionDuration_ = desiredAnimationState_ == AnimationState::Jump ? tuning_.jumpBlendInSeconds :
            (landingFromJump ? tuning_.landingBlendSeconds : tuning_.locomotionBlendSeconds);
        animationState_ = desiredAnimationState_;
    }

    const size_t clipIndex = clipIndexForState(animationState_);
    const Resources::AnimationClip& clip = meshAsset_.animations[clipIndex];
    const float duration = std::max(clip.durationSeconds, 0.001f);
    if (animationState_ == AnimationState::Jump) {
        const float holdTime = duration * tuning_.jumpHoldNormalizedTime;
        animationTime_ = std::min(animationTime_ + clampedDelta * tuning_.jumpPlaybackRate, holdTime);
    } else {
        animationTime_ = std::fmod(animationTime_ + clampedDelta, duration);
    }

    std::vector<glm::mat4> localTransforms = sampleClipLocalTransforms(clipIndex, animationTime_);
    if (transitionDuration_ > 0.0f && !transitionStartLocalTransforms_.empty()) {
        transitionTime_ = std::min(transitionTime_ + clampedDelta, transitionDuration_);
        const float blend = glm::smoothstep(0.0f, transitionDuration_, transitionTime_);
        const size_t jointCount = std::min(localTransforms.size(), transitionStartLocalTransforms_.size());
        for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
            localTransforms[jointIndex] = transitionStartLocalTransforms_[jointIndex] * (1.0f - blend) + localTransforms[jointIndex] * blend;
        }

        if (transitionTime_ >= transitionDuration_) {
            transitionDuration_ = 0.0f;
            transitionStartLocalTransforms_.clear();
        }
    }

    applyLocalTransforms(localTransforms);
}

size_t Animator::clipIndexForState(AnimationState state) const
{
    if (state == AnimationState::Forward && meshAsset_.animations.size() > 1) {
        return 1;
    }
    if (state == AnimationState::Jump && meshAsset_.animations.size() > 2) {
        return 2;
    }
    return 0;
}

std::vector<glm::mat4> Animator::sampleClipLocalTransforms(size_t clipIndex, float timeSeconds) const
{
    std::vector<glm::mat4> localTransforms = bindLocalTransforms_;
    if (clipIndex >= meshAsset_.animations.size()) {
        return localTransforms;
    }

    const Resources::AnimationClip& clip = meshAsset_.animations[clipIndex];
    for (const Resources::AnimationChannel& channel : clip.channels) {
        if (channel.jointIndex >= localTransforms.size()) {
            continue;
        }

        const glm::vec3 position = sampleVec3Key(channel.positions, timeSeconds, glm::vec3(0.0f));
        glm::vec3 adjustedPosition = position;
        if (tuning_.lockRootVerticalMotion &&
            meshAsset_.skeleton[channel.jointIndex].parentIndex < 0 &&
            channel.jointIndex < bindLocalTransforms_.size()) {
            const float bindY = bindLocalTransforms_[channel.jointIndex][3].y;
            adjustedPosition.y = bindY + (position.y - bindY) * tuning_.rootVerticalMotionScale;
        }
        if (tuning_.lockRootHorizontalMotion &&
            meshAsset_.skeleton[channel.jointIndex].parentIndex < 0 &&
            channel.jointIndex < bindLocalTransforms_.size()) {
            const glm::vec3 bindPosition = glm::vec3(bindLocalTransforms_[channel.jointIndex][3]);
            adjustedPosition.x = bindPosition.x + (position.x - bindPosition.x) * tuning_.rootHorizontalMotionScale;
            adjustedPosition.z = bindPosition.z + (position.z - bindPosition.z) * tuning_.rootHorizontalMotionScale;
        }
        const glm::quat rotation = sampleQuatKey(channel.rotations, timeSeconds, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        const glm::vec3 scale = sampleVec3Key(channel.scales, timeSeconds, glm::vec3(1.0f));
        localTransforms[channel.jointIndex] =
            glm::translate(glm::mat4(1.0f), adjustedPosition) *
            glm::mat4_cast(rotation) *
            glm::scale(glm::mat4(1.0f), scale);
    }

    return localTransforms;
}

void Animator::applyLocalTransforms(const std::vector<glm::mat4>& localTransforms)
{
    currentLocalTransforms_ = localTransforms;
    std::vector<uint8_t> visited(localTransforms.size(), 0);
    std::function<void(size_t)> computeGlobal = [&](size_t jointIndex) {
        if (visited[jointIndex]) {
            return;
        }
        const int32_t parentIndex = meshAsset_.skeleton[jointIndex].parentIndex;
        if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < localTransforms.size()) {
            computeGlobal(static_cast<size_t>(parentIndex));
            globalTransforms_[jointIndex] = globalTransforms_[static_cast<size_t>(parentIndex)] * localTransforms[jointIndex];
        } else {
            globalTransforms_[jointIndex] = localTransforms[jointIndex];
        }
        skinningMatrices_[jointIndex] = globalTransforms_[jointIndex] * meshAsset_.skeleton[jointIndex].inverseBindMatrix;
        visited[jointIndex] = 1;
    };

    for (size_t jointIndex = 0; jointIndex < localTransforms.size(); ++jointIndex) {
        computeGlobal(jointIndex);
    }
}

void Animator::shutdown()
{
    if (initialized_) {
        MENGINE_INFO("[AnimationSystem] Shutdown");
        initialized_ = false;
    }
}

void Animator::buildBindPose()
{
    bindLocalTransforms_.clear();
    currentLocalTransforms_.clear();
    transitionStartLocalTransforms_.clear();
    globalTransforms_.clear();
    skinningMatrices_.clear();
    hasSkinnedAnimation_ = false;

    if (!hasMeshAsset_ || meshAsset_.skeleton.empty()) {
        return;
    }

    const size_t jointCount = meshAsset_.skeleton.size();
    std::vector<glm::mat4> bindGlobalTransforms(jointCount, glm::mat4(1.0f));
    bindLocalTransforms_.resize(jointCount, glm::mat4(1.0f));
    globalTransforms_.resize(jointCount, glm::mat4(1.0f));
    skinningMatrices_.resize(jointCount, glm::mat4(1.0f));

    for (size_t i = 0; i < jointCount; ++i) {
        bindGlobalTransforms[i] = glm::inverse(meshAsset_.skeleton[i].inverseBindMatrix);
    }
    for (size_t i = 0; i < jointCount; ++i) {
        const int32_t parentIndex = meshAsset_.skeleton[i].parentIndex;
        if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < jointCount) {
            bindLocalTransforms_[i] = glm::inverse(bindGlobalTransforms[static_cast<size_t>(parentIndex)]) * bindGlobalTransforms[i];
        } else {
            bindLocalTransforms_[i] = bindGlobalTransforms[i];
        }
    }
    currentLocalTransforms_ = bindLocalTransforms_;
}

} // namespace MEngine::AnimationSystem
