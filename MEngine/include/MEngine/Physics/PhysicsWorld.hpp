#pragma once

namespace MEngine::Physics {

class PhysicsWorld {
public:
    void initialize();
    void step(float deltaSeconds);
    void shutdown();

private:
    bool initialized_ = false;
};

} // namespace MEngine::Physics
