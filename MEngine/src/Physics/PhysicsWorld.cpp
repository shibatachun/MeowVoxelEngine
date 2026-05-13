#include "MEngine/Physics/PhysicsWorld.hpp"

#include "MEngine/Core/Log.hpp"

namespace MEngine::Physics {

void PhysicsWorld::initialize()
{
    initialized_ = true;
    MENGINE_INFO("[Physics] Initialized");
}

void PhysicsWorld::step(float deltaSeconds)
{
    if (initialized_) {
        MENGINE_DEBUG("[Physics] Step {}s", deltaSeconds);
    }
}

void PhysicsWorld::shutdown()
{
    if (initialized_) {
        MENGINE_INFO("[Physics] Shutdown");
        initialized_ = false;
    }
}

} // namespace MEngine::Physics
