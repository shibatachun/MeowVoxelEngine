#include "MEngine/Audio/AudioSystem.hpp"

#include "MEngine/Core/Log.hpp"

namespace MEngine::Audio {

void AudioSystem::initialize()
{
    initialized_ = true;
    MENGINE_INFO("[Audio] Initialized");
}

void AudioSystem::update()
{
    if (initialized_) {
        MENGINE_DEBUG("[Audio] Update");
    }
}

void AudioSystem::shutdown()
{
    if (initialized_) {
        MENGINE_INFO("[Audio] Shutdown");
        initialized_ = false;
    }
}

} // namespace MEngine::Audio
