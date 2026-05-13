#pragma once

namespace MEngine::Audio {

class AudioSystem {
public:
    void initialize();
    void update();
    void shutdown();

private:
    bool initialized_ = false;
};

} // namespace MEngine::Audio
