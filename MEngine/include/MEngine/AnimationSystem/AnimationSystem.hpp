#pragma once

namespace MEngine::AnimationSystem {

class Animator {
public:
    void initialize();
    void update(float deltaSeconds);
    void shutdown();

private:
    bool initialized_ = false;
};

} // namespace MEngine::AnimationSystem
