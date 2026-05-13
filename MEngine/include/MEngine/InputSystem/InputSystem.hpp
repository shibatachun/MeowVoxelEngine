#pragma once

namespace MEngine::InputSystem {

class Input {
public:
    void initialize();
    void poll();
    void shutdown();

private:
    bool initialized_ = false;
};

} // namespace MEngine::InputSystem
