#include "MEngine/AnimationSystem/AnimationSystem.hpp"

#include "MEngine/Core/Log.hpp"

namespace MEngine::AnimationSystem {

void Animator::initialize()
{
    initialized_ = true;
    MENGINE_INFO("[AnimationSystem] Initialized");
}

void Animator::update(float deltaSeconds)
{
    if (initialized_) {
        MENGINE_DEBUG("[AnimationSystem] Update {}s", deltaSeconds);
    }
}

void Animator::shutdown()
{
    if (initialized_) {
        MENGINE_INFO("[AnimationSystem] Shutdown");
        initialized_ = false;
    }
}

} // namespace MEngine::AnimationSystem
