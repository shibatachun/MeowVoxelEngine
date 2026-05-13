#include "MEngine/InputSystem/InputSystem.hpp"

#include "MEngine/Core/Log.hpp"

namespace MEngine::InputSystem {

void Input::initialize()
{
    initialized_ = true;
    MENGINE_INFO("[InputSystem] Initialized");
}

void Input::poll()
{
    if (initialized_) {
        MENGINE_DEBUG("[InputSystem] Poll");
    }
}

void Input::shutdown()
{
    if (initialized_) {
        MENGINE_INFO("[InputSystem] Shutdown");
        initialized_ = false;
    }
}

} // namespace MEngine::InputSystem
